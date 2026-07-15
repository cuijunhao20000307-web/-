#define TESLA_INIT_IMPL
#include <tesla.hpp>
#include <switch.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
constexpr u16 kHttpPort = 37890;
std::atomic_bool g_serverRunning{false};
std::atomic_bool g_serverStop{false};
Thread g_serverThread{};
bool g_threadCreated = false;

constexpr const char kMobilePage[] = R"HTML(<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover,user-scalable=no"><meta name="theme-color" content="#07111f"><meta name="apple-mobile-web-app-capable" content="yes"><title>海拉鲁导航</title><style>
:root{--c:#72e7ff;--g:#f4d478;--m:#91a8b8}*{box-sizing:border-box}body{margin:0;min-height:100vh;background:radial-gradient(circle at 50% -10%,#17415f,#071522 40%,#030910);color:#f5fbff;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif}.app{max-width:520px;margin:auto;padding:calc(env(safe-area-inset-top) + 30px) 18px calc(env(safe-area-inset-bottom) + 30px)}header{text-align:center;padding:20px 0}.ey{font-size:10px;letter-spacing:.35em;color:var(--c)}h1{margin:8px 0 4px;font-size:30px;letter-spacing:.08em}.sub{font-size:13px;color:var(--m)}.card{margin:14px 0;padding:18px;border:1px solid rgba(114,231,255,.2);border-radius:22px;background:linear-gradient(145deg,rgba(13,35,57,.94),rgba(5,17,29,.94));box-shadow:0 18px 40px rgba(0,0,0,.3)}.status{display:flex;align-items:center;gap:12px}.dot{width:11px;height:11px;border-radius:50%;background:#75e0a1;box-shadow:0 0 16px #75e0a1}.muted{color:var(--m);font-size:12px;margin-top:4px}.pill{margin-left:auto;border:1px solid rgba(244,212,120,.25);color:var(--g);border-radius:999px;padding:6px 9px;font-size:11px}.head{display:flex;justify-content:space-between;align-items:center;margin-bottom:14px}.head span{font-size:11px;color:var(--m)}.coords{display:grid;grid-template-columns:repeat(3,1fr);gap:9px}.coord{text-align:center;padding:14px 8px;border-radius:15px;background:rgba(2,11,19,.65);border:1px solid rgba(255,255,255,.06)}.coord small{display:block;color:var(--c);font-size:10px;letter-spacing:.18em;margin-bottom:7px}.coord b{font-size:17px}.notice{margin-top:13px;padding:11px;border-radius:13px;background:rgba(244,212,120,.07);border:1px solid rgba(244,212,120,.16);font-size:12px;color:#ddcea0;line-height:1.55}.grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}.item{padding:15px 12px;border-radius:16px;background:rgba(3,13,23,.58);border:1px solid rgba(114,231,255,.12)}.item b{display:block;font-size:14px}.item span{display:block;margin-top:5px;color:var(--m);font-size:11px}.foot{text-align:center;color:#566e7d;font-size:10px;padding-top:15px}
</style></head><body><main class="app"><header><div class="ey">HYRULE BRIDGE</div><h1>海拉鲁导航</h1><div class="sub">手机局域网导游 · Switch 实时桥接</div></header><section class="card"><div class="status"><i class="dot"></i><div><b>Switch 已连接</b><div class="muted" id="server">正在读取桥接状态…</div></div><span class="pill" id="version">ONLINE</span></div></section><section class="card"><div class="head"><b>当前位置</b><span id="time">等待数据</span></div><div class="coords"><div class="coord"><small>X</small><b id="x">—</b></div><div class="coord"><small>Y</small><b id="y">—</b></div><div class="coord"><small>Z</small><b id="z">—</b></div></div><div class="notice" id="notice">桥接网页运行正常。实时坐标功能仍需在 Switch 端完成游戏版本地址标定。</div></section><section class="card"><div class="head"><b>导游功能</b><span>WEB 0.1</span></div><div class="grid"><div class="item"><b>📍 实时位置</b><span>同步游戏坐标</span></div><div class="item"><b>🧭 路线指引</b><span>下一阶段加入</span></div><div class="item"><b>⭐ 地点收藏</b><span>保存目标地点</span></div><div class="item"><b>📱 手机模式</b><span>浏览器直接使用</span></div></div></section><div class="foot">LINKO · HYRULE GUIDE PROJECT</div></main><script>
async function update(){try{const s=await fetch('/v1/status',{cache:'no-store'}).then(r=>r.json());document.querySelector('#server').textContent=(s.ip||location.hostname)+':'+(s.port||37890);document.querySelector('#version').textContent='v'+(s.version||'?');const p=await fetch('/v1/position',{cache:'no-store'}).then(r=>r.json());if(p.ready&&p.x!=null){for(const k of ['x','y','z'])document.querySelector('#'+k).textContent=Number(p[k]).toFixed(1);document.querySelector('#time').textContent=new Date().toLocaleTimeString();document.querySelector('#notice').textContent='坐标已同步，下一步可加入目标距离与方向箭头。'}else{document.querySelector('#time').textContent='等待坐标标定'}}catch(e){document.querySelector('#server').textContent='连接异常，请重新打开 Overlay'}}update();setInterval(update,2500);
</script></body></html>)HTML";

