"""检查 router 是否安装写访问处理器，以及 RC 条目的格式（决定 RCB 是否可写）。"""
import re

SRV = "/home/admin/src/configuration-free/iec61850_router/src/server/server_controller.cpp"

with open(SRV, errors="replace") as fh:
    src = fh.read()

for pat in ("WriteAccessHandler", "writeAccess", "setRCBValues", "Resv", "resv"):
    print("%-22s %d" % (pat, len(re.findall(pat, src))))

# 主站订阅成功的 C_B5043X vs 失败的 P_L5031A 在 cfg 中 RC 行格式是否相同
CFG = "/home/admin/router/config/supervision.cfg"
with open(CFG, encoding="utf-8", errors="replace") as fh:
    cfg = fh.read()

def rc_lines(ied, base):
    pat = re.compile(r"^RC\(%s\d\d %s\S*/\S+ 1 %s .*\);$" % (base, ied, "dsAlarm"), re.M)
    return pat.findall(cfg)

print("C_B5043XLD0 brcbAlarm01 line:", rc_lines("C_B5043XLD0", "brcbAlarm")[:1])
print("P_L5031ALD0 brcbAlarm01 line:", rc_lines("P_L5031ALD0", "brcbAlarm")[:1])
