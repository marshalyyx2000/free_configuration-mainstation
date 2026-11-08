"""在子站上：查 P_L5031A 在线状态（按台账顺序解析 dsCommState 条目序号）。"""
import json
import re
import subprocess

IEDLIST = "/home/ipofas/stdown/maintain/CONF/IEDList.xml"
IED = "P_L5031A"

with open(IEDLIST, encoding="utf-8", errors="replace") as fh:
    xml = fh.read()
devices = re.findall(r'ied_name="([^"]+)"', xml)
idx = devices.index(IED) if IED in devices else -1
print("%s index (0-based): %d -> dsCommState entry #%d" % (IED, idx, idx + 1))
print("neighbors:", devices[max(0, idx - 2): idx + 3])

LOG = "/home/admin/router/logs/iec61850Connection.log"
import os
size = os.path.getsize(LOG)
with open(LOG, errors="replace") as fh:
    fh.seek(max(0, size - 2000000))
    tail = fh.read()
hits = [ln for ln in tail.splitlines() if IED in ln]
print("recent log lines mentioning %s: %d" % (IED, len(hits)))
for ln in hits[-8:]:
    print("  ", ln[:200])
