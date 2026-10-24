AsyncTCP.cpp.patched:pbolduc/AsyncTCP 1.2.0(WLED 0.15 使用)的 `_tcp_recv` 加了 arg==NULL 保护。
症状:手机 App 频繁发起/中止 HTTP 请求时,固件在 AsyncClient::_lwip_fin 以 this==NULL 崩溃(Core 0 LoadProhibited,EXCVADDR=0),
2026-09-08 在 25 分钟压测里复现 12 次。PlatformIO 重新拉取 libdeps 后需要重新覆盖:
  cp usermods/wellvow_ble/patches/AsyncTCP.cpp.patched .pio/libdeps/<env>/AsyncTCP@src-*/src/AsyncTCP.cpp
