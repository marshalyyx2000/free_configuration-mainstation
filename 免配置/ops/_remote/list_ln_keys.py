# -*- coding: utf-8 -*-
"""列出 CID 中 C_B5043XLD0 LDevice 的 LN key，与 dsAlarm 引用对比。"""
from __future__ import print_function

import re

PATH = "/home/admin/router/config/supervision.CID"
text = open(PATH, "r", encoding="utf-8", errors="replace").read()
m = re.search(r'<LDevice[^>]*inst="C_B5043XLD0"[^>]*>', text)
start = m.start()
end = text.find("</LDevice>", start)
block = text[start:end]
print("block len", len(block), "LN0", block.count("<LN0"), "LN open", block.count("<LN "))

attr = re.compile(r'([A-Za-z_][A-Za-z0-9_]*)="([^"]*)"')
lns = []
for mm in re.finditer(r"<LN\s[^>]*>", block):
    tag = mm.group()
    d = dict(attr.findall(tag))
    key = d.get("prefix", "") + d.get("lnClass", "") + d.get("inst", "")
    lns.append(key)
    print(mm.start(), key)
print("has DevAlmGGIO1:", "DevAlmGGIO1" in lns)

# dsAlarm FCDA keys
for mm in re.finditer(r'<DataSet[^>]*name="dsAlarm"[^>]*>([\s\S]*?)</DataSet>', block):
    for fm in re.finditer(r"<FCDA\s[^>]*>", mm.group(1)):
        d = dict(attr.findall(fm.group()))
        print("FCDA key", d.get("prefix", "") + d.get("lnClass", "") + d.get("lnInst", ""), d.get("doName"))
