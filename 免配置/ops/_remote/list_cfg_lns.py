# -*- coding: utf-8 -*-
"""列出 test3.cfg 中 C_B5043XLD0 块内的 LN(...) 行。"""
from __future__ import print_function

import re

PATH = "/tmp/test3.cfg"
text = open(PATH, "r", encoding="utf-8", errors="replace").read()
m = re.search(r"(?m)^LD\(C_B5043XLD0\)\{", text)
print("start", m.start() if m else None)
if m:
    start = m.start()
    nxt = re.search(r"(?m)^LD\(", text[start + 1:])
    end = start + 1 + nxt.start() if nxt else len(text)
    block = text[start:end]
    print("block len", len(block))
    lns = [ln for ln in block.splitlines() if ln.startswith("LN(")]
    print("LN count", len(lns), lns[:20])
    print("has DevAlm", "DevAlm" in block)
