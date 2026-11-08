"""对比 P_L5031A 两个 LD 的 RCB 分布，确认哪些 RCB 落在不存在的 LD 上。"""
import re

CFG = "/home/admin/router/config/supervision.cfg"
IED = "P_L5031A"

with open(CFG, encoding="utf-8", errors="replace") as fh:
    cfg = fh.read()

lds = set(re.findall(r"^LD\((\w+)\)\{", cfg, re.M))
print("LDs for", IED, ":", sorted(x for x in lds if x.startswith(IED)))

# 所有 RC 按引用 LD 分组，只看该 IED
groups = {}
for m in re.finditer(r"^RC\((\w+)\s+(\S+?)/(\w+)\$(\w+)\$(\w+)\s", cfg, re.M):
    name, ld, ln, fc, base = m.groups()
    if not ld.startswith(IED):
        continue
    key = (ld, fc, base)
    groups.setdefault(key, []).append(name)

for (ld, fc, base), names in sorted(groups.items()):
    exists = ld in lds
    print("%-18s %s$%s  n=%-3d exists=%-5s e.g. %s" % (ld, fc, base, len(names), exists, names[0]))
