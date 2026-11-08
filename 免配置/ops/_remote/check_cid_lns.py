# -*- coding: utf-8 -*-
"""检查 CID 中 C_B5043XLD0 LDevice 是否保留 <LN> 块。"""
from __future__ import print_function

import re

PATH = "/home/admin/router/config/supervision.CID"
text = open(PATH, "r", encoding="utf-8", errors="replace").read()
m = re.search(r'<LDevice[^>]*inst="C_B5043XLD0"[^>]*>', text)
print("LDevice at", m.start() if m else None)
if m:
    start = m.start()
    end = text.find("</LDevice>", start)
    block = text[start:end]
    print("block len", len(block), "LN0", block.count("<LN0"), "LN open", block.count("<LN "))
    for mm in re.finditer(r"<LN\s[^>]*>", block):
        print(mm.start(), mm.group()[:160])
print("global DevAlm LN:", len(re.findall(r'<LN[^>]*prefix="DevAlm"', text)))
print("global DevAlm any:", text.count('prefix="DevAlm"'))
