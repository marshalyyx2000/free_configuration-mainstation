# -*- coding: utf-8 -*-
"""检查 supervision.cfg 中每个 LD 的 LN(LPHD1) 数量与括号平衡。"""
from __future__ import print_function

PATH = "/home/admin/router/config/supervision.cfg"
with open(PATH, "r", encoding="utf-8", errors="replace") as fh:
    text = fh.read()

# 按行首 LD( 切块
import re
starts = [m.start() for m in re.finditer(r"(?m)^LD\(", text)]
print("LD count", len(starts), "size", len(text))
bad = []
for i, s in enumerate(starts):
    e = starts[i + 1] if i + 1 < len(starts) else len(text)
    block = text[s:e]
    name = block.split("{", 1)[0]
    n = block.count("LN(LPHD1)")
    if n > 1:
        bad.append((name, n))
print("LD with duplicate LPHD1:", len(bad))
for name, n in bad[:10]:
    print("  ", name, n)

# 括号平衡（全文件）
print("braces", text.count("{"), text.count("}"))
print("tail:")
print(text[-300:])
