#!/usr/bin/env python3
"""SSH：清 stamp、full-ln 生成生产 supervision.cfg、杀掉 router 由 MaintainServer 重装。"""
from __future__ import annotations

import os
import sys

import paramiko

HOST = os.environ.get("SUBST_HOST", "192.168.0.10")
USER = os.environ.get("SUBST_USER", "admin")
PASS = os.environ.get("SUBST_PASS", "")
CID = "/home/admin/router/config/supervision.CID"
CFG = "/home/admin/router/config/supervision.cfg"
STAMP = CFG + ".stamp"
GEN = "/home/admin/router/bin/genconfig-run.sh"
JAR = "/home/admin/router/bin/genconfig.jar"


def main() -> int:
    if not PASS:
        print("SUBST_PASS required", file=sys.stderr)
        return 1
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(HOST, username=USER, password=PASS, timeout=15, allow_agent=False, look_for_keys=False)
    cmd = (
        f"rm -f {STAMP}; "
        f"pkill -f iec61850router 2>/dev/null || true; "
        f"OUT=/tmp/gen_supervision.cfg; rm -f $OUT $OUT.stamp; "
        f"T0=$(date +%s); "
        f"if [ -x {GEN} ]; then GENCONFIG_MODE=full-ln {GEN} {CID} $OUT; "
        f"else GENCONFIG_MODE=full-ln java -Xms128m -Xmx512m -XX:+UseSerialGC "
        f"-XX:+ExitOnOutOfMemoryError -jar {JAR} {CID} $OUT; fi; "
        f"EC=$?; T1=$(date +%s); echo GEN_EXIT:$EC WALL:$((T1-T0)); "
        f"test $EC -eq 0 -a -s $OUT && mv -f $OUT {CFG} && cp -f {CFG}.stamp {STAMP} 2>/dev/null; "
        f"ls -l {CFG}; grep -c 'LN(DevAlmGGIO' {CFG} || true; grep -c '^RC(' {CFG} || true"
    )
    _stdin, stdout, stderr = client.exec_command(cmd, timeout=1800, get_pty=True)
    out = stdout.read().decode("utf-8", "replace")
    err = stderr.read().decode("utf-8", "replace")
    rc = stdout.channel.recv_exit_status()
    print(out)
    if err.strip():
        print(err, file=sys.stderr)
    client.close()
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
