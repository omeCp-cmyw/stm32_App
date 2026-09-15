# -*- coding: utf-8 -*-
# 闭环验证：标准表与Bootloader当前(错误)表分别计算当前bin的CRC32，对照日志值
import re

# 1. 标准表
std_tbl = []
for i in range(256):
    crc = i
    for _ in range(8):
        if crc & 1:
            crc = (crc >> 1) ^ 0xEDB88320
        else:
            crc >>= 1
    std_tbl.append(crc & 0xFFFFFFFF)

# 2. Bootloader当前表
src = open(r'D:\self_work\stm32\bootloader\bootloader_pro\FwUpgrade\fw_boot.c', encoding='utf-8', errors='replace').read()
m = re.search(r'crc32_table\[256\]\s*=\s*\{(?P<body>.*?)\};', src, re.S)
hexs = re.findall(r'0x[0-9A-Fa-f]{8}', m.group('body'))
cur_tbl = [int(h, 16) for h in hexs]

def crc_with_table(tbl, data):
    crc = 0xFFFFFFFF
    for b in data:
        crc = tbl[(crc ^ b) & 0xFF] ^ (crc >> 8)
    return (crc ^ 0xFFFFFFFF) & 0xFFFFFFFF

# 3. 当前bin
data = open(r'D:\self_work\stm32\STM32_APP\self_function\stm32_App\Output\IoT_Collector_F407.bin', 'rb').read()
print('bin size =', len(data))
print('std table crc = 0x%08X (expect 0xEE44E06C)' % crc_with_table(std_tbl, data))
print('cur table crc = 0x%08X (expect 0x26163CE2)' % crc_with_table(cur_tbl, data))
