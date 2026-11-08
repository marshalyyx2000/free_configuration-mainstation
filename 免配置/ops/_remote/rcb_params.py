"""检查 cfg 中 P_L5031ALD0 的 RCB 条目参数（是否被标记为可写/带 resv 等）。"""
import re

CFG = "/home/admin/router/config/supervision.cfg"
LD = "P_L5031ALD0"

with open(CFG, encoding="utf-8", errors="replace") as fh:
    cfg = fh.read()

start = cfg.find("LD(%s){" % LD)
nxt = re.compile(r"^LD\(", re.M).search(cfg, start + 1)
seg = cfg[start: nxt.start()]

lines = [ln for ln in seg.splitlines() if ln.startswith("RC(")]
print("RC lines:", len(lines))
for ln in lines[:3] + lines[64:67] + lines[-2:]:
    print(" ", ln)
