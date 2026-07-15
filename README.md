# Hyrule Bridge Ultra 0.2.2

海拉鲁导航 Switch 端局域网桥接插件源码。

## 当前功能

- 中文界面
- 显示 Switch 局域网 IP（NIFM + UDP Socket 双重检测）
- HTTP 服务端口 `37890`
- `GET /v1/status` 返回运行状态
- `GET /v1/position` 返回“坐标尚未标定”占位数据
- 默认关闭游戏内存读取
- 编译后自动追加 Ultrahand `ULTR` 签名

## 编译

需要 devkitPro/devkitA64、libnx、portlibs 和 libultrahand。

```bash
git clone https://github.com/ppkantorski/libultrahand.git lib/libultrahand
make -j
```

输出：

```text
HyruleBridgeUltra.ovl
out/switch/.overlays/HyruleBridgeUltra.ovl
```

## 手机测试

Switch 和手机连接同一 Wi‑Fi 后，在手机浏览器打开：

```text
http://Switch-IP:37890/v1/status
```

正常时会返回 JSON。

## 注意

`/v1/position` 目前没有读取《塞尔达传说 王国之泪》的实时坐标。坐标地址必须在真机上针对游戏版本做只读标定，不能凭空写死，否则可能崩溃或读到错误数据。
