#pragma once

#include "wled.h"
#include <esp_wifi.h>   // 扫描失败时取 IDF 错误码
#include <driver/i2s.h>   // 原始 PDM 自检用
#include "wellvow_board.h"
#include "esp_system.h"
RTC_NOINIT_ATTR uint32_t wlvCheckpoint;
  #define WELLVOW_BLE_NAME   "WVOW_WLED_STRIP_%04X"
  #if defined(WELLVOW_STRIP_H706)
    #define WELLVOW_MODEL    "WLED_STRIP_H706"   // 10-24:只适配 H706 / Pro 系列(SM16825),接口正负极与 H705 相反
  #elif defined(WELLVOW_STRIP_W705)
    #define WELLVOW_MODEL    "WLED_STRIP_W705"   // 10-23:N4 版产品型号 W705,App 据此只给 H705/H805 的灯带选项
  #else
    #define WELLVOW_MODEL    "WLED_STRIP_ESP32"
  #endif
  #define WELLVOW_MDNS       "wellvow-strip-%04x"

/* 开关机渐变时长(毫秒)。开机比关机稍慢一点,"慢慢亮起来"比瞬间点亮更像成品灯具;
 * 用编译参数可覆盖。走的是 WLED 自己的亮度过渡,不额外占 CPU。 */
/* 重建分段时最多逐段保留这么多段的颜色(段上限本来就是 8 / MAX_NUM_SEGMENTS) */
/* 动画整体倍速(1 = 原速)。WLED 的效果速度滑块封顶 255,顶到头还是偏慢,所以在效果时钟上再乘一档。 */
/* 每条 DP 的串口日志:默认关。串口缓冲满时 printf 会阻塞主循环,高频指令下明显拖慢灯带响应。 */
#ifndef WELLVOW_LOG_DP
  #define WELLVOW_LOG_DP 0
#endif

#ifndef WELLVOW_SPEED_BOOST
  #define WELLVOW_SPEED_BOOST 2
#endif
/* 配网 / 恢复出厂时的默认效果:91 = Rainbow(整条彩虹流动),一眼就能看出设备活着 */
#ifndef WELLVOW_DEFAULT_FX
  #define WELLVOW_DEFAULT_FX FX_MODE_RAINBOW_CYCLE
#endif

#ifndef WELLVOW_SEG_KEEP
  #define WELLVOW_SEG_KEEP 16
#endif

#ifndef WELLVOW_BOOT_FADE_MS
  #define WELLVOW_BOOT_FADE_MS   800
#endif
#ifndef WELLVOW_POWER_FADE_MS
  #define WELLVOW_POWER_FADE_MS  800
#endif

#ifdef ARDUINO_ARCH_ESP32
#include <NimBLEDevice.h>
#ifndef WELLVOW_DIY_CODE
#error "This public DIY tree has no brand key: build with -D WELLVOW_DIY_CODE=\"WV1-...\" (see docs/DIY.md)"
#endif
#include <esp_system.h>
#include <mbedtls/md.h>   // HMAC-SHA256:DIY 码派生(规范第二节)
#include <nvs.h>
#include <nvs_flash.h>

// WELLVOW APP protocol bridge for WLED.
// GATT: FFF0 service, FFF3 write, FFF4 notify.

/* ===== WELLVOW Sky(效果号 142,占用 WLED 的 RSVD 位):日出 / 日落 =====
 * WLED 自带的 Sunrise(123)是太阳从段"中心"向两端长出来:灯带只接了一半时中心正好在灯带外,整段看起来不动。
 * 这个效果把地平线放在段的起点(勾选"反向"就换到另一端),光从地平线往远端铺开,每颗灯的颜色 = 调色板[索引],
 * 索引 0 = 夜、255 = 太阳,远端比地平线滞后(强度越大铺得越远):配日出 /palette0.json(255)、日落 /palette1.json(254)。
 * 速度 = 时长:1..60 日出分钟数,61..120 日落分钟数(反向推进,最后停在夜色),0 = 静止白天,>120 = 快速日出日落循环。 */
/* 日出(槽 0 → 调色板 255)/ 日落(槽 1 → 254)的关键色表:效果直接在相邻两色之间按 16 位进度插值,
 * 不走 WLED 的 16 格 CRGBPalette16(16 格 + 8 位索引下,相邻两色之间只有十几级,5 分钟里看起来就是一格一格跳)。 */
static uint8_t g_skyKeys[2][8][3] = {
  {{0x2a,0x25,0x66},{0xc8,0x4a,0x7a},{0xf5,0x73,0x1f},{0xff,0xc1,0x4a},{0xdf,0xe9,0xf5}},
  {{0xdf,0xe9,0xf5},{0xff,0xc1,0x4a},{0xf5,0x73,0x1f},{0xc8,0x4a,0x7a},{0x2a,0x25,0x66}} };
static uint8_t g_skyKeyN[2] = {5, 5};
static uint16_t mode_wellvow_sky() {
  /* 整体渐变模型(2026-10-21 用户改:“整体渐渐变化就可以,不用左右灯刷色”):
   * 整条灯带同一颜色,沿调色板(日出 6 关键色 / 用户选的配色组渐变)从头走到尾,走完停在最后一个颜色。
   * 速度滑块 = 总时长:sx=255 → 2 分钟,sx=0 → 5 分钟(越慢时间越长,用户 10-22 定);换速度或换调色板都从头开始。 */
  const uint8_t sp = SEGMENT.speed;
  const uint32_t durMins = 2 + ((255UL - sp) * 3UL + 127UL) / 255UL;   // sx=255 → 2 分钟,sx=0 → 5 分钟(用户 10-22 定)
  if (SEGENV.call == 0 || sp != SEGENV.aux0 || SEGMENT.palette != SEGENV.aux1) { SEGENV.step = millis(); SEGENV.aux0 = sp; SEGENV.aux1 = SEGMENT.palette; Serial.printf("[WLV-SKY] restart call=%u sx=%u pal=%u dur=%u min\n", unsigned(SEGENV.call), unsigned(sp), unsigned(SEGMENT.palette), unsigned(durMins)); }
  uint32_t t10 = (millis() - SEGENV.step) / 100, tgt = durMins * 600;
  if (t10 > tgt) t10 = tgt;
  const uint32_t prog16 = (t10 * 65535UL) / tgt;   // 0..65535
  const uint8_t slot = SEGMENT.palette == 255 ? 0 : (SEGMENT.palette == 254 ? 1 : 0xFF);
  uint32_t col;
  if (slot != 0xFF && g_skyKeyN[slot] >= 2) {
    /* 日出在第一个颜色前面多加一段“第一色 15% 亮度 → 第一色”(从暗开始亮起来,用户 10-22 要求);
     * 日落在最后一个颜色后面多加一段“末色 → 末色 15%”(慢慢暗下去)。n 个关键色 → n 段,总时长平均分。 */
    const uint8_t n = g_skyKeyN[slot];
    uint32_t pos = prog16 * (uint32_t)n;                 // 段号.小数(16 位),共 n 段
    uint8_t i = pos >> 16; uint16_t frac = pos & 0xFFFF;
    if (i >= n) { i = n - 1; frac = 0xFFFF; }
    uint32_t ca, cb;
    if (slot == 0) {   // 日出:段 0 = 暗→第一色,段 k = 第 k 色 → 第 k+1 色
      const uint8_t *a = g_skyKeys[0][i == 0 ? 0 : i - 1], *b = g_skyKeys[0][i == 0 ? 0 : i];
      ca = RGBW32(a[0], a[1], a[2], 0); cb = RGBW32(b[0], b[1], b[2], 0);
      if (i == 0) ca = color_fade(ca, 38, true);
    } else {           // 日落:段 k = 第 k 色 → 第 k+1 色,最后一段 = 末色 → 暗
      const uint8_t *a = g_skyKeys[1][i], *b = g_skyKeys[1][i + 1 < n ? i + 1 : n - 1];
      ca = RGBW32(a[0], a[1], a[2], 0); cb = RGBW32(b[0], b[1], b[2], 0);
      if (i + 1 >= n) cb = color_fade(cb, 38, true);
    }
    col = color_blend(ca, cb, frac, true);
  } else col = SEGMENT.color_from_palette(prog16 >> 8, false, true, 255);
  SEGMENT.fill(col);
  return FRAMETIME;
}
static const char _data_FX_MODE_WLV_SKY[] PROGMEM = "WELLVOW Sky@Duration,;;!;;sx=85";

class WellvowBleUsermod : public Usermod, public NimBLEServerCallbacks, public NimBLECharacteristicCallbacks, public NimBLEAdvertisedDeviceCallbacks {
private:
  NimBLEServer *server = nullptr;
  NimBLECharacteristic *notifyChar = nullptr;
  NimBLEScan *radioScan = nullptr;
  bool bleStarted = false;
  uint32_t lastBleAttempt = 0;
  volatile uint8_t pendingRadioKey = 0;
  bool bleConnected = false;   // 至少有一条蓝牙连接
  /* 多连接(2026-10-23 用户定:最多 9 部手机同时连):每条连接各自一份鉴权状态(挑战-应答按连接做),
   * 没鉴权的连接只能收到鉴权应答;状态通知只群发给已鉴权的连接;指令的应答只回给发它的那条连接。 */
  static constexpr uint8_t BLE_MAX_CONN = CONFIG_BT_NIMBLE_MAX_CONNECTIONS;
  struct BleConn { uint16_t handle = 0xFFFF; bool auth = false, pending = false; uint8_t nonce[8] = {0}; };
  BleConn bleConns[BLE_MAX_CONN];
  uint16_t bleReplyConn = 0xFFFF;   // serviceBleQueue 执行一帧期间 = 该帧的来源连接(应答只回它);其它时候 0xFFFF = 群发已鉴权连接
  uint16_t blobConn = 0xFFFF;       // 正在传 blob 的那条连接,它断了才中止 blob
  BleConn *bleConnOf(uint16_t h) { for (auto &c : bleConns) if (c.handle == h) return &c; return nullptr; }
  uint8_t bleConnCount() { uint8_t n = 0; for (auto &c : bleConns) if (c.handle != 0xFFFF) n++; return n; }
  bool bleConnAuthed(uint16_t h) { BleConn *c = bleConnOf(h); return c && c->auth; }
  uint8_t deviceSeed[8] = {0};
  uint32_t deviceKey[4] = {0};
  bool wifiScanPending = false; bool wifiScanRequested = false;
  bool wifiConnectPending = false;
  uint32_t wifiConnectStarted = 0;
  wl_status_t previousWifiStatus = WL_IDLE_STATUS;
  File blobFile;
  bool blobActive = false;
  uint8_t blobKind = 0, blobNextSeq = 0, blobTotal = 0;
  uint8_t blobSlot = 0;
  uint32_t blobExpected = 0, blobReceived = 0;
  File sequenceFile;
  bool sequencePlaying = false, sequenceLoop = true, sequenceRle = false;
  uint16_t sequenceLeds = 0, sequenceFrames = 0, sequenceFrame = 0;
  uint8_t sequenceFps = 20;
  uint8_t sequenceSlot = 0;   // 正在播放的帧序列槽位(/N.wseq)
  uint32_t sequenceLastMs = 0;
  WiFiUDP studioUdp;
  bool studioUdpReady = false, studioLive = false;
  uint8_t *studioFrame = nullptr;
  uint16_t studioLeds = 0;
  uint32_t studioFrameId = 0, studioPresentAt = 0;
  uint8_t musicPreviousFx = 0;
  bool audioTrace = false; uint32_t audioTraceNext = 0;
  /* 音乐灵敏度(2026-09-25 重做):AGC 开(普通档)负责把音量归一化,灵敏度只决定"多大的声音才算有声音":
   *   噪声门 = 校准出来的环境底噪 audioFloor × 系数(1 档 4.3×,5 档 2.9×,10 档 1.15×)+ 6,低于门的一律当作没声音。
   *   底噪要校准:App / 网页点"校准",保持安静 1.5 秒,固件把增益归零量出底噪(raw × 16),存 NVS。没校准过按 20。 */
  uint8_t audioGain = 40; uint8_t audioSquelch = 64; uint8_t audioSens = 5; uint16_t audioFloor = 20;
  uint8_t audioCalState = 0; uint32_t audioCalT0 = 0; uint16_t audioCalMax = 0; uint32_t audioCalSum = 0, audioCalN = 0;
  /* 麦克风入口缩放(运行时可改):WLED 噪声门只有 8 位(≤255),而 ES7201 的原始幅度动辄上千,门挡不住。
   * 校准时把量到的底噪归一化到 40:scale ← scale × 40 / 底噪,之后门 = 40 × 灵敏度系数(52..178)总在 8 位内。默认 1/16。 */
  float micScale = 0.0625f;
  void applyMicScale() {
#if defined(USERMOD_AUDIOREACTIVE)
    if (audioSource) audioSource->setScale(micScale);
#endif
  }
  /* 门 = 底噪(校准时 1.5 秒内的峰值)× 系数 + 3:10 档 0.5×(手机小声放在咪头旁也能触发,底噪峰值偶尔漏过去算正常),
   * 5 档 2.5×,1 档 4.1×。底噪归一化到 40 → 门 23 / 103 / 167。 */
  // 10 档 0.1× 底噪 + 3(底噪 40 → 门 7,最灵敏),1 档 2.7×(111),5 档 1.55×(65)
  uint8_t squelchFor(uint8_t sens) { float q = audioFloor * (0.1f + (10 - sens) * 0.289f) + 3.0f; return q > 255.0f ? 255 : uint8_t(q); }
  bool musicActive = false;
  bool musicWanted = false;   // NVS 里记的"上次关机时音乐模式是开着的"
  struct ScheduleSlot { uint8_t id, enabled, repeatMask, onH, onM, offH, offM; } schedules[8];
  uint8_t scheduleCount = 0, clockWeekday = 1, clockHour = 0, clockMinute = 0, clockSecond = 0;
  uint32_t clockSetMillis = 0, lastScheduleMinute = UINT32_MAX;
  volatile bool segmentConfigPending = false;
  volatile bool colorOrderPending = false;
  volatile bool colorCalibrationPending = false;
  bool colorCalibrationActive = false;
  uint32_t lastColorCalibrationShow = 0;
#ifndef WELLVOW_DEFAULT_COLOR_ORDER
#define WELLVOW_DEFAULT_COLOR_ORDER COL_ORDER_RGB   // W3 板灯芯片校准默认;开发板自带 WS2812 用 -D WELLVOW_DEFAULT_COLOR_ORDER=COL_ORDER_GRB
#endif
  uint8_t pendingColorOrder = WELLVOW_DEFAULT_COLOR_ORDER;
  uint8_t savedColorOrder = WELLVOW_DEFAULT_COLOR_ORDER;
  bool savedColorOrderPending = false;
  uint8_t pendingCalibrationOrder = COL_ORDER_RGB;
  uint8_t pendingCalibrationRgb[3] = {0, 0, 0};
  bool calibrationSnapshotValid = false;
  uint8_t calibrationPreviousBri = 0, calibrationPreviousMode = 0, calibrationPreviousSpeed = 128, calibrationPreviousIntensity = 128, calibrationPreviousPalette = 0;
  uint32_t calibrationPreviousColors[3] = {0, 0, 0};
  uint32_t lastAdvertisingCheck = 0;
  // 多设备联动(不经手机):linkRole 0 独立 / 1 主设备 / 2 从设备。主设备播放本机槽位时每 500ms 向局域网广播
  uint8_t linkRole = 0;
  uint32_t linkLastTx = 0;
  bool linkRestarting = false;
  //  · 对等发现:任何一台收到 'WVDQ' 都单播应答 'WVDR'(角色/灯数/播放槽/开关/亮度/组号/名字);主设备每 5s 广播一次
  //    发现包维护对等表(App / 网页经 /wellvow/peers 或 BLE DP 0x21 读取,也可 ?scan=1 立即扫一次)。
  //  · 组指令转发:手机经 BLE 只把 CMD 0x04(组号 + TLV)发给主设备,主设备原样广播 'WVGS',同网段所有控制器
  //  · 主从跟随:主设备自己收到的普通控制(开关/亮度/速度/方向/模式/颜色/强度/调色板/过渡/音乐)同步广播 'WVDP',
  //    从设备照做;播放帧序列的对齐仍由 'WVSY' 负责。经 UDP 收到的指令不再转发,避免回环。
  struct Peer { uint32_t ip; uint32_t seen; uint16_t leds; uint8_t role, playing, on, bri, group; char name[25]; };
  static constexpr uint8_t MAX_PEERS = 8;
  static constexpr uint32_t PEER_ONLINE_MS = 20000, PEER_DROP_MS = 90000;
  Peer peers[MAX_PEERS]; uint8_t peerCount = 0;
  uint32_t lastPeerQuery = 0;
  /* 预览流(DP 0x23):App 打开后,每 100ms 把灯带真实像素缓冲抽样成 40 个点经蓝牙推给手机,
   * 手机上的预览就是灯带此刻真的在显示的东西 —— 预设 / 场景 / DIY / WLED 效果全都一致,不再靠本地模拟。
   * App 每 5 秒续一次,6 秒没续自动停,免得 App 退到后台还在占蓝牙带宽。以前这个 DP 只有注释没有实现。 */
  uint32_t previewUntil = 0, lastPreviewTx = 0;
  static constexpr uint8_t PREVIEW_PIX = 40;
  void servicePreview() {
    if (!previewUntil) return;
    if (millis() >= previewUntil || !bleConnected) { previewUntil = 0; return; }
    if (millis() - lastPreviewTx < 100) return;
    lastPreviewTx = millis();
    uint8_t out[8 + PREVIEW_PIX * 3]; size_t n = 0;
    appendTlv(out, n, 0x23);
    sendFrame(CMD_DP_REPORT, out, n);
  }
  // 输出口允许切换的芯片类型(都是 WLED 的数字类型号):WS2812 RGB / SK6812·WS2814 RGBW / WS2811 400k / TM1814 / WS2805 / SM16825 / UCS8903 / APA106 / TM1914
  static bool outTypeAllowed(uint8_t t) { return t == 22 || t == 30 || t == 24 || t == 31 || t == 32 || t == 34 || t == 26 || t == 27 || t == 33; }
  // 把所有输出口的芯片类型改成 type,引脚 / 灯数 / 线序都不动
  void queueBusType(uint8_t type) {
    uint8_t nb = BusManager::getNumBusses(); if (!nb) return;
    busCfgClear(); uint16_t start = 0;
    for (uint8_t i = 0; i < nb; i++) {
      Bus *b = BusManager::getBus(i); if (!b) continue;
      uint8_t pins[5] = {255,255,255,255,255}; b->getPins(pins); uint16_t len = b->getLength();
      busCfgAdd(BusConfig(type, pins, start, len, b->getColorOrder(), b->isReversed(), b->skippedLeds(), b->getAutoWhiteMode(), b->getFrequency(), useGlobalLedBuffer, b->getLEDCurrent(), b->getMaxCurrent()));
      start += len;
    }
    busResizeTarget = start ? start : 1; doInitBusses = true;
    Serial.printf("[WLV-LEDS] output type -> %u over %u bus(es)\n", unsigned(type), unsigned(nb));
  }
  // 蓝牙收到的指令帧先排队,在 loop()(主任务,栈大)里执行。NimBLE 的 nimble_host 任务栈只有 4KB,
  // 在它的回调里直接跑 handleDpSet → colorUpdated → 预设/JSON 那一串会栈溢出(S3 上实测:
  // App 点效果 / 亮度 → "Stack canary watchpoint triggered (nimble_host)" 整机重启,表现为"开关能用、效果都没反应")。
  struct BleFrame { uint8_t cmd, len; uint16_t conn; uint8_t d[200]; };
  static constexpr uint8_t BLE_Q = 8;
  BleFrame bleQ[BLE_Q]; volatile uint8_t bleQHead = 0, bleQTail = 0;   // 单生产者(nimble_host)/ 单消费者(loop)
  void serviceBleQueue() {
    while (bleQTail != bleQHead) {
      BleFrame &f = bleQ[bleQTail];
      cp(0x300 + f.cmd);
      bleReplyConn = f.conn;   // 这一帧的应答只回给发它的连接
      switch (f.cmd) {
        case CMD_DP_SET: handleDpSet(f.d, f.len); break;
        case CMD_DP_GET: handleDpGet(f.d, f.len); break;
        case CMD_GROUP_SET: handleGroupSet(f.d, f.len); break;
        case CMD_WIFI: handleWifi(f.d, f.len); break;
        case CMD_BLOB: handleBlob(f.d, f.len); break;
        case CMD_IDENTITY_REQ: sendIdentity(); break;
        default: break;
      }
      bleReplyConn = 0xFFFF;
      bleQTail = uint8_t((bleQTail + 1) % BLE_Q);
    }
  }

  // 崩溃定位:RTC_NOINIT 变量跨软件复位保留;各关键路径进入时写编号,开机打印复位原因 + 最后编号。USB CDC 上 panic 文本常常丢失,只能靠这个
  static inline void cp(uint32_t v) { wlvCheckpoint = v; }
  // 单键写 NVS(帧序号 / 会话计数这种高频或须立即落盘的小值,不走完整 saveRuntimeConfig)
  void nvsPutSmall(const char *key, uint32_t v, bool u16) { nvs_handle h = 0; if (nvs_open("wlv", NVS_READWRITE, &h) != ESP_OK) return; if (u16) nvs_set_u16(h, key, uint16_t(v)); else nvs_set_u32(h, key, v); nvs_commit(h); nvs_close(h); }
  static constexpr uint16_t WELLVOW_MAX_LEDS = 512;   // 布线图/段配置可把控制器灯数调到的上限
  uint16_t busResizeTarget = 0;                      // 正在重建总线的目标灯数(0 = 无)
  uint32_t studioLastPacketMs = 0;
  uint32_t studioInhibitUntil = 0;   // 手机/网页发了控制指令后,这段时间内丢弃实时帧(人工控制优先),Studio 轮询到 live=false 会自行暂停  // 实时推流最后一个包的时间:手机失联 3s 自动退出推流,恢复原效果
  bool lastWifiLinkUp = false;      // 路由器链路上次状态,变化时主动经 BLE 上报(App 状态条不用轮询)
  uint8_t lastWifiResult = 4;       // 最近一次配网结果:1 成功 2 密码错 4 超时/未连接(网页 /wellvow/info 用)
  uint8_t pendingSegmentCount = 0;
  uint16_t pendingSegmentLengths[32] = {0};   // 与 WLED ESP32 的 MAX_NUM_SEGMENTS(32)对齐;App 段配置/DIY 按段效果最多 32 段

  static constexpr uint8_t CMD_DP_SET = 0x01;
  static constexpr uint8_t CMD_DP_GET = 0x02;
  static constexpr uint8_t CMD_DP_REPORT = 0x03;
  static constexpr uint8_t CMD_GROUP_SET = 0x04;
  static constexpr uint8_t CMD_AUTH = 0x40;
  static constexpr uint8_t CMD_WIFI = 0x42;
  static constexpr uint8_t CMD_BLOB = 0x30;
  /* 设备身份(《WELLVOW DIY 固件与 APP 修改规范》第三节):
   *   APP 发 0x16 请求 → 设备回 0x17 [version=1, MAC(6), product_role(1), chip_type(1)]。
   * MAC 一律用 esp_efuse_mac_get_default() 读出厂 eFuse 的 6 字节原始顺序,
   * 不用 ESP.getEfuseMac() 的十六进制打印顺序(那个字节序是反的)。
   * 设备只报自己读到的 MAC,不接受 APP 传来的 MAC —— DIY 码与设备密钥都由它派生。 */
  static constexpr uint8_t CMD_IDENTITY_REQ = 0x16;
  static constexpr uint8_t CMD_IDENTITY_RSP = 0x17;
  static constexpr uint8_t IDENTITY_VERSION = 2;   // v2:上报的是算好的 DIY 码,不再报 MAC
  static constexpr uint8_t PRODUCT_ROLE = 2;
#if defined(CONFIG_IDF_TARGET_ESP32S3)
  static constexpr uint8_t CHIP_TYPE = 2;      // 1 ESP32-MINI-N4 / 2 ESP32-S3-N8R2
#else
  static constexpr uint8_t CHIP_TYPE = 1;
#endif

  static uint16_t readU16(const uint8_t *p) { return (uint16_t(p[0]) << 8) | p[1]; }
  static uint32_t readU32(const uint8_t *p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }

  // 只发给一条连接(NimBLE 1.4 的 notify() 只能群发,这里直接走 host 层按连接句柄发)
  void bleNotifyTo(uint16_t h, const uint8_t *frame, size_t n) {
    if (h == 0xFFFF || !notifyChar) return;
    os_mbuf *om = ble_hs_mbuf_from_flat(frame, n);
    if (om) ble_gattc_notify_custom(h, notifyChar->getHandle(), om);   // om 由 host 释放
  }
  // to = 0xFFFF → 群发给所有已鉴权连接(状态变化 / 预览流);否则只发给那条连接(指令应答 / 鉴权应答)
  void sendFrame(uint8_t cmd, const uint8_t *data, size_t len, uint16_t to = 0xFFFE) {
    if (!bleConnected || !notifyChar || len > 245) return;
    if (to == 0xFFFE) to = bleReplyConn;
    uint8_t frame[250];
    frame[0] = 0xAA; frame[1] = 0x55; frame[2] = cmd; frame[3] = uint8_t(len);
    uint16_t sum = cmd + len;
    for (size_t i = 0; i < len; i++) { frame[4 + i] = data[i]; sum += data[i]; }
    frame[4 + len] = uint8_t(sum);
    notifyChar->setValue(frame, len + 5);
    if (to != 0xFFFF) { bleNotifyTo(to, frame, len + 5); return; }
    for (auto &c : bleConns) if (c.handle != 0xFFFF && c.auth) bleNotifyTo(c.handle, frame, len + 5);
  }

  void deny(uint16_t to) { const uint8_t d[] = {0x05}; sendFrame(CMD_AUTH, d, sizeof(d), to); }

