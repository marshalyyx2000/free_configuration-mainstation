"""在 cfg 中列出 C_B5043XLD0 的 RCB 定义，拿到准确的 handler/实例名。"""
import re
import sys

CFG = "/home/admin/router/config/supervision.cfg"
LD = sys.argv[1] if len(sys.argv) > 1 else "C_B5043XLD0"

with open(CFG, encoding="utf-8", errors="replace") as fh:
    cfg = fh.read()

start = cfg.find("LD(%s){" % LD)
if start < 0:
    print("LD not found")
    raise SystemExit(2)
nxt = re.compile(r"^LD\(", re.M).search(cfg, start + 1)
seg = cfg[start: nxt.start() if nxt else len(cfg)]
print("LD", LD, "size", len(seg))
print("DS:", re.findall(r"^DS\((\w+)\)\{", seg, re.M))
for name, ref in re.findall(r"^RC\((\w+)\s+(\S+)\s", seg, re.M):
    print("  RC", name, "ref", ref)
