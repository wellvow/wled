# WELLVOW 固件基线:WLED 0.15.0-b6(MIT)

## 为什么是 b6

WLED 在 `0.15.0-b7`(build 2410270)把项目许可证从 **MIT 改成了 EUPL v1.2**
(CHANGELOG: “Re-license the WLED project from MIT to EUPL (#4194)”)。
`0.15.1` 及其之后都是 EUPL。本产品要保持闭源发行,所以基线固定在
**最后一个 MIT 版本 `v0.15.0-b6`**(build 2410140),本目录的 `LICENSE` 即为 MIT。

上游 tarball:`https://codeload.github.com/wled/WLED/tar.gz/refs/tags/v0.15.0-b6`

## 这棵树是怎么来的

原来的 `../WLED-0.15.1_SM16825` 是在 EUPL 的 0.15.1 上改的。迁移做法是**三方合并**:
对每个被 WELLVOW 改过的文件跑
`git merge-file <fork 文件> <0.15.1 原版> <b6 原版>`,
即把“上游 0.15.1 → b6”的差异反向应用回 fork 文件,只保留 WELLVOW 自己的改动。
10 处冲突全部是上游 API 改名(见下),一律取 b6 侧。

结果:本树相对 b6 的改动量与原树相对 0.15.1 的改动量逐文件一致,
说明 WELLVOW 改动完整保留、且没有 0.15.1 的代码混进来。

### 合并时取 b6 侧的上游差异

| 位置 | 0.15.1 | b6(本树采用) |
|---|---|---|
| FX.cpp | `beatsin8_t` / `cos8_t` | `beatsin8` / `cos8` |
| FX.h | 新增 `MIN_FRAME_DELAY` / FPS 计算宏 | 无(b6 没有这套 FPS 计算) |
| button.cpp | `colPri` | `col` |
| wled.cpp | `DEBUG_PRINTF_P` 单行日志 | 分三段 `DEBUG_PRINT` |
| wled.h | `VERSION 2507300` | `VERSION 2410140` |
| audio_reactive.h | `uiScript.print(...)` | `oappend(SET_F(...))` |
| bus_wrapper.h | `I_32_I2_*` + `_useParallelI2S` | `I_32_I1_*` / `I_32_I0_*` + `WLED_NO_I2S*_PIXELBUS` |

### 因为 b6 没有对应机制而**未**移植的两项

- `-D WLED_DISABLE_DITHER`:b6 里根本没有这个开关(PWM dithering 是后来加的),所以删掉。
- `getDataSize()` 里给 SM16825 加 4 字节:b6 的 `bus_wrapper.h` 没有 `getDataSize()`(0.15.1 才有),无处可打。
  SM16825 真正起作用的初始化(`beginSM16825()` 写 5 通道增益)已按 b6 的 case 名重新贴好,
  RMT / I2S0 / I2S1 三条路径都覆盖了。

### 因为 b6 缺少而新增的一项

`platformio.ini` 里 b6 **没有** `[env:esp32dev_V4]`(上游 b7 之后才加)。
经典 ESP32(W3 MINI-1)必须走 arduino-esp32 2.0.9 / IDF4.4,否则 NimBLE 一起来就
`coex_init` abort() 无限重启。已在 `platformio_override.ini` 里照 b6 自带的
`[esp32_idf_V4]` 组补了一份同名环境。

另外 b6 的 `releaseString` 走 `TOSTRING(WLED_RELEASE_NAME)`,
所以编译参数写 `-D WLED_RELEASE_NAME=WELLVOW_S3_N8R2`,**不能**像 0.15.1 那样带转义引号。

### usermod 适配

`usermods/wellvow_ble/wellvow_ble.h` 里重建总线的两个函数原来用
`busConfigs.clear()` / `busConfigs.push_back()`(0.15.1 的 `std::vector`)。
b6 的 `busConfigs` 是 `BusConfig* busConfigs[N]` 指针数组,主循环取用后 `delete` 并置 `nullptr`。
已加 `busCfgClear()` / `busCfgAdd()` 两个小包装,调用点不变。

## 网页 UI 头文件

`wled00/html_*.h` 必须用**本树**的 `node tools/cdata.js` 从 b6 的 `wled00/data/` 重新生成,
不能从 0.15.1 的树里拷(那是 EUPL 的产物)。
`wled00/html_wellvow.h` 是 WELLVOW 自己的页面,由
`python3 usermods/wellvow_ble/web/gen_html.py` 生成,别手改头文件。

## 编译

```
pio run -e wellvow_s3_n8r2        # W3-S3 正式板(ESP32-S3-N8R2)
pio run -e wellvow_full           # 经典 ESP32(W3 MINI-1),不裁 WLED 模块
```

实测(2026-09-22,均通过):

| 环境 | RAM | Flash |
|---|---|---|
| wellvow_s3_n8r2 | 65752 B (20.1%) | 1756197 B (83.7%) |
| wellvow_full | 67804 B (20.7%) | 1891373 B (93.1%) |

S3 版静态 RAM 比 0.15.1 版多约 4 KB(b6 少了几项上游优化),S3 有 PSRAM,余量充足。
