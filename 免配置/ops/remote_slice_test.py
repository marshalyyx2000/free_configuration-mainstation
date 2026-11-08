#!/usr/bin/env python3
import os
import sys
from pathlib import Path

import paramiko

ROOT = Path(__file__).resolve().parents[1]
SLICE = ROOT / "master_sim/tools/scl_genconfig_slice.py"
HOST = os.environ.get("SUBST_HOST", "192.168.0.10")


def main() -> int:
    if not os.environ.get("SUBST_PASS"):
        raise SystemExit("需要 SUBST_PASS")
    c = paramiko.SSHClient()
    c.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    c.connect(
        HOST,
        username=os.environ.get("SUBST_USER", "admin"),
        password=os.environ["SUBST_PASS"],
        timeout=15,
        allow_agent=False,
        look_for_keys=False,
    )
    sftp = c.open_sftp()
    sftp.put(str(SLICE), "/tmp/scl_genconfig_slice.py")
    sftp.close()
    cmd = (
        "CID=/home/admin/router/config/supervision.CID; "
        "python3 /tmp/scl_genconfig_slice.py $CID /tmp/mini.scl; "
        "ls -l /tmp/mini.scl; "
        "T0=$(date +%s); "
        "java -jar /home/admin/router/bin/genconfig.jar /tmp/mini.scl /tmp/mini.cfg "
        ">/tmp/mini.log 2>&1; EC=$?; T1=$(date +%s); "
        "echo EXIT:$EC; echo WALL:$((T1-T0)); ls -l /tmp/mini.cfg 2>/dev/null; "
        "tail -10 /tmp/mini.log"
    )
    _, o, e = c.exec_command(cmd, timeout=300)
    out = o.read().decode("utf-8", errors="replace")
    err = e.read().decode("utf-8", errors="replace")
    print(out)
    if err.strip():
        print(err, file=sys.stderr)
    c.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
