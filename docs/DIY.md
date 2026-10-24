# WELLVOW DIY 固件教程(把 DIY 码填进 WLED 0.15.0-b6 WELLVOW 版)

适用:自己用 WLED 源码给 WELLVOW 控制器(或任何 ESP32 板)编译固件,编完后 WELLVOW App 仍能正常连接、鉴权、控制。

## 原理(一句话)
原厂固件里有品牌密钥;DIY 固件里没有。App 的「我的 → DIY 码」给每台设备算一串与芯片绑定的码 `WV1-…`,
DIY 固件编译时把这串码填进去,运行时用它和自己芯片的 MAC 算出设备密钥;App 连接时也用同样方法算,所以两边对得上。
码和芯片绑定:换一块板子就要重新复制那块板子的码。

## 一、拿到 DIY 码
1. 手机到 App Store / 安卓应用市场搜索 **WELLVOW** 下载安装。
2. 用原厂固件先把设备添加进 App(蓝牙鉴权通过)。
3. 我的 → DIY 码 → 找到这台设备,点「复制 DIY 码」,得到形如 `WV1-842C……4AC5` 的 36 位字符串。

## 二、准备工程
- 固件工程:本仓库(WLED 0.15.0-b6,MIT 许可,叠加 WELLVOW usermod `usermods/wellvow_ble/`)。
- 工具:PlatformIO(VS Code 插件或 `pip install platformio`)。
- 本仓库不包含品牌密钥,也不含 HUB 采集/同步代码;不带 `-D WELLVOW_DIY_CODE` 直接编译会在 `wellvow_ble.h` 处报 `#error`,这是预期的。

## 三、填 DIY 码并编译
在 `platformio_override.ini` 对应环境的 `build_flags` 末尾加一行(把码换成你自己的):
```
  -D WELLVOW_DIY_CODE=\"WV1-842C0000000000000000000000004AC5\"
```
或者不改文件,直接在命令行传:
```bash
PLATFORMIO_BUILD_FLAGS='-D WELLVOW_DIY_CODE=\"WV1-你的码\"' pio run -e wellvow_esp32 -t upload --upload-port /dev/cu.usbserial-XXXX
```
环境名:经典 ESP32(N4)用 `wellvow_esp32`;ESP32-S3 用 `wellvow_s3_n8r2`。

## 四、验证
1. 串口 115200 看到 `[WLV-DIY] DIY build, code WV1-…, mac …` 说明码被读到了。
2. App 重新连接这台设备(已添加过的不用删),串口出现 `[WLV-BLE] auth ok` 即鉴权通过;App 里日志会显示「DIY 固件,按 DIY 码派生密钥」。
3. 鉴权失败只有两种原因:码不是这块板子的(MAC 不同),或码抄错(必须 `WV1-` + 32 位十六进制)。

## 五、限制
- DIY 固件没有品牌密钥,不能给其他芯片生成新码;码只能从 App 里拿。
- HUB 采集/同步等原厂功能不在 DIY 工程里;DIY 工程只包含灯带控制部分。
- 升级到更新的 WLED 版本时,把 `usermods/wellvow_ble/` 整个目录和 `platformio_override.ini` 里的 wellvow 环境搬过去即可,DIY 码填法不变。

## 开源声明
固件基于 WLED 0.15.0-b6(MIT,Copyright 2016-2025 Christian Schwinne),WELLVOW usermod 以同样方式提供;分发二进制或源码时保留 WLED 的 LICENSE 文件。

## 环境一览(platformio_override.ini)
| 环境 | 硬件 | 说明 |
|---|---|---|
| `wellvow_esp32` | ESP32-MINI-1-N4(WLED-STRIP-W705) | 两路输出 GPIO15 / GPIO2,按键 GPIO25,ES7201 麦克风 |
| `wellvow_esp32_h706` | 同上 | 型号 WLED-STRIP-H706,默认 SM16825 |
| `wellvow_s3_n8r2` | ESP32-S3-WROOM-1-N8R2 | 16 段,带音乐律动 |

编译前把 `upload_port` 按自己的串口填到命令行(`--upload-port`)即可。
