import re
import sys

path = sys.argv[1]
ied = sys.argv[2]
s = open(path, encoding="utf-8", errors="ignore").read()
i = s.find(f'name="{ied}"')
print("idx", i)
seg = s[i:i + 300000]
for m in re.finditer(r"<ReportControl[^>]*>", seg):
    g = m.group(0)
    if any(k in g for k in ("RelayEna", "Alarm", "Warning", "Din", "din")):
        print(g[:300])
