"""统计当前在线装置：以 dsCommState 索引对照台账顺序。"""
import re

IEDLIST = "/home/ipofas/stdown/maintain/CONF/IEDList.xml"
CFG = "/home/admin/router/config/supervision.cfg"

with open(IEDLIST, encoding="utf-8", errors="replace") as fh:
    xml = fh.read()
devices = re.findall(r'ied_name="([^"]+)"', xml)

with open(CFG, encoding="utf-8", errors="replace") as fh:
    cfg = fh.read()

m = re.search(r"LD\(SubStationLD0\)\{.*?DS\(dsCommState\)\{(.*?)\}", cfg, re.S)
fcdas = re.findall(r"DE\(([^/]+)/LPHD1\$ST\$Proxy\);", m.group(1)) if m else []
print("IEDList devices:", len(devices), "dsCommState entries:", len(fcdas))
same = [a == b + "LD0" for a, b in zip(fcdas, devices)]
print("order matches IEDList+LD0:", sum(same), "/", len(fcdas))
print("first 5 entries:", fcdas[:5])
print("first 5 devices:", devices[:5])
idx = devices.index("P_X5071A") if "P_X5071A" in devices else -1
print("P_X5071A index:", idx, "-> entry:", fcdas[idx] if idx >= 0 else None)
