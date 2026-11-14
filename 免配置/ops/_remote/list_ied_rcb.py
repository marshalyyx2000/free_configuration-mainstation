#!/usr/bin/env python3
import re
import sys

path = sys.argv[1] if len(sys.argv) > 1 else "/home/ipofas/stdown/maintain/SMCD/station.SMCD"
ied = sys.argv[2] if len(sys.argv) > 2 else "C_B5013X"
data = open(path, encoding="utf-8", errors="replace").read()
m = re.search(rf'<IED[^>]*name="{re.escape(ied)}".*?</IED>', data, re.S)
if not m:
    print("IED not found:", ied)
    raise SystemExit(1)
block = m.group(0)
for name, ds in re.findall(r'<ReportControl[^>]*name="([^"]+)"[^>]*datSet="([^"]+)"', block):
    print(f"{name}\t{ds}")
