"""诊断 iec61850router 持续 99% CPU：线程、wchan、日志、fd、连接。
兼容子站自带老版本 python3（不用 f-string）。"""
from __future__ import print_function

import glob
import os
import subprocess

PID = None
for p in glob.glob("/proc/[0-9]*/cmdline"):
    try:
        cmd = open(p, "rb").read().replace(b"\x00", b" ").decode("utf-8", "replace")
    except OSError:
        continue
    if "iec61850router" in cmd:
        PID = int(p.split("/")[2])
        print("PID", PID, cmd)
if PID is None:
    raise SystemExit("router not running")

print("--- threads ---")
for t in sorted(glob.glob("/proc/%d/task/*" % PID), key=lambda x: int(x.rsplit("/", 1)[1])):
    tid = t.rsplit("/", 1)[1]
    try:
        stat = open(t + "/stat").read().split()
        state = stat[2]
        utime = int(stat[13])
        stime = int(stat[14])
    except OSError:
        continue
    try:
        wchan = open(t + "/wchan").read()
    except OSError:
        wchan = "?"
    print("tid=%s state=%s utime=%s stime=%s wchan=%s" % (tid, state, utime, stime, wchan))

print("--- status ---")
for line in open("/proc/%d/status" % PID):
    if line.split(":")[0] in ("Name", "State", "Threads", "VmRSS", "VoluntaryCtxtSwitches",
                              "NonvoluntaryCtxtSwitches", "Seccomp"):
        print(line.rstrip())

print("--- fds ---")
try:
    fds = os.listdir("/proc/%d/fd" % PID)
    print("fd count", len(fds))
    for fd in sorted(fds, key=int)[:40]:
        try:
            print(fd, "->", os.readlink("/proc/%d/fd/%s" % (PID, fd)))
        except OSError:
            pass
except OSError as e:
    print("fd err", e)

print("--- logs (newest) ---")
logs = sorted(glob.glob("/home/admin/router/logs/*"), key=os.path.getmtime, reverse=True)[:6]
for p in logs:
    print(p, os.path.getsize(p), "mtime", os.path.getmtime(p))
    try:
        data = open(p, "rb").read()[-3000:].decode("utf-8", "replace")
        print(data)
    except OSError as e:
        print("read err", e)

print("--- connections ---")
try:
    out = subprocess.check_output("ss -tnp | grep -E '102|%d' | head -40" % PID,
                                  shell=True, stderr=subprocess.STDOUT)
    print(out.decode("utf-8", "replace"))
except subprocess.CalledProcessError as e:
    print(e.output.decode("utf-8", "replace"))
