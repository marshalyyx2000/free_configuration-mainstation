"""扫描 cfg 中所有 RCB 引用，找出 LD inst 与实际不符（如 ALDO vs ALD0）的条目。"""
import re

CFG = "/home/admin/router/config/supervision.cfg"

with open(CFG, encoding="utf-8", errors="replace") as fh:
    cfg = fh.read()

lds = set(re.findall(r"^LD\((\w+)\)\{", cfg, re.M))
print("total LDs:", len(lds))

bad = []
for m in re.finditer(r"^RC\((\w+)\s+(\S+?)/LLN0\$", cfg, re.M):
    name, ld = m.group(1), m.group(2)
    if ld not in lds:
        bad.append((name, ld))

print("RC entries whose LD is missing from model:", len(bad))
seen = set()
for name, ld in bad:
    if ld not in seen:
        seen.add(ld)
        print("  missing LD:", ld, "e.g.", name)
print("distinct missing LDs:", len(seen))

# 找出形似 ALDO / LDO 的（字母O代替数字0）
suspect = sorted({ld for _, ld in bad if "LDO" in ld or "ALDO" in ld})
print("LDs containing 'LDO' (letter O):", suspect[:10])

# 对应的正确 LD 是否存在
for s in suspect[:5]:
    fixed = s.replace("LDO", "LD0").replace("ALDO", "ALD0")
    print("  ", s, "-> candidate", fixed, "exists:", fixed in lds)
