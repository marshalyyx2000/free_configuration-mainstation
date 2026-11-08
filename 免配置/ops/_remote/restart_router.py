# -*- coding: utf-8 -*-
"""强制重建 supervision.cfg：删除 stamp 并杀掉当前 router，让 MaintainServer 重启。"""
from __future__ import print_function

import glob
import os
import signal
import time

killed = []
for p in glob.glob("/proc/[0-9]*/cmdline"):
    try:
        cmd = open(p, "rb").read().replace(b"\x00", b" ").decode("utf-8", "replace")
    except OSError:
        continue
    if "iec61850router" in cmd:
        pid = int(p.split("/")[2])
        try:
            os.kill(pid, signal.SIGKILL)
            killed.append(pid)
        except OSError as e:
            print("kill", pid, e)
print("killed", killed)
stamp = "/home/admin/router/config/supervision.cfg.stamp"
try:
    os.remove(stamp)
    print("stamp removed")
except OSError as e:
    print("stamp", e)
time.sleep(1)
