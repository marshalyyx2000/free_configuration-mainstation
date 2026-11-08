"""对比可订阅/不可订阅装置的 RC 参数与台账 BRCB_TrgOps/OptFlds 的关系。"""
import re

CFG = "/home/admin/router/config/supervision.cfg"
IEDLIST = "/home/ipofas/stdown/maintain/CONF/IEDList.xml"

with open(CFG, encoding="utf-8", errors="replace") as fh:
    cfg = fh.read()
with open(IEDLIST, encoding="utf-8", errors="replace") as fh:
    xml = fh.read()

def ledger(ied):
    m = re.search(r'<Device[^>]*ied_name="%s"[^>]*>' % re.escape(ied), xml)
    if not m:
        return {}
    tag = m.group(0)
    out = {}
    for k in ("BRCB_TrgOps", "BRCB_OptFlds", "rptno"):
        mm = re.search(r'%s="([^"]*)"' % k, tag)
        out[k] = mm.group(1) if mm else None
    return out

def rc_params(ld, base):
    m = re.search(r"^RC\(%s01 %s/\S+ 1 (\w+) 1 (\d+) (\d+) (\d+) (\d+)\);" % (base, ld), cfg, re.M)
    return m.groups() if m else None

for ied in ("C_B5043X", "P_L5031A", "P_X5071A", "P_B5013A"):
    for ld, base in ((ied + "LD0", "brcbAlarm"), (ied + "PROT", "brcbWarning")):
        rc = rc_params(ld, base)
        if rc:
            ds, ena, trg, opt, a = rc
            trg_i, opt_i = int(trg), int(opt)
            print("%-14s %-16s trg=0x%02X opt=0x%02X  %s" % (ld, base, trg_i, opt_i, ledger(ied)))
            break
