#!/usr/bin/env python3
"""子站上测 production 路径 genconfig 是否因 stamp 跳过（通常 <5s）。"""
from __future__ import annotations

import os
import re
import sys

import paramiko

PASS = os.environ.get("SUBST_PASS", "")
HOST = os.environ.get("SUBST_HOST", "192.168.0.10")
CID = "/home/admin/router/config/supervision.CID"
CFG = "/home/admin/router/config/supervision.cfg"
STAMP = CFG + ".stamp"
JAR = "/home/admin/router/bin/genconfig.jar"


def main() -> int:
    if not PASS:
        print("SUBST_PASS required", file=sys.stderr)
        return 1
    cmd = (
        f"head -1 {STAMP} 2>/dev/null || echo NO_STAMP; "
        f"ls -l {CFG} {STAMP} 2>&1; "
        f"T0=`date +%s`; "
        f"GENCONFIG_MODE=full-ln java -jar {JAR} {CID} {CFG} >/tmp/stamp_skip.log 2>&1; "
        f"EC=$?; T1=`date +%s`; "
        f"echo GEN_EXIT:$EC; echo WALL:$((T1-T0)); "
        f"tail -5 /tmp/stamp_skip.log"
    )
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(
        HOST, username="admin", password=PASS, timeout=15, allow_agent=False, look_for_keys=False
    )
    stdin, stdout, stderr = client.exec_command(cmd, timeout=90)
    out = stdout.read().decode("utf-8", "replace")
    err = stderr.read().decode("utf-8", "replace")
    rc = stdout.channel.recv_exit_status()
    client.close()
    print(out)
    if err.strip():
        print(err, file=sys.stderr)
    wall_m = re.search(r"WALL:(\d+)", out)
    wall = int(wall_m.group(1)) if wall_m else 999
    skipped = "skip genconfig" in out
    if skipped and wall <= 10:
        print("OK: stamp 命中，已跳过完整生成")
        return 0
    if wall <= 10 and "GEN_EXIT:0" in out:
        print("OK: 墙钟很短（可能已有 cfg 未重跑）")
        return 0
    print(f"WARN: 未检测到跳过（wall={wall}s）；stamp 可能为旧格式，需用新 jar 完整生成一次以写入 SHA256", file=sys.stderr)
    return 1 if wall > 60 else 0


if __name__ == "__main__":
    raise SystemExit(main())
