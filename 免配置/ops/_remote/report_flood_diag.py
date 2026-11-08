# -*- coding: utf-8 -*-
"""分析 router 报告空值风暴：起始时间、涉及设备、报表频率、日志增长。"""
from __future__ import print_function

import os
import re
import subprocess
import time

REPORT = "/home/admin/router/logs/iec61850Report.log"
REPORT_1 = "/home/admin/router/logs/iec61850Report.log.1"
STATUS = "/home/admin/router/logs/iec61850ReportStatus.log"
BUSINESS = "/home/admin/router/logs/iec61850Business.log"


def tail_text(path, nbytes=200000):
    with open(path, "rb") as fh:
        fh.seek(0, os.SEEK_END)
        size = fh.tell()
        fh.seek(max(0, size - nbytes))
        return fh.read().decode("utf-8", "replace")


def show_first_null(path):
    print("=== first null in", path)
    with open(path, "rb") as fh:
        for i, raw in enumerate(fh):
            line = raw.decode("utf-8", "replace")
            if " is null" in line:
                print("line", i, line.rstrip())
                break
        else:
            print("none")


for p in (REPORT_1, REPORT):
    if os.path.exists(p):
        show_first_null(p)

print("=== last 12 non-null lines of report log")
text = tail_text(REPORT)
lines = text.splitlines()
shown = 0
for line in reversed(lines):
    if " is null" in line:
        continue
    if line.strip():
        print(line[:300])
        shown += 1
        if shown >= 12:
            break

print("=== null refs by device (last 200k)")
counts = {}
for m in re.finditer(r"Value for ([A-Za-z0-9_]+)/", text):
    counts[m.group(1)] = counts.get(m.group(1), 0) + 1
for k, v in sorted(counts.items(), key=lambda kv: -kv[1])[:20]:
    print(k, v)

print("=== report log growth over 6s")
size0 = os.path.getsize(REPORT)
time.sleep(6)
size1 = os.path.getsize(REPORT)
print("delta bytes", size1 - size0, "per sec", (size1 - size0) / 6.0)

print("=== last 40 ReportStatus lines")
print(tail_text(STATUS, 12000))

print("=== business requests after 01:30")
for line in tail_text(BUSINESS, 400000).splitlines():
    if "192.168.0.20" in line or "Send Read" in line or "proxy" in line.lower():
        print(line[:300])

print("=== gdb?")
try:
    print(subprocess.check_output("which gdb", shell=True).decode())
except subprocess.CalledProcessError:
    print("no gdb")

print("=== report thread cpu now")
try:
    print(subprocess.check_output("top -b -n1 -H -p 15115 | head -12", shell=True).decode())
except subprocess.CalledProcessError as e:
    print(e)
