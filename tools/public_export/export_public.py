#!/usr/bin/env python3
"""把私有固件工程导出成公开的 DIY 工程(选项 B:剥掉 HUB 采集 / WVF2 分发 / USB 复用代码和品牌密钥)。
用法:python3 tools/public_export/export_public.py <输出目录>
    1. 复制工程(不含 .pio / build_output / 备份 / node_modules / HUB 专用头文件 / 品牌密钥 / 主机单测)
    2. wellvow_ble.h、wellvow_ctrl.html:删掉 //<<HUB ... //HUB>> 标记段;#ifdef WELLVOW_HUB 只留 #else 分支;
       再按符号表删掉残余单行、做几处替换,并清掉提到 HUB 的注释
    3. wellvow_board.h / platformio_override.ini:删 HUB 引脚、HUB 环境、Mac 专用串口
    4. 自检:公开树里不得再出现任何 HUB 符号;然后由调用方 gen_html.py + pio run 验证"""
import sys, re, shutil, pathlib, subprocess
SRC = pathlib.Path(__file__).resolve().parents[2]
if len(sys.argv) < 2: sys.exit('usage: export_public.py <out_dir>')
OUT = pathlib.Path(sys.argv[1]).resolve()
if OUT.exists(): shutil.rmtree(OUT)
EXCLUDE_DIRS = {'.pio', 'build_output', 'node_modules', 'backup', '.git', '__pycache__', 'test'}
EXCLUDE_FILES = {'wellvow_capture.h', 'wellvow_frame_rx.h', 'wellvow_proto.h', 'wellvow_brand_key.h',
                 'VERIFICATION_2026-09-08.md', 'platformio_override.ini.bak_2026-10-15'}
def skip(p: pathlib.Path):
    if p.name in EXCLUDE_FILES or '.bak' in p.name or p.name.endswith('.patched'): return True
    return any(part in EXCLUDE_DIRS for part in p.relative_to(SRC).parts)
for p in SRC.rglob('*'):
    if p.is_dir() or skip(p): continue
    d = OUT / p.relative_to(SRC); d.parent.mkdir(parents=True, exist_ok=True); shutil.copy2(p, d)
# 注意:tools/public_export 自身也被复制了 —— 公开工程里留着无妨(里面没有 HUB 代码,只是符号名)
# ---------------------------------------------------------------- 文本处理
TOKEN = re.compile(r'\bcap\.|\bcap[A-Z]\w*|\busb(?:Mode|Role|NextPoll)\b|\bserviceUsbMux\b|\bsetUsbMode\b|\bwvf2\w*|\bWvf2\w*|\bwlv::|WELLVOW_PROTO\w*'
                   r'|WellvowLedProto|WellvowCapture|customProto|\bsplitN\b|\bsplitManual\b|MAX_SPLIT|rebuildCustomProto|captureJson|serviceCapture'
                   r'|applyCaptureConfig|composeTarget|sendCaptureTarget|autoSplit|suppressWledSync|syncSuppressed|WELLVOW_PIN_CAP_\w+|WELLVOW_PIN_USB_\w+'
                   r'|WELLVOW_USB_MODE_DEFAULT|wellvow_capture\.h|wellvow_frame_rx\.h|CAP_CONFIRM_\w+|\bcapBox\b|\bstripBox\b|\btestUi\b|\bcapLoad\b|\bCAP\b|W\.hubTest')
COMMENT_WORDS = re.compile(r'HUB|WVF2|wvf2|采集|解析设备|解析器|切片|分发|Micro-USB|USB 复用|capture|Capture|decoder')
SUBS = [
  ('(linkRole == 1 || (capEnabled && capRoute != 0))', 'linkRole == 1'),
  (' && !wvf2Live', ''),
  (' j += F(",\\"capture\\":"); j += cap.status();', ''),
  ('cp(7); serviceCapture(); ', ''),
  ('#include "wellvow_brand_key.h"   // 原厂固件:品牌密钥(不进公开仓库)',
   '#error "This public DIY tree has no brand key: build with -D WELLVOW_DIY_CODE=\\"WV1-...\\" (see docs/DIY.md)"'),
  # 网页:HUB 页去掉解析设备分支后只剩 STRIP 的分路 / 灯带型号
  ("  else {drawStripType();root.append(routeBox,stripTypeBox);}", "  drawStripType();root.append(routeBox,stripTypeBox);"),
  ("clearInterval(t);clearInterval(tc);", "clearInterval(t);"),
]
def strip_blocks(lines):
    out, inblk = [], False
    for l in lines:
        s = l.strip()
        if s == '//<<HUB': inblk = True; continue
        if s == '//HUB>>': inblk = False; continue
        if not inblk: out.append(l)
    assert not inblk, 'unterminated //<<HUB'
    return out