std::string ipFromNifm() {
    u32 ip = 0;
    Result rc = nifmGetCurrentIpAddress(&ip);
    if (R_FAILED(rc) || ip == 0) return {};
    in_addr address{};
    address.s_addr = ip;
    char text[INET_ADDRSTRLEN]{};
    if (!inet_ntop(AF_INET, &address, text, sizeof(text))) return {};
    return text;
}

std::string ipFromUdpSocket() {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return {};
    sockaddr_in remote{};
    remote.sin_family = AF_INET;
    remote.sin_port = htons(53);
    inet_pton(AF_INET, "1.1.1.1", &remote.sin_addr);
    std::string result;
    if (connect(fd, reinterpret_cast<sockaddr*>(&remote), sizeof(remote)) == 0) {
        sockaddr_in local{};
        socklen_t length = sizeof(local);
        if (getsockname(fd, reinterpret_cast<sockaddr*>(&local), &length) == 0) {
            char text[INET_ADDRSTRLEN]{};
            if (inet_ntop(AF_INET, &local.sin_addr, text, sizeof(text))) result = text;
        }
    }
    close(fd);
    return result;
}

std::string currentIp() {
    auto ip = ipFromNifm();
    if (!ip.empty() && ip != "0.0.0.0") return ip;
    ip = ipFromUdpSocket();
    if (!ip.empty() && ip != "0.0.0.0") return ip;
    return {};
}

void sendResponse(int client, const char* status, const char* contentType, const std::string& body) {
    char header[512]{};
    const int size = std::snprintf(header, sizeof(header),
        "HTTP/1.1 %s\r\nContent-Type: %s; charset=utf-8\r\nAccess-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: %zu\r\n\r\n",
        status, contentType, body.size());
    if (size > 0) send(client, header, static_cast<size_t>(size), 0);
    if (!body.empty()) send(client, body.data(), body.size(), 0);
}

void handleClient(int client) {
    char request[1024]{};
    const ssize_t count = recv(client, request, sizeof(request) - 1, 0);
    if (count <= 0) return;
    std::string firstLine(request, static_cast<size_t>(count));
    const auto end = firstLine.find("\r\n");
    if (end != std::string::npos) firstLine.resize(end);

    if (firstLine.rfind("GET / ", 0) == 0 || firstLine.rfind("GET /index.html ", 0) == 0) {
        sendResponse(client, "200 OK", "text/html", kMobilePage);
        return;
    }
    if (firstLine.rfind("GET /v1/status ", 0) == 0) {
        const std::string ip = currentIp();
        const std::string body = std::string("{\"ok\":true,\"name\":\"Hyrule Bridge\",\"version\":\"0.3.0\",\"ip\":\"") +
            (ip.empty() ? "unavailable" : ip) + "\",\"port\":37890,\"memory_access\":false}";
        sendResponse(client, "200 OK", "application/json", body);
        return;
    }
    if (firstLine.rfind("GET /v1/position ", 0) == 0) {
        sendResponse(client, "200 OK", "application/json", "{\"ok\":false,\"ready\":false,\"reason\":\"coordinate_address_not_calibrated\",\"x\":null,\"y\":null,\"z\":null}");
        return;
    }
    sendResponse(client, "404 Not Found", "application/json", "{\"ok\":false,\"error\":\"not_found\"}");
}

