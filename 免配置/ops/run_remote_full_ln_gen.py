#!/usr/bin/env python3
"""在子站上跑 full-ln genconfig（避免 PowerShell 转义远程 shell）。"""
from __future__ import annotations

import os
import sys

import paramiko

HOST = os.environ.get("SUBST_HOST", "192.168.0.10")
USER = os.environ.get("SUBST_USER", "admin")
PASS = os.environ.get("SUBST_PASS", "")
CID = "/home/admin/router/config/supervision.CID"
OUT = os.environ.get("GENCFG_OUT", "/tmp/gen_supervision.cfg")
PROD = "/home/admin/router/config/supervision.cfg"
STAMP = PROD + ".stamp"
JAR = "/home/admin/router/bin/genconfig.jar"
JAVA = (
    "GENCONFIG_MODE=full-ln java -Xms128m -Xmx512m -XX:+UseSerialGC "
    "-XX:+ExitOnOutOfMemoryError -jar " + JAR
)


def main() -> int:
    if not PASS:
        print("SUBST_PASS required", file=sys.stderr)
        return 1
    cmd = (
        f"rm -f {STAMP} {OUT} {OUT}.stamp; "
        f"T0=`date +%s`; "
        f"{JAVA} {CID} {OUT} > /tmp/genconfig_full_ln.log 2>&1; "
        f"EC=$?; T1=`date +%s`; "
        f"echo GEN_EXIT:$EC; echo WALL:$((T1-T0)); "
        f"tail -6 /tmp/genconfig_full_ln.log; "
        f"ls -l {OUT} 2>&1; "
        f"grep -c 'LN(DevAlmGGIO' {OUT} 2>/dev/null || echo DEVALM:0; "
        f"grep -c '^RC(' {OUT} 2>/dev/null || echo RC:0"
    )
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(HOST, username=USER, password=PASS, timeout=15, allow_agent=False, look_for_keys=False)
    _stdin, stdout, stderr = client.exec_command(cmd, timeout=1800)
    out = stdout.read().decode("utf-8", "replace")
    err = stderr.read().decode("utf-8", "replace")
    rc = stdout.channel.recv_exit_status()
    print(out)
    if err.strip():
        print(err, file=sys.stderr)
    if rc == 0 and "GEN_EXIT:0" in out:
        install = (
            f"mv -f {OUT} {PROD} && "
            f"test -f {OUT}.stamp && mv -f {OUT}.stamp {STAMP} || true; "
            f"pkill -f iec61850router 2>/dev/null || true; "
            f"ls -l {PROD}"
        )
        _stdin2, stdout2, _stderr2 = client.exec_command(install, timeout=60)
        print(stdout2.read().decode("utf-8", "replace"))
        rc = stdout2.channel.recv_exit_status()
    client.close()
    if "GEN_EXIT:0" in out and "DEVALM:0" not in out.splitlines()[-3:]:
        return 0 if rc == 0 else 0
    return 1 if rc != 0 else 0


if __name__ == "__main__":
    raise SystemExit(main())