def resolve_hub_ifdef(lines):
    """#ifdef WELLVOW_HUB / #if defined(WELLVOW_HUB) ... #else ... #endif → 只留 #else 分支(公开树永远不是 HUB)。只处理不嵌套的情形。"""
    out, i = [], 0
    while i < len(lines):
        s = lines[i].strip()
        if s in ('#ifdef WELLVOW_HUB', '#if defined(WELLVOW_HUB)'):
            depth, j, else_at = 0, i, None
            while True:
                t = lines[j].strip()
                if j > i and t.startswith('#if'): depth += 1
                elif t == '#else' and depth == 0: else_at = j
                elif t == '#endif':
                    if depth == 0: break
                    depth -= 1
                j += 1
            if else_at is not None: out.extend(lines[else_at + 1:j])
            i = j + 1; continue
        out.append(lines[i]); i += 1
    return out
def scrub_line(l):
    """返回 None = 删掉整行;否则返回处理后的行。"""
    code, sep, comment = l.partition('//')
    if TOKEN.search(code): return None
    s = l.strip()
    if (s.startswith('*') or s.startswith('/*') or s.startswith('//')) and COMMENT_WORDS.search(l):
        if '*/' in l and not s.startswith('/*'): return re.match(r'\s*', l).group(0) + '*/'
        if s.startswith('/*') and '*/' not in l: return re.match(r'\s*', l).group(0) + '/*'
        return None
    if sep and COMMENT_WORDS.search(comment): return code.rstrip()
    return l
def process(rel, do_ifdef=True, do_tokens=True):
    p = OUT / rel; text = p.read_text()
    for a, b in SUBS: text = text.replace(a, b)
    lines = strip_blocks(text.split('\n'))
    if do_ifdef: lines = resolve_hub_ifdef(lines)
    if do_tokens: lines = [x for x in (scrub_line(l) for l in lines) if x is not None]
    p.write_text('\n'.join(lines)); print(f'processed {rel}: {len(text.splitlines())} -> {len(lines)} lines')
process('usermods/wellvow_ble/wellvow_ble.h')
process('usermods/wellvow_ble/web/wellvow_ctrl.html')
process('usermods/wellvow_ble/wellvow_board.h', do_ifdef=False)
# platformio_override.ini:去掉名字带 hub 的环境段、提到 HUB 的注释行、Mac 专用串口
ini = (OUT / 'platformio_override.ini').read_text().split('\n'); out = []; skipping = False
for l in ini:
    if l.startswith('[env:'):
        skipping = 'hub' in l.lower()
    elif skipping and (l.startswith('[') or l.startswith(';; ──')): skipping = False
    if skipping: continue
    if l.lstrip().startswith(';') and COMMENT_WORDS.search(l): continue
    if re.match(r'\s*(upload_port|monitor_port)\s*=', l): continue
    l = l.replace('-D WELLVOW_USB_MODE_DEFAULT=1 ', '')
    out.append(l)
(OUT / 'platformio_override.ini').write_text('\n'.join(out))
# 自检:公开树里不能残留 HUB 符号 / 密钥
BAD = re.compile(TOKEN.pattern + r'|WELLVOW_BRAND_KEY\[|0x65137D33|WELLVOW_HUB|wvf2_|WVF2')
hits = []
for p in OUT.rglob('*'):
    if p.is_dir() or p.suffix in ('.png', '.jpg', '.gif', '.ico', '.bin', '.woff', '.woff2') : continue
    if 'tools/public_export' in str(p): continue
    try: t = p.read_text(errors='ignore')
    except Exception: continue
    for n, l in enumerate(t.split('\n'), 1):
        if BAD.search(l): hits.append(f'{p.relative_to(OUT)}:{n}: {l.strip()[:120]}')
if hits:
    print('\n'.join(hits[:60])); sys.exit(f'FAILED: {len(hits)} leftover HUB reference(s)')
print('OK: no HUB symbols left in', OUT)
