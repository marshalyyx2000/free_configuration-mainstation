"""检查 router 业务日志中读数据集请求的完整往返（含装置回调错误）。"""
import re

LOG = "/home/admin/router/logs/iec61850Business.log"

with open(LOG) as fh:
    lines = fh.readlines()

keys = ("dsWarning", "dsRelayDin1", "read named variable", "Read named")
hits = [i for i, ln in enumerate(lines) if "read named" in ln.lower() or "Read named" in ln]
print("total read-named lines:", len(hits))
for i in hits[-24:]:
    print(lines[i].rstrip()[:220])
