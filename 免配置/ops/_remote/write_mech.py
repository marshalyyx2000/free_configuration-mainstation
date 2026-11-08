"""分析 router 对主站 RCB 写/订阅的转发机制，检查 ProxyWrite 是否真正到达装置。"""
import re

LOG = "/home/admin/router/logs/iec61850Business.log"
with open(LOG, errors="replace") as fh:
    text = fh.read()

for pat in ("Receive Write", "Send request write", "write variable", "ProxyWrite", "resv"):
    n = len(re.findall(pat, text, re.I))
    print("%-20s %d" % (pat, n))

# 看 C_B5043X 成功订阅时刻前后的全部日志
target = "01:12:27"
i = text.find(target)
if i > 0:
    seg = text[i - 200: i + 1500]
    print("--- context around first read of C_B5043X ---")
    print(seg[:1500])
