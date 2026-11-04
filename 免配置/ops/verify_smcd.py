import re
import xml.etree.ElementTree as ET
from pathlib import Path

NS = "{http://www.iec.ch/61850/2003/SCL}"
p = Path(r"C:\工程项目开发\1U装置稳定性测试\免配置\master_sim\out\station.SMCD")
root = ET.parse(p).getroot()

for ied in root.findall(NS + "IED"):
    if ied.get("name") != "SubStation":
        continue
    for ap in ied.findall(NS + "AccessPoint"):
        srv = ap.find(NS + "Server")
        for ld in srv.findall(NS + "LDevice"):
            ln0 = ld.find(NS + "LN0")
            if ln0 is None:
                continue
            for ds in ln0.findall(NS + "DataSet"):
                fc = ds.findall(NS + "FCDA")
                print("DS", ds.get("name"), "count", len(fc))
                for i, f in enumerate(fc[:4]):
                    print("  ", i + 1, f.attrib)
                if fc:
                    print("   last:", fc[-1].attrib)

# also verify each referenced point exists in its IED
print("=== verify refs ===")
devices = {}
for ied in root.findall(NS + "IED"):
    name = ied.get("name") or ""
    lns = set()
    for ap in ied.findall(NS + "AccessPoint"):
        srv = ap.find(NS + "Server")
        if srv is None:
            continue
        for ld in srv.findall(NS + "LDevice"):
            for ln in list(ld.findall(NS + "LN")) + list(ld.findall(NS + "LN0")):
                lns.add((ld.get("inst"), ln.get("prefix") or "", ln.get("lnClass"), ln.get("inst")))
    devices[name] = lns

bad = []
for ied in root.findall(NS + "IED"):
    if ied.get("name") != "SubStation":
        continue
    for ds in ied.iter(NS + "DataSet"):
        for f in ds.findall(NS + "FCDA"):
            ldinst = f.get("ldInst")
            dev = ldinst[:-3] if ldinst.endswith("LD0") else ldinst
            key = ("LD0", f.get("prefix") or "", f.get("lnClass"), f.get("lnInst"))
            if dev not in devices or key not in devices[dev]:
                bad.append((dev, f.attrib))
print("bad refs:", len(bad))
for b in bad[:5]:
    print("  ", b)
