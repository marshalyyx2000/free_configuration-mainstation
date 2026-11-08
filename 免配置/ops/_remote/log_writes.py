"""查看 router 业务日志中所有写/RCB 相关记录，判断订阅请求是否被转发。"""
import os

LOG = "/home/admin/router/logs/iec61850Business.log"
with open(LOG, errors="replace") as fh:
    lines = fh.readlines()

keys = ("Write", "write", "RCB", "Rcb", "resv", "Resv", "RptEna", "report")
hits = [ln for ln in lines if any(k in ln for k in keys)]
print("total write/rcb-ish lines:", len(hits))
for ln in hits[-25:]:
    print(ln.rstrip()[:220])
