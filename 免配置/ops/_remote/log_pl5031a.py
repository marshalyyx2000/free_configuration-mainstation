"""查看最近 P_L5031A 的 RCB 读写请求在 router 侧的实际转发与错误。"""
import os

LOG = "/home/admin/router/logs/iec61850Business.log"
size = os.path.getsize(LOG)
with open(LOG, errors="replace") as fh:
    fh.seek(max(0, size - 400000))
    lines = fh.read().splitlines()

hits = [ln for ln in lines if "P_L5031A" in ln or "P_L5031" in ln]
print("hits:", len(hits))
for ln in hits[-40:]:
    print(ln[:230])
