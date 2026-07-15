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
    const int size = std::snprintf(
        header, sizeof(header),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s; charset=utf-8\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n"
        "Content-Length: %zu\r\n\r\n",
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

    if (firstLine.rfind("GET /v1/status ", 0) == 0 || firstLine.rfind("GET / ", 0) == 0) {
        const std::string ip = currentIp();
        const std::string body =
            std::string("{\"ok\":true,\"name\":\"Hyrule Bridge\",\"version\":\"0.2.2\",\"ip\":\"") +
            (ip.empty() ? "unavailable" : ip) +
            "\",\"port\":37890,\"memory_access\":false}";
        sendResponse(client, "200 OK", "application/json", body);
        return;
    }

    if (firstLine.rfind("GET /v1/position ", 0) == 0) {
        const std::string body =
            "{\"ok\":false,\"ready\":false,\"reason\":\"coordinate_address_not_calibrated\","
            "\"x\":null,\"y\":null,\"z\":null}";
        sendResponse(client, "200 OK", "application/json", body);
        return;
    }

    sendResponse(client, "404 Not Found", "application/json", "{\"ok\":false,\"error\":\"not_found\"}");
}

void serverMain(void*) {
    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) {
        g_serverRunning = false;
        return;
    }

    int yes = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(kHttpPort);

    if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || listen(server, 4) != 0) {
        close(server);
        g_serverRunning = false;
        return;
    }

    fcntl(server, F_SETFL, fcntl(server, F_GETFL, 0) | O_NONBLOCK);
    g_serverRunning = true;

    while (!g_serverStop.load()) {
        sockaddr_in clientAddress{};
        socklen_t clientLength = sizeof(clientAddress);
        int client = accept(server, reinterpret_cast<sockaddr*>(&clientAddress), &clientLength);
        if (client >= 0) {
            handleClient(client);
            close(client);
        } else {
            svcSleepThread(100'000'000ULL);
        }
    }

    close(server);
    g_serverRunning = false;
}

bool startServer() {
    if (g_threadCreated) return true;
    g_serverStop = false;
    Result rc = threadCreate(&g_serverThread, serverMain, nullptr, nullptr, 0x6000, 0x2C, -2);
    if (R_FAILED(rc)) return false;
    rc = threadStart(&g_serverThread);
    if (R_FAILED(rc)) {
        threadClose(&g_serverThread);
        return false;
    }
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
        auto* frame = new tsl::elm::OverlayFrame("海拉鲁导航", "Hyrule Bridge v0.2.2");
        auto* list = new tsl::elm::List();

        list->addItem(new tsl::elm::CategoryHeader("局域网连接"));
        ipItem = new tsl::elm::ListItem("Switch IP", "正在检测…");
        serviceItem = new tsl::elm::ListItem("手机服务", "正在启动…");
        list->addItem(ipItem);
        list->addItem(new tsl::elm::ListItem("端口", "37890"));
        list->addItem(serviceItem);

        list->addItem(new tsl::elm::CategoryHeader("兼容状态"));
        list->addItem(new tsl::elm::ListItem("Ultrahand 签名", "正常"));
        list->addItem(new tsl::elm::ListItem("内存读取", "已关闭"));
        list->addItem(new tsl::elm::ListItem("坐标标定", "尚未完成"));

        list->addItem(new tsl::elm::CategoryHeader("手机测试地址"));
        list->addItem(new tsl::elm::ListItem("状态接口", "/v1/status"));
        list->addItem(new tsl::elm::ListItem("位置接口", "/v1/position"));

        frame->setContent(list);
        refresh();
        return frame;
    }

    void refresh() {
        const std::string ip = currentIp();
        if (ipItem) ipItem->setValue(ip.empty() ? "未连接 / 获取失败" : ip);
        if (serviceItem) serviceItem->setValue(g_serverRunning.load() ? "运行中" : "未启动");
    }

    void update() override {
        const u64 now = armGetSystemTick();
        if (now - lastRefresh > armGetSystemTickFreq() * 2) {
            lastRefresh = now;
            refresh();
        }
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

    std::unique_ptr<tsl::Gui> loadInitialGui() override {
        return initially<MainGui>();
    }
};
} // namespace

int main(int argc, char** argv) {
    return tsl::loop<HyruleBridgeOverlay>(argc, argv);
}
