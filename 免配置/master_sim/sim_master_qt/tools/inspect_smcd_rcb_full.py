import re
import sys

path, ied = sys.argv[1], sys.argv[2]
s = open(path, encoding="utf-8", errors="ignore").read()
i = s.find(f'name="{ied}"')
seg = s[i:i + 300000]
for m in re.finditer(r"<ReportControl[^>]*>.*?</ReportControl>", seg, re.S):
    g = m.group(0)
    if "brcbAlarm" in g or "brcbRelayEna" in g:
        print(re.sub(r"\s+", " ", g)[:500])
