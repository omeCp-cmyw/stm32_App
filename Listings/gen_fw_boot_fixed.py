# -*- coding: utf-8 -*-
# 生成修正后的 fw_boot.c：仅替换 crc32_table 为权威标准表
import re

src_path = r'D:\self_work\stm32\bootloader\bootloader_pro\FwUpgrade\fw_boot.c'
dst_path = r'D:\self_work\stm32\STM32_APP\self_function\stm32_App\Listings\fw_boot_fixed.c'

src = open(src_path, encoding='utf-8', errors='replace').read()

# 权威标准表（逐位反射算法生成）
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

# 替换原表定义块
pat = re.compile(r'static const uint32_t crc32_table\[256\]\s*=\s*\{.*?\};', re.S)
new_src, n = pat.subn(new_table, src)
print('replaced table blocks =', n)
assert n == 1

open(dst_path, 'w', encoding='utf-8', newline='\n').write(new_src)
print('written:', dst_path)
