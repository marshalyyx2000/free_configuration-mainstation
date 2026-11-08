"""从 router 报告日志中提取装置真实上报的数据集引用（确认域命名）。"""
import collections
import re

LOG = "/home/admin/router/logs/iec61850Report.log"

pat = re.compile(r"update dataset values (\S+)")
cnt = collections.Counter()
with open(LOG, errors="replace") as fh:
    for ln in fh:
        m = pat.search(ln)
        if m:
            cnt[m.group(1)] += 1
print("distinct refs:", len(cnt))
for ref, n in cnt.most_common(20):
    print("  ", n, ref)
