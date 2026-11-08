# -*- coding: utf-8 -*-
"""定位 C_B5043XLD0 块内两个 LPHD1 的位置与上下文。"""
from __future__ import print_function

import re

PATH = "/home/admin/router/config/supervision.cfg"
text = open(PATH, "r", encoding="utf-8", errors="replace").read()
name = "C_B5043XLD0"
m = re.search(r"(?m)^LD\(" + re.escape(name) + r"\)\{", text)
print("LD at", m.start())
start = m.start()
nxt = re.search(r"(?m)^LD\(", text[start + 1:])
end = start + 1 + nxt.start() if nxt else len(text)
block = text[start:end]
print("block len", len(block))
for mm in re.finditer(r"LN\(LPHD1\)", block):
    print("LPHD at", mm.start(), "context:", repr(block[max(0, mm.start() - 80):mm.start() + 40]))
print("first 300:", repr(block[:300]))
