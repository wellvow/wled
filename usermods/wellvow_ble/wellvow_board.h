#pragma once
// 板级引脚定义 —— 同一份固件适配两块主板,编译时用 -D WELLVOW_BOARD_S3 切换。
//
//  W3 主板(20250929-PABA-W3-V10,模组 ESP32-MINI-1-N4)实际用到的脚:
//    GPIO15 → U5 SN74LVC1G17 → D1_A_OUT(灯带输出 1)      GPIO2  → U6 → D2_A_OUT(灯带输出 2)
//    GPIO25 → SW1 按键                                     GPIO0 → SW2(下载)   EN → SW3(复位)/DTR·RTS 自动下载
//    GPIO27 → ES7201 CLOCK   GPIO35 → ES7201 DATA(音频 ADC,音乐律动)
//
//  换 ESP32-S3-WROOM-1-N8R2(8MB Flash + 2MB 四线 PSRAM)时按“改动最小”对应(模组脚号见 docs/W3换S3-N8R2引脚对照.md):
//    GPIO15、GPIO2、GPIO0、EN、TXD0/RXD0、GPIO35 都还在,原样接;
//    GPIO25 在 S3 上不存在 → 按键改 GPIO4;GPIO27 在 S3 上是 flash 脚 → ES7201 CLOCK 改 GPIO5;
//    S3 的 strapping 脚 GPIO3 / GPIO45 / GPIO46 悬空不接。
#if defined(WELLVOW_BOARD_S3)
  #define WELLVOW_BOARD_NAME   "W3-S3-N8R2"
  #define WELLVOW_PIN_OUT1     15   // 灯带输出 1(模组 pin 8)
  #define WELLVOW_PIN_OUT2     2    // 灯带输出 2(模组 pin 38)
  #define WELLVOW_PIN_KEY      4    // 按键(模组 pin 4)—— W3 的 GPIO25 在 S3 上没有
  #define WELLVOW_PIN_MIC_CLK  5    // ES7201 CLOCK(模组 pin 5)—— W3 的 GPIO27 在 S3 上是 flash 引脚
  #define WELLVOW_PIN_MIC_DATA 35   // ES7201 DATA(模组 pin 28)—— N8R2 四线 PSRAM 不占用 35;若将来换 R8(八线)要改到 GPIO6
#else
  #define WELLVOW_BOARD_NAME   "W3-MINI-1"
  #define WELLVOW_PIN_OUT1     15
  #define WELLVOW_PIN_OUT2     2
  #define WELLVOW_PIN_KEY      25
  #define WELLVOW_PIN_MIC_CLK  27
  #define WELLVOW_PIN_MIC_DATA 35
#endif
