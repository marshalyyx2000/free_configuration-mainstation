"""对比 P_L5031A 与 C_B5043X 的 RCB 订阅结果（router 下联侧）。"""
import os
import re

LOGS = [
    "/home/admin/router/logs/iec61850ReportStatus.log",
    "/home/admin/router/logs/iec61850ReportStatus.log.1",
]

for ied in ("P_L5031A", "C_B5043X"):
    ok = fail = 0
    samples = []
    for path in LOGS:
        if not os.path.exists(path):
            continue
        with open(path, errors="replace") as fh:
            for ln in fh:
                if ied not in ln:
                    continue
                if "success" in ln:
                    ok += 1
                    if len(samples) < 3:
                        samples.append("OK  " + ln.strip()[:160])
                elif "failed" in ln:
                    fail += 1
                    if len(samples) < 6:
                        samples.append("ERR " + ln.strip()[:160])
    print("== %s: success=%d failed=%d" % (ied, ok, fail))
    for s in samples:
        print("   ", s)
