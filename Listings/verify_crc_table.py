# -*- coding: utf-8 -*-
import re

src = open(r'D:\self_work\stm32\bootloader\bootloader_pro\FwUpgrade\fw_boot.c', encoding='utf-8', errors='replace').read()
m = re.search(r'crc32_table\[256\]\s*=\s*\{(?P<body>.*?)\};', src, re.S)
if not m:
    print('TABLE NOT FOUND'); raise SystemExit(1)
hexs = re.findall(r'0x[0-9A-Fa-f]{8}', m.group('body'))
file_tbl = [int(h, 16) for h in hexs]
print('extracted entries =', len(file_tbl))
if len(file_tbl) != 256:
    print('TABLE SIZE ERROR'); raise SystemExit(1)

# 权威标准表：逐位反射算法（多项式 0xEDB88320）
std_tbl = []
for i in range(256):
    crc = i
    for _ in range(8):
        if crc & 1:
            crc = (crc >> 1) ^ 0xEDB88320
        else:
            crc >>= 1
    std_tbl.append(crc & 0xFFFFFFFF)

bad = []
for k in range(256):
    if file_tbl[k] != std_tbl[k]:
        bad.append((k, file_tbl[k], std_tbl[k]))
print('bad entries =', len(bad))
for k, f, s in bad:
    print('idx %3d: file=0x%08X std=0x%08X' % (k, f, s))

# 输出修正后的完整表（C 格式，每行8项）
print()
print('--- corrected table (C format) ---')
for r in range(32):
    row = []
    for c in range(8):
        row.append('0x%08X' % std_tbl[r * 8 + c])
    print('    ' + ', '.join(row) + ',')
