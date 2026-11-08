"""列出 cfg 中指定 LD 的 DS/RC 定义，确认主站引用是否与模型一致。"""
import re
import sys

CFG = "/home/admin/router/config/supervision.cfg"
LD = sys.argv[1] if len(sys.argv) > 1 else "P_X5071APROT"

with open(CFG) as fh:
    cfg = fh.read()

start = cfg.find("LD(%s){" % LD)
if start < 0:
    print("LD not found:", LD)
    raise SystemExit(2)

# 找下一个顶层 LD(
m = re.compile(r"^LD\(", re.M)
nxt = m.search(cfg, start + 1)
end = nxt.start() if nxt else len(cfg)
seg = cfg[start:end]
print("LD %s: %d bytes" % (LD, len(seg)))
ds = re.findall(r"^DS\((\w+)\)\{", seg, re.M)
rc = re.findall(r"^RC\((\w+)\s+([^\s]+)\s", seg, re.M)
ln = re.findall(r"^LN\((\w+)\)", seg, re.M)
print("LN:", ln)
print("DS:", ds)
print("RC count:", len(rc))
seen = {}
for name, ref in rc:
    seen.setdefault(ref, []).append(name)
for ref, names in list(seen.items())[:10]:
    print("  ref", ref, "->", len(names), "instances:", names[:6])
