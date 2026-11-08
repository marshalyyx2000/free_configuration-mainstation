"""检查 IEDList 中 P_L5031A 的 rptno（决定 router 使用的 RCB 实例序号）。"""
import re

IEDLIST = "/home/ipofas/stdown/maintain/CONF/IEDList.xml"

with open(IEDLIST, encoding="utf-8", errors="replace") as fh:
    xml = fh.read()

for ied in ("P_L5031A", "C_B5043X", "P_X5071A", "P_B5013A"):
    m = re.search(r'<Device[^>]*ied_name="%s"[^>]*>' % re.escape(ied), xml)
    if not m:
        print(ied, "not found")
        continue
    tag = m.group(0)
    rptno = re.search(r'rptno="(\d+)"', tag)
    ch = re.search(r'ch_para="([^"]+)"', tag)
    print("%-10s rptno=%s  ch_para=%s" % (ied, rptno.group(1) if rptno else "?",
                                          ch.group(1) if ch else "?"))
