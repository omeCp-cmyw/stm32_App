# -*- coding: utf-8 -*-
# 修补 bootloader_pro\FwUpgrade\fw_boot.c：仅替换 crc32_table 定义块为标准表，其余不动
import re

src_path = r'D:\self_work\stm32\bootloader\bootloader_pro\FwUpgrade\fw_boot.c'

src = open(src_path, encoding='utf-8', errors='replace').read()

# 权威标准表（逐位反射算法，多项式0xEDB88320）
std_tbl = []
for i in range(256):
    crc = i
    for _ in range(8):
        if crc & 1:
            crc = (crc >> 1) ^ 0xEDB88320
        else:
            crc >>= 1
    std_tbl.append(crc & 0xFFFFFFFF)

rows = []
for r in range(32):
    row = []
    for c in range(8):
        row.append('0x%08X' % std_tbl[r * 8 + c])
    rows.append('    ' + ', '.join(row) + ',')

new_table = 'static const uint32_t crc32_table[256] = {\n' + '\n'.join(rows) + '\n};'

pat = re.compile(r'static const uint32_t crc32_table\[256\]\s*=\s*\{.*?\};', re.S)
new_src, n = pat.subn(new_table, src)
print('replaced table blocks =', n)
assert n == 1, 'table block not found or multiple matched'

open(src_path, 'w', encoding='utf-8', newline='\n').write(new_src)
print('patched:', src_path)

# 回读验证
chk = open(src_path, encoding='utf-8', errors='replace').read()
m = re.search(r'crc32_table\[256\]\s*=\s*\{(?P<body>.*?)\};', chk, re.S)
hexs = re.findall(r'0x[0-9A-Fa-f]{8}', m.group('body'))
tbl = [int(h, 16) for h in hexs]
bad = sum(1 for k in range(256) if tbl[k] != std_tbl[k])
print('re-verified entries =', len(tbl), 'bad =', bad)
print('PATCH OK' if bad == 0 and len(tbl) == 256 else 'PATCH FAILED')