void serverMain(void*) {
    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) { g_serverRunning = false; return; }
    int yes = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(kHttpPort);
    if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || listen(server, 4) != 0) {
        close(server); g_serverRunning = false; return;
    }
    fcntl(server, F_SETFL, fcntl(server, F_GETFL, 0) | O_NONBLOCK);
    g_serverRunning = true;
    while (!g_serverStop.load()) {
        sockaddr_in clientAddress{};
        socklen_t clientLength = sizeof(clientAddress);
        int client = accept(server, reinterpret_cast<sockaddr*>(&clientAddress), &clientLength);
        if (client >= 0) { handleClient(client); close(client); }
        else svcSleepThread(100'000'000ULL);
    }
    close(server);
    g_serverRunning = false;
}

bool startServer() {
    if (g_threadCreated) return true;
    g_serverStop = false;
    Result rc = threadCreate(&g_serverThread, serverMain, nullptr, nullptr, 0x9000, 0x2C, -2);
    if (R_FAILED(rc)) return false;
    rc = threadStart(&g_serverThread);
    if (R_FAILED(rc)) { threadClose(&g_serverThread); return false; }
    g_threadCreated = true;
    return true;
}

void stopServer() {
    if (!g_threadCreated) return;
    g_serverStop = true;
    threadWaitForExit(&g_serverThread);
    threadClose(&g_serverThread);
    g_threadCreated = false;
}

class MainGui final : public tsl::Gui {
public:
    tsl::elm::ListItem* ipItem = nullptr;
    tsl::elm::ListItem* serviceItem = nullptr;
    u64 lastRefresh = 0;
    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame("海拉鲁导航", "Hyrule Bridge v0.3.0");
        auto* list = new tsl::elm::List();
        list->addItem(new tsl::elm::CategoryHeader("手机网页连接"));
        ipItem = new tsl::elm::ListItem("打开网址", "正在检测…");
        serviceItem = new tsl::elm::ListItem("网页服务", "正在启动…");
        list->addItem(ipItem);
        list->addItem(new tsl::elm::ListItem("端口", "37890"));
        list->addItem(serviceItem);
        list->addItem(new tsl::elm::CategoryHeader("使用方法"));
        list->addItem(new tsl::elm::ListItem("手机和 Switch", "连接同一 Wi-Fi"));
        list->addItem(new tsl::elm::ListItem("浏览器输入", "http://IP:37890"));
        list->addItem(new tsl::elm::CategoryHeader("当前状态"));
        list->addItem(new tsl::elm::ListItem("手机网页", "已内置"));
        list->addItem(new tsl::elm::ListItem("坐标标定", "尚未完成"));
        frame->setContent(list);
        refresh();
        return frame;
    }
    void refresh() {
        const std::string ip = currentIp();
        if (ipItem) ipItem->setValue(ip.empty() ? "未连接 / 获取失败" : ("http://" + ip + ":37890"));
        if (serviceItem) serviceItem->setValue(g_serverRunning.load() ? "运行中" : "未启动");
    }
    void update() override {
        const u64 now = armGetSystemTick();
        if (now - lastRefresh > armGetSystemTickFreq() * 2) { lastRefresh = now; refresh(); }
    }
};

class HyruleBridgeOverlay final : public tsl::Overlay {
public:
    Result nifmResult = 0;
    Result socketResult = 0;
    void initServices() override {
        nifmResult = nifmInitialize(NifmServiceType_User);
        socketResult = socketInitializeDefault();
        if (R_SUCCEEDED(socketResult)) startServer();
    }
    void exitServices() override {
        stopServer();
        if (R_SUCCEEDED(socketResult)) socketExit();
        if (R_SUCCEEDED(nifmResult)) nifmExit();
    }
    std::unique_ptr<tsl::Gui> loadInitialGui() override { return initially<MainGui>(); }
};
} // namespace

int main(int argc, char** argv) {
    return tsl::loop<HyruleBridgeOverlay>(argc, argv);
}
