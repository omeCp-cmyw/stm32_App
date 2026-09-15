# -*- coding: utf-8 -*-
# 验证 fw_boot_fixed.c 的表完全正确
import re

src = open(r'D:\self_work\stm32\STM32_APP\self_function\stm32_App\Listings\fw_boot_fixed.c', encoding='utf-8', errors='replace').read()
m = re.search(r'crc32_table\[256\]\s*=\s*\{(?P<body>.*?)\};', src, re.S)
hexs = re.findall(r'0x[0-9A-Fa-f]{8}', m.group('body'))
file_tbl = [int(h, 16) for h in hexs]
print('entries =', len(file_tbl))

std_tbl = []
for i in range(256):
    crc = i
    for _ in range(8):
        if crc & 1:
            crc = (crc >> 1) ^ 0xEDB88320
        else:
            crc >>= 1
    std_tbl.append(crc & 0xFFFFFFFF)

bad = sum(1 for k in range(256) if file_tbl[k] != std_tbl[k])
print('bad entries =', bad)
print('VERIFY PASS' if bad == 0 and len(file_tbl) == 256 else 'VERIFY FAIL')
