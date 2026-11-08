# -*- coding: utf-8 -*-
"""统计部署 SMCD 的 ReportControl/RptEnabled 数量（大文件分块）。"""
import re
from collections import Counter

PATH = "/home/ipofas/stdown/maintain/SMCD/station.SMCD"
needles = [b"<ReportControl", b'RptEnabled max="9"', b'RptEnabled max="1"',
           b'RptEnabled max="8"', b'RptEnabled max="2"']
counts = Counter()
size = 0
with open(PATH, "rb") as fh:
    while True:
        chunk = fh.read(4 * 1024 * 1024)
        if not chunk:
            break
        size += len(chunk)
        for n in needles:
            counts[n] += chunk.count(n)
print("size", size)
for n in needles:
    print(n.decode(), counts[n])