  /* 本机 DIY 码(规范第二节):
   *   C(M) = HMAC-SHA256(B, "WELLVOW-DIY-CODE-V1" || M)[0..15]
   * M 用 esp_efuse_mac_get_default() 的 6 字节原始顺序;B 是品牌密钥,四个 32 位字按**大端**序列化成 16 字节。
   * 两端必须用同一套序列化,否则以后 DIY 库算出来的设备密钥对不上。 */
  /* DIY 固件(-D WELLVOW_DIY_CODE="WV1-…" 编译):没有品牌密钥。设备密钥 K = HMAC-SHA256(C, "WELLVOW-DEVICE-AUTH-V1" || MAC)[0..15],
   * C 就是用户从 App「我的 → DIY 码」复制、粘贴进编译参数的那串码(与这颗芯片的 eFuse MAC 绑定,换芯片即失效)。
   * App 拿到 0x14 应答里的 'D'+MAC 后,用品牌密钥自己算出 C 再算 K,两边一致。 */
#ifdef WELLVOW_DIY_CODE
  static bool parseDiyCode(uint8_t out[16]) {
    const char *t = WELLVOW_DIY_CODE; if (!t) return false;
    if (strncmp(t, "WV1-", 4) == 0) t += 4;
    if (strlen(t) != 32) return false;
    for (uint8_t i = 0; i < 16; i++) { char h[3] = {t[i*2], t[i*2+1], 0}; char *e = nullptr; long v = strtol(h, &e, 16); if (*e) return false; out[i] = uint8_t(v); }
    return true;
  }
  bool computeDiyCode(uint8_t out[16]) { return parseDiyCode(out); }
#else
  bool computeDiyCode(uint8_t out[16]) {
    uint8_t mac[6] = {0};
    if (esp_efuse_mac_get_default(mac) != ESP_OK) return false;
    const uint32_t *brand = WELLVOW_BRAND_KEY;
    uint8_t key[16];
    for (uint8_t i = 0; i < 4; i++) {
      key[i*4+0] = uint8_t(brand[i] >> 24); key[i*4+1] = uint8_t(brand[i] >> 16);
      key[i*4+2] = uint8_t(brand[i] >> 8);  key[i*4+3] = uint8_t(brand[i]);
    }
    static const char label[] = "WELLVOW-DIY-CODE-V1";
    uint8_t msg[sizeof(label) - 1 + 6];
    memcpy(msg, label, sizeof(label) - 1);
    memcpy(msg + sizeof(label) - 1, mac, 6);

    uint8_t mac32[32];
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!info) return false;
    int rc = mbedtls_md_hmac(info, key, sizeof(key), msg, sizeof(msg), mac32);
    memset(key, 0, sizeof(key));               // 品牌密钥用完立刻从栈上抹掉
    if (rc != 0) return false;
    memcpy(out, mac32, 16);
    return true;
  }
