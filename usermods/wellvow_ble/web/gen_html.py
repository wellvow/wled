#!/usr/bin/env python3
"""把 web/wellvow_landing.html + web/wellvow_ctrl.html 打包成 wled00/html_wellvow.h。
2026-10-16 起存 gzip 字节数组(PROGMEM)+ 长度 + 内容哈希 ETag:控制页原文 200KB+,gzip 后约 1/4,
N4(4MB flash,App 分区 2MB)放得下;wled_server.cpp 用 handleStaticContent(..., gzip=true, etag) 发送,
ETag 随内容变,改网页重刷后浏览器不会拿旧缓存。改网页只改 web/*.html,然后运行本脚本再编译固件。"""
import os, gzip, zlib
here = os.path.dirname(os.path.abspath(__file__))
root = os.path.abspath(os.path.join(here, '..', '..', '..'))
out = os.path.join(root, 'wled00', 'html_wellvow.h')
def check_js(name, html):
    """页面里的 <script> 先过一遍 node --check:一个语法错就是整页黑屏(2026-10-22 日落配色表被 // 注释吞掉 + 多一个括号,网页全黑)。"""
    import subprocess, tempfile, re
    for m in re.finditer(r'<script>([\s\S]*?)</script>', html):
        with tempfile.NamedTemporaryFile('w', suffix='.js', delete=False, encoding='utf-8') as f:
            f.write(m.group(1)); tmp = f.name
        r = subprocess.run(['node', '--check', tmp], capture_output=True, text=True)
        os.unlink(tmp)
        if r.returncode != 0:
            raise SystemExit('JS 语法错误,拒绝生成 (%s):\n%s' % (name, r.stderr.strip()[:600]))
def load(name):
    s = open(os.path.join(here, name), encoding='utf-8').read().strip()
    check_js(name, s)
    s = '\n'.join(l.strip() for l in s.split('\n') if l.strip())   # 去掉行首空白(不动 <pre>/字符串内容)
    return s.encode('utf-8')
def arr(name, raw):
    gz = gzip.compress(raw, 9, mtime=0)
    etag = zlib.crc32(gz) & 0xFFFF
    nums = [str(b) for b in gz]
    lines = [','.join(nums[i:i + 32]) + ',' for i in range(0, len(nums), 32)]   # 按元素切行,不能按字符切(会把数字切成 "09" 之类的非法八进制)
    return ('// %s: %d B raw -> %d B gzip\nconst uint8_t %s[] PROGMEM = {\n%s\n};\nconst uint32_t %s_L = %d;\nconst uint16_t %s_ETAG = 0x%04X;\n'
            % (name, len(raw), len(gz), name, '\n'.join(lines), name, len(gz), name, etag)), len(gz)
landing, ctrl = load('wellvow_landing.html'), load('wellvow_ctrl.html')
a1, n1 = arr('PAGE_wellvow_landing', landing)
a2, n2 = arr('PAGE_wellvow_ctrl', ctrl)
hdr = '''#pragma once
// WELLVOW 落地页(/)+ 网页控制台(/wellvow),gzip 压缩后的字节数组。
// 由 usermods/wellvow_ble/web/gen_html.py 从同目录的 wellvow_landing.html / wellvow_ctrl.html 生成,请勿手改本文件。
// 控制台与 App 的页面同构:所有控制编成与 App 相同的 DP TLV 帧,经 POST /wellvow/dp 交给 usermod 的 handleDpSet()。
// .gitignore 已对本文件放行(/wled00/html_*.h 规则的例外)。完整 WLED 界面在 /wled。
#include <stdint.h>
#include <pgmspace.h>

''' + a1 + '\n' + a2
open(out, 'w', encoding='utf-8').write(hdr)
print('wrote', out, len(landing), '->', n1, 'B landing,', len(ctrl), '->', n2, 'B ctrl')
