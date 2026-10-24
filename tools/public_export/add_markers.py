#!/usr/bin/env python3
"""一次性:在私有源码里给 HUB(采集 / WVF2 分发 / USB 复用)相关的多行段落加上 //<<HUB ... //HUB>> 标记。
标记只是注释,不影响原厂编译;export_public.py 导出公开 DIY 工程时把标记之间的内容整段删掉。
幂等:已经有标记的段落不会重复加。"""
import sys, re, pathlib
ROOT = pathlib.Path(__file__).resolve().parents[2]
BLOCKS = {
 'usermods/wellvow_ble/wellvow_ble.h': [
  # (起始行包含, 结束行包含或 exact:整行去空白后相等, 结束行之后再多包含几行)
  ('// R3 载板 Micro-USB 用途。usbMode', 'uint8_t usbMode = WELLVOW_USB_MODE_DEFAULT;', 0),
  ('// HUB 解析设备:采集外部灯带数据', 'uint16_t capGain = 100; uint8_t capPeak = 0;', 0),
  ('/* 自定义协议:测试模式量出', 'customProto.t0h = cap.t0h;', 1),
  ('uint8_t capHead[18] = {0};', '/* WVF2 会话/帧序号', 1),
  ('wlv::FrameRx wvf2Rx;', 'else if (!on && syncSuppressed)', 1),
  ('nvs_set_u8(h, "capen", capEnabled)', 'nvs_set_blob(h, "capsplit3"', 0),
  ('uint8_t u = 0; int8_t v8 = 0;', 'capFrameId = 0;   // 分发帧序号', 0),
  ('case 0x22: if (len) { capEnabled = v[0] != 0;', 'capPendingApply = true; capPendingIdent = capEnabled; capFails = 0; capBootArm = false; saveRuntimeConfig(); } break;', 0),
  ('// ---------------- HUB 解析设备:采集 → 本机显示 + 切片转发', 'j += F("]}"); return j;', 1),
  ('else if (line == "c1")', 'else if (line == "ct")', 0),
  ('else if (line == "cs") {', 'Serial.println("[WLV-CAP] pin scan done")', 1),
  ('else if (line == "ce") {', 'if (cap.running) { cap.end(); cap.begin(); }   // attachInterrupt', 1),
  ('case 0x22: { const WellvowLedProto *pr = cap.proto();', 'out[n++]=cap.ident.state; out[n++]=cap.identAliases; }', 0),
  ('// ---- 作为对等设备接收 HUB 的 WVF2 分发', '!(capEnabled && cap.running)) { realtimeTimeout = 0; exitRealtime(', 2),
  ('if (wlv::isWvf2(packet, n)) {', 'if (wvf2Rx.feed(h, pix, millis()) == wlv::FrameRx::SHOW) showWvf2();', 2),
  ('pinMode(WELLVOW_PIN_USB_ID, INPUT_PULLUP);', 'exact:serviceUsbMux(true);', 1, -1),
  ('// Micro-USB 用途:GET 读当前', 'String j = F("{\\"mode\\":"); j += usbMode;', 2),
  ('// 解码后的当前帧(前 300 颗 RGB,hex)', 'request->send(200, "application/json", captureJson());', 1),
  ('/* R3 载板的 Micro-USB 复用:ID 脚高', 'usbMode = m; saveRuntimeConfig(); serviceUsbMux(true);', 1),
 ],
 'usermods/wellvow_ble/web/wellvow_ctrl.html': [
  ('/* ---- 作为解析设备:采集外部灯带信号', 'capBox.append(splitBox);drawSplit();};', 0),
 ],
}
START, END = '//<<HUB', '//HUB>>'
def match(line, pat):
    return line.strip() == pat[6:] if pat.startswith('exact:') else pat in line
for rel, blocks in BLOCKS.items():
    p = ROOT / rel; lines = p.read_text().split('\n'); added = 0
    for blk in blocks:
        s_pat, e_pat, extra = blk[0], blk[1], blk[2]; before = blk[3] if len(blk) > 3 else 0
        si = [i for i, l in enumerate(lines) if s_pat in l]
        if not si: sys.exit(f'{rel}: start anchor not found: {s_pat}')
        s = si[0] + before
        if s > 0 and lines[s-1].strip() == START: continue   # 已标记
        e = next((i for i in range(s, len(lines)) if match(lines[i], e_pat)), None)
        if e is None: sys.exit(f'{rel}: end anchor not found after line {s+1}: {e_pat}')
        e += extra
        indent = re.match(r'\s*', lines[s]).group(0)
        lines.insert(e + 1, indent + END); lines.insert(s, indent + START); added += 1
    p.write_text('\n'.join(lines)); print(f'{rel}: {added} block(s) marked')