#endif

  /* 上报本机身份:[version=2, DIY 码(16), product_role, chip_type]。
   * 按用户要求由固件算好 DIY 码再上报,APP 直接显示,不再自己从 MAC 推导。
   * 注意:这条链路会把 DIY 码经蓝牙发出,所以必须在鉴权之后才受理(serviceBleQueue 前已判 authenticated)。 */
  void sendIdentity() {
    uint8_t code[16] = {0};
    if (!computeDiyCode(code)) { Serial.println("[WLV-ID] diy code derivation failed"); return; }
    uint8_t d[19];
    d[0] = IDENTITY_VERSION;
    memcpy(d + 1, code, 16);
    d[17] = PRODUCT_ROLE;
    d[18] = CHIP_TYPE;
    sendFrame(CMD_IDENTITY_RSP, d, sizeof(d));
    uint8_t mac[6] = {0}; esp_efuse_mac_get_default(mac);
    Serial.printf("[WLV-ID] mac=%02X:%02X:%02X:%02X:%02X:%02X role=%u chip=%u diy=WV1-%02X%02X...%02X%02X\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                  unsigned(PRODUCT_ROLE), unsigned(CHIP_TYPE),
                  code[0], code[1], code[14], code[15]);
  }

  void xteaWith(uint32_t v[2], const uint32_t key[4]) {
    uint32_t v0 = v[0], v1 = v[1], sum = 0;
    for (uint8_t i = 0; i < 32; i++) {
      v0 += ((((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + key[sum & 3]));
      sum += 0x9E3779B9;
      v1 += ((((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + key[(sum >> 11) & 3]));
    }
    v[0] = v0; v[1] = v1;
  }

  void loadDeviceKey() {
#ifdef WELLVOW_DIY_CODE
    // DIY 固件:K = HMAC-SHA256(C, "WELLVOW-DEVICE-AUTH-V1" || MAC)[0..15],按大端拆成 4 个 32 位字(与 App setDeviceKey 一致)
    uint8_t code[16] = {0}, mac[6] = {0};
    if (!parseDiyCode(code) || esp_efuse_mac_get_default(mac) != ESP_OK) { Serial.println("[WLV-DIY] DIY code invalid: check -D WELLVOW_DIY_CODE"); return; }
    static const char label[] = "WELLVOW-DEVICE-AUTH-V1";
    uint8_t msg[sizeof(label) - 1 + 6]; memcpy(msg, label, sizeof(label) - 1); memcpy(msg + sizeof(label) - 1, mac, 6);
    uint8_t mac32[32]; const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!info || mbedtls_md_hmac(info, code, 16, msg, sizeof(msg), mac32) != 0) { Serial.println("[WLV-DIY] HMAC failed"); return; }
    for (uint8_t i = 0; i < 4; i++) deviceKey[i] = (uint32_t(mac32[i*4]) << 24) | (uint32_t(mac32[i*4+1]) << 16) | (uint32_t(mac32[i*4+2]) << 8) | mac32[i*4+3];
    for (uint8_t i = 0; i < 6; i++) deviceSeed[i] = mac[i]; deviceSeed[6] = 'D'; deviceSeed[7] = 'Y';   // DIY 固件的 seed 只是占位
    Serial.printf("[WLV-DIY] DIY build, code WV1-%02X%02X...%02X%02X, mac %02X:%02X:%02X:%02X:%02X:%02X\n", code[0], code[1], code[14], code[15], mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return;
#else
    // 2026-09-21:改成上架版 App(caps.js h1 / theme.js p0 常量被改过)实际运行时的密钥,否则商店版鉴权全失败。
    // 量产前用 tools/gen_brand_key.mjs 两端一起换成随机新密钥。原开发密钥 57454C4C 564F5721 4F50454E 50495821 已作废。
    const uint32_t *brand = WELLVOW_BRAND_KEY;
    nvs_handle handle = 0;
    size_t seedLen = sizeof(deviceSeed);
    esp_err_t opened = nvs_open("wlvsec", NVS_READWRITE, &handle);
    bool found = opened == ESP_OK && nvs_get_blob(handle, "seed", deviceSeed, &seedLen) == ESP_OK && seedLen == sizeof(deviceSeed);
    if (!found) {
      uint64_t mac = ESP.getEfuseMac();
      uint32_t r0 = esp_random(), r1 = esp_random();
      for (uint8_t i = 0; i < 6; i++) deviceSeed[i] = uint8_t(mac >> (i * 8));
      deviceSeed[6] = uint8_t(r0 ^ (r0 >> 8) ^ (r0 >> 16) ^ (r0 >> 24));
      deviceSeed[7] = uint8_t(r1 ^ (r1 >> 8) ^ (r1 >> 16) ^ (r1 >> 24));
      for (uint8_t i = 0; i < 6; i++) deviceSeed[i] ^= uint8_t((i < 3 ? r0 : r1) >> ((i % 3) * 8));
      if (opened == ESP_OK) { nvs_set_blob(handle, "seed", deviceSeed, sizeof(deviceSeed)); nvs_commit(handle); }
    }
    if (opened == ESP_OK) nvs_close(handle);
    uint32_t s0 = (uint32_t(deviceSeed[0]) << 24) | (uint32_t(deviceSeed[1]) << 16) | (uint32_t(deviceSeed[2]) << 8) | deviceSeed[3];
    uint32_t s1 = (uint32_t(deviceSeed[4]) << 24) | (uint32_t(deviceSeed[5]) << 16) | (uint32_t(deviceSeed[6]) << 8) | deviceSeed[7];
    uint32_t a[2] = {s0, s1}, b[2] = {s1 ^ 0xA5A5A5A5u, s0 ^ 0x5A5A5A5Au};
    xteaWith(a, brand); xteaWith(b, brand);
    deviceKey[0] = a[0]; deviceKey[1] = a[1]; deviceKey[2] = b[0]; deviceKey[3] = b[1];
#endif
  }

  // Same XTEA word/byte convention as packages/sdk/src/auth.js.
  void xtea(uint8_t block[8]) {
    uint32_t v0, v1;
    memcpy(&v0, block, 4); memcpy(&v1, block + 4, 4);
    uint32_t words[2] = {v0, v1}; xteaWith(words, deviceKey);
    v0 = words[0]; v1 = words[1];
    memcpy(block, &v0, 4); memcpy(block + 4, &v1, 4);
  }

  // ---- Wi-Fi HTTP API 小工具(网页 / 桌面 Studio / App 局域网直连共用) ----
  static int slotParam(AsyncWebServerRequest *request) {
    const char *keys[] = {"slot", "id"};
    for (const char *k : keys) {
      if (request->hasParam(k, true)) return request->getParam(k, true)->value().toInt();
      if (request->hasParam(k)) return request->getParam(k)->value().toInt();
    }
    return -1;
  }
  static String jsonEscape(const String &in) {
    String o; o.reserve(in.length() + 4);
    for (size_t i = 0; i < in.length(); i++) { char c = in[i]; if (c == '"' || c == '\\') o += '\\'; if (c == '\n' || c == '\r') continue; o += c; }
    return o;
  }
  String readSlotName(uint8_t slot) {
    char path[20]; snprintf(path, sizeof(path), "/%u.name", slot);
    File f = WLED_FS.open(path, "r"); if (!f) return String();
    String n = f.readStringUntil('\n'); f.close(); n.trim(); return n.substring(0, 40);
  }
  void writeSlotName(uint8_t slot, const String &name) {
    char path[20]; snprintf(path, sizeof(path), "/%u.name", slot);
    File f = WLED_FS.open(path, "w"); if (!f) return; f.print(name.substring(0, 40)); f.close();
  }

  void handleAuth(const uint8_t *d, size_t len, BleConn &c) {
    if (!len) return;
    uint8_t *nonce = c.nonce; bool authenticated = false;
    if (d[0] == 0x14) {
#ifdef WELLVOW_DIY_CODE
      uint8_t out[16] = {0x15}; memcpy(out + 1, deviceSeed, sizeof(deviceSeed)); out[9] = 'D'; esp_efuse_mac_get_default(out + 10);   // [0x15, seed8, 'D', mac6]:App 据此按 DIY 方案算密钥
#else
      uint8_t out[9] = {0x15}; memcpy(out + 1, deviceSeed, sizeof(deviceSeed));
#endif
      sendFrame(CMD_AUTH, out, sizeof(out), c.handle);
      return;
    }
    if (d[0] == 0x01) {
      esp_fill_random(nonce, 8);
      c.pending = true;
      uint8_t out[9] = {0x02}; memcpy(out + 1, nonce, 8);
      sendFrame(CMD_AUTH, out, sizeof(out), c.handle);
    } else if (d[0] == 0x03 && len == 9 && c.pending) {
      uint8_t expected[8]; memcpy(expected, nonce, 8); xtea(expected);
      authenticated = memcmp(expected, d + 1, 8) == 0; c.auth = authenticated;
#ifdef WLV_AUTH_DEBUG
      // 联调用:打印 seed / nonce / 期望应答 / 实收应答,和 App 端 KDF 对比(量产不要开)
      Serial.printf("[WLV-AUTH] seed=%02X%02X%02X%02X%02X%02X%02X%02X nonce=%02X%02X%02X%02X%02X%02X%02X%02X exp=%02X%02X%02X%02X%02X%02X%02X%02X got=%02X%02X%02X%02X%02X%02X%02X%02X ok=%u\n",
        deviceSeed[0],deviceSeed[1],deviceSeed[2],deviceSeed[3],deviceSeed[4],deviceSeed[5],deviceSeed[6],deviceSeed[7],
        nonce[0],nonce[1],nonce[2],nonce[3],nonce[4],nonce[5],nonce[6],nonce[7],
        expected[0],expected[1],expected[2],expected[3],expected[4],expected[5],expected[6],expected[7],
        d[1],d[2],d[3],d[4],d[5],d[6],d[7],d[8], unsigned(authenticated));
#endif
      c.pending = false;
      const uint8_t out[] = {0x04, uint8_t(authenticated)};
      sendFrame(CMD_AUTH, out, sizeof(out), c.handle);
      Serial.printf("[WLV-BLE] auth %s on conn %u (%u/%u connected)\n", authenticated ? "ok" : "FAIL", unsigned(c.handle), unsigned(bleConnCount()), unsigned(BLE_MAX_CONN));
    }
  }

  uint8_t  lastSeenMode = 255;   // 效果号变化监视
  // 开机渐亮状态
  bool     bootFadePending = false;
  uint8_t  bootFadeTarget  = 0;
  uint32_t bootFadeAt      = 0;
  /* 开机渐亮:等 500ms(让总线初始化、第一帧刷出来)再把亮度渐变到目标值,
   * 用的是 WLED 自己的亮度过渡,所以和效果动画是同一套时间轴,不会打架。 */
  /* 动画整体提速:WLED 的效果时钟是 strip.now = millis() + timebase,绝大多数 1D 效果
   * 都按它算相位(例如 rainbow: strip.now * ((speed>>2)+2))。这里每毫秒把 timebase 多推进
   * (WELLVOW_SPEED_BOOST-1) 毫秒,效果时钟就跑 BOOST 倍 —— 速度滑块顶到 255 也还能再快一倍。
   * 亮度渐变 / 开关机渐变用的是真实 millis(),不受影响。 */
  uint32_t speedBoostLast = 0;
  void serviceSpeedBoost() {
#if WELLVOW_SPEED_BOOST > 1
    uint32_t nowMs = millis();
    if (!speedBoostLast) { speedBoostLast = nowMs; return; }
    uint32_t d = nowMs - speedBoostLast;
    if (!d) return;
    speedBoostLast = nowMs;
    strip.timebase += d * (WELLVOW_SPEED_BOOST - 1);
#endif
  }
  void serviceBootFade() {
    if (!bootFadePending) return;
    if (!bootFadeAt) { bootFadeAt = millis() + 500; return; }
    if (millis() < bootFadeAt) return;
    bootFadePending = false;
    /* auto-save 的预设 250 是延后到主循环才套用的:setup() 里记下的目标亮度是 cfg 的默认值(255),不是用户上次的亮度。
     * 到这里预设已经套用过了,bri 若非 0 就是用户上次的亮度,以它为准(10-23 断电记忆:亮度也要恢复)。 */
    if (bri > 0) bootFadeTarget = bri;
    bri = 0; briT = 0; briOld = 0;   // 从 0 渐亮到目标
    bri = bootFadeTarget;
    briLast = bootFadeTarget;
    /* 上次停在音乐效果(auto-save 预设记的是效果号,记不住"音乐模式开着"这件事):
     * 不把 AudioReactive 一起打开的话,开机就是一条不亮的灯带,用户以为设备坏了。
     * 灵敏度用 NVS 里存的上次值。 */
    if (isAudioFx(strip.getMainSegment().mode)) {
      if (musicWanted) {
        // 用户上次就是开着音乐模式关机的:照旧开麦克风,灯带随声音跑
        musicActive = true;
        setAudioReactive(true);
        Serial.printf("[WLV-MUSIC] boot restored audio fx=%u, microphone on (gain=%u squelch=%u)\n",
                      unsigned(strip.getMainSegment().mode), unsigned(audioGain), unsigned(audioSquelch));
      } else {
        // 没开过音乐模式却停在音频效果上(auto-save 只记效果号):退回进音乐前那个效果,
        // 否则开机就是一条不亮的灯带,用户分不清是坏了还是在等声音。
        uint8_t back = isAudioFx(musicPreviousFx) ? 0 : musicPreviousFx;
        for (uint8_t i = 0; i < strip.getSegmentsNum(); i++) applyEffect(strip.getSegment(i), back);
        syncGlobalsFromSegment();
        Serial.printf("[WLV-MUSIC] boot: audio fx but music mode was off -> fall back to fx=%u\n", unsigned(back));
      }
    }
#if defined(USERMOD_AUDIOREACTIVE) && defined(WELLVOW_PIN_MIC_DATA)
    if (!musicActive) { AudioReactive::wellvowAudioPower(false); Serial.printf("[WLV-MUSIC] boot: music off -> audio released, heap=%u\n", unsigned(ESP.getFreeHeap())); }
#endif
    strip.setTransition(WELLVOW_BOOT_FADE_MS);
    jsonTransitionOnce = true;
    stateUpdated(CALL_MODE_DIRECT_CHANGE);
    Serial.printf("[WLV-FADE] boot fade-in to bri=%u over %ums\n", unsigned(bootFadeTarget), unsigned(WELLVOW_BOOT_FADE_MS));
  }

  /* 开 / 关都走渐变,不硬切。WLED 的亮度过渡由 briOld→briT 在 strip.getTransition() 毫秒内完成,
   * 这里临时把这一次的过渡时长抬到 WELLVOW_POWER_FADE_MS(jsonTransitionOnce 让它只影响这一次,
   * 过渡结束后 handleTransitions() 自动恢复成全局 transitionDelay)。 */
  void setPower(bool on) {
    bri = on ? (briLast ? briLast : 255) : 0;   // 没有上次亮度时开到满,不是半亮
    // 过渡时长由 handleDpSet 按整帧内容统一决定(带开关机的帧用 WELLVOW_POWER_FADE_MS),这里不再单独设
  }

  void saveRuntimeConfig() {
    nvs_handle h = 0;
    if (nvs_open("wlv", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "group", syncGroups);
    nvs_set_u8(h, "schedn", scheduleCount);
    nvs_set_u8(h, "colorord", savedColorOrder);
    nvs_set_u8(h, "linkrole", linkRole);
    nvs_set_u8(h, "augain", audioGain); nvs_set_u8(h, "ausquel", audioSquelch); nvs_set_u8(h, "ausens", audioSens); nvs_set_u16(h, "aufloor", audioFloor); nvs_set_u32(h, "aumsc", uint32_t(micScale * 1000000.0f));   // 音乐灵敏度 / 底噪:重启后保持
    nvs_set_u8(h, "music", musicActive ? 1 : 0); nvs_set_u8(h, "mprevfx", musicPreviousFx);   // 音乐模式开关 + 进音乐前的效果
    if (scheduleCount) nvs_set_blob(h, "sched", schedules, scheduleCount * sizeof(ScheduleSlot));
    nvs_commit(h); nvs_close(h);
  }

  void loadRuntimeConfig() {
    nvs_handle h = 0; if (nvs_open("wlv", NVS_READONLY, &h) != ESP_OK) return;
    uint8_t group = 1, count = 0; nvs_get_u8(h, "group", &group); nvs_get_u8(h, "schedn", &count);
    uint8_t order = WELLVOW_DEFAULT_COLOR_ORDER;
    bool haveOrder = nvs_get_u8(h, "colorord", &order) == ESP_OK;
#ifdef WELLVOW_DEVKIT_S3
    if (haveOrder && order == COL_ORDER_RGB) order = COL_ORDER_GRB;   // 开发板:之前存过 RGB 的话按板载 WS2812 纠正为 GRB
#endif
    if (haveOrder && order <= COL_ORDER_MAX) {
      savedColorOrder = order; pendingColorOrder = order; savedColorOrderPending = true;
    }
    if (group) syncGroups = receiveGroups = group;
    uint8_t role = 0; if (nvs_get_u8(h, "linkrole", &role) == ESP_OK && role <= 2) linkRole = role;
    audioGain = 40;   // 增益固定 ×1,由 AGC 归一化;旧版存的增益不再使用
    uint8_t aq = 0; if (nvs_get_u8(h, "ausquel", &aq) == ESP_OK) audioSquelch = aq;
    { uint8_t v8 = 0; if (nvs_get_u8(h, "ausens", &v8) == ESP_OK && v8 >= 1 && v8 <= 10) audioSens = v8; uint16_t f = 0; if (nvs_get_u16(h, "aufloor", &f) == ESP_OK && f >= 4 && f <= 4000) audioFloor = f; audioSquelch = squelchFor(audioSens); uint32_t ms = 0; if (nvs_get_u32(h, "aumsc", &ms) == ESP_OK && ms >= 500 && ms <= 8000000) micScale = ms / 1000000.0f; }
    uint8_t mu = 0; if (nvs_get_u8(h, "music", &mu) == ESP_OK) musicWanted = mu != 0;
    uint8_t mp = 0; if (nvs_get_u8(h, "mprevfx", &mp) == ESP_OK) musicPreviousFx = mp;
    if (count <= 8) {
      size_t n = count * sizeof(ScheduleSlot);
      if (!count || nvs_get_blob(h, "sched", schedules, &n) == ESP_OK) scheduleCount = count;
    }
    nvs_close(h);
  }

  /* 自定义调色板(2026-10-21,日出多套配色):n 个颜色等距铺成 WLED 的渐变调色板,放进 customPalettes[slot](调色板号 255-slot),
   * 同时写回 /palette<slot>.json,重启后 loadCustomPalettes() 还能读到。日出用槽 0(255)、日落用槽 1(254)。 */
  void setCustomPalette(uint8_t slot, const uint8_t *rgb, uint8_t n) {
    uint8_t tcp[4 * 9]; uint8_t k = 0;
    for (uint8_t i = 0; i < n; i++) { tcp[k++] = (uint8_t)((i * 255UL) / (n - 1)); tcp[k++] = rgb[i*3]; tcp[k++] = rgb[i*3+1]; tcp[k++] = rgb[i*3+2]; }
    if (slot < 2) { g_skyKeyN[slot] = n; memcpy(g_skyKeys[slot], rgb, n * 3); }   // 日出/日落效果直接用关键色插值
    CRGBPalette16 pal; pal.loadDynamicGradientPalette(tcp);
    while (strip.customPalettes.size() <= slot) strip.customPalettes.push_back(pal);
    strip.customPalettes[slot] = pal;
    char fn[16]; snprintf(fn, sizeof(fn), "/palette%u.json", unsigned(slot));
    File f = WLED_FS.open(fn, "w");
    if (f) { f.print(F("{\"palette\":[")); for (uint8_t i = 0; i < n; i++) { if (i) f.print(','); f.printf("%u,\"%02x%02x%02x\"", unsigned((i * 255UL) / (n - 1)), rgb[i*3], rgb[i*3+1], rgb[i*3+2]); } f.print(F("]}")); f.close(); }
    for (uint8_t i = 0; i < strip.getSegmentsNum(); i++) { Segment &sg = strip.getSegment(i); if (sg.palette == 255 - slot) { sg.palette = 0; sg.setPalette(255 - slot); sg.markForReset(); } }   // 正在用这张的段重新载入,并让日出从第一个颜色重新开始
    Serial.printf("[WLV-PAL] custom palette slot %u <- %u colors\n", unsigned(slot), unsigned(n));
  }
  void queueSegmentConfig(const uint8_t *v, size_t len) {
    if (!len || v[0] < 1 || v[0] > 32 || v[0] > MAX_NUM_SEGMENTS || len < size_t(1 + v[0] * 2)) return;
    uint16_t total = busResizeTarget ? busResizeTarget : strip.getLengthTotal(), used = 0;   // 总线重建中按目标灯数校验
    /* 以前只要 App 给的分段总数比设备实际灯数多一颗,整份分段配置就被**静默丢掉**:
     * 段没建起来 → 后面按段发的效果(DP 0x17)和按段颜色(DP 0x07)全部被 `sid < 段数` 挡掉,
     * 表现就是"DIY 点了没反应",而且日志里什么都看不到。
     * 改成按实际灯数裁剪:能放下几段就建几段,最后一段截断,至少让用户看到东西。 */
    uint8_t cnt = 0;
    for (uint8_t i = 0; i < v[0]; i++) {
      uint16_t n = readU16(v + 1 + i * 2);
      if (!n) continue;
      if (used >= total) break;                       // 灯数用完了,后面的段放弃
      if (used + n > total) n = total - used;         // 最后一段截断
      pendingSegmentLengths[cnt++] = n; used += n;
    }
    if (!cnt) return;
    if (cnt != v[0]) Serial.printf("[WLV-SEG] requested %u segs but only %u LEDs -> trimmed to %u segs\n",
                                   unsigned(v[0]), unsigned(total), unsigned(cnt));
    pendingSegmentCount = cnt;
    segmentConfigPending = true;
    Serial.printf("[WLV-SEG] queued count=%u leds=%u\n", unsigned(pendingSegmentCount), unsigned(used));
  }

  /* WLED 0.15.0-b6 的 busConfigs 是 `BusConfig* busConfigs[N]` 指针数组(0.15.1 起才换成 std::vector),
   * 主循环 doInitBusses 会逐个取用、delete 并置回 nullptr。这里包两个小函数,保持下面的调用点不变。 */
  static void busCfgClear() {
    for (unsigned i = 0; i < WLED_MAX_BUSSES + WLED_MIN_VIRTUAL_BUSSES; i++) {
      if (busConfigs[i]) { delete busConfigs[i]; busConfigs[i] = nullptr; }
    }
  }
  static void busCfgAdd(const BusConfig &bc) {
    for (unsigned i = 0; i < WLED_MAX_BUSSES + WLED_MIN_VIRTUAL_BUSSES; i++) {
      if (!busConfigs[i]) { busConfigs[i] = new BusConfig(bc); return; }
    }
  }

  // 按现有输出口(引脚/类型/线序等不变)的长度比例把总灯数重建为 count;WLED 主循环里 doInitBusses 生效
  void queueBusResize(uint16_t count) {
    uint8_t nb = BusManager::getNumBusses(); if (!nb || count == 0) return;
    uint16_t oldTotal = strip.getLengthTotal(); if (!oldTotal) return;
    busCfgClear(); uint16_t start = 0;
    for (uint8_t i = 0; i < nb; i++) {
      Bus *b = BusManager::getBus(i); if (!b) continue;
      uint8_t pins[5] = {255,255,255,255,255}; b->getPins(pins);
      uint16_t len = (i == nb - 1) ? (count - start) : uint16_t((uint32_t(count) * b->getLength() + oldTotal / 2) / oldTotal);
      if (len == 0 && i < nb - 1) len = 1;
      if (start + len > count) len = count - start;
      busCfgAdd(BusConfig(b->getType(), pins, start, len, b->getColorOrder(), b->isReversed(), b->skippedLeds(), b->getAutoWhiteMode(), b->getFrequency(), useGlobalLedBuffer, b->getLEDCurrent(), b->getMaxCurrent()));
      start += len;
    }
    busResizeTarget = count; doInitBusses = true;
    Serial.printf("[WLV-LEDS] resize %u -> %u over %u bus(es)\n", unsigned(oldTotal), unsigned(count), unsigned(nb));
  }
  // 显式指定各输出口灯数(网页/App 配置页):lens[i] 对应第 i 路输出,总数 ≤ 512
  void queueBusLengths(const uint16_t *lens, uint8_t n) {
    uint8_t nb = BusManager::getNumBusses(); if (!nb) return; if (n > nb) n = nb;
    busCfgClear(); uint16_t start = 0;
    for (uint8_t i = 0; i < nb; i++) {
      Bus *b = BusManager::getBus(i); if (!b) continue;
      uint8_t pins[5] = {255,255,255,255,255}; b->getPins(pins);
      uint16_t len = (i < n) ? lens[i] : 0; if (start + len > WELLVOW_MAX_LEDS) len = WELLVOW_MAX_LEDS - start;
      if (len == 0) continue;   // 0 = 这一路不用
      busCfgAdd(BusConfig(b->getType(), pins, start, len, b->getColorOrder(), b->isReversed(), b->skippedLeds(), b->getAutoWhiteMode(), b->getFrequency(), useGlobalLedBuffer, b->getLEDCurrent(), b->getMaxCurrent()));
      start += len;
    }
    if (!start) return;
    busResizeTarget = start; doInitBusses = true;
    Serial.printf("[WLV-LEDS] outputs set, total %u\n", unsigned(start));
  }
  /* 输出口布局(2026-10-21 用户要求:OUT1/OUT2 各自的起点、终点、方向、总点数):
   * 固定两路 OUT1=GPIO15 / OUT2=GPIO2;每路给 start(0 基)、len、rev(反向 = 该口内像素顺序倒过来,数据从远端往近端编号)。
   * len=0 的口不建总线;总灯数 = 各口 len 之和(WLED 的总线长度就是这么算的),段表随后按新总数重建。 */
  static Bus *findOutBus(uint8_t pin) {
    for (uint8_t i = 0; i < BusManager::getNumBusses(); i++) { Bus *b = BusManager::getBus(i); if (!b) continue; uint8_t pins[5] = {255,255,255,255,255}; b->getPins(pins); if (pins[0] == pin) return b; }
    return nullptr;
  }
  void queueOutputs(const uint16_t *starts, const uint16_t *lens, const int8_t *revs, uint8_t n) {
    static const uint8_t OUTP[2] = {WELLVOW_PIN_OUT1, WELLVOW_PIN_OUT2};
    if (n > 2) n = 2;
    Bus *tpl = BusManager::getBus(0);
    busCfgClear(); uint16_t total = 0, autoStart = 0;
    for (uint8_t i = 0; i < n; i++) {
      uint16_t len = lens[i]; if (!len) continue;
      uint16_t st = starts ? starts[i] : autoStart; if (st >= WELLVOW_MAX_LEDS) continue;
      if (st + len > WELLVOW_MAX_LEDS) len = WELLVOW_MAX_LEDS - st;
      Bus *b = findOutBus(OUTP[i]); if (!b) b = tpl;
      bool rev = (revs && revs[i] >= 0) ? revs[i] != 0 : (b ? b->isReversed() : false);
      uint8_t pins[5] = {OUTP[i],255,255,255,255};
      busCfgAdd(BusConfig(b ? b->getType() : TYPE_WS2812_RGB, pins, st, len, b ? b->getColorOrder() : COL_ORDER_GRB, rev, b ? b->skippedLeds() : 0,
                          b ? b->getAutoWhiteMode() : RGBW_MODE_MANUAL_ONLY, b ? b->getFrequency() : 0, useGlobalLedBuffer,
                          b ? b->getLEDCurrent() : LED_MILLIAMPS_DEFAULT, b ? b->getMaxCurrent() : ABL_MILLIAMPS_DEFAULT));
      autoStart = st + len; total += len;
      Serial.printf("[WLV-OUT] OUT%u GPIO%u start=%u len=%u rev=%u\n", unsigned(i + 1), unsigned(OUTP[i]), unsigned(st), unsigned(len), unsigned(rev));
    }
    if (!total) return;
    busResizeTarget = total; doInitBusses = true;
    Serial.printf("[WLV-OUT] layout set, total %u\n", unsigned(total));
  }
  // 输出口布局 JSON:固定列出 OUT1/OUT2(没建总线的口 len=0),App / 网页配置页用
  String outputsJson() {
    static const uint8_t OUTP[2] = {WELLVOW_PIN_OUT1, WELLVOW_PIN_OUT2};
    String j = F("{\"outputs\":[");
    for (uint8_t i = 0; i < 2; i++) { Bus *b = findOutBus(OUTP[i]); if (i) j += ',';
      j += F("{\"pin\":"); j += OUTP[i]; j += F(",\"start\":"); j += b ? b->getStart() : 0; j += F(",\"len\":"); j += b ? b->getLength() : 0; j += F(",\"rev\":"); j += (b && b->isReversed()) ? F("true") : F("false"); j += '}'; }
    j += F("],\"total\":"); j += strip.getLengthTotal(); { Bus *b0 = BusManager::getBus(0); j += F(",\"type\":"); j += b0 ? b0->getType() : 0; } j += F(",\"max\":512}");   // type = 输出口芯片类型(WLED TYPE_*),STRIP 的灯带设置用
    return j;
  }
  /* 补齐第二路输出。
   * 这块板有两个幻彩输出口(OUT1=GPIO15 / OUT2=GPIO2),但 WLED 只按 DATA_PINS 建默认总线,
   * 已经存过 cfg.json 的设备更是只认文件里那一条 —— 结果就是"只有一个口能亮"。
   * 开机时检查:只有一条数字总线、第二个脚也没被占用、且第一条仍用着出厂的 OUT1,
   * 就按当前灯数平分补一条并写回 cfg。用户自己改过引脚就完全不插手。 */
  bool secondOutChecked = false;
  void ensureSecondOutput() {
    if (secondOutChecked || doInitBusses || busResizeTarget) return;
    secondOutChecked = true;
    if (BusManager::getNumBusses() != 1) return;
    Bus *b0 = BusManager::getBus(0);
    if (!b0 || !Bus::isDigital(b0->getType()) || Bus::is2Pin(b0->getType())) return;
    uint8_t p0[5] = {255,255,255,255,255}; b0->getPins(p0);
    if (p0[0] != WELLVOW_PIN_OUT1) return;
    if (PinManager::isPinAllocated(WELLVOW_PIN_OUT2)) return;
    uint16_t total = strip.getLengthTotal(); if (total < 2) return;
    uint16_t half = total / 2, rest = total - half;
    busCfgClear();
    uint8_t pa[5] = {WELLVOW_PIN_OUT1,255,255,255,255};
    uint8_t pb[5] = {WELLVOW_PIN_OUT2,255,255,255,255};
    busCfgAdd(BusConfig(b0->getType(), pa, 0,    half, b0->getColorOrder(), b0->isReversed(), b0->skippedLeds(), b0->getAutoWhiteMode(), b0->getFrequency(), useGlobalLedBuffer, b0->getLEDCurrent(), b0->getMaxCurrent()));
    busCfgAdd(BusConfig(b0->getType(), pb, half, rest, b0->getColorOrder(), b0->isReversed(), b0->skippedLeds(), b0->getAutoWhiteMode(), b0->getFrequency(), useGlobalLedBuffer, b0->getLEDCurrent(), b0->getMaxCurrent()));
    busResizeTarget = total; doInitBusses = true;
    Serial.printf("[WLV-OUT] only 1 output -> adding OUT2 on GPIO%u (%u + %u leds)\n",
                  unsigned(WELLVOW_PIN_OUT2), unsigned(half), unsigned(rest));
  }

  /* 出厂默认配置(2026-10-23 用户按 WLED LED Preferences 截图定):版本号存 /wlv_def.ver,固件里改了版本就在下次开机套用一次并重启。
   *   输出口:GPIO2 1–144、GPIO15 145–288(每路 144 颗),SK6812/WS2814 RGBW,线序 BRG + W&G 交换,56 mA/颗、每路 1152 mA(每路限流,总限流 0),W 不自动算,全局 LED 缓冲
   *   按键:Button0 = 板上按键(推按),N4 再加 Button1 GPIO25;关内部上下拉;触摸阈值 32;红外关
   *   开机默认效果:Rainbow(91),存进自动保存槽 250 */
  bool factoryPending = false; uint32_t factoryRebootAt = 0;
  void applyFactoryDefaults() {
    static const uint8_t DEF_VER = 3;   // 改了默认值就 +1(v3:两路各 144 颗,用户 10-24 定)
    // 标记 = 版本号 + 固件型号:换固件变体(W705 ↔ H706)也要重新套一次默认值(10-24),否则 H706 的默认芯片 SM16825 落不下去
    String want = String(DEF_VER) + "/" + WELLVOW_MODEL, cur;
    { File v = WLED_FS.open("/wlv_def.ver", "r"); if (v) { cur = v.readString(); cur.trim(); v.close(); } }
    if (cur == want) return;
    Serial.printf("[WLV-DEF] applying factory defaults %s (had %s)\n", want.c_str(), cur.c_str());
    busCfgClear();
    const uint8_t order = COL_ORDER_BRG | (3 << 4);   // 高 4 位 = W 通道交换:3 = W & G
    uint8_t pa[5] = {WELLVOW_PIN_OUT2, 255, 255, 255, 255}, pb[5] = {WELLVOW_PIN_OUT1, 255, 255, 255, 255};
#if defined(WELLVOW_STRIP_H706)
    const uint8_t defType = TYPE_SM16825;       // H706 固件:默认 SM16825
#else
    const uint8_t defType = TYPE_SK6812_RGBW;   // W705 / S3:默认 WS2814 / SK6812 RGBW
#endif
    busCfgAdd(BusConfig(defType, pa, 0,   144, order, false, 0, RGBW_MODE_MANUAL_ONLY, 0, true, 56, 1152));
    busCfgAdd(BusConfig(defType, pb, 144, 144, order, false, 0, RGBW_MODE_MANUAL_ONLY, 0, true, 56, 1152));
    busResizeTarget = 288; doInitBusses = true; factoryPending = true;
    BusManager::setMilliampsMax(0);   // 0 = 每路用自己的限流
    useGlobalLedBuffer = true;
    savedColorOrder = COL_ORDER_BRG; pendingColorOrder = COL_ORDER_BRG;   // App 的线序查询(0x1D)也按 BRG 报
    btnPin[0] = BTNPIN; buttonType[0] = BTN_TYPE_PUSH;
#if !defined(WELLVOW_BOARD_S3)
    btnPin[1] = 25; buttonType[1] = BTN_TYPE_PUSH;
#endif
    disablePullUp = true; touchThreshold = 32;
#ifndef WLED_DISABLE_INFRARED
    irPin = -1; irEnabled = 0;
#endif
    saveRuntimeConfig();   // 线序 BRG 立刻写进 NVS
    { File v = WLED_FS.open("/wlv_def.ver", "w"); if (v) { v.print(want); v.close(); } }
  }
  void serviceBusResize() {
    if (!busResizeTarget || doInitBusses) return;   // 等 WLED 主循环把总线重建完
    uint16_t total = strip.getLengthTotal();
    if (total != busResizeTarget) Serial.printf("[WLV-LEDS] resize result %u (wanted %u, memory limit?)\n", unsigned(total), unsigned(busResizeTarget));
    busResizeTarget = 0;
    Segment &m = strip.getMainSegment(); m.setOption(SEG_OPTION_ON, true); if (m.stop != total) strip.setSegment(0, 0, total);
    for (uint8_t i = 1; i < strip.getSegmentsNum(); i++) strip.setSegment(i, 0, 0);   // 清掉按旧灯数留下的多余段(段表随后由 0x1C 重建)
    strip.purgeSegments();
    if (factoryPending) {   // 出厂默认效果:彩虹,存成自动保存槽 250(开机恢复用),然后重启让按键 / 引脚分配按新配置生效
      Segment &m = strip.getMainSegment(); m.setMode(FX_MODE_RAINBOW_CYCLE); m.setPalette(0); m.speed = 128; m.intensity = 128;
      effectCurrent = FX_MODE_RAINBOW_CYCLE; effectPalette = 0; bri = 128; briLast = 128;
    }
    serializeConfig();   // 新灯数写进 cfg.json,断电不丢
    stateUpdated(CALL_MODE_DIRECT_CHANGE);
    Serial.printf("[WLV-LEDS] applied total=%u, saved\n", unsigned(total));
    if (factoryPending) { factoryPending = false; saveRuntimeConfig(); savePreset(250, "autosave"); factoryRebootAt = millis() + 2000; Serial.println("[WLV-DEF] factory defaults saved, reboot in 2s"); }   // savePreset 是延后到主循环才写文件的,等它写完再重启
  }

  void serviceSegmentConfig() {
    if (!segmentConfigPending) return;
    if (busResizeTarget) return;   // 总线还在重建,段表等重建完再落
    { // 灯数在排队期间变小了(例如刚重建过总线):按当前灯数再裁一次,而不是整份丢掉
      uint16_t total = strip.getLengthTotal(), used = 0; uint8_t keep = 0;
      for (uint8_t i = 0; i < pendingSegmentCount; i++) {
        if (used >= total) break;
        uint16_t n = pendingSegmentLengths[i];
        if (used + n > total) n = total - used;
        pendingSegmentLengths[keep++] = n; used += n;
      }
      if (!keep) { segmentConfigPending = false; return; }
      pendingSegmentCount = keep;
    }
    uint8_t count = pendingSegmentCount;
    segmentConfigPending = false;
    // strip.resetSegments() 会把所有段销毁重建成 WLED 默认段——默认颜色是琥珀色
    // 0xFFAA00、默认效果 Static。存配置/校准保存都会走这里,若不保留当前状态,灯带
    // 会在每次保存后被重置成"黄色"(用户反馈:线序没变却不保持上次的颜色和模式)。
    // 因此先记住当前主段的颜色/模式,重建分段后再逐段恢复。
    Segment &cur = strip.getMainSegment();
    const uint32_t keepCol0 = cur.colors[0];
    const uint32_t keepCol1 = cur.colors[1];
    const uint32_t keepCol2 = cur.colors[2];
    const uint8_t keepMode = cur.mode;
    const uint8_t keepSpeed = cur.speed;
    const uint8_t keepIntensity = cur.intensity;
    const uint8_t keepPalette = cur.palette;
    /* 逐段记下各自的颜色:以前只记主段、重建后拿它刷所有段,结果 Smart Color 这类
     * "每段一个颜色"的功能一落地就被压平成同一个色(表现:应用后整条变成一个静止的颜色)。
     * 新增的段没有旧值可用,才退回主段颜色。 */
    const uint8_t oldSegN = strip.getSegmentsNum();
    uint32_t oldCols[WELLVOW_SEG_KEEP][3];
    uint8_t keptN = oldSegN < WELLVOW_SEG_KEEP ? oldSegN : WELLVOW_SEG_KEEP;
    for (uint8_t i = 0; i < keptN; i++) {
      Segment &os = strip.getSegment(i);
      oldCols[i][0] = os.colors[0]; oldCols[i][1] = os.colors[1]; oldCols[i][2] = os.colors[2];
    }
    strip.resetSegments();
    uint16_t start = 0;
    for (uint8_t i = 0; i < count; i++) {
      uint16_t n = pendingSegmentLengths[i];
      strip.setSegment(i, start, start + n);
      start += n;
    }
    for (uint8_t i = 0; i < strip.getSegmentsNum(); i++) {
      Segment &s = strip.getSegment(i);
      s.setMode(keepMode);
      s.speed = keepSpeed; s.intensity = keepIntensity; s.palette = keepPalette;
      if (i < keptN) { s.setColor(0, oldCols[i][0]); s.setColor(1, oldCols[i][1]); s.setColor(2, oldCols[i][2]); }
      else           { s.setColor(0, keepCol0);      s.setColor(1, keepCol1);      s.setColor(2, keepCol2); }
    }
    stateUpdated(CALL_MODE_DIRECT_CHANGE);
    serializeConfig();
    Serial.printf("[WLV-SEG] applied count=%u leds=%u keepMode=%u keepCol0=%06X\n", unsigned(count), unsigned(start), unsigned(keepMode), unsigned(keepCol0 & 0xFFFFFF));
  }

  bool applyDiy(uint8_t slot) {
    char path[18]; snprintf(path,sizeof(path),"/diy%u.bin",slot); File f=WLED_FS.open(path,"r");
    uint8_t h[7]; if (!f || f.read(h,7)!=7 || h[0]!='D' || h[1]!='1' || h[6]<1 || h[6]>8) { if(f)f.close(); return false; }
    Segment &main=strip.getMainSegment(); applyEffect(main, h[2] < strip.getModeCount() ? h[2] : 0);
    main.setOption(SEG_OPTION_REVERSED,h[3]&1); bri=uint8_t(constrain(h[4],5,100)*255/100); main.speed=uint8_t(constrain(h[5],1,10)*25);
    for(uint8_t s=0;s<h[6];s++) { int nc=f.read(); if(nc<1||nc>4){f.close();return false;} uint8_t rgb[3];
      for(int k=0;k<nc;k++){if(f.read(rgb,3)!=3){f.close();return false;} if(k==0&&s<strip.getSegmentsNum())strip.getSegment(s).setColor(0,RGBW32(rgb[0],rgb[1],rgb[2],0));}
    } f.close(); return true;
  }

  void serviceSchedule() {
    if (!scheduleCount) return;
    uint8_t wd, hh, mm;
    /* 时钟来源优先级:
     *   ① 联网 NTP(WLED 自己的 localTime)—— 断电重启后不用等手机连上也准;
     *   ② 手机连上时经 BLE 对过的时间(DP 0x08)+ millis() 推算。
     * 以前只有 ②:设备断电重启、手机又没打开的话,定时**完全不会执行**,
     * 而用户以为设置好了就该自己开关。 */
    if (ntpLastSyncTime != NTP_NEVER && localTime > 1700000000) {
      wd = uint8_t(weekday(localTime));          // 1=周日..7=周六,与 BLE 对时同一套编码
      hh = uint8_t(hour(localTime)); mm = uint8_t(minute(localTime));
      uint32_t key = uint32_t(localTime / 60);
      if (key == lastScheduleMinute) return; lastScheduleMinute = key;
    } else {
      if (!clockSetMillis) return;
      uint32_t elapsed=(millis()-clockSetMillis)/1000, minuteKey=elapsed/60;
      if (minuteKey==lastScheduleMinute) return; lastScheduleMinute=minuteKey;
      uint32_t mins=uint32_t(clockHour)*60+clockMinute+minuteKey, dayOffset=mins/1440;
      wd=uint8_t(((clockWeekday-1+dayOffset)%7)+1); hh=uint8_t((mins%1440)/60); mm=uint8_t(mins%60);
    }
    uint8_t bit=uint8_t(1u<<(wd-1)); bool changed=false;
    for(uint8_t i=0;i<scheduleCount;i++) if(schedules[i].enabled&&(schedules[i].repeatMask&bit)) {
      if(schedules[i].onH==hh&&schedules[i].onM==mm){setPower(true);changed=true;}
      if(schedules[i].offH==hh&&schedules[i].offM==mm){setPower(false);changed=true;}
    }
    if(changed) {
      // 定时开关机也走渐变,和手动开关机一致
      strip.setTransition(WELLVOW_POWER_FADE_MS); jsonTransitionOnce = true;
      colorUpdated(CALL_MODE_BUTTON);
      Serial.printf("[WLV-SCHED] fired at %02u:%02u wd=%u -> %s\n", unsigned(hh), unsigned(mm), unsigned(wd), bri ? "on" : "off");
    }
  }

  void serviceColorOrder() {
    if (!colorOrderPending && !savedColorOrderPending) return;
    colorOrderPending = false;
    savedColorOrderPending = false;
    // A per-pixel color-order map overrides the bus default. Calibration is
    // for one uniform WS2811 strip, so remove stale mappings before saving.
    BusManager::getColorOrderMap().reset();
    for (uint8_t i = 0; i < BusManager::getNumBusses(); i++) {
      Bus *bus = BusManager::getBus(i);
      if (bus) bus->setColorOrder((bus->getColorOrder() & 0xF0) | (pendingColorOrder & 0x0F));   // 只改 RGB 顺序,保留 WLED 里设的 W 通道交换位(W&G 等)
    }
    savedColorOrder = pendingColorOrder;
    saveRuntimeConfig();
    serializeConfig();
    colorUpdated(CALL_MODE_DIRECT_CHANGE);
    Serial.printf("[WLV-ORDER] applied=%u\n", unsigned(pendingColorOrder));
  }

  void serviceColorCalibration() {
    if (colorCalibrationPending) {
      colorCalibrationPending = false;
      if (!colorCalibrationActive && !calibrationSnapshotValid) {
        Segment &before = strip.getMainSegment();
        calibrationPreviousBri = bri;
        calibrationPreviousMode = before.mode;
        calibrationPreviousSpeed = before.speed;
        calibrationPreviousIntensity = before.intensity;
        calibrationPreviousPalette = before.palette;
        calibrationPreviousColors[0] = before.colors[0];
        calibrationPreviousColors[1] = before.colors[1];
        calibrationPreviousColors[2] = before.colors[2];
        calibrationSnapshotValid = true;
      }
      colorCalibrationActive = true;
      BusManager::getColorOrderMap().reset();
      strip.setTransition(0); jsonTransitionOnce = true;   // 只让校准这一帧硬切,不动全局渐变设置
      bri = 255;
      briLast = 255;
      effectCurrent = FX_MODE_STATIC;
      const uint32_t pure = RGBW32(pendingCalibrationRgb[0], pendingCalibrationRgb[1], pendingCalibrationRgb[2], 0);
      for (uint8_t i = 0; i < strip.getSegmentsNum(); i++) {
        Segment &seg = strip.getSegment(i);
        seg.stopTransition();
        seg.setOption(SEG_OPTION_ON, true);
        seg.setOpacity(255);
        seg.setMode(FX_MODE_STATIC);
        seg.setColor(0, pure);
        seg.setColor(1, 0);
        seg.setColor(2, 0);
      }
      Serial.printf("[WLV-CAL] active order=%u rgb=%u,%u,%u segments=%u buses=%u\n", unsigned(pendingCalibrationOrder), unsigned(pendingCalibrationRgb[0]), unsigned(pendingCalibrationRgb[1]), unsigned(pendingCalibrationRgb[2]), unsigned(strip.getSegmentsNum()), unsigned(BusManager::getNumBusses()));
      for (uint8_t i = 0; i < BusManager::getNumBusses(); i++) {
        Bus *bus = BusManager::getBus(i);
        if (bus) Serial.printf("[WLV-CAL] bus=%u type=%u start=%u len=%u oldOrder=%u\n", unsigned(i), unsigned(bus->getType() & 0x7F), unsigned(bus->getStart()), unsigned(bus->getLength()), unsigned(bus->getColorOrder() & 0x0F));
      }
      lastColorCalibrationShow = 0;
    }
    // 纯色由 FX 管线(分段 STATIC + 纯色)渲染,与 Solid 走完全相同的路径,
    // 这里只需保证每帧刷新持续进行即可,不再对 bus 直刷像素(避免与效果路径分叉)。
    if (!colorCalibrationActive) return;
    if (millis() - lastColorCalibrationShow < 20) return;
    lastColorCalibrationShow = millis();
    const uint32_t pure = RGBW32(pendingCalibrationRgb[0], pendingCalibrationRgb[1], pendingCalibrationRgb[2], 0);
    for (uint8_t i = 0; i < strip.getSegmentsNum(); i++) {
      Segment &seg = strip.getSegment(i);
      seg.setMode(FX_MODE_STATIC);
      seg.setColor(0, pure);
    }
    stateChanged = true;
    strip.trigger();
  }

  /* 线序校准(0x1F)进行中,serviceColorCalibration() 每 20ms 把所有段压成 STATIC + 纯色;以前只有 0x1D(确认线序)
   * 和 /wellvow/order 才会退出 —— 用户在「配置」页点了校准颜色又没确认就去选效果,灯带跑半秒就被压回常亮,
   * 串口 effect changed 4 -> 0。现在收到新的颜色 / 效果就视为放弃校准:退出、丢掉快照、把保存的线序重新排队应用
   * (进入校准时 ColorOrderMap 被 reset 过,必须恢复),之后这次的颜色 / 效果正常落下。 */
  void abandonColorCalibration() {
    if (!colorCalibrationActive && !colorCalibrationPending) return;
    colorCalibrationActive = false; colorCalibrationPending = false; calibrationSnapshotValid = false;
    pendingColorOrder = savedColorOrder; colorOrderPending = true;
    Serial.println("[WLV-CAL] abandoned by new color/effect, color order restored");
  }
  // 手动上色前的统一准备(DP 0x06 / 0x07 共用):退出可能卡住的实时/序列播放——realtime 一旦
  // 激活会直接显示实时缓冲、忽略一切效果与颜色(表现为怎么发颜色灯带都不变);再关过渡、置 ON、
  // 停当前过渡,让颜色立即生效(和校准同款做法)。
  void prepareManualColor() {
    abandonColorCalibration();
    /* App 的"应用配色"是先发分段(0x1C)再发颜色(0x06/0x07),而 0x1C 是挂起到下一轮主循环才生效的 ——
     * 颜色先落到旧分段上,随后的重建又把它盖掉。这里在上色前先把挂起的分段重建执行掉,顺序就不重要了。 */
    if (segmentConfigPending) serviceSegmentConfig();
    /* strip.setTransition(0) 之后必须自己还回去:stateUpdated() 在过渡时长为 0 时走提前返回分支,
     * 顺手把 jsonTransitionOnce 清了,handleTransitions() 那条"用完自动恢复"的路径就永远不会执行。
     * 不还的话,手动上一次色以后开关机渐变就再也没有了。 */
    sequencePlaying = false;
    if (realtimeMode != REALTIME_MODE_INACTIVE || realtimeOverride != REALTIME_OVERRIDE_NONE) {
      realtimeOverride = REALTIME_OVERRIDE_NONE; realtimeMode = REALTIME_MODE_INACTIVE; exitRealtime();
    }
    if (currentPlaylist >= 0) unloadPlaylist();
    /* ⚠ 这里以前写的是 `transitionDelay = 0`,那是"全局设置",一旦手动上过一次色,
     * 整机的渐变就被永久关掉了 —— 开关灯从此是硬切,没有慢慢亮起/暗下去。
     * 正确做法是只让"这一次"更新不带渐变:WLED 有现成的一次性覆盖 jsonTransitionOnce,
     * handleTransitions() 会在这次过渡结束后自动把 strip 的过渡时长恢复成 transitionDelay。 */
    strip.setTransition(0); jsonTransitionOnce = true;
    for (uint8_t i = 0; i < strip.getSegmentsNum(); i++) {
      Segment &s = strip.getSegment(i);
      s.stopTransition();
      s.setOption(SEG_OPTION_ON, true);
    }
  }
  // 直接改段颜色后必须把 WLED 的全局 col[]/colSec[] 等同步成段的新值:
  // handleDpSet 末尾的 colorUpdated() 会先跑 applyValuesToSelectedSegs(),它拿“全局值”和
  // 第一选中段比较、不一致就把段颜色改回全局值 —— 不同步的话刚写进去的颜色立刻被旧色覆盖
  // (串口能看到 [WLV-DP] id=0x06 收到了绿色、[WLV-STATE] col0 却还是红色,即“发了颜色灯带不变”)。
  void syncGlobalsFromSegment() { setValuesFromFirstSelectedSeg(); }
  // App 选效果 = 和 WLED 网页选效果一样的“干净起点”:加载该效果自己的默认参数(强度 / 自定义滑块 / 选项 / 1D-2D 映射),
  // 效果没定义默认调色板的一律回到 0(按段颜色渲染)。以前只换 mode 编号,段上残留上一个预设 / DIY 的调色板和强度,
  // 灯带看起来就和 App 预览不是一回事(例如 Breathe 用了 Party 调色板变成七彩、Twinkle 密度不对)。
  // 速度沿用当前值:App 的速度滑块是唯一来源,预览动画也按它走。同一效果再点一次也要重置(先切走再切回来)。
  void applyEffect(Segment &s, uint8_t fx) {
    uint8_t spd = s.speed;
    if (s.mode == fx) s.setMode(fx == FX_MODE_STATIC ? FX_MODE_BLINK : FX_MODE_STATIC);
    s.setMode(fx, true);
    int16_t pal = extractModeDefaults(fx, "pal"); s.setPalette(pal >= 0 ? uint8_t(pal) : 0);
    s.speed = spd;
  }
  // 把 v 里的 n 组 RGB 写入该段色槽 0..n-1(最多 3 槽:主色/过渡1/过渡2),其余槽清黑——
  // 灯带显示的就是 App 这次发的颜色,不残留上一个模式留下的过渡色。
  void applySegmentColors(Segment &s, const uint8_t *v, uint8_t n) {
    if (n > 3) n = 3;
    for (uint8_t k = 0; k < 3; k++) s.setColor(k, k < n ? RGBW32(v[k*3], v[k*3+1], v[k*3+2], 0) : 0);
  }

  void applyTlv(uint8_t dp, const uint8_t *v, size_t len) {

    Segment &seg = strip.getMainSegment();
#if WELLVOW_LOG_DP
    // 每条指令都打日志会拖慢响应:USB CDC / UART 的 TX 缓冲满时 Serial.printf 是阻塞的,
    // 拖滑条那种高频指令下累计几十毫秒,正是"点了半天才动"的一部分。排查时再打开。
    Serial.printf("[WLV-DP] id=0x%02X len=%u", dp, unsigned(len));
    for (size_t i = 0; i < len && i < 8; i++) Serial.printf(" %02X", v[i]);
    Serial.println();
#endif
    switch (dp) {
      case 0x01: if (len) setPower(v[0]); break;
      case 0x02: if (len) bri = v[0]; break;
      /* 这几个参数以前只写 strip.getMainSegment() —— 多段灯带上,除了第 0 段以外
       * 速度/方向/强度/调色板全都不生效(DIY 的动态效果因此"没反应")。
       * WLED 自己的 applyValuesToSelectedSegs() 也救不回来:它是拿全局值和第一个选中段比较,
       * 而 applyTlv 已经把主段改成和全局一致了,比较结果永远是"没变化"。所以这里直接逐段写。 */
      case 0x03: if (len) {
        /* 速度下限(10-22 用户:滑块拉到最慢时普通效果看着完全不动):普通效果最低 24;WELLVOW Sky(142)的速度是时长,保留 0..255 全程 */
        uint8_t sp = v[0]; bool sky = false;
        for (uint8_t i = 0; i < strip.getSegmentsNum(); i++) if (strip.getSegment(i).mode == 142) sky = true;
        if (!sky && sp < 24) sp = 24;
        effectSpeed = sp; for (uint8_t i = 0; i < strip.getSegmentsNum(); i++) strip.getSegment(i).speed = sp;
      } break;
      case 0x04: if (len) for (uint8_t i = 0; i < strip.getSegmentsNum(); i++) strip.getSegment(i).setOption(SEG_OPTION_REVERSED, v[0] != 0); break;
      case 0x05: if (len >= 2) {
        uint16_t mode = readU16(v);
        abandonColorCalibration();   // 校准没确认就选效果 = 放弃校准,否则效果会被校准循环压回常亮
        // 切换效果前也退出实时/序列播放,否则 realtime 覆盖会让效果切换看不到。
        sequencePlaying = false;
        if (realtimeMode != REALTIME_MODE_INACTIVE || realtimeOverride != REALTIME_OVERRIDE_NONE) {
          realtimeOverride = REALTIME_OVERRIDE_NONE; realtimeMode = REALTIME_MODE_INACTIVE; exitRealtime();
        }
        if (currentPlaylist >= 0) unloadPlaylist();
        if ((mode >> 8) == 2) { if (applyDiy(mode & 0xFF)) syncGlobalsFromSegment(); else applyPreset(mode & 0xFF); }   // DIY 改了段的效果/速度/颜色,必须同步到全局,否则 colorUpdated 会用旧全局值盖回去
        else { uint8_t fx = mode & 0xFF; for (uint8_t i=0; i<strip.getSegmentsNum(); i++) { Segment &sg = strip.getSegment(i); bool same = sg.mode == fx; applyEffect(sg, fx); if (same && fx == 142) sg.markForReset(); } syncGlobalsFromSegment(); stateUpdated(CALL_MODE_DIRECT_CHANGE); }   // 再点一次日出也从头开始
        Serial.printf("[WLV-FX] bank=%u effect=%u active=%u\n", unsigned(mode >> 8), unsigned(mode & 0xFF), unsigned(seg.mode));
      } break;
      case 0x06: if (len >= 3) {
        // 颜色:3 字节 = 主色;6/9 字节 = 主色+过渡1(+过渡2) -> 每段色槽 0/1/2(旧 App 只发 3 字节,兼容)
        prepareManualColor();
        for (uint8_t i = 0; i < strip.getSegmentsNum(); i++) applySegmentColors(strip.getSegment(i), v, uint8_t(len / 3));
        syncGlobalsFromSegment();
        strip.trigger();
      } break;
      case 0x07: if (len) {
        // 按段颜色:[n, (段号, 色数, RGB*色数)...],每段最多 3 色 -> 该段色槽 0/1/2
        prepareManualColor();
        size_t p = 1;
        for (uint8_t e = 0; e < v[0] && p + 2 <= len; e++) {
          uint8_t sid = v[p++], nc = v[p++];
          if (p + size_t(nc) * 3 > len) break;
          if (nc && sid < strip.getSegmentsNum()) applySegmentColors(strip.getSegment(sid), v + p, nc);
          p += size_t(nc) * 3;
        }
        syncGlobalsFromSegment();
        strip.trigger();
      } break;
      case 0x08: if (len>=7) { clockWeekday=constrain(v[3],1,7); clockHour=constrain(v[4],0,23); clockMinute=constrain(v[5],0,59); clockSecond=constrain(v[6],0,59); clockSetMillis=millis()-uint32_t(clockSecond)*1000; lastScheduleMinute=UINT32_MAX; } break;
      /* 定时表。**空表(len==0)= 全部删除**,必须受理:
       * 以前只写了 `if (len>=7)`,用户在 APP 里把定时全删掉再保存,这一帧被直接忽略,
       * 设备上的旧定时仍然存在并照常到点开关灯 —— 用户以为删掉了,灯却还会自己动。 */
      case 0x09:
        if (len == 0) { scheduleCount = 0; lastScheduleMinute = UINT32_MAX; saveRuntimeConfig(); Serial.println("[WLV-SCHED] all schedules cleared"); }
        else if (len >= 7) { scheduleCount=min(uint8_t(8),uint8_t(len/7)); memcpy(schedules,v,scheduleCount*sizeof(ScheduleSlot)); lastScheduleMinute = UINT32_MAX; saveRuntimeConfig(); Serial.printf("[WLV-SCHED] %u schedule(s) stored\n", unsigned(scheduleCount)); }
        break;
      case 0x0B: if (len >= 4) { blobSlot = v[0]; blobExpected = readU16(v + 2); } break;
      case 0x1B: if (len >= 2) {
        if (v[0] == 0xFF) stopSequence(); else startSequence(v[0], v[1] != 0);
      } break;
      case 0x0D: if (len >= 2) {
        // 灯数跟随布线图/段配置:与当前硬件总数不同就按现有输出口比例重建总线(上限 512),重建完成后持久化到 cfg.json
        uint16_t count = readU16(v);
        if (count > WELLVOW_MAX_LEDS) count = WELLVOW_MAX_LEDS;
        if (count > 0 && count != strip.getLengthTotal()) queueBusResize(count);
        else if (count > 0) seg.stop = count;
      } break;
      case 0x12: if (len) { effectIntensity = v[0]; for (uint8_t i = 0; i < strip.getSegmentsNum(); i++) strip.getSegment(i).intensity = v[0]; } break;
      case 0x13: if (len) { effectPalette = v[0]; for (uint8_t i = 0; i < strip.getSegmentsNum(); i++) { Segment &sg = strip.getSegment(i); sg.setPalette(v[0]); if (sg.mode == 142) sg.markForReset(); } } break;   // 日出换调色板就从头开始
      case 0x14: if (len >= 2) { transitionDelay = readU16(v); strip.setTransition(transitionDelay); } break;
      case 0x15: if (len >= 2) { nightlightActive = v[0]; nightlightDelayMins = v[1]; } break;
      case 0x17: if (len >= 7 && v[0] >= strip.getSegmentsNum()) {
        Serial.printf("[WLV-SEGFX] ignored: seg %u does not exist (device has %u segs)\n", unsigned(v[0]), unsigned(strip.getSegmentsNum()));
      } else if (len >= 7) {
        // 按段设效果(DIY 选中某一段再点模式):只改这一段;改完同步全局,否则 colorUpdated() 里的
        // applyValuesToSelectedSegs() 会拿旧的全局 effectCurrent/speed 把它改回去
        Segment &s = strip.getSegment(v[0]);
        s.setOption(SEG_OPTION_ON, v[1]); s.setOpacity(v[2]); s.setMode(v[3]);
        s.speed = v[4]; s.intensity = v[5]; s.palette = v[6];
        syncGlobalsFromSegment();
        Serial.printf("[WLV-SEGFX] seg=%u fx=%u sx=%u pal=%u\n", unsigned(v[0]), unsigned(v[3]), unsigned(v[4]), unsigned(v[6]));
      } break;
      case 0x18: if (len) savePreset(v[0]); break;
      case 0x19: if (len >= 4) strip.timebase = millis() - readU32(v); break;
      case 0x1C: queueSegmentConfig(v,len); break;
      case 0x20: if (len) setLinkRole(v[0]); break;
      case 0x23: if (len) { previewUntil = v[0] ? millis() + 6000 : 0; lastPreviewTx = 0; } break;   // 预览流开关(见 servicePreview)
      case 0x25: if (len && outTypeAllowed(v[0])) queueBusType(v[0]); break;
      case 0x29: if (len >= 1 && len <= 32) { char nm[33]; memcpy(nm, v, len); nm[len] = 0; strlcpy(serverDescription, nm, sizeof(serverDescription)); serializeConfig(); Serial.printf("[WLV-NAME] device name -> %s\n", serverDescription); } break;   // 设备显示名(用户自己改,存 WLED 的 serverDescription)
      case 0x27: if (len >= 2 && v[0] < 4 && v[1] >= 2 && v[1] <= 8 && len >= size_t(2 + v[1] * 3)) setCustomPalette(v[0], v + 2, v[1]); break;   // 自定义调色板 [槽 0..3, n, rgb×n] → 调色板号 255-槽
      case 0x26: if (len >= 1 && len >= size_t(1 + v[0] * 5)) {   // 输出口布局 [n, (start u16, len u16, rev u8) × n],n ≤ 2
        uint8_t n = v[0] > 2 ? 2 : v[0]; uint16_t sv[2] = {0,0}, lv[2] = {0,0}; int8_t rv[2] = {-1,-1};
        for (uint8_t i = 0; i < n; i++) { const uint8_t *q = v + 1 + i * 5; sv[i] = (q[0] << 8) | q[1]; lv[i] = (q[2] << 8) | q[3]; rv[i] = q[4] ? 1 : 0; }
        queueOutputs(sv, lv, rv, n);
      } break;   // 输出口芯片类型(WLED TYPE_*),没有 Wi-Fi 时 App 走这条
      case 0x1D: if (len && v[0] <= COL_ORDER_GBR) { pendingColorOrder = v[0]; colorOrderPending = true; colorCalibrationActive = false; if (calibrationSnapshotValid) { bri = calibrationPreviousBri; for (uint8_t i=0; i<strip.getSegmentsNum(); i++) { Segment &before = strip.getSegment(i); before.setMode(calibrationPreviousMode); before.speed = calibrationPreviousSpeed; before.intensity = calibrationPreviousIntensity; before.palette = calibrationPreviousPalette; before.setColor(0, calibrationPreviousColors[0]); before.setColor(1, calibrationPreviousColors[1]); before.setColor(2, calibrationPreviousColors[2]); } calibrationSnapshotValid = false; } Serial.printf("[WLV-ORDER] queued=%u restore=%u\n", unsigned(v[0]), unsigned(calibrationPreviousMode)); } break;
      case 0x1F: if (len >= 4 && v[0] <= COL_ORDER_GBR) { pendingCalibrationOrder = v[0]; pendingCalibrationRgb[0] = v[1]; pendingCalibrationRgb[1] = v[2]; pendingCalibrationRgb[2] = v[3]; colorCalibrationPending = true; Serial.printf("[WLV-CAL] queued order=%u rgb=%u,%u,%u\n", unsigned(v[0]), unsigned(v[1]), unsigned(v[2]), unsigned(v[3])); } break;
      case 0x1E: if (len>=3) {   // [on, mode, sensitivity 1..10, wledFx(可选,直接指定 WLED 音频效果号)]
        static const uint8_t fx[4]={FX_MODE_FREQMAP,FX_MODE_FREQWAVE,FX_MODE_PUDDLEPEAK,FX_MODE_GRAVCENTER};
        const uint8_t sens = uint8_t(constrain((int)v[2],1,10));
        /* 灵敏度 1..10 的映射。原来顶格只有 180,而 ES7201 经 S3 PDM 读回来的幅度本就只有满量程的
         * 百分之零点几,顶格仍嫌钝。现在放到 255,低端也抬到 60(1 档主要靠噪声门滤环境音,不是靠压增益)。
         * 噪声门同步收窄到 27..0,让中高档更容易被小声触发。 */
        /* 2026-09-25:AGC(自动增益)一开,滑条基本没意义 —— 没声音时它把底噪放大到满幅,灯照样跑。
         * 现在 AGC 关掉,灵敏度只由手动增益 + 噪声门决定。增益 40 = ×1;实测这块板说话时 raw 就有 100~255,
         * 增益给到 ×3 会把 FFT 全部顶到 255(效果看着像定住不动),所以量程压到 12..96:1 档 ×0.3 / 门 40,5 档 ×1.2 / 门 12,10 档 ×2.4 / 门 0。 */
        if (v[0] == 2) { audioSens = sens; startAudioCal(); break; }   // [2, -, sens, -] = 校准底噪(保持安静 1.5 秒)
        audioSens = sens; audioGain = 40; audioSquelch = squelchFor(sens);
        /* wledFx 为 0 的含义:
         *   音乐已在跑 -> "只改灵敏度",保持当前效果不变(App 拖灵敏度滑条走这条);
         *   音乐没在跑 -> 按 mode 选固件内置的 4 个音频效果(兼容老 App)。 */
        const uint8_t wantFx = (len >= 4 && v[3] && v[3] < strip.getModeCount()) ? v[3]
                             : (musicActive ? seg.mode : fx[v[1]&3]);
        /*
         */
        if (v[0]) {
          sequencePlaying = false;
          if (realtimeMode != REALTIME_MODE_INACTIVE || realtimeOverride != REALTIME_OVERRIDE_NONE) { realtimeOverride = REALTIME_OVERRIDE_NONE; realtimeMode = REALTIME_MODE_INACTIVE; realtimeTimeout = 0; exitRealtime(); }
        }
        setAudioReactive(v[0]!=0);
        /* 效果必须写到**每一段**。以前只写 strip.getMainSegment():灯带一旦分了段,
         * 除第 0 段外全都还在跑原来的效果,用户看到的就是"音乐模式点了没反应"。 */
        if (v[0]) {
          if (!musicActive) musicPreviousFx = seg.mode;
          musicActive = true; musicWanted = true;
          for (uint8_t i = 0; i < strip.getSegmentsNum(); i++) {
            Segment &sg = strip.getSegment(i);
            if (sg.mode != wantFx) sg.setMode(wantFx);
            sg.intensity = uint8_t(sens * 25);
          }
        } else {
          musicActive = false; musicWanted = false;
          for (uint8_t i = 0; i < strip.getSegmentsNum(); i++) applyEffect(strip.getSegment(i), musicPreviousFx);
        }
        syncGlobalsFromSegment();   // handleDpSet 末尾的 colorUpdated() 会用全局 effectCurrent/effectIntensity 覆盖段值,先把段值同步到全局
        saveRuntimeConfig();   // 灵敏度存 NVS,下次开机沿用
        Serial.printf("[WLV-MUSIC] on=%u fx=%u sens=%u gain=%u squelch=%u\n", unsigned(v[0]), unsigned(wantFx), unsigned(sens), unsigned(audioGain), unsigned(audioSquelch));
      } break;
      default: break;
    }
  }

  // fromPeer = 指令来自局域网另一台控制器(WVGS/WVDP),本机只执行、不再转发
  /* 一帧里是不是只有亮度/速度。这两个是拖滑条来的高频量,必须"边拖边变"而且**不能打断动画** —— 
   * 走 colorUpdated() 的话 stateUpdated() 会 setTransitionMode(true) 把所有段推进过渡混合,
   * 肉眼看到的就是"调亮度时灯带先停一下再重新跑"。 */
  static bool liveOnlyFrame(const uint8_t *d, size_t len) {
    size_t i = 0; bool any = false;
    while (i + 3 <= len) {
      uint8_t dp = d[i], n = d[i + 2]; i += 3;
      if (i + n > len) return false;
      if (dp != 0x02 && dp != 0x03) return false;
      any = true; i += n;
    }
    return any;
  }
  void handleDpSet(const uint8_t *d, size_t len, bool fromPeer = false) {
    /*
     * Wi-Fi 同步的帧全丢了(2026-10-09 真机:从设备一帧不显示,查了半天是手机 App 连着)。 */
    { bool manual = false; for (size_t i = 0; i + 3 <= len;) { uint8_t dp = d[i], n = d[i + 2]; if (i + 3 + n > len) break; if (dp < 0x20 || dp > 0x25) manual = true; i += 3 + n; }
      if (manual) { if (studioLive) stopStudioStream(); studioInhibitUntil = millis() + 6000; } }

    const bool liveOnly = liveOnlyFrame(d, len);
    /* 判据必须是"开关状态真的变了",不能是"帧里带了 0x01"。
     * App 现在把 开机+颜色+效果+调色板 打成一帧下发(少几个 BLE 往返),每一帧都带 0x01,
     * 按"带了就当开关机"处理的话,每次改颜色/换效果都会走 800ms 渐变分支:
     * setTransitionMode(true) 会给每个段 startTransition,段要同时渲染新旧两套效果 800ms,
     * 还伴随每次 malloc/memcpy 整段缓冲 —— 堆一紧张效果就悄悄退化成纯色,看起来就是"DIY 没作用"。 */
    const bool wasOn = bri != 0;

    size_t i = 0;
    while (i + 3 <= len) {
      uint8_t dp = d[i], n = d[i + 2]; i += 3;
      if (i + n > len) break;
      applyTlv(dp, d + i, n); i += n;
    }

    /* 过渡时长一律当场设好、当场还原,不再留到下一轮 loop 去还 ——
     * 那样会在开关机渐变进行到一半时把时长改掉,渐变可能永远走不完。 */
    if (liveOnly) {
      /* 只调亮度/速度:直接写进硬件,不走 colorUpdated()。
       * 效果的相位、段的过渡状态一律不动,动画连续跑,滑条跟手。 */
      briOld = briT = bri;
      if (bri > 0) briLast = bri;
      strip.setBrightness(scaledBri(bri));
      stateChanged = true;         // 让 auto-save 知道状态变了
      strip.trigger();
    } else {
      /* 其余手动控制(选效果 / 上色 / 切模式)要求 100ms 内见效:把过渡时长压到 0,
       * 否则默认 750ms 的效果混合会让用户觉得"点了好几百毫秒才动"。
       * 只有开关机例外,那里要的就是慢慢亮起/暗下去。 */
      const bool powerChanged = (bri != 0) != wasOn;
      if (powerChanged) {
        // 开关机:要慢慢亮起/暗下去。jsonTransitionOnce 必须在 colorUpdated 之前置位,
        // handleTransitions() 在渐变结束时才据此把时长还原成 transitionDelay。
        strip.setTransition(WELLVOW_POWER_FADE_MS);
        jsonTransitionOnce = true;
        colorUpdated(CALL_MODE_DIRECT_CHANGE);
      } else {
        // 其余手动控制:0 过渡 = 立刻见效。stateUpdated() 会走提前返回分支并顺手清掉
        // jsonTransitionOnce,所以这里必须自己同步还原,否则后面的开关机渐变就没了。
        strip.setTransition(0);
        colorUpdated(CALL_MODE_DIRECT_CHANGE);
        strip.setTransition(transitionDelay);
      }
    }
    if (!fromPeer) forwardToSlaves(d, len);
  }
  // CMD 0x04 组指令:d = [组号位掩码, TLV...]。本机在组内就执行;主设备再把整帧广播给局域网(fromPeer 的不广播)
  void handleGroupSet(const uint8_t *d, size_t len, bool fromPeer = false) {
    if (len < 4) return;
    if (d[0] & syncGroups) handleDpSet(d + 1, len - 1, true);   // 组指令已经(或即将)广播,本机执行时不再按主从规则二次转发
    if (!fromPeer) forwardGroup(d, len);
  }

  void udpSend(IPAddress to, const uint8_t *p, size_t n) {
    if (!studioUdpReady || !WLED_CONNECTED) return;
    studioUdp.beginPacket(to, 21325); studioUdp.write(p, n); studioUdp.endPacket();
  }
  IPAddress broadcastIp() { return IPAddress(uint32_t(WiFi.localIP()) | ~uint32_t(WiFi.subnetMask())); }
  void sendPeerQuery() { const uint8_t p[5] = {'W','V','D','Q',1}; udpSend(broadcastIp(), p, sizeof(p)); lastPeerQuery = millis(); }
  void sendPeerReply(IPAddress to) {
    uint8_t p[48]; size_t n = 0;
    memcpy(p, "WVDR", 4); n = 4; p[n++] = 1; p[n++] = linkRole;
    uint16_t leds = strip.getLengthTotal(); p[n++] = leds >> 8; p[n++] = leds & 0xFF;
    p[n++] = sequencePlaying ? sequenceSlot : 0xFF; p[n++] = bri != 0; p[n++] = bri; p[n++] = syncGroups;
    size_t nl = strlen(serverDescription); if (nl > 24) nl = 24; p[n++] = uint8_t(nl); memcpy(p + n, serverDescription, nl); n += nl;

    udpSend(to, p, n);
  }
  uint8_t onlinePeers() { uint8_t c = 0; for (uint8_t i = 0; i < peerCount; i++) if (millis() - peers[i].seen < PEER_ONLINE_MS) c++; return c; }
  void updatePeer(IPAddress from, const uint8_t *p, int n) {
    if (n < 12 || p[4] != 1) return;
    uint32_t ip = uint32_t(from); if (ip == uint32_t(WiFi.localIP())) return;
    for (uint8_t i = 0; i < peerCount;) { if (millis() - peers[i].seen > PEER_DROP_MS) { peers[i] = peers[peerCount - 1]; peerCount--; } else i++; }   // 太久没应答的丢掉
    int idx = -1; for (uint8_t i = 0; i < peerCount; i++) if (peers[i].ip == ip) { idx = i; break; }
    if (idx < 0) {
      if (peerCount < MAX_PEERS) idx = peerCount++;
      else { idx = 0; for (uint8_t i = 1; i < peerCount; i++) if (peers[i].seen < peers[idx].seen) idx = i; }   // 满了替换最久没见的
    }
    Peer &pe = peers[idx]; bool fresh = pe.ip != ip;
    pe.ip = ip; pe.seen = millis(); pe.role = p[5]; pe.leds = readU16(p + 6); pe.playing = p[8]; pe.on = p[9]; pe.bri = p[10]; pe.group = p[11];
    int rawNl = n > 12 ? p[12] : 0, nl = rawNl; if (nl > 24) nl = 24; if (13 + nl > n) nl = n - 13; if (nl < 0) nl = 0;
    memcpy(pe.name, p + 13, nl); pe.name[nl] = 0;

    if (fresh) Serial.printf("[WLV-HUB] peer %s \"%s\" role=%u leds=%u\n", from.toString().c_str(), pe.name, unsigned(pe.role), unsigned(pe.leds));
  }
  static bool linkForwardable(uint8_t dp) {   // 主设备同步给从设备的 DP 白名单(灯数/分段/配网/校准等本机参数不同步)
    switch (dp) { case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: case 0x12: case 0x13: case 0x14: case 0x1E: return true; default: return false; }
  }
  void forwardToSlaves(const uint8_t *d, size_t len) {
    if (linkRole != 1 || !studioUdpReady || !WLED_CONNECTED) return;
    uint8_t p[250]; size_t n = 5; memcpy(p, "WVDP", 4); p[4] = 1;
    size_t i = 0;
    while (i + 3 <= len) { uint8_t dp = d[i], l = d[i + 2]; if (i + 3 + l > len) break; if (linkForwardable(dp) && n + 3 + l <= sizeof(p)) { memcpy(p + n, d + i, 3 + l); n += 3 + l; } i += 3 + l; }
    if (n > 5) udpSend(broadcastIp(), p, n);
  }
  void forwardGroup(const uint8_t *d, size_t len) {   // d = [组号位掩码, TLV...]
    if (linkRole != 1 || !studioUdpReady || !WLED_CONNECTED || len < 4 || len > 240) return;
    uint8_t p[250]; memcpy(p, "WVGS", 4); p[4] = 1; memcpy(p + 5, d, len); udpSend(broadcastIp(), p, 5 + len);
    Serial.printf("[WLV-HUB] group relay mask=0x%02X len=%u\n", unsigned(d[0]), unsigned(len - 1));
  }
  void setLinkRole(uint8_t r) {
    if (r > 2) return;
    bool changed = r != linkRole; linkRole = r;
    if (changed) saveRuntimeConfig();
    if (linkRole == 1) { sendPeerQuery(); if (sequencePlaying) sendLinkSync(false); }
    Serial.printf("[WLV-HUB] role=%u\n", unsigned(linkRole));
  }
  void serviceSerialDebug() {
#ifndef WELLVOW_SERIAL_CMDS
    return;   // 正式固件不占串口输入(WLED 自己的 handleSerial 要用)
#else
    serialCanRX = false;   // WLED 的 setup 在 usermod 之后又会把它置真,每圈压一次
#endif
    if (!Serial.available()) return;
    String line = Serial.readStringUntil('\n'); line.trim(); if (!line.length()) return;
    /* d<hex> —— 把一串十六进制当作 DP_SET 帧喂进去,走的就是 App 那条 handleDpSet 路径。
     * 没有手机也能在 Mac 上复现"关机没反应""亮度不变"这类问题。
     *   d 01 01 01 00   关机(DP 0x01 = 0)
     *   d 02 02 01 FF   亮度 255
     *   d 05 03 02 00 5B 效果 91(彩虹) */
    if (line.length() > 1 && (line[0] == 'd' || line[0] == 'D')) {
      uint8_t buf[64]; size_t n = 0; int hi = -1;
      for (size_t k = 1; k < line.length() && n < sizeof(buf); k++) {
        char c = line[k]; int v;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
        else continue;
        if (hi < 0) hi = v; else { buf[n++] = uint8_t((hi << 4) | v); hi = -1; }
      }
      if (n) {
        Serial.printf("[WLV-INJECT] %u bytes -> handleDpSet\n", unsigned(n));
        handleDpSet(buf, n);
        Serial.printf("[WLV-INJECT] after: bri=%u briT=%u briLast=%u on=%u mode=%u transition=%u\n",
                      unsigned(bri), unsigned(briT), unsigned(briLast), unsigned(bri != 0),
                      unsigned(strip.getMainSegment().mode), unsigned(strip.getTransition()));
        Serial.printf("[WLV-INJECT] stripBri=%u ablMax=%umA segs=%u seg0(bri=%u fx=%u ix=%u pal=%u)\n",
                      unsigned(strip.getBrightness()),
                      unsigned(BusManager::ablMilliampsMax()), unsigned(strip.getSegmentsNum()),
                      unsigned(strip.getMainSegment().opacity), unsigned(strip.getMainSegment().mode),
                      unsigned(strip.getMainSegment().intensity), unsigned(strip.getMainSegment().palette));
      }
      return;
    }
    /* px —— 把真正发给灯带的数据打出来:段级像素 / 总线级像素 / 总线亮度 / 线序覆盖。
     * 排"灯不够亮"时用:看最大通道到底有没有到 FF。 */
    /* btn —— 采样按键脚 2 秒,统计电平翻转次数。
     * 悬空的按键脚会被当成一直在按,WLED 的短按动作里有 `++effectCurrent`,
     * 结果就是效果自己乱跳(表现:灯带莫名变暗/变成音乐效果/颜色不对)。 */
    /* acfg —— 把 AudioReactive usermod 自己持有的配置导出来。
     * 它在 setup() 里按这些值建 I2S;建失败就永久 disableSoundProcessing,
     * 表现是音乐效果全黑而 setAudioReactive 又报 "on"。 */
    if (line == "acfg") {
#if defined(USERMOD_AUDIOREACTIVE)
      Usermod *um = UsermodManager::lookup(USERMOD_ID_AUDIOREACTIVE);
      if (!um) { Serial.println("[WLV-ACFG] usermod not found"); return; }
      DynamicJsonDocument doc(2048);
      JsonObject root = doc.to<JsonObject>();
      um->addToConfig(root);
      String out; serializeJson(root, out);
      Serial.print("[WLV-ACFG] "); Serial.println(out);
      DynamicJsonDocument idoc(2048);
      JsonObject iroot = idoc.to<JsonObject>();
      JsonObject u = iroot.createNestedObject("u");
      um->addToJsonInfo(iroot);
      String iout; serializeJson(iroot, iout);
      Serial.print("[WLV-AINFO] "); Serial.println(iout.substring(0, 600));
      // 引脚归属:I2SSource::initialize() 只要 allocatePin 失败就直接 return,不装驱动也不报错
      for (uint8_t g : {uint8_t(WELLVOW_PIN_MIC_DATA), uint8_t(WELLVOW_PIN_MIC_CLK)}) {
        Serial.printf("[WLV-APIN] gpio%u allocated=%u owner=%d\n", unsigned(g),
                      unsigned(PinManager::isPinAllocated(g)), int(PinManager::getPinOwner(g)));
      }
#endif
      return;
    }
    if (line == "mem") {   // 堆内存快照:排查“每 15 秒段表被重置成琥珀色”(WLED 在 heap<8192 时 resetSegments)
      Serial.printf("[WLV-MEM] up=%lus free=%u maxBlock=%u minFree=%u internal=%u psram=%u\n",
                    (unsigned long)(millis() / 1000), unsigned(ESP.getFreeHeap()), unsigned(ESP.getMaxAllocHeap()),
                    unsigned(ESP.getMinFreeHeap()), unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)), unsigned(ESP.getFreePsram()));
      return;
    }
    if (line == "btn") {
      Serial.printf("[WLV-BTN] disablePullUp=%u btnPin[0]=%d btnPin[1]=%d type0=%u type1=%u\n",
                    unsigned(disablePullUp), int(btnPin[0]), int(btnPin[1]),
                    unsigned(buttonType[0]), unsigned(buttonType[1]));
      for (uint8_t b = 0; b < 2; b++) {
        if (btnPin[b] < 0) continue;
        int last = digitalRead(btnPin[b]); uint32_t flips = 0, lows = 0, n = 0;
        uint32_t t0 = millis();
        while (millis() - t0 < 2000) { int v = digitalRead(btnPin[b]); if (v != last) { flips++; last = v; } if (!v) lows++; n++; delayMicroseconds(200); }
        Serial.printf("[WLV-BTN] pin %d: samples=%u low=%u (%u%%) flips=%u\n",
                      int(btnPin[b]), unsigned(n), unsigned(lows), unsigned(n ? lows*100/n : 0), unsigned(flips));
      }
      return;
    }
    /* gam <值> —— 运行时改颜色伽马(0 或 1 = 关)。
     * 伽马高:暗部压得狠,对比度/饱和度好看但整体偏暗;伽马关:亮但发灰发平。
     * 音频类效果(DJ Light 等)本身输出就只有半幅,对这个特别敏感。 */
    if (line.startsWith("gam")) {
      int sp = line.indexOf(' ');
      float g = sp > 0 ? line.substring(sp + 1).toFloat() : 0.0f;
      if (g <= 1.01f) { gammaCorrectCol = false; }
      else { gammaCorrectCol = true; gammaCorrectVal = g; NeoGammaWLEDMethod::calcGammaTable(gammaCorrectVal); }
      strip.trigger();
      Serial.printf("[WLV-GAM] gammaCorrectCol=%u val=%.2f\n", unsigned(gammaCorrectCol), gammaCorrectVal);
      return;
    }
    if (line == "px") {
      Serial.printf("[WLV-PX] bri=%u briT=%u briMultiplier=%u scaledBri=%u stripBri=%u\n",
                    unsigned(bri), unsigned(briT), unsigned(briMultiplier), unsigned(scaledBri(briT)), unsigned(strip.getBrightness()));
      Serial.printf("[WLV-PX] gammaCorrectCol=%u gammaCorrectBri=%u gammaVal=%.2f outOrder=%u ablMax=%umA\n",
                    unsigned(gammaCorrectCol), unsigned(gammaCorrectBri), gammaCorrectVal,
                    unsigned(wellvowOutputColorOrder), unsigned(BusManager::ablMilliampsMax()));
      Segment &m0 = strip.getMainSegment();
      Serial.printf("[WLV-PX] seg0 fx=%u opacity=%u col0=%06X col1=%06X pal=%u sx=%u ix=%u len=%u\n",
                    unsigned(m0.mode), unsigned(m0.opacity),
                    unsigned(m0.colors[0] & 0xFFFFFF), unsigned(m0.colors[1] & 0xFFFFFF), unsigned(m0.palette), unsigned(m0.speed), unsigned(m0.intensity), unsigned(m0.length()));
      Bus *b0 = BusManager::getNumBusses() ? BusManager::getBus(0) : nullptr;
      for (uint8_t i = 0; i < 8 && i < strip.getLengthTotal(); i++) {
        uint32_t sp = strip.getPixelColor(i);
        uint32_t bp = b0 ? b0->getPixelColor(i) : 0;
        Serial.printf("[WLV-PX] #%u strip=%02X%02X%02X bus=%02X%02X%02X\n", unsigned(i),
                      unsigned(R(sp)), unsigned(G(sp)), unsigned(B(sp)),
                      unsigned(R(bp)), unsigned(G(bp)), unsigned(B(bp)));
      }
      return;
    }
    if (line == "w") { uint8_t d[1] = {0x01}; handleWifi(d, 1); }   // 串口触发一次 WiFi 扫描,和 APP 走同一条路
    else if (line.startsWith("ar")) {   // ar / ar1 / ar32 / ar132:原始 I2S 自检(通道 0/1,16/32 位)
      int ch = line.indexOf('1') == 2 ? 1 : 0;
      int bits = line.endsWith("32") ? 32 : 16;
      rawPdmTest(ch, bits, line.indexOf('d') > 0);   // 命令里带 d = 用 16S 过采样
    }
    /* agc <0-3> —— 单独切自动增益档位(0 关 / 1 普通 / 2 灵敏 / 3 迟钝)。
     * AGC 会把音量归一化,档位太激进的话频谱长期顶满,DJ Light 这类按频段上色的效果就"糊"成一片。 */
    else if (line.startsWith("agc")) {
#if defined(USERMOD_AUDIOREACTIVE)
      int sp = line.indexOf(' ');
      int mode = sp > 0 ? line.substring(sp + 1).toInt() : 0;
      if (mode < 0 || mode > 3) mode = 0;
      applyAudioTuning(audioGain, audioSquelch, uint8_t(mode));
      Serial.printf("[WLV-AGC] agc=%d gain=%u squelch=%u\n", mode, unsigned(audioGain), unsigned(audioSquelch));
#endif
      return;
    }
    else if (line.startsWith("ag")) {   // ag <gain 0-255> <squelch 0-255>:运行时调灵敏度,判断是不是被噪声门压没了
#if defined(USERMOD_AUDIOREACTIVE)
      int sp = line.indexOf(' ');
      int gain = sp > 0 ? line.substring(sp + 1).toInt() : 255;
      int sq = 0; int sp2 = sp > 0 ? line.indexOf(' ', sp + 1) : -1;
      if (sp2 > 0) sq = line.substring(sp2 + 1).toInt();
      Usermod *um = UsermodManager::lookup(USERMOD_ID_AUDIOREACTIVE);
      if (um) {
        StaticJsonDocument<512> doc; JsonObject root = doc.to<JsonObject>();
        JsonObject ar = root.createNestedObject("AudioReactive");
        ar["enabled"] = true;
        JsonObject dm = ar.createNestedObject("digitalmic");
        dm["type"] = 5; JsonArray pins = dm.createNestedArray("pin");
        pins.add(WELLVOW_PIN_MIC_DATA); pins.add(WELLVOW_PIN_MIC_CLK); pins.add(-1); pins.add(-1);
        JsonObject cf = ar.createNestedObject("config");
        cf["squelch"] = sq; cf["gain"] = gain; cf["AGC"] = 0;
        um->readFromConfig(root);
        Serial.printf("[WLV-AR] gain=%d squelch=%d applied\n", gain, sq);
      }
#endif
    }
    else if (line == "a1") { setAudioReactive(true); audioTrace = true; Serial.println("[WLV-AR] on + trace"); }
    else if (line == "a0") { setAudioReactive(false); audioTrace = false; Serial.println("[WLV-AR] off"); }
    else if (line == "am") { audioTrace = !audioTrace; Serial.printf("[WLV-AR] trace=%u\n", audioTrace); }
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#endif
    else if (line.startsWith("co")) {   // 输出线序:co 0=GRB 1=RGB 2=BRG 3=RBG 4=BGR 5=GBR(与 DP 0x1D 相同,存 NVS)
      int o = line.substring(2).toInt(); if (o >= 0 && o <= COL_ORDER_GBR) { savedColorOrder = pendingColorOrder = o; colorOrderPending = true; saveRuntimeConfig(); }
      Serial.printf("[WLV-ORDER] saved=%u\n", savedColorOrder);
    }
    else if (line == "bd") resetPairing("serial");   // 串口:清除配对(换蓝牙地址并重启)
    else if (line == "h") { Serial.printf("[WLV-HUB] role=%u peers=%u\n", linkRole, onlinePeers()); for (uint8_t k = 0; k < peerCount; k++) Serial.printf("  %s %s role=%u leds=%u\n", IPAddress(peers[k].ip).toString().c_str(), peers[k].name, peers[k].role, peers[k].leds); }
  }
  void serviceHub() {   // 主设备定期广播发现包,维护对等表(其它角色只在 ?scan=1 时扫)
    if (linkRole == 1 && WLED_CONNECTED && studioUdpReady && millis() - lastPeerQuery > 5000) sendPeerQuery();
  }

  void appendTlv(uint8_t *out, size_t &n, uint8_t dp) {
    Segment &seg = strip.getMainSegment();
    switch (dp) {
      case 0x01: out[n++] = dp; out[n++] = 1; out[n++] = 1; out[n++] = bri != 0; break;
      case 0x02: out[n++] = dp; out[n++] = 2; out[n++] = 1; out[n++] = bri; break;
      case 0x03: out[n++] = dp; out[n++] = 2; out[n++] = 1; out[n++] = seg.speed; break;
      case 0x05: out[n++] = dp; out[n++] = 3; out[n++] = 2; out[n++] = 0; out[n++] = seg.mode; break;
      case 0x06: { uint32_t c = seg.colors[0]; out[n++]=dp; out[n++]=5; out[n++]=3; out[n++]=R(c); out[n++]=G(c); out[n++]=B(c); } break;  // 上报当前颜色(App 预览用真实颜色)
      case 0x0F: { const char *s = "WLED-0.15.1-WELLVOW"; uint8_t l = strlen(s); out[n++]=dp;out[n++]=5;out[n++]=l;memcpy(out+n,s,l);n+=l; } break;
      case 0x10: { const char *s = WELLVOW_MODEL; uint8_t l = strlen(s); out[n++]=dp;out[n++]=5;out[n++]=l;memcpy(out+n,s,l);n+=l; } break;
      case 0x11: out[n++]=dp;out[n++]=2;out[n++]=1;out[n++]=WLED_CONNECTED ? 1 : 0; break;
      // 上报当前分段布局 [段数, 每段灯数 u16 ...]:App 进 DIY 时核对,和本地段配置不一致就重发 0x1C,按段效果才有段可落
      case 0x1C: { uint8_t cnt = strip.getSegmentsNum(); if (cnt > 32) cnt = 32; out[n++]=dp; out[n++]=5; out[n++]=uint8_t(1 + cnt * 2); out[n++]=cnt;
        for (uint8_t i = 0; i < cnt; i++) { uint16_t L = strip.getSegment(i).length(); out[n++] = L >> 8; out[n++] = L & 0xFF; } } break;
      case 0x1D: { out[n++]=dp;out[n++]=2;out[n++]=1;out[n++]=savedColorOrder; } break;
      // 音乐状态 [开, 内置模式, 灵敏度 1..10, 当前 WLED 音频效果号] —— App 进音乐页据此还原滑条与选中项
      case 0x1E: { uint8_t vol = 0, raw = 0; audioLevels(vol, raw);   // 7 字节版:末尾带实时电平(App 画 VU)和底噪
        out[n++]=dp; out[n++]=5; out[n++]=7; out[n++]=musicActive?1:0; out[n++]=audioCalState; out[n++]=audioSens;
        out[n++]=musicActive?strip.getMainSegment().mode:0; out[n++]=vol; out[n++]=raw; out[n++]=audioSquelch; } break;   // 末字节 = 当前噪声门,App 画在电平条上
      // 联动角色 + 在线对等设备数;0x21 = 对等表 [数量, (ip 4B, 角色, 灯数 u16, 播放槽, 开关) ...](名字经 HTTP /wellvow/peers 取)
      case 0x20: out[n++]=dp; out[n++]=5; out[n++]=2; out[n++]=linkRole; out[n++]=onlinePeers(); break;
      case 0x25: { Bus *b0 = BusManager::getBus(0); out[n++] = dp; out[n++] = 5; out[n++] = 1; out[n++] = b0 ? b0->getType() : 0; } break;
      case 0x28: out[n++] = dp; out[n++] = 2; out[n++] = 1; out[n++] = MAX_NUM_SEGMENTS; break;   // 段数上限
      case 0x29: { uint8_t l = strlen(serverDescription); if (l > 32) l = 32; out[n++] = dp; out[n++] = 5; out[n++] = l; memcpy(out + n, serverDescription, l); n += l; } break;   // 设备显示名
      case 0x26: {   // 输出口布局 [2, (pin, start u16, len u16, rev) × 2]
        static const uint8_t OUTP[2] = {WELLVOW_PIN_OUT1, WELLVOW_PIN_OUT2};
        out[n++] = dp; out[n++] = 5; out[n++] = 13; out[n++] = 2;
        for (uint8_t i = 0; i < 2; i++) { Bus *b = findOutBus(OUTP[i]); uint16_t st = b ? b->getStart() : 0, L = b ? b->getLength() : 0;
          out[n++] = OUTP[i]; out[n++] = st >> 8; out[n++] = st & 0xFF; out[n++] = L >> 8; out[n++] = L & 0xFF; out[n++] = (b && b->isReversed()) ? 1 : 0; }
      } break;
      case 0x23: {   // 像素快照:[N, r g b × N],按整条灯带等距抽样;W 通道并进三色
        uint16_t total = strip.getLengthTotal(); uint8_t cnt = total < PREVIEW_PIX ? (total ? total : 1) : PREVIEW_PIX;
        out[n++] = dp; out[n++] = 5; out[n++] = 1 + cnt * 3; out[n++] = cnt;
        for (uint8_t k = 0; k < cnt; k++) {
          uint16_t i = total ? uint16_t(uint32_t(k) * total / cnt) : 0; uint32_t c = bri ? strip.getPixelColor(i) : 0;
          uint16_t w = (c >> 24) & 0xFF, r = ((c >> 16) & 0xFF) + w, g = ((c >> 8) & 0xFF) + w, b = (c & 0xFF) + w;
          out[n++] = r > 255 ? 255 : r; out[n++] = g > 255 ? 255 : g; out[n++] = b > 255 ? 255 : b;
        } } break;
      case 0x21: { size_t hdr = n; uint8_t cnt = 0; out[n++]=dp; out[n++]=5; out[n++]=0; out[n++]=0;
        for (uint8_t i = 0; i < peerCount && cnt < MAX_PEERS; i++) { Peer &pe = peers[i]; if (millis() - pe.seen >= PEER_ONLINE_MS) continue;
          out[n++]=pe.ip & 0xFF; out[n++]=(pe.ip >> 8) & 0xFF; out[n++]=(pe.ip >> 16) & 0xFF; out[n++]=(pe.ip >> 24) & 0xFF;
          out[n++]=pe.role; out[n++]=pe.leds >> 8; out[n++]=pe.leds & 0xFF; out[n++]=pe.playing; out[n++]=pe.on; cnt++; }
        out[hdr + 2] = uint8_t(1 + cnt * 9); out[hdr + 3] = cnt; } break;
      default: break;
    }
  }

  void handleDpGet(const uint8_t *d, size_t len) {
    uint8_t out[240]; size_t n = 0;
    for (size_t i = 0; i < len && n < sizeof(out) - 80; i++) appendTlv(out, n, d[i]);
    sendFrame(CMD_DP_REPORT, out, n);
  }

  void abortBlob() {
    if (blobFile) blobFile.close();
    blobActive = false;
    WLED_FS.remove("/diy.tmp");
    WLED_FS.remove("/ledmap.tmp");
    WLED_FS.remove("/sequence.tmp");
  }

  void handleBlob(const uint8_t *d, size_t len) {
    if (len < 3 || (d[0] < 0x01 || d[0] > 0x03) || d[2] == 0) return;
    const uint8_t kind = d[0], seq = d[1], total = d[2];
    if (seq == 0) {
      abortBlob();
      blobFile = WLED_FS.open(kind == 0x01 ? "/diy.tmp" : (kind == 0x02 ? "/ledmap.tmp" : "/sequence.tmp"), "w");
      if (!blobFile) return;
      blobActive = true; blobKind = kind; blobNextSeq = 0; blobTotal = total; blobReceived = 0; blobConn = bleReplyConn;
    }
    if (!blobActive || kind != blobKind || seq != blobNextSeq || total != blobTotal) { abortBlob(); return; }
    const size_t chunkLen = len - 3;
    if (chunkLen && blobFile.write(d + 3, chunkLen) != chunkLen) { abortBlob(); return; }
    blobReceived += chunkLen; blobNextSeq++;
    if (blobNextSeq != blobTotal) return;
    blobFile.close(); blobActive = false;
    const char *tmpPath = kind == 0x01 ? "/diy.tmp" : (kind == 0x02 ? "/ledmap.tmp" : "/sequence.tmp");
    if (blobExpected && blobReceived != blobExpected) { WLED_FS.remove(tmpPath); return; }
    if (kind == 0x01) {
      File check=WLED_FS.open(tmpPath,"r"); uint8_t h[7]; bool valid=check&&check.read(h,7)==7&&h[0]=='D'&&h[1]=='1'; check.close();
      if(!valid){WLED_FS.remove(tmpPath);return;} char dst[18];snprintf(dst,sizeof(dst),"/diy%u.bin",blobSlot);WLED_FS.remove(dst);
      if(!WLED_FS.rename(tmpPath,dst)){WLED_FS.remove(tmpPath);return;} applyDiy(blobSlot);
    } else if (kind == 0x02) {
      File check = WLED_FS.open(tmpPath, "r");
      DynamicJsonDocument doc(min((size_t)16384, max((size_t)2048, (size_t)blobReceived + 1024)));
      DeserializationError err = deserializeJson(doc, check); check.close();
      if (err || !doc["map"].is<JsonArray>() || doc["map"].size() == 0) { WLED_FS.remove(tmpPath); return; }
      WLED_FS.remove("/ledmap.json");
      if (!WLED_FS.rename(tmpPath, "/ledmap.json")) { WLED_FS.remove(tmpPath); return; }
      strip.deserializeMap(); enumerateLedmaps();
    } else {
      File check = WLED_FS.open(tmpPath, "r"); uint8_t h[12];
      bool valid = check && check.read(h, sizeof(h)) == sizeof(h) && h[0]=='W' && h[1]=='L' && h[2]=='V' && h[3]=='Q';
      check.close();
      if (!valid) { WLED_FS.remove(tmpPath); return; }
      char dst[20]; snprintf(dst, sizeof(dst), "/%u.wseq", blobSlot);
      WLED_FS.remove(dst);
      if (!WLED_FS.rename(tmpPath, dst)) { WLED_FS.remove(tmpPath); return; }
      startSequence(blobSlot, true);
    }
    const uint8_t ok[] = {kind, seq, total, 1}; sendFrame(CMD_BLOB, ok, sizeof(ok));
  }

  // 主设备:广播播放进度(stop=true 表示已停止,从设备跟着停)
  void sendLinkSync(bool stop) {
    if (linkRole != 1 || !studioUdpReady || !WLED_CONNECTED) return;
    uint8_t p[16] = {'W','V','S','Y', 1, uint8_t(stop ? 0xFF : sequenceSlot), uint8_t(sequenceFrame >> 8), uint8_t(sequenceFrame & 0xFF),
                     sequenceFps, uint8_t(sequenceLoop ? 1 : 0), 0, 0, 0, 0, 0, 0};
    uint16_t el = stop ? 0 : uint16_t(min<uint32_t>(millis() - sequenceLastMs, 65535)); p[10] = el >> 8; p[11] = el & 0xFF;
    IPAddress bc(uint32_t(WiFi.localIP()) | ~uint32_t(WiFi.subnetMask()));   // 本网段广播地址
    studioUdp.beginPacket(bc, 21325); studioUdp.write(p, sizeof(p)); studioUdp.endPacket();
    linkLastTx = millis();
  }
  // 定位到第 frame 帧(RLE 文件只能从头逐帧跳过;帧数不多,毫秒级)
  bool seekSequence(uint16_t frame) {
    if (!sequenceFile || !sequenceFrames) return false;
    frame %= sequenceFrames;
    if (!sequenceRle) { sequenceFile.seek(12 + uint32_t(frame) * sequenceLeds * 3, SeekSet); sequenceFrame = frame; return true; }
    sequenceFile.seek(12, SeekSet);
    for (uint16_t f = 0; f < frame; f++) {
      while (true) { int c = sequenceFile.read(); if (c < 0) return false; if (c == 0) break; sequenceFile.seek(sequenceFile.position() + 3, SeekSet); }
    }
    sequenceFrame = frame; return true;
  }
  void handleLinkSync(const uint8_t *p, int n) {
    if (linkRole != 2 || n < 12) return;
    uint8_t slot = p[5];
    if (slot == 0xFF) { if (sequencePlaying) stopSequence(); return; }
    uint16_t frame = (uint16_t(p[6]) << 8) | p[7]; uint16_t el = (uint16_t(p[10]) << 8) | p[11];
    if (!sequencePlaying || sequenceSlot != slot) { if (!startSequence(slot, true)) return; }
    int diff = int(sequenceFrame) - int(frame);
    if (diff > 1 || diff < -1) seekSequence(frame);
    sequenceLastMs = millis() - min<uint32_t>(el, 1000U / sequenceFps);   // 下一帧的时间点也对齐
  }

  void stopSequence() {
    if ((sequencePlaying || linkRole == 1) && !linkRestarting) sendLinkSync(true);   // 主设备:停止指令无论本机是否在播都广播给从机
    if (sequenceFile) sequenceFile.close();
    sequencePlaying = false; sequenceFrame = 0;
    if (realtimeMode == REALTIME_MODE_GENERIC && !studioLive) { realtimeTimeout = 0; exitRealtime(); }
    strip.trigger();
  }

  bool startSequence(uint8_t slot, bool loopPlay) {
    linkRestarting = true; stopSequence(); linkRestarting = false;
    char path[20]; snprintf(path, sizeof(path), "/%u.wseq", slot);
    sequenceFile = WLED_FS.open(path, "r");
    if (!sequenceFile) return false;
    uint8_t h[12];
    if (sequenceFile.read(h, sizeof(h)) != sizeof(h) || h[0]!='W' || h[1]!='L' || h[2]!='V' || h[3]!='Q' || h[4] != 1) {
      sequenceFile.close(); return false;
    }
    sequenceLeds = readU16(h + 5); sequenceFps = h[7] ? h[7] : 20;
    sequenceFrames = readU16(h + 8); sequenceRle = h[10] & 1;
    sequenceLoop = loopPlay || (h[10] & 2); sequenceFrame = 0; sequenceLastMs = 0; sequenceSlot = slot;
    if (!sequenceLeds || !sequenceFrames || sequenceFps > 60) { sequenceFile.close(); return false; }
    sequencePlaying = true; realtimeLock(3000, REALTIME_MODE_GENERIC); sendLinkSync(false); return true;
  }

  void sequenceEof() {
    if (sequenceLoop) { sequenceFile.seek(12, SeekSet); sequenceFrame = 0; }
    else stopSequence();
  }

  void serviceSequence() {
    if (!sequencePlaying || millis() - sequenceLastMs < 1000U / sequenceFps) return;
    sequenceLastMs = millis(); uint16_t out = 0, limit = min(sequenceLeds, strip.getLengthTotal());
    realtimeLock(3000, REALTIME_MODE_GENERIC);
    for (uint16_t i=limit; i<strip.getLengthTotal(); i++) strip.setPixelColor(i, 0);
    if (!sequenceRle) {
      uint8_t rgb[3];
      for (uint16_t i=0; i<sequenceLeds; i++) {
        if (sequenceFile.read(rgb, 3) != 3) { sequenceEof(); return; }
        if (i < limit) strip.setPixelColor(i, RGBW32(rgb[0], rgb[1], rgb[2], 0));
      }
    } else {
      while (true) {
        int count = sequenceFile.read(); if (count < 0) { sequenceEof(); return; }
        if (count == 0) break;
        uint8_t rgb[3]; if (sequenceFile.read(rgb, 3) != 3) { sequenceEof(); return; }
        for (uint16_t k=0; k<(uint8_t)count; k++,out++) if (out < limit) strip.setPixelColor(out, RGBW32(rgb[0], rgb[1], rgb[2], 0));
      }
    }
    strip.show(); if (++sequenceFrame >= sequenceFrames) sequenceEof();
    if (linkRole == 1 && (sequenceFrame == 0 || millis() - linkLastTx > 500)) sendLinkSync(false);
  }

  void stopStudioStream() {
    studioLive = false; studioPresentAt = 0;
    if (realtimeMode == REALTIME_MODE_GENERIC && !sequencePlaying) { realtimeTimeout = 0; exitRealtime(); }
    strip.trigger();
  }

  void serviceStudioStream() {
    if (studioUdpReady) for (uint8_t packets=0; packets<6; packets++) {
      int packetLen = studioUdp.parsePacket(); if (packetLen <= 0) break;
      if (packetLen > 1300) { while (studioUdp.available()) studioUdp.read(); continue; }
      uint8_t packet[1300]; int n = studioUdp.read(packet, sizeof(packet));
      IPAddress from = studioUdp.remoteIP(); bool self = uint32_t(from) == uint32_t(WiFi.localIP());   // 自己发的广播有的协议栈会回环,忽略
      if (n >= 12 && memcmp(packet, "WVSY", 4) == 0) { handleLinkSync(packet, n); continue; }   // 主设备进度包(不受人工控制抑制影响)
      if (n >= 5 && memcmp(packet, "WVDQ", 4) == 0) { if (!self) sendPeerReply(from); continue; }            // 对等发现:应答
      if (n >= 12 && memcmp(packet, "WVDR", 4) == 0) { updatePeer(from, packet, n); continue; }             // 对等发现:登记
      if (n >= 9 && memcmp(packet, "WVGS", 4) == 0) { if (!self) handleGroupSet(packet + 5, n - 5, true); continue; }   // 主设备转发的组指令
      if (n >= 8 && memcmp(packet, "WVDP", 4) == 0) { if (!self && linkRole == 2) handleDpSet(packet + 5, n - 5, true); continue; }   // 主从跟随
      if (studioInhibitUntil && millis() < studioInhibitUntil) continue;   // 人工控制期间只丢弃电脑实时帧流
      if (n < 10 || memcmp(packet, "WVF1", 4) != 0) continue;
      studioLastPacketMs = millis();
      uint8_t type = packet[4]; uint32_t frameId = readU32(packet + 6);
      if (type == 1 && n >= 16) {
        uint16_t total = readU16(packet + 10), start = readU16(packet + 12), count = readU16(packet + 14);
        if (!total || total > 1200 || start + count > total || n != 16 + count * 3) continue;
        if (studioLeds != total) {
          uint8_t *next = (uint8_t*)realloc(studioFrame, total * 3);
          if (!next) { stopStudioStream(); continue; }
          studioFrame = next; studioLeds = total;
        }
        if (studioFrameId != frameId) { studioFrameId = frameId; memset(studioFrame, 0, studioLeds * 3); }
        memcpy(studioFrame + start * 3, packet + 16, count * 3);
      } else if (type == 2 && n >= 12 && frameId == studioFrameId && studioFrame && !studioPresentAt) {
        uint16_t lead = readU16(packet + 10); studioPresentAt = millis() + min((uint16_t)200, max((uint16_t)5, lead));
        studioLive = true; sequencePlaying = false;
        if (sequenceFile) sequenceFile.close();
        realtimeLock(3000, REALTIME_MODE_GENERIC);   // 每个呈现包续期 3s(与看门狗一致),效果引擎在此期间完全停止
      } else if (type == 3) stopStudioStream();
    }
    // 推流方(App/Studio)失联:3 秒没收到任何包就退出实时模式,灯带回到之前的效果/预设,不会停在最后一帧
    if (studioLive && millis() - studioLastPacketMs > 3000) { stopStudioStream(); Serial.println("[WLV-STUDIO] stream timeout, back to normal"); return; }
    if (!studioLive || !studioPresentAt || (int32_t)(millis() - studioPresentAt) < 0) return;
    studioPresentAt = 0; uint16_t limit = min(studioLeds, strip.getLengthTotal());
    { static uint32_t _dbg = 0; if (millis() - _dbg > 1000) { _dbg = millis();   // 调试:每秒打印一次收到的实时帧摘要
        Serial.printf("[WLV-STUDIO] live frame=%u leds=%u limit=%u rgb0=%02X%02X%02X rgb1=%02X%02X%02X rgbLast=%02X%02X%02X\n",
          unsigned(studioFrameId), unsigned(studioLeds), unsigned(limit), studioFrame[0], studioFrame[1], studioFrame[2],
          studioLeds > 1 ? studioFrame[3] : 0, studioLeds > 1 ? studioFrame[4] : 0, studioLeds > 1 ? studioFrame[5] : 0,
          studioFrame[(studioLeds-1)*3], studioFrame[(studioLeds-1)*3+1], studioFrame[(studioLeds-1)*3+2]); } }
    for (uint16_t i=0; i<limit; i++) {
      uint8_t *p = studioFrame + i * 3; strip.setPixelColor(i, RGBW32(p[0], p[1], p[2], 0));
    }
    for (uint16_t i=limit; i<strip.getLengthTotal(); i++) strip.setPixelColor(i, 0);   // 布线点数之外的灯清黑,不残留旧效果
    strip.show();
  }

  void sendWifiState(uint8_t state) { const uint8_t d[] = {0x04, state}; sendFrame(CMD_WIFI, d, 2); }
  void sendWifiIp() {
    IPAddress ip = WiFi.localIP(); const uint8_t d[] = {0x06, ip[0], ip[1], ip[2], ip[3]};
    sendFrame(CMD_WIFI, d, sizeof(d));
  }

  // 配网(BLE CMD 0x42 与网页 POST /wellvow/wifi 共用同一路径):写入 WLED 的 STA 配置(cfg.json,断电不丢)
  // 并强制重连;之后 WLED 自己负责开机自动连、掉线每 18s 重试,不需要 App/网页参与。
  void provisionWifi(const char *ssid, const char *pass) {
    if (multiWiFi.empty()) multiWiFi.push_back(WiFiConfig());
    strlcpy(multiWiFi[0].clientSSID, ssid, sizeof(multiWiFi[0].clientSSID));
    strlcpy(multiWiFi[0].clientPass, pass, sizeof(multiWiFi[0].clientPass));
    selectedWiFi = 0; doSerializeConfig = true; forceReconnect = true;
    wifiConnectPending = true; wifiConnectStarted = millis(); previousWifiStatus = WL_IDLE_STATUS;
    lastWifiResult = 0;
    /* 配网一定要有肉眼可见的反馈:切到默认彩虹。
     * 否则如果灯带正停在音乐效果(没声音=全黑)或亮度很低,用户按下"发送到设备"后什么都看不到,
     * 分不清是指令没收到还是设备卡住了。 */
    musicActive = false; musicWanted = false;
    for (uint8_t i = 0; i < strip.getSegmentsNum(); i++) applyEffect(strip.getSegment(i), WELLVOW_DEFAULT_FX);
    if (!bri) { bri = briLast ? briLast : 128; }
    syncGlobalsFromSegment();
    saveRuntimeConfig();
    stateUpdated(CALL_MODE_DIRECT_CHANGE);
    Serial.printf("[WLV-WIFI] provisioning -> default rainbow fx=%u\n", unsigned(WELLVOW_DEFAULT_FX));
    sendWifiState(0);
  }

  void handleWifi(const uint8_t *d, size_t len) {
    if (!len) return;
    if (d[0] == 0x01) {
      wifiScanRequested = true;   // 真正的扫描在 serviceWifi() 里同步做
    } else if (d[0] == 0x03 && len >= 3) {
      uint8_t sl = d[1]; if (sl > 32 || 2 + sl >= len) return;
      uint8_t pl = d[2 + sl]; if (pl > 63 || 3 + sl + pl > len) return;
      char ssid[33] = {0}, pass[64] = {0};
      memcpy(ssid, d + 2, sl); memcpy(pass, d + 3 + sl, pl);
      provisionWifi(ssid, pass);
    } else if (d[0] == 0x05) {
      if (WLED_CONNECTED) sendWifiIp(); else sendWifiState(wifiConnectPending ? 0 : 4);
    }
  }

  void serviceWifi() {
    /* WiFi 扫描:原先用异步扫描 + 轮询 scanComplete(),在"没配过网(纯 AP 模式)+ 蓝牙已连接"这种
     * 组合下经常拿不到结果,APP 那边就是一直空列表等到 15 秒超时。改成在主循环里同步扫一次:
     * 结果一定归我们自己读,读完立刻发给 APP;失败就显式开 STA 再试一次;无论如何都发结束帧,
     * 不让 APP 干等。每信道 150ms,13 个信道约 2 秒,配网时短暂卡一下灯效可以接受。 */
    if (wifiScanRequested) {
      cp(20); wifiScanRequested = false;
      WiFi.scanDelete();
      /* 没连上路由器时 STA 往往卡在"正在连接"(没配过网时 WLED 会反复尝试),
       * 这个状态下 esp_wifi_scan_start 直接返回 ESP_ERR_WIFI_STATE。先断开再扫。
       * 已经连上的设备不能断,否则会掉线,连着的时候本来也允许扫描。 */
      if (!WLED_CONNECTED) { esp_wifi_disconnect(); delay(120); }
      int n = WiFi.scanNetworks(false, true, false, 150);
      if (n < 0) {
        // Arduino 层只回 -2,看不出原因;直接问 IDF 要错误码
        WiFi.enableSTA(true); delay(300);
        wifi_mode_t wm; esp_wifi_get_mode(&wm);
        esp_err_t st = esp_wifi_start();
        esp_err_t sp = esp_wifi_scan_stop();
        wifi_scan_config_t sc = {}; sc.show_hidden = true;
        esp_err_t e = esp_wifi_scan_start(&sc, true);
        uint16_t num = 0; esp_wifi_scan_get_ap_num(&num);
        Serial.printf("[WLV-WIFI] idf mode=%d start=%s stop=%s scan=%s num=%u\n",
                      int(wm), esp_err_to_name(st), esp_err_to_name(sp), esp_err_to_name(e), unsigned(num));
        if (e == ESP_OK && num) n = int(num);
        else { WiFi.scanDelete(); n = WiFi.scanNetworks(false, true, false, 150); }
      }
      Serial.printf("[WLV-WIFI] scan n=%d mode=%d connected=%d\n", n, int(WiFi.getMode()), int(WLED_CONNECTED));
      int total = (n > 0) ? min(n, 15) : 0;
      if (total == 0) { const uint8_t d0[] = {0x02,0,0,0,0,0}; sendFrame(CMD_WIFI, d0, 6); }
      for (int i = 0; i < total; i++) {
        String ss = WiFi.SSID(i); uint8_t sl = min((int)ss.length(), 32);
        if (!sl) { continue; }
        uint8_t out[38] = {0x02, uint8_t(i), uint8_t(total), uint8_t(WiFi.RSSI(i)), uint8_t(WiFi.encryptionType(i)), sl};
        memcpy(out + 6, ss.c_str(), sl);
        sendFrame(CMD_WIFI, out, 6 + sl);
        delay(25);
      }
      // 末帧补发一次:BLE 拥塞时最后一帧丢了,APP 会一直等到超时
      if (total > 0) {
        int i = total - 1; String ss = WiFi.SSID(i); uint8_t sl = min((int)ss.length(), 32);
        uint8_t out[38] = {0x02, uint8_t(i), uint8_t(total), uint8_t(WiFi.RSSI(i)), uint8_t(WiFi.encryptionType(i)), sl};
        memcpy(out + 6, ss.c_str(), sl); delay(30); sendFrame(CMD_WIFI, out, 6 + sl);
      }
      WiFi.scanDelete();
    }
    if (wifiConnectPending) {
      wl_status_t st = WiFi.status();
      if (st == WL_CONNECTED) { wifiConnectPending = false; lastWifiResult = 1; lastWifiLinkUp = true; sendWifiState(1); sendWifiIp(); }
      else if (st != previousWifiStatus && st == WL_CONNECT_FAILED) { wifiConnectPending = false; lastWifiResult = 2; sendWifiState(2); }
      else if (millis() - wifiConnectStarted > 35000) { wifiConnectPending = false; lastWifiResult = 4; sendWifiState(4); }
      previousWifiStatus = st;
    } else {
      // 路由器链路变化(开机自动连上 / 掉线 / 重连成功)主动经 BLE 报给 App:连上报 IP 帧(0x06),掉线报状态 4,
      // App 的 Wi-Fi 状态条据此实时切换“已连接 · IP”/“WiFi 未连接”,不必等用户手动查询。
      bool up = WLED_CONNECTED;
      if (up != lastWifiLinkUp) { lastWifiLinkUp = up; if (up) sendWifiIp(); else sendWifiState(4); }
    }
  }

public:
  void onResult(NimBLEAdvertisedDevice *advertisedDevice) override {
    const uint8_t *p = advertisedDevice->getPayload(); size_t n = advertisedDevice->getPayloadLength();
    // OPEN-BLE 2.4G remote AD structure: 0D xx 55 AA ........ key(at +12).
    for (size_t i=0; i+12<n; i++) if (p[i]==0x0D && p[i+2]==0x55 && p[i+3]==0xAA) { pendingRadioKey=p[i+12]; break; }
  }

  void serviceRadioRemote() {
    uint8_t key = pendingRadioKey; if (!key) return; pendingRadioKey = 0;
    Segment &seg = strip.getMainSegment();
    switch (key) {
      case 0x08: setPower(true); break;
      case 0x02: setPower(false); break;
      case 0x14: bri = qadd8(bri, 16); break;
      case 0x0E: bri = qsub8(bri, 16); break;
      case 0x04: seg.setMode((seg.mode + 1) % strip.getModeCount()); break;
      case 0x05: seg.setMode(seg.mode ? seg.mode - 1 : strip.getModeCount() - 1); break;
      case 0x16: seg.setColor(0, RGBW32(255,0,0,0)); break;
      case 0x10: seg.setColor(0, RGBW32(0,255,0,0)); break;
      case 0x0A: seg.setColor(0, RGBW32(0,0,255,0)); break;
      case 0x12: seg.setColor(0, RGBW32(255,255,255,0)); break;
      case 0x3D: effectSpeed = seg.speed = qadd8(seg.speed, 16); break;
      case 0x3C: effectSpeed = seg.speed = qsub8(seg.speed, 16); break;
      default: return;
    }
    syncGlobalsFromSegment();       // 同上:先同步全局,否则 colorUpdated 会把按键设的颜色改回去
    colorUpdated(CALL_MODE_BUTTON); // also propagates over WLED Wi-Fi sync
  }

  void onConnect(NimBLEServer* srv, ble_gap_conn_desc* desc) override {
    BleConn *slot = bleConnOf(desc->conn_handle); if (!slot) slot = bleConnOf(0xFFFF);
    if (slot) { slot->handle = desc->conn_handle; slot->auth = false; slot->pending = false; }
    bleConnected = true;
    const uint8_t n = bleConnCount();
    /* 请求更短的连接间隔:手机默认协商出来常是 30-50ms,一条指令要等一个连接事件才送达,
     * 连点几下就能感觉到"慢半拍"。7.5-15ms 是 iOS 接受范围内最快的一档。
     * 多于 2 部手机时射频排不开这么密的间隔,后面的连接用 30..60ms。 */
    if (n <= 2) srv->updateConnParams(desc->conn_handle, 12, 24, 0, 600);   // 单位 1.25ms -> 15..30ms;监督超时 6s
    else        srv->updateConnParams(desc->conn_handle, 24, 48, 0, 600);
    if (n < BLE_MAX_CONN) NimBLEDevice::startAdvertising();   // 还有空位就继续广播,让下一部手机也能连
    bleConnHandle = desc->conn_handle;
    const uint8_t *a = desc->peer_ota_addr.val;   // 打出对端地址:同一台手机在一段时间内地址不变,能分清是哪台在连
    Serial.printf("[WLV-BLE] connected peer=%02X:%02X:%02X:%02X:%02X:%02X conn=%u (%u/%u)\n", a[5], a[4], a[3], a[2], a[1], a[0], unsigned(desc->conn_handle), unsigned(n), unsigned(BLE_MAX_CONN));

  }
  uint16_t bleConnHandle = 0xFFFF;
  /* "清除配对"(板上按键长按 3 秒 / 串口 "bd"):换一个新的随机静态蓝牙地址并重启。
   * 手机端没有可清的"绑定":谁存过这块板谁就会自动回连并占住唯一的蓝牙连接,别的手机永远扫不到。
   * 换地址后,iOS 把它当成一台新设备 —— 所有存过它的手机都失效、要重新添加;想用的那台手机扫描即可连上。
   * 地址存 NVS,重启后不变(换成的新身份是稳定的)。踢人 + 拉黑不行:iOS 每 300ms 就回连一次,别的手机根本插不进去。 */
  bool loadBleAddr(uint8_t out[6]) {
    nvs_handle h = 0; if (nvs_open("wellvow", NVS_READONLY, &h) != ESP_OK) return false;
    size_t n = 6; bool ok = nvs_get_blob(h, "bleaddr", out, &n) == ESP_OK && n == 6; nvs_close(h); return ok;
  }
  void resetPairing(const char *why) {
    uint8_t a[6]; esp_fill_random(a, 6); a[5] |= 0xC0;   // 随机静态地址:最高两位必须是 11
    nvs_handle h = 0;
    if (nvs_open("wellvow", NVS_READWRITE, &h) == ESP_OK) { nvs_set_blob(h, "bleaddr", a, 6); nvs_commit(h); nvs_close(h); }
    Serial.printf("[WLV-BLE] reset pairing (%s): new addr %02X:%02X:%02X:%02X:%02X:%02X, restarting\n", why, a[5], a[4], a[3], a[2], a[1], a[0]);
    for (uint16_t i = 0; i < strip.getLengthTotal(); i++) strip.setPixelColor(i, RGBW32(255, 255, 255, 0)); strip.show();   // 白闪作反馈
    delay(400); ESP.restart();
  }
  /* 板上按键:必须先看到"松开 ≥ 0.5 秒"再"按住 ≥ 3 秒"才算长按(有的板子按键脚常态就读成按下,不能上电就触发)。
   * 3 秒内不接管 WLED 自己的按键逻辑(短按仍是开关)。 */
  uint32_t btnDownAt = 0, btnUpAt = 0; bool btnArmed = false, btnTaken = false; int8_t btnUsable = -1;
  bool handleButton(uint8_t b) override {
    if (b != 0) return false;
    bool down = isButtonPressed(0); uint32_t now = millis();
    if (btnUsable < 0) { btnUsable = down ? 0 : 1;
      if (!btnUsable) {   // 悬空 / 常按的脚:除了关掉自家的配对复位,还必须让 WLED 原生按键逻辑彻底不认它 ——
        // button.cpp 里按住 >10s 再"松开"会 WLED_FS.format() 恢复出厂(2026-10-09 真机:两块开发板 cfg.json 被格式化成默认、Wi-Fi 全丢,表现像随机崩溃重启)
        for (uint8_t b = 0; b < WLED_MAX_BUTTONS; b++) if (btnPin[b] == btnPin[0] && btnPin[0] >= 0) buttonType[b] = BTN_TYPE_NONE;
        Serial.println("[WLV-BTN] button reads pressed at boot -> pairing reset AND WLED button (incl. 10s factory reset) disabled on this board"); } }
    if (!btnUsable) return false;   // 悬空 / 常按的脚:不要拿噪声当长按,否则会莫名重启换地址(表现为"蓝牙老掉线")
    if (!down) { if (!btnUpAt) btnUpAt = now; if (now - btnUpAt >= 2000) btnArmed = true; btnDownAt = 0; if (btnTaken) { btnTaken = false; return true; } return false; }
    btnUpAt = 0;
    if (!btnArmed) return false;
    if (!btnDownAt) btnDownAt = now;
    if (!btnTaken && now - btnDownAt >= 3000) { btnTaken = true; buttonPressedBefore[0] = false; buttonLongPressed[0] = false; resetPairing("long-press"); }
    return btnTaken;
  }
  void onDisconnect(NimBLEServer *s, ble_gap_conn_desc *desc) override {
    BleConn *c = bleConnOf(desc->conn_handle);
    if (c) { c->handle = 0xFFFF; c->auth = false; c->pending = false; }
    bleConnected = bleConnCount() > 0;
    if (blobActive && blobConn == desc->conn_handle) abortBlob();
    if (bleConnHandle == desc->conn_handle) bleConnHandle = 0xFFFF;
    NimBLEDevice::startAdvertising();
    Serial.printf("[WLV-BLE] disconnected conn=%u (%u/%u left); advertising restarted\n", unsigned(desc->conn_handle), unsigned(bleConnCount()), unsigned(BLE_MAX_CONN));
  }
  void onWrite(NimBLECharacteristic *c, ble_gap_conn_desc *desc) override {   // 注意:NimBLE 会同时调用不带 desc 的 onWrite,所以只能重写这一个
    std::string raw = c->getValue();
    const uint16_t ch = desc ? desc->conn_handle : 0xFFFF;
    BleConn *conn = bleConnOf(ch);
    if (!conn) { Serial.printf("[WLV-BLE] RX from unknown conn %u, ignored\n", unsigned(ch)); return; }
    if (raw.size() < 5) return;
    const uint8_t *f = reinterpret_cast<const uint8_t*>(raw.data()); size_t len = raw.size();
    if (f[0] != 0xAA || f[1] != 0x55 || size_t(f[3]) + 5 != len) return;
    uint16_t sum = f[2] + f[3]; for (size_t i = 0; i < f[3]; i++) sum += f[4+i];
    if (uint8_t(sum) != f[len-1]) return;
    const uint8_t *d = f + 4; size_t dl = f[3];
    Serial.printf("[WLV-BLE] RX cmd=0x%02X len=%u conn=%u auth=%u\n", unsigned(f[2]), unsigned(dl), unsigned(ch), unsigned(conn->auth));
    if (f[2] == CMD_AUTH) { handleAuth(d, dl, *conn); return; }
    if (!conn->auth) { Serial.println("[WLV-BLE] DENY not authenticated"); deny(ch); return; }
    if (dl > sizeof(BleFrame::d)) return;
    uint8_t next = uint8_t((bleQHead + 1) % BLE_Q);
    if (next == bleQTail) { Serial.println("[WLV-BLE] queue full, frame dropped"); return; }
    BleFrame &q = bleQ[bleQHead]; q.cmd = f[2]; q.len = uint8_t(dl); q.conn = ch; memcpy(q.d, d, dl);
    bleQHead = next;   // 实际执行见 serviceBleQueue()(主循环)
  }

  void startBle() {
    if (bleStarted) return;
    // BT 控制器 + Wi-Fi/BT 共存模块要在内部 RAM 里拿一大块连续内存;不够时 IDF 的
    // coex_core_enable() 会直接 abort() —— 表现为整机无限重启、串口只剩 "Ada" + Backtrace。
    // 这里先看堆,不够就先不开 BLE、每 5 秒再试一次,至少让灯带/网页照常工作并留下日志。
    const uint32_t freeHeap = ESP.getFreeHeap(), maxBlock = ESP.getMaxAllocHeap();
    if (millis() - lastBleAttempt < 5000) return;
    lastBleAttempt = millis();
    Serial.printf("[WLV-BLE] start attempt: heap free=%u largest=%u leds=%u\n", unsigned(freeHeap), unsigned(maxBlock), unsigned(strip.getLengthTotal()));
    if (maxBlock < 40000) { Serial.println("[WLV-BLE] not enough contiguous heap for BT controller, retry later"); return; }
    WiFi.setSleep(true);   // 兜底:开 BT 控制器前确保 Wi-Fi 省电已开(见 setup() 注释),否则 coex 直接 abort
    bleStarted = true;
    char bleName[24];
    snprintf(bleName, sizeof(bleName), WELLVOW_BLE_NAME, uint16_t(ESP.getEfuseMac()));
    NimBLEDevice::init(bleName);
    { uint8_t a[6]; if (loadBleAddr(a)) { ble_hs_id_set_rnd(a); NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM); Serial.printf("[WLV-BLE] using random static addr %02X:%02X:%02X:%02X:%02X:%02X (pairing was reset)\n", a[5], a[4], a[3], a[2], a[1], a[0]); } }
    NimBLEDevice::setMTU(185);
    server = NimBLEDevice::createServer(); server->setCallbacks(this);
    NimBLEService *svc = server->createService(NimBLEUUID((uint16_t)0xFFF0));
    NimBLECharacteristic *writeChar = svc->createCharacteristic(NimBLEUUID((uint16_t)0xFFF3), NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    notifyChar = svc->createCharacteristic(NimBLEUUID((uint16_t)0xFFF4), NIMBLE_PROPERTY::NOTIFY);
    writeChar->setCallbacks(this); svc->start();
    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising(); adv->addServiceUUID(NimBLEUUID((uint16_t)0xFFF0)); adv->setName(bleName); adv->setScanResponse(true);
    /* 广播间隔调快:NimBLE 默认是 20ms..10.24s 的宽区间,实际会退到很慢的一档,
     * 设备上电后手机要等好几秒才发现得到。100-150ms 既够快,功耗也可接受。 */
    adv->setMinInterval(160);   // 单位 0.625ms -> 100ms
    adv->setMaxInterval(240);   // -> 150ms
    adv->start();
    radioScan = NimBLEDevice::getScan(); radioScan->setAdvertisedDeviceCallbacks(this, true);
    radioScan->setActiveScan(false); radioScan->setInterval(160); radioScan->setWindow(48);
    radioScan->start(0, nullptr, false);
    for (uint8_t i = 0; i < BusManager::getNumBusses(); i++) {
      Bus *bus = BusManager::getBus(i);
      if (bus) { uint8_t _pins[5] = {255,255,255,255,255}; bus->getPins(_pins); Serial.printf("[WLV-BUSTYPE] bus=%u type=%u rgb=%u white=%u cct=%u pin=%u len=%u\n", unsigned(i), unsigned(bus->getType() & 0x7F),
                             unsigned(bus->hasRGB()), unsigned(bus->hasWhite()), unsigned(bus->hasCCT()), unsigned(_pins[0]), unsigned(bus->getLength())); }
    }
    // 自动限流(ABL)会在灯数多时把亮度压得很低,表现为"亮度滑块拉到底也没什么变化" —— 打出来好排查
    Serial.printf("[WLV-ABL] maxCurrent=%umA perLed=%umA briMultiplier=%u\n",
                  unsigned(BusManager::ablMilliampsMax()),
                  unsigned(BusManager::getNumBusses() && BusManager::getBus(0) ? BusManager::getBus(0)->getLEDCurrent() : 0),
                  unsigned(briMultiplier));
  }

  /* 日出 / 日落预设用的两张调色板(WLED 自定义调色板文件 /palette0.json、/palette1.json → 调色板号 255、254),
   * 配合 WLED 自带的 Sunrise 效果(123):它按时间把"太阳"从灯带中心向两端推开,像素颜色 = 调色板[0..240],
   * 0 = 背景(夜),240 = 太阳。速度 1..60 = 日出分钟数,61..120 = 日落分钟数(反向推进,最后停在夜色)。
   * 颜色序列按真实晨昏蒙影:天文(近黑的深蓝)→ 航海(深蓝)→ 民用(靛/紫 → 地平线粉红 → 橙红)→ 日出(金 → 暖白)。
   * 日落更红更橙(傍晚空气里气溶胶/云更多),余晖经洋红、紫、靛蓝回到夜色。文件缺失才写,用户自己改过就不动。 */
  void ensureSkyPalettes() {
    /* 日出 6 个关键色(用户 2026-10-21 定):夜藏蓝 → 靛紫 → 粉红(民用晨昏地平线)→ 橙红(太阳将出)→ 金(太阳露头)→ 淡蓝白(白天)。等距放在 0..255 上,配合效果里 N=6 的两两过渡。 */
    static const char *sunrise = "{\"palette\":[0,\"2a2566\",63,\"c84a7a\",127,\"f5731f\",191,\"ffc14a\",255,\"dfe9f5\"]}";   // 10-21:5 色默认(= App 日出配色第 1 组)
    static const uint8_t PAL_VER = 5;   // 改了调色板内容就 +1:标记文件版本不符时强制重写(否则"缺失才写"永远用旧的)
    static const char *sunset  = "{\"palette\":[0,\"dfe9f5\",63,\"ffc14a\",127,\"f5731f\",191,\"c84a7a\",255,\"2a2566\"]}";   // 日落 = 日出第 1 组倒序
    bool wrote = false, force = true;
    { File v = WLED_FS.open("/wlv_pal.ver", "r"); if (v) { force = (v.parseInt() != PAL_VER); v.close(); } }
    const char *names[2] = {"/palette0.json", "/palette1.json"}; const char *bodies[2] = {sunrise, sunset};
    for (int i = 0; i < 2; i++) {
      if (!force && WLED_FS.exists(names[i])) continue;
      File f = WLED_FS.open(names[i], "w"); if (!f) continue;
      f.print(bodies[i]); f.close(); wrote = true;
      Serial.printf("[WLV-PAL] wrote %s (sky palette)\n", names[i]);
    }
    if (force) { File v = WLED_FS.open("/wlv_pal.ver", "w"); if (v) { v.print(PAL_VER); v.close(); } }
    if (wrote) strip.loadCustomPalettes();
    for (int i = 0; i < 2; i++) {   // 把文件里的关键色读回 g_skyKeys(用户换过配色后重启也一致)
      File f = WLED_FS.open(names[i], "r"); if (!f) continue; String body = f.readString(); f.close();
      uint8_t n = 0; int p = 0;
      while (n < 8) { int q = body.indexOf('"', p); if (q < 0) break; int e = body.indexOf('"', q + 1); if (e < 0) break;
        if (e - q - 1 == 6) { uint32_t v = strtoul(body.substring(q + 1, e).c_str(), nullptr, 16); g_skyKeys[i][n][0] = v >> 16; g_skyKeys[i][n][1] = (v >> 8) & 0xFF; g_skyKeys[i][n][2] = v & 0xFF; n++; }
        p = e + 1; }
      if (n >= 2) g_skyKeyN[i] = n;
    }
  }
  void setup() override {
    ensureSkyPalettes();
    { uint8_t id = strip.addEffect(142, &mode_wellvow_sky, _data_FX_MODE_WLV_SKY); Serial.printf("[WLV-FX] WELLVOW Sky effect id=%u (expect 142)\n", unsigned(id)); }
    /* 串口写入绝不能阻塞主循环。
     * S3 走原生 USB-CDC:没有主机在读的时候 TX 缓冲很快写满,默认 Serial.printf 会一直等,
     * 主循环被卡住 → serviceBleQueue() 来不及取,而 BLE 帧环只有 8 个槽 → 指令直接丢。
     * 现象极具迷惑性:电脑开着串口监听时一切正常,一关掉监听 App 就"控制不了"。
     * 超时设为 0 = 写不进去就丢日志,宁可少几行日志也不能让主循环停。 */
#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
    Serial.setTxTimeoutMs(0);
#endif
    { esp_reset_reason_t rr = esp_reset_reason(); Serial.printf("[WLV-BOOT] reset reason=%d last checkpoint=0x%X\n", int(rr), unsigned(rr == ESP_RST_POWERON ? 0 : wlvCheckpoint)); wlvCheckpoint = 0; }
    sendNotifications = true; sendNotificationsRT = true; notifyDirect = true;
    // ESP32 上 BLE 与 Wi-Fi 共存有个硬约束:STA 模式下若关闭了 Wi-Fi 省电(WIFI_PS_NONE,
    // 即 WLED 的“Disable WiFi sleep”),IDF 的 coex_core_enable() 会因为 condition_check 失败
    // 直接 abort() —— 表现为一配了路由器就无限重启(纯 AP 模式不触发,所以出厂状态下看不出来)。
    // 因此本 usermod 强制打开 Wi-Fi modem sleep;WLED 会在 initConnection() 里据此调用 WiFi.setSleep(true)。
    noWifiSleep = false;
    // 开机恢复上次状态:bootPreset=250 = auto-save 预设。beginStrip() 已在本 setup 之前
    // 应用过 bootPreset,故这里设置对"本次"开机不生效,持久化后从下次开机起生效。
    // 安全闸:只有 /cfg.json 存在且看起来完整时才允许把上面的默认值写回去。否则(文件缺失/写到一半掉电
    // 或崩溃留下的残缺文件)WLED 会带着出厂默认值启动,这里一 serializeConfig() 就把默认值永久写进 flash,
    // 用户的灯数/引脚/名字/热点全部丢失(2026-09-07 发生过一次:cfg.json 变成 30 灯 GPIO1、名 WLED)。
    // 残缺时不写,下次开机 WLED 仍会重试读原文件,给恢复留机会。
    bool cfgLooksValid = false;
    { File cf = WLED_FS.open("/cfg.json", "r");
      if (cf) { char head[8] = {0}; size_t sz = cf.size(); cf.readBytes(head, 6); cf.close();
        cfgLooksValid = sz > 300 && strncmp(head, "{\"rev\"", 6) == 0; }
      if (!cfgLooksValid) Serial.println("[WLV-CFG] /cfg.json missing or incomplete: NOT writing defaults back"); }
    if (bootPreset != 250) { bootPreset = 250; if (cfgLooksValid) serializeConfig(); }
    // 固定 mDNS 名:http://wellvow-strip.local —— 路由器哪天换了分配的 IP,网页/App 仍可用同一个地址找到设备。
    // 只在用户没自己改过名字(仍是 WLED 默认 "wled"/"wled-xxxxxx")时设置;UsermodManager::setup() 早于 WLED 替换默认名。
    // 新设备默认名带 MAC 后 4 位(wellvow-strip-xxxx),多台同网时 mDNS 名不冲突;已经起过名的设备不动
    if (!cmDNS[0] || strncmp(cmDNS, "wled", 4) == 0) { snprintf(cmDNS, sizeof(cmDNS), WELLVOW_MDNS, unsigned(uint16_t(ESP.getEfuseMac() >> 32))); if (cfgLooksValid) serializeConfig(); }
    if (!syncGroups) syncGroups = 1;
    if (!receiveGroups) receiveGroups = 1;
    /* 早期固件的 prepareManualColor() 把"全局"过渡时长写成了 0 并被存进 cfg.json。
     * 过渡为 0 时 handleTransitions() 的 `transitionActive && getTransition() > 0` 判断不成立,
     * 那次渐变永远走不完、applyFinalBri() 也不会执行 —— 表现就是**关机按了没反应**(亮度停在原值)。
     * 这里把 0 治回默认值。 */
    if (transitionDelay == 0) {
      transitionDelay = transitionDelayDefault = 750;
      strip.setTransition(transitionDelay);
      Serial.println("[WLV-FIX] transitionDelay was 0 (old firmware bug) -> restored to 750ms");
      if (cfgLooksValid) serializeConfig();
    }
    /* 关掉 WLED 的自动限流(ABL)。
     * WLED 默认按"灯带由控制器供电"算:上限 850mA、每颗 55mA。这块产品的灯带是外部电源直供的,
     * 控制器根本不过灯电流,这个上限没有物理意义。但 162 颗灯会被估成 8.9A,ABL 就把亮度压到一成左右,
     * 而且滑块调高多少它就压回去多少 —— 用户看到的就是"亮度滑块完全没作用"。
     * 只在还是 WLED 出厂默认值时才改,用户自己在 LED 设置里填过就不动。 */
    /* 关掉颜色伽马校正。WLED 默认 gammaCorrectCol=true / gamma=2.8,是按"人眼感知线性"
     * 调的显示曲线:纯色不受影响(255 还是 255),但**所有中间色都被大幅压暗** ——
     * 实测段色 0x80 下到灯带只剩 0x25,0x40 直接变 0x00。
     * 灯带产品要的是亮度和饱和度,不是显示器观感,所以默认关掉;用户仍可在 WLED 设置里开回来。 */
    if (gammaCorrectCol) {
      gammaCorrectCol = false;
      NeoGammaWLEDMethod::calcGammaTable(gammaCorrectVal);
      Serial.println("[WLV-FIX] color gamma correction disabled (was 2.8, dimmed all mid-tones)");
      if (cfgLooksValid) serializeConfig();
    }
    if (BusManager::ablMilliampsMax() == 850) {
      BusManager::setMilliampsMax(0);
      Serial.println("[WLV-ABL] external PSU: auto brightness limiter disabled (was 850mA)");
      if (cfgLooksValid) serializeConfig();
    }
    /* 开机渐亮:beginStrip() 已经按 bootPreset 把亮度设成了用户上次的值,灯带会"啪"地全亮。
     * 这里先记下目标亮度并把当前亮度压到 0,loop() 里等灯带真正开始刷新后再渐变回去。 */
    if (bri > 0) { bootFadeTarget = bri; bri = 0; briT = 0; briOld = 0; bootFadePending = true; }
#ifdef WELLVOW_SERIAL_CMDS
    serialCanRX = false;   // 调试固件:串口输入归 usermod 的调试命令,WLED 的 handleSerial 不再吞字节(它会把所有未识别字节丢掉)
#endif
    loadDeviceKey();
    loadRuntimeConfig();
    applyFactoryDefaults();   // 必须在 loadRuntimeConfig 之后:它会把 NVS 里的线序读回来盖掉默认值
    ::server.on("/wellvow/play", HTTP_POST, [this](AsyncWebServerRequest *request) {
      uint8_t slot = request->hasParam("slot") ? request->getParam("slot")->value().toInt() : 1;
      bool ok = startSequence(slot, true);
      request->send(ok ? 200 : 404, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });
    ::server.on("/wellvow/stop", HTTP_POST, [this](AsyncWebServerRequest *request) { stopSequence(); request->send(200, "application/json", "{\"ok\":true}"); });
    // ---- Wi-Fi 局域网 HTTP API:网页控制台 / 桌面 Studio / App 共用(控制器连上路由器后按它的 IP 访问) ----
    // 设备信息:名称 / 路由器 IP / 热点名 / 灯数 / 当前分段 / 正在播放的槽位
    ::server.on("/wellvow/info", HTTP_GET, [this](AsyncWebServerRequest *request) {
      String j = F("{\"name\":\""); j += jsonEscape(serverDescription);
      j += F("\",\"model\":\"" WELLVOW_MODEL "\",\"ver\":\"WLED-0.15.1-WELLVOW\",\"ip\":\"");
      if (WLED_CONNECTED) j += WiFi.localIP().toString();
      j += F("\",\"sta\":\""); j += jsonEscape(multiWiFi.empty() ? String() : String(multiWiFi[0].clientSSID));
      j += F("\",\"connected\":"); j += WLED_CONNECTED ? "true" : "false";
      j += F(",\"rssi\":"); j += (WLED_CONNECTED ? (int)WiFi.RSSI() : 0);
      j += F(",\"wifiState\":"); j += (WLED_CONNECTED ? 1 : (wifiConnectPending ? 0 : (int)lastWifiResult));
      j += F(",\"mdns\":\""); j += jsonEscape(cmDNS); j += F("\",\"link\":"); j += linkRole; j += F(",\"peers\":"); j += onlinePeers();
      j += F(",\"ap\":\""); j += jsonEscape(apSSID); j += F("\",\"apIp\":\"4.3.2.1\",\"leds\":"); j += strip.getLengthTotal();
      j += F(",\"segments\":[");
      for (uint8_t i = 0; i < strip.getSegmentsNum(); i++) { if (i) j += ','; j += strip.getSegment(i).length(); }
      j += F("],\"playing\":"); j += (sequencePlaying ? int(sequenceSlot) : -1);
      j += F(",\"live\":"); j += studioLive ? "true" : "false";   // live = 正在接收电脑/App 的实时帧流
      j += F(",\"maxseg\":"); j += MAX_NUM_SEGMENTS;   // 段数上限(N4 8 / S3 16),App/网页配置页按它钳位
      j += F(",\"decoder\":false"); j += '}';
      request->send(200, "application/json", j);
    });
    // ---- 网页控制台与 App 同一套控制逻辑:App 发什么 DP TLV 帧,网页就发同样的字节,固件走同一个 handleDpSet() ----
    // POST /wellvow/dp?d=<hex>   d = [dpId, type, len, value...]* ,与 BLE CMD 0x01(DP_SET)的负载完全一致
    ::server.on("/wellvow/dp", HTTP_ANY, [this](AsyncWebServerRequest *request) {
      String hexs = request->hasParam("d", true) ? request->getParam("d", true)->value() : (request->hasParam("d") ? request->getParam("d")->value() : String());
      uint8_t buf[240]; size_t n = 0;
      for (size_t i = 0; i + 1 < hexs.length() && n < sizeof(buf); i += 2) { char t[3] = {hexs[i], hexs[i + 1], 0}; buf[n++] = (uint8_t)strtoul(t, nullptr, 16); }
      if (!n) { request->send(400, "application/json", "{\"ok\":false,\"err\":\"d\"}"); return; }
      handleDpSet(buf, n);
      request->send(200, "application/json", "{\"ok\":true}");
    });
    // GET /wellvow/state  当前状态(与 DP_GET 0x01/02/03/05/06/1C/1D/0A 上报同源):网页进页面时用真实颜色/模式驱动预览
    ::server.on("/wellvow/state", HTTP_GET, [this](AsyncWebServerRequest *request) {
      Segment &m = strip.getMainSegment(); char cb[8];
      String j = F("{\"on\":"); j += (bri != 0) ? "true" : "false";
      j += F(",\"bri\":"); j += bri; j += F(",\"speed\":"); j += m.speed; j += F(",\"ix\":"); j += m.intensity; j += F(",\"pal\":"); j += m.palette;
      j += F(",\"mode\":"); j += m.mode; j += F(",\"rev\":"); j += m.reverse ? "true" : "false";
      j += F(",\"cols\":[");
      for (uint8_t k = 0; k < 3; k++) { uint32_t c = m.colors[k]; snprintf(cb, sizeof(cb), "%02X%02X%02X", R(c), G(c), B(c)); if (k) j += ','; j += "\"#"; j += cb; j += '"'; }
      j += F("],\"segments\":[");
      for (uint8_t i = 0; i < strip.getSegmentsNum(); i++) { if (i) j += ','; j += strip.getSegment(i).length(); }
      j += F("],\"order\":"); j += savedColorOrder; j += F(",\"group\":"); j += syncGroups; j += F(",\"music\":"); j += musicActive ? "true" : "false";
      j += F(",\"playing\":"); j += (sequencePlaying ? int(sequenceSlot) : -1); j += '}';
      request->send(200, "application/json", j);
    });
    // POST /wellvow/wifi  ssid=&pass=  网页配网,与 BLE 配网(CMD 0x42/0x03)走同一个 provisionWifi();结果轮询 /wellvow/info
    ::server.on("/wellvow/wifi", HTTP_POST, [this](AsyncWebServerRequest *request) {
      String ssid = request->hasParam("ssid", true) ? request->getParam("ssid", true)->value() : String();
      String pass = request->hasParam("pass", true) ? request->getParam("pass", true)->value() : String();
      if (!ssid.length() || ssid.length() > 32 || pass.length() > 63) { request->send(400, "application/json", "{\"ok\":false,\"err\":\"ssid\"}"); return; }
      provisionWifi(ssid.c_str(), pass.c_str());
      request->send(200, "application/json", "{\"ok\":true}");
    });
    // 各输出口灯数:GET /wellvow/outputs -> {"outputs":[{"pin":2,"len":60},...]};POST /wellvow/outputs?len=60,60 设置(总数 ≤ 512,0 = 不用该路)
    ::server.on("/wellvow/outputs", HTTP_ANY, [this](AsyncWebServerRequest *request) {
      auto prm = [&](const char *k) -> String { return request->hasParam(k, true) ? request->getParam(k, true)->value() : (request->hasParam(k) ? request->getParam(k)->value() : String()); };
      auto split = [](const String &str, int32_t *out, uint8_t max) -> uint8_t { uint8_t n = 0; int p = 0; if (!str.length()) return 0;
        while (n < max && p <= (int)str.length()) { int c = str.indexOf(',', p); if (c < 0) c = str.length(); String t = str.substring(p, c); out[n++] = t.length() ? t.toInt() : -1; p = c + 1; } return n; };
      { String ty = prm("type"); if (ty.length()) { int t = ty.toInt(); if (t >= 0 && t < 256 && outTypeAllowed(t)) queueBusType(t); } }   // STRIP 灯带设置:只换驱动协议(10-22)
      String lens = prm("len");
      if (lens.length()) {
        int32_t L[8] = {0}, S[8] = {-1,-1,-1,-1,-1,-1,-1,-1}, R[8] = {-1,-1,-1,-1,-1,-1,-1,-1};
        uint8_t n = split(lens, L, 8); uint8_t ns = split(prm("start"), S, 8); split(prm("rev"), R, 8);
        uint16_t lv[2] = {0,0}, sv[2] = {0,0}; int8_t rv[2] = {-1,-1}; uint16_t run = 0;
        for (uint8_t i = 0; i < 2 && i < n; i++) { lv[i] = L[i] < 0 ? 0 : (L[i] > 512 ? 512 : L[i]); sv[i] = (i < ns && S[i] >= 0) ? S[i] : run; rv[i] = R[i] < 0 ? -1 : (R[i] != 0); run = sv[i] + lv[i]; }
        queueOutputs(sv, lv, rv, n < 2 ? n : 2);
      }
      request->send(200, "application/json", outputsJson());
    });
    // 多设备联动角色:GET /wellvow/link -> {"role":n};/wellvow/link?role=0|1|2 设置并保存(0 独立 1 主设备 2 从设备)
    /* 音乐律动:POST ?on=1 开关;GET 读实时电平,用来判断"一直安静"到底卡在哪:
     *   vol/raw 一直 0      → I2S 收不到数据(脚位 / 通道 / PDM 模式)
     *   raw 有值但 vol 很小  → 灵敏度问题,调 gain / squelch
     *   两者都有值但灯不动    → 是效果或段的问题,不是麦克风 */
    ::server.on("/wellvow/audio", HTTP_ANY, [this](AsyncWebServerRequest *request) {
      if (request->hasParam("on", true) || request->hasParam("on")) {
        const AsyncWebParameter *p = request->hasParam("on", true) ? request->getParam("on", true) : request->getParam("on");
        setAudioReactive(p->value().toInt() != 0);
      }
      if (request->hasParam("cal")) startAudioCal();
      if (request->hasParam("gate")) { int gv = request->getParam("gate")->value().toInt(); if (gv >= 0 && gv <= 255) { audioSquelch = gv; if (musicActive || audioCalState) applyAudioTuning(audioGain, audioSquelch); } }   // 调试:直接指定噪声门(不存,下次设灵敏度即覆盖)
      if (request->hasParam("sens")) { int sv = request->getParam("sens")->value().toInt(); if (sv >= 1 && sv <= 10) { audioSens = sv; audioSquelch = squelchFor(sv); if (musicActive || audioCalState) applyAudioTuning(audioGain, audioSquelch); saveRuntimeConfig(); } }   // 没开音乐只记值(readFromConfig 会误把 enabled 置 true)
      String j = F("{\"ok\":true,\"cal\":"); j += audioCalState; j += F(",\"sens\":"); j += audioSens; j += F(",\"floor\":"); j += audioFloor; j += F(",\"squelch\":"); j += audioSquelch; j += F(",\"micScale\":"); j += String(micScale, 5); j += F(",\"pre\":"); j += String(wellvowPreGate, 1);
#if defined(USERMOD_AUDIOREACTIVE)
      um_data_t *ud = nullptr;
      if (UsermodManager::getUMData(&ud, USERMOD_ID_AUDIOREACTIVE) && ud) {
        const float vol = *(float*)ud->u_data[0];
        const int16_t raw = *(int16_t*)ud->u_data[1];
        const uint8_t *fft = (uint8_t*)ud->u_data[2];
        const float peak = *(float*)ud->u_data[4];
        const float mag  = *(float*)ud->u_data[5];
        j += F(",\"on\":true,\"vol\":"); j += vol;
        j += F(",\"raw\":"); j += int(raw);
        j += F(",\"peakHz\":"); j += peak;
        j += F(",\"mag\":"); j += mag;
        j += F(",\"fft\":[");
        for (uint8_t i = 0; i < 16; i++) { if (i) j += ','; j += fft[i]; }
        j += ']';
      } else j += F(",\"on\":false");
#endif
      j += '}';
      request->send(200, "application/json", j);
    });
    ::server.on("/wellvow/link", HTTP_ANY, [this](AsyncWebServerRequest *request) {
      if (request->hasParam("role") || request->hasParam("role", true)) {
        int r = (request->hasParam("role", true) ? request->getParam("role", true) : request->getParam("role"))->value().toInt();
        if (r < 0 || r > 2) { request->send(400, "application/json", "{\"ok\":false}"); return; }
        setLinkRole((uint8_t)r);
      }
      request->send(200, "application/json", String("{\"ok\":true,\"role\":") + linkRole + "}");
    });
    // scan=1 先广播一次发现包(应答在几十毫秒内到达,调用方稍后再读一次即可);主设备不加 scan 也每 5s 自动扫
    ::server.on("/wellvow/peers", HTTP_ANY, [this](AsyncWebServerRequest *request) {
      if (request->hasParam("scan") || request->hasParam("scan", true)) sendPeerQuery();
      String j = F("{\"role\":"); j += linkRole; j += F(",\"ip\":\""); if (WLED_CONNECTED) j += WiFi.localIP().toString();
      j += F("\",\"name\":\""); j += jsonEscape(serverDescription); j += F("\",\"group\":"); j += syncGroups; j += F(",\"peers\":[");
      bool first = true;
      for (uint8_t i = 0; i < peerCount; i++) { Peer &pe = peers[i]; uint32_t age = millis() - pe.seen; if (age > PEER_DROP_MS) continue;
        if (!first) j += ','; first = false;
        j += F("{\"ip\":\""); j += IPAddress(pe.ip).toString(); j += F("\",\"name\":\""); j += jsonEscape(String(pe.name));
        j += F("\",\"role\":"); j += pe.role; j += F(",\"leds\":"); j += pe.leds; j += F(",\"playing\":"); j += (pe.playing == 0xFF ? -1 : int(pe.playing));
        j += F(",\"on\":"); j += pe.on ? "true" : "false"; j += F(",\"bri\":"); j += pe.bri; j += F(",\"group\":"); j += pe.group;
        j += F(",\"age\":"); j += age; j += F(",\"online\":"); j += age < PEER_ONLINE_MS ? "true" : "false"; j += '}'; }
      j += F("]}"); request->send(200, "application/json", j);
    });
    // 组指令(与 BLE CMD 0x04 同负载):POST /wellvow/group?d=<hex>  d = [组号位掩码, TLV...];本机在组内就执行,主设备再广播给局域网
    ::server.on("/wellvow/group", HTTP_ANY, [this](AsyncWebServerRequest *request) {
      String hexs = request->hasParam("d", true) ? request->getParam("d", true)->value() : (request->hasParam("d") ? request->getParam("d")->value() : String());
      uint8_t buf[240]; size_t n = 0;
      for (size_t i = 0; i + 1 < hexs.length() && n < sizeof(buf); i += 2) { char t[3] = {hexs[i], hexs[i + 1], 0}; buf[n++] = (uint8_t)strtoul(t, nullptr, 16); }
      if (n < 4) { request->send(400, "application/json", "{\"ok\":false,\"err\":\"d\"}"); return; }
      handleGroupSet(buf, n);
      request->send(200, "application/json", String("{\"ok\":true,\"relayed\":") + (linkRole == 1 ? "true" : "false") + "}");
    });
    // 控制器本地效果库:/0.wseq../15.wseq(Studio / App 上传的帧序列),名字在 /N.name
    ::server.on("/wfx/list", HTTP_GET, [this](AsyncWebServerRequest *request) {
      String j = "["; bool first = true;
      for (uint8_t slot = 0; slot <= 15; slot++) {
        char path[20]; snprintf(path, sizeof(path), "/%u.wseq", slot);
        if (!WLED_FS.exists(path)) continue;
        File f = WLED_FS.open(path, "r"); if (!f) continue;
        uint8_t h[12]; size_t got = f.read(h, sizeof(h)); size_t size = f.size(); f.close();
        if (got != sizeof(h) || h[0] != 'W' || h[1] != 'L' || h[2] != 'V' || h[3] != 'Q') continue;
        uint16_t leds = readU16(h + 5), frames = readU16(h + 8); uint8_t fps = h[7] ? h[7] : 20;
        if (!first) j += ','; first = false;
        j += F("{\"slot\":"); j += slot; j += F(",\"name\":\""); j += jsonEscape(readSlotName(slot));
        j += F("\",\"leds\":"); j += leds; j += F(",\"fps\":"); j += fps; j += F(",\"frames\":"); j += frames;
        j += F(",\"size\":"); j += size; j += F(",\"loop\":"); j += ((h[10] & 2) ? 1 : 0);
        j += F(",\"playing\":"); j += ((sequencePlaying && sequenceSlot == slot) ? 1 : 0); j += '}';
      }
      j += ']'; request->send(200, "application/json", j);
    });
    ::server.on("/wfx/play", HTTP_ANY, [this](AsyncWebServerRequest *request) {
      int slot = slotParam(request);
      if (slot < 0 || slot > 15) { request->send(400, "application/json", "{\"ok\":false,\"err\":\"slot\"}"); return; }
      bool ok = startSequence((uint8_t)slot, true);
      request->send(ok ? 200 : 404, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"err\":\"no such slot\"}");
    });
    ::server.on("/wfx/stop", HTTP_ANY, [this](AsyncWebServerRequest *request) { stopSequence(); request->send(200, "application/json", "{\"ok\":true}"); });
    ::server.on("/wfx/delete", HTTP_ANY, [this](AsyncWebServerRequest *request) {
      int slot = slotParam(request);
      if (slot < 0 || slot > 15) { request->send(400, "application/json", "{\"ok\":false}"); return; }
      if (sequencePlaying && sequenceSlot == slot) stopSequence();
      char path[20]; snprintf(path, sizeof(path), "/%u.wseq", slot); WLED_FS.remove(path);
      snprintf(path, sizeof(path), "/%u.name", slot); WLED_FS.remove(path);
      request->send(200, "application/json", "{\"ok\":true}");
    });
    // 给槽位起名(Studio 上传 /upload 后调用):/wfx/name?slot=N&name=xxx
    ::server.on("/wfx/name", HTTP_ANY, [this](AsyncWebServerRequest *request) {
      int slot = slotParam(request); String name;
      if (request->hasParam("name", true)) name = request->getParam("name", true)->value();
      else if (request->hasParam("name")) name = request->getParam("name")->value();
      if (slot < 0 || slot > 15) { request->send(400, "application/json", "{\"ok\":false}"); return; }
      writeSlotName((uint8_t)slot, name);
      request->send(200, "application/json", "{\"ok\":true}");
    });
    // 控制器上保存的布线图(.wmap JSON,Studio/App 通过 /upload 以文件名 wellvow.wmap 上传);App/Studio 都可从这里取回
    ::server.on("/wellvow/ledmap", HTTP_ANY, [this](AsyncWebServerRequest *request) {
      if (request->hasParam("del") || request->hasParam("del", true)) {   // POST /wellvow/ledmap?del=1 删除布线图
        WLED_FS.remove("/wellvow.wmap"); request->send(200, "application/json", "{\"ok\":true}"); return;
      }
      if (WLED_FS.exists("/wellvow.wmap")) request->send(WLED_FS, "/wellvow.wmap", "application/json");
      else request->send(404, "application/json", "{\"ok\":false,\"err\":\"no ledmap\"}");
    });
    // WELLVOW 网页线序校准:设置并保存线序(0-5)。走 serviceColorOrder 应用+持久化。
    ::server.on("/wellvow/order", HTTP_GET, [this](AsyncWebServerRequest *request) {
      int o = request->hasParam("o") ? request->getParam("o")->value().toInt() : -1;
      if (o < 0 || o > COL_ORDER_GBR) { request->send(400, "application/json", "{\"ok\":false}"); return; }
      colorCalibrationActive = false;
      pendingColorOrder = (uint8_t)o;
      colorOrderPending = true;
      request->send(200, "application/json", String("{\"ok\":true,\"order\":") + o + "}");
    });
    // WELLVOW 网页线序校准:让灯带显示纯红/绿/蓝(STATIC)供对照调线序;c=off 结束并恢复。
    // 校准只改线序:首次测试前记住当前模式/颜色/亮度,结束时原样恢复(不让灯带变黄/丢模式)。
    ::server.on("/wellvow/test", HTTP_GET, [this](AsyncWebServerRequest *request) {
      String c = request->hasParam("c") ? request->getParam("c")->value() : String("off");
      uint32_t col = 0; bool paint = true;
      if (c == "r") col = RGBW32(255,0,0,0);
      else if (c == "g") col = RGBW32(0,255,0,0);
      else if (c == "b") col = RGBW32(0,0,255,0);
      else paint = false;
      if (paint) {
        if (!calibrationSnapshotValid) {   // 第一次测试:保存校准前的模式/颜色/亮度
          Segment &m = strip.getMainSegment();
          calibrationPreviousBri = bri; calibrationPreviousMode = m.mode;
          calibrationPreviousSpeed = m.speed; calibrationPreviousIntensity = m.intensity; calibrationPreviousPalette = m.palette;
          calibrationPreviousColors[0]=m.colors[0]; calibrationPreviousColors[1]=m.colors[1]; calibrationPreviousColors[2]=m.colors[2];
          calibrationSnapshotValid = true;
        }
        bri = 255; briLast = 255;
        for (uint8_t i=0;i<strip.getSegmentsNum();i++){ Segment &s=strip.getSegment(i); s.setOption(SEG_OPTION_ON,true); s.setMode(FX_MODE_STATIC); s.setColor(0,col); }
        strip.trigger();
      } else {
        if (calibrationSnapshotValid) {    // 结束:恢复校准前的模式/颜色/亮度,只保留线序改动
          bri = calibrationPreviousBri;
          for (uint8_t i=0;i<strip.getSegmentsNum();i++){ Segment &s=strip.getSegment(i);
            s.setMode(calibrationPreviousMode); s.speed=calibrationPreviousSpeed; s.intensity=calibrationPreviousIntensity; s.palette=calibrationPreviousPalette;
            s.setColor(0,calibrationPreviousColors[0]); s.setColor(1,calibrationPreviousColors[1]); s.setColor(2,calibrationPreviousColors[2]); }
          calibrationSnapshotValid = false;
        }
        stateUpdated(CALL_MODE_DIRECT_CHANGE);
      }
      request->send(200, "application/json", "{\"ok\":true}");
    });
  }
  void connected() override {
    studioUdp.stop(); studioUdpReady = studioUdp.begin(21325);
  }

  /* 音乐律动:WLED 的 AudioReactive usermod 出厂是关的,不打开的话 FREQMAP 之类的效果收不到音频,
   * 表现就是"设了 I2S 也一直安静"。这里在开/关音乐模式时顺带开关它,并把 ES7201 的脚位写进去。
   * 只在需要时打开,平时不占 FFT 任务和堆(S3 上 BLE 对连续堆很敏感)。 */
  /* ES7201 经 S3 的 PDM 读回来的幅度本来就小(IDF 已知问题),WLED 出厂 gain=60 / squelch=10
   * 会把整段环境声压成 0,表现为"一直安静"。这里按板子实测值下发:squelch 0、gain 由 APP 灵敏度决定。 */
  /* 这些 WLED 效果的画面完全由麦克风数据驱动:没开 AudioReactive 时它们渲染出来是全黑的。
   * 开机恢复上次状态时如果正好停在其中一个,灯带就一片漆黑 —— 用户看不出是坏了还是在等声音。 */
  static bool isAudioFx(uint8_t fx) {
    switch (fx) {
      case FX_MODE_BLURZ: case FX_MODE_DJLIGHT: case FX_MODE_FREQMAP: case FX_MODE_FREQMATRIX:
      case FX_MODE_FREQPIXELS: case FX_MODE_FREQWAVE: case FX_MODE_GRAVCENTER: case FX_MODE_GRAVCENTRIC:
      case FX_MODE_GRAVFREQ: case FX_MODE_GRAVIMETER: case FX_MODE_JUGGLES: case FX_MODE_MATRIPIX:
      case FX_MODE_MIDNOISE: case FX_MODE_NOISEMETER: case FX_MODE_PIXELS: case FX_MODE_PIXELWAVE:
      case FX_MODE_PLASMOID: case FX_MODE_PUDDLEPEAK: case FX_MODE_PUDDLES: case FX_MODE_RIPPLEPEAK:
      case FX_MODE_ROCKTAVES: case FX_MODE_WATERFALL:
        return true;
      default: return false;
    }
  }

  void applyAudioTuning(uint8_t gain, uint8_t squelch, uint8_t agc = 1) {   // agc: 0 关 1 普通(默认:AGC 归一化音量,门决定多响才算有声) 2 灵敏 3 迟钝
#if defined(USERMOD_AUDIOREACTIVE) && defined(WELLVOW_PIN_MIC_DATA)
    Usermod *um = UsermodManager::lookup(USERMOD_ID_AUDIOREACTIVE);
    if (!um) return;
    StaticJsonDocument<512> doc; JsonObject root = doc.to<JsonObject>();
    JsonObject ar = root.createNestedObject("AudioReactive");
    ar["enabled"] = true;
    JsonObject dm = ar.createNestedObject("digitalmic");
    dm["type"] = 5; JsonArray pins = dm.createNestedArray("pin");
    pins.add(WELLVOW_PIN_MIC_DATA); pins.add(WELLVOW_PIN_MIC_CLK); pins.add(-1); pins.add(-1);
    JsonObject cf = ar.createNestedObject("config");
    cf["squelch"] = squelch; cf["gain"] = gain; cf["AGC"] = agc;
    um->readFromConfig(root);
#endif
  }

  void audioLevels(uint8_t &vol, uint8_t &raw) {
    vol = 0; raw = 0;
#if defined(USERMOD_AUDIOREACTIVE)
    um_data_t *ud = nullptr;
    if (UsermodManager::getUMData(&ud, USERMOD_ID_AUDIOREACTIVE) && ud) {
      float v = *(float*)ud->u_data[0];
      vol = uint8_t(constrain((int)v, 0, 255)); raw = uint8_t(constrain((int)wellvowPreGate, 0, 255));   // raw = 过门前的幅度(和门直接可比)
    }
#endif
  }
  /* 底噪校准:增益 0 / 门 0 / AGC 关 → raw = 环境幅度 / 16;等 400ms 稳定后取 1.5 秒内的最大值 × 16 当底噪。 */
  void startAudioCal() {
    if (realtimeMode != REALTIME_MODE_INACTIVE || realtimeOverride != REALTIME_OVERRIDE_NONE) { realtimeOverride = REALTIME_OVERRIDE_NONE; realtimeMode = REALTIME_MODE_INACTIVE; realtimeTimeout = 0; exitRealtime(); }
    setAudioReactive(true); applyAudioTuning(0, 0, 0); applyMicScale();
    audioCalState = 1; audioCalT0 = millis(); audioCalMax = 0; audioCalSum = 0; audioCalN = 0;
    Serial.println("[WLV-MUSIC] noise-floor calibration started (keep quiet 1.5s)");
  }
  void serviceAudioCal() {
    if (!audioCalState) return;
    uint32_t dt = millis() - audioCalT0;
    if (dt < 400) return;
    if (dt < 1900) { audioCalState = 2; uint16_t r = uint16_t(constrain((int)wellvowPreGate, 0, 65535)); if (r > audioCalMax) audioCalMax = r; audioCalSum += r; audioCalN++; return; }
    /* 量到的底噪(当前 scale 下,tmp 单位 = raw × 16)→ 调 scale 让它落到 40,门就总在 8 位内 */
    /* 用平均值而不是峰值:实测安静时峰值是平均的 ~9 倍,按峰值定底噪会把手机小声音乐一起挡掉。平均 × 1.3 留一点余量 */
    float avg = audioCalN ? float(audioCalSum) / float(audioCalN) : 0.0f;
    float measured = avg * 1.3f; if (measured < 2.0f) measured = 2.0f;   // 直接是过门前幅度(当前 scale 下)
    float ns = micScale * 40.0f / measured; if (ns < 0.0005f) ns = 0.0005f; if (ns > 8.0f) ns = 8.0f;
    micScale = ns; applyMicScale();
    audioFloor = 40; audioSquelch = squelchFor(audioSens); audioCalState = 0;
    if (musicActive) applyAudioTuning(audioGain, audioSquelch); else setAudioReactive(false);
    saveRuntimeConfig();
    Serial.printf("[WLV-MUSIC] cal avg=%.2f peak=%u measured=%.0f -> micScale=%.5f floor=%u squelch=%u (sens %u)\n", avg, unsigned(audioCalMax), measured, micScale, unsigned(audioFloor), unsigned(audioSquelch), unsigned(audioSens));
  }

  void setAudioReactive(bool on) {
#if defined(USERMOD_AUDIOREACTIVE) && defined(WELLVOW_PIN_MIC_DATA)
    Usermod *um = UsermodManager::lookup(USERMOD_ID_AUDIOREACTIVE);
    if (!um) { Serial.println("[WLV-AR] audioreactive usermod not built in"); return; }
    StaticJsonDocument<256> doc;
    JsonObject root = doc.to<JsonObject>();
    JsonObject ar = root.createNestedObject("AudioReactive");
    ar["enabled"] = on;
    if (on) AudioReactive::wellvowAudioPower(true);    // 先重建 I2S 驱动 + FFT 任务(enabled 置 true,下面的 readFromJsonState 就不会再 onUpdateBegin)
    um->readFromJsonState(root);
    /* readFromJsonState 只在 enabled 真的翻转时才 onUpdateBegin(挂起 / 恢复 FFT 任务)。可 applyAudioTuning 走的
     * readFromConfig 会把 enabled 直接置 true 而不恢复任务 —— 之后再"开"就被当成没变化,FFT 任务一直挂着,
     * 电平永远是 0(fft 停在 16,0,16 的占位图案)。这里显式按目标状态挂起 / 恢复一次,幂等。 */
    if (on) um->onUpdateBegin(false); else AudioReactive::wellvowAudioPower(false);   // off:任务自删 + 卸 I2S,真正释放 ~16KB(N4 无 PSRAM)
    if (on) { applyAudioTuning(audioGain, audioSquelch); applyMicScale(); }
    Serial.printf("[WLV-AR] audioreactive %s (sd=%d ws=%d PDM gain=%u)\n", on ? "on" : "off",
                  int(WELLVOW_PIN_MIC_DATA), int(WELLVOW_PIN_MIC_CLK), unsigned(audioGain));
#endif
  }

  /* 串口打印实时音频电平,判断"一直安静"卡在哪一步:
   *   vol/raw 恒 0        → I2S 读到的全是零(通道选反 / PDM 配置 / 接线)
   *   raw 有值 vol 很小    → 灵敏度,调 gain / squelch
   *   都有值灯不动         → 效果或分段问题,与麦克风无关 */
  void serviceAudioTrace() {
#if defined(USERMOD_AUDIOREACTIVE)
    if (!audioTrace || millis() < audioTraceNext) return;
    audioTraceNext = millis() + 500;
    um_data_t *ud = nullptr;
    if (!UsermodManager::getUMData(&ud, USERMOD_ID_AUDIOREACTIVE) || !ud) { Serial.println("[WLV-AR] disabled / no data"); return; }
    const float vol = *(float*)ud->u_data[0]; const int16_t raw = *(int16_t*)ud->u_data[1];   // volumeRaw 是 int16_t
    const uint8_t *fft = (uint8_t*)ud->u_data[2];
    // sampleAvg 是 WLED info 页判断"quiet"的那个值(<1 就判静音),单独打出来对照
    extern float sampleAvg;
    Serial.printf("[WLV-AR] vol=%.1f raw=%d avg=%.2f peak=%.0fHz fft=%u %u %u %u %u %u %u %u\n",
                  vol, int(raw), sampleAvg, *(float*)ud->u_data[4],
                  fft[0], fft[1], fft[2], fft[3], fft[4], fft[5], fft[6], fft[7]);
#endif
  }

  /* 绕开 WLED,直接用 IDF 的 I2S PDM 驱动读原始采样,判断"有波形但读到 0"到底卡在哪一层。
   * ch: 0=只取左声道 1=只取右声道;bits: 16 或 32。打印 install/pin 的返回码、读到的字节数和样本极值。 */
  void rawPdmTest(int ch = 0, int bits = 16, bool dsr16 = false) {
#if defined(WELLVOW_PIN_MIC_DATA) && defined(ARDUINO_ARCH_ESP32)
    setAudioReactive(false);
    delay(200);
    i2s_driver_uninstall(I2S_NUM_0);
    delay(50);
    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM);
    cfg.sample_rate = 22050;
    cfg.bits_per_sample = (bits == 32) ? I2S_BITS_PER_SAMPLE_32BIT : I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = ch ? I2S_CHANNEL_FMT_ONLY_RIGHT : I2S_CHANNEL_FMT_ONLY_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = 8;
    cfg.dma_buf_len = 256;
    cfg.use_apll = false;
    esp_err_t e1 = i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr);
    i2s_pin_config_t pins = {};
    pins.mck_io_num = I2S_PIN_NO_CHANGE;
    pins.bck_io_num = I2S_PIN_NO_CHANGE;
    pins.ws_io_num = WELLVOW_PIN_MIC_CLK;      // PDM 模式下 WS 脚 = 输出给麦克风的时钟
    pins.data_out_num = I2S_PIN_NO_CHANGE;
    pins.data_in_num = WELLVOW_PIN_MIC_DATA;   // PDM 数据回来的脚
    esp_err_t e2 = i2s_set_pin(I2S_NUM_0, &pins);
    esp_err_t e3 = i2s_set_clk(I2S_NUM_0, 22050, cfg.bits_per_sample, I2S_CHANNEL_MONO);
    // 过采样率:8S = PDM 时钟 64×采样率,16S = 128×。提高过采样通常能提升信噪比和输出幅度
    esp_err_t e4 = i2s_set_pdm_rx_down_sample(I2S_NUM_0, dsr16 ? I2S_PDM_DSR_16S : I2S_PDM_DSR_8S);
    Serial.printf("[WLV-RAW] ch=%d bits=%d dsr=%s install=%d setpin=%d setclk=%d dsr_rc=%d ws=%d sd=%d\n",
                  ch, bits, dsr16 ? "16S" : "8S", int(e1), int(e2), int(e3), int(e4), int(WELLVOW_PIN_MIC_CLK), int(WELLVOW_PIN_MIC_DATA));
    // 实测真实采样率:S3 的 PDM 有"实际只有设定值一半"的已知缺陷,直接量出来
    { static int32_t wbuf[256]; size_t g=0; uint32_t t0; int total=0;
      i2s_read(I2S_NUM_0, wbuf, sizeof(wbuf), &g, pdMS_TO_TICKS(300));   // 丢掉第一批
      t0 = millis();
      while (millis() - t0 < 1000) { i2s_read(I2S_NUM_0, wbuf, sizeof(wbuf), &g, pdMS_TO_TICKS(300)); total += int(g / ((bits==32)?4:2)); }
      Serial.printf("[WLV-RAW] measured rate = %d samples/s (requested 22050)\n", total);
    }
    static int32_t buf[256];
    for (int pass = 0; pass < 4; pass++) {
      size_t got = 0;
      esp_err_t er = i2s_read(I2S_NUM_0, buf, sizeof(buf), &got, pdMS_TO_TICKS(300));
      long mn = 1L << 30, mx = -(1L << 30), sum = 0; int n = 0;
      if (bits == 32) { n = got / 4; for (int i = 0; i < n; i++) { long v = buf[i]; if (v < mn) mn = v; if (v > mx) mx = v; sum += v; } }
      else { n = got / 2; int16_t *b16 = (int16_t*)buf; for (int i = 0; i < n; i++) { long v = b16[i]; if (v < mn) mn = v; if (v > mx) mx = v; sum += v; } }
      Serial.printf("[WLV-RAW] read=%d got=%u n=%d min=%ld max=%ld avg=%ld\n", int(er), unsigned(got), n, n ? mn : 0, n ? mx : 0, n ? sum / n : 0);
      delay(60);
    }
    i2s_driver_uninstall(I2S_NUM_0);
    Serial.println("[WLV-RAW] done (a1 可重新打开音乐律动)");
#endif
  }

  void loop() override {
    if (factoryRebootAt && millis() > factoryRebootAt) { factoryRebootAt = 0; doReboot = true; }
    serviceAudioTrace(); serviceAudioCal();
    // 校准进行中(BLE 直刷纯色 或 网页测试)时置位,供 auto-save 暂停,避免把纯色测试值
    // 存成开机状态(否则断电重启会显示纯红/绿/蓝而不是用户的真实模式)。
    wellvowCalActive = colorCalibrationActive || calibrationSnapshotValid;
    // WLED registers the web UI after usermod setup. Starting the ESP32 BLE
    // stack there exhausted/fragmented heap while AsyncWebServer added routes.
    // The first loop runs only after initServer() has completed.
    serviceSpeedBoost(); serviceBootFade(); ensureSecondOutput();
    { // 效果号被"别人"改了就记一笔:排查"灯带莫名其妙换了效果/变暗"
      uint8_t m = strip.getMainSegment().mode;
      if (m != lastSeenMode) {
        Serial.printf("[WLV-WATCH] effect changed %u -> %u (effectCurrent=%u)\n",
                      unsigned(lastSeenMode), unsigned(m), unsigned(effectCurrent));
        lastSeenMode = m;
      }
    }
    if (!bleStarted) startBle();
    if (bleStarted && bleConnCount() < BLE_MAX_CONN && millis() - lastAdvertisingCheck >= 2000) {   // 还有空位就要一直在广播
      lastAdvertisingCheck = millis();
      NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
      if (!adv->isAdvertising()) {
        adv->start();
        Serial.println("[WLV-BLE] advertising watchdog restarted");
      }
    }
    cp(1); serviceBleQueue(); cp(2); servicePreview(); cp(3); serviceWifi(); cp(4); serviceSequence(); cp(5); serviceStudioStream(); cp(6); serviceHub(); cp(8); serviceSerialDebug(); cp(9); serviceRadioRemote(); serviceSchedule(); serviceBusResize(); serviceSegmentConfig(); serviceColorOrder(); serviceColorCalibration();
    // 状态日志放慢到 5 秒一次:Serial.printf 在 TX FIFO 满时会阻塞主循环几毫秒,快闪(帧周期 ~50ms)时肉眼可见卡一下
    { static uint32_t _st=0; if (millis()-_st>5000) { _st=millis();
      Segment &m = strip.getMainSegment();
      Serial.printf("[WLV-STATE] rtMode=%u seq=%u calib=%u pl=%d preset=%u mode=%u col0=%06X bri=%u on=%u heap=%u\n",
        unsigned(realtimeMode), unsigned(sequencePlaying), unsigned(colorCalibrationActive),
        int(currentPlaylist), unsigned(currentPreset), unsigned(m.mode),
        unsigned(m.colors[0] & 0xFFFFFF), unsigned(bri), unsigned(m.on), unsigned(ESP.getFreeHeap())); } }
  }
  void handleOverlayDraw() override {
    // 校准与所有正常效果(Solid/预设/DIY/音乐/实时帧)共用同一条最终线序变换:
    // 只在这里统一设置输出线序,绝不再对 bus 做额外的“直接刷像素”上色——
    // 否则校准走的是 bus 直刷、效果走的是 FX 渲染,两条不同路径会让线序差一格,
    // 表现为“校准对了、Solid 反而不对”。校准时纯色由 serviceColorCalibration 把
    // 分段设为 STATIC+纯色、经正常 FX 管线渲染,与 Solid 完全一致 => 校准对则效果对。
    const uint8_t outputOrder = colorCalibrationActive ? pendingCalibrationOrder : savedColorOrder;
    wellvowOutputColorOrder = outputOrder;
    for (uint8_t i = 0; i < BusManager::getNumBusses(); i++) {
      Bus *bus = BusManager::getBus(i);
      if (bus) bus->setColorOrder(outputOrder);
    }
  }
  uint16_t getId() override { return 0x57B1; }
};
#endif
