#!/usr/bin/env python3
"""在装置上上传并执行 ops 目录下的 python 脚本。

    set SUBST_PASS=...
    python ops/run_remote_py.py smcdstat.py [args...]
"""
from __future__ import annotations

import os
import posixpath
import sys

import paramiko

HOST = os.environ.get("SUBST_HOST", "192.168.0.10")
USER = os.environ.get("SUBST_USER", "admin")
PASS = os.environ.get("SUBST_PASS", "")


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    if not PASS:
        raise SystemExit("请先设置 SUBST_PASS")

    script = sys.argv[1]
    local = os.path.join(os.path.dirname(os.path.abspath(__file__)), script)
    remote = posixpath.join("/tmp", script)

    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(HOST, username=USER, password=PASS, timeout=15,
                   allow_agent=False, look_for_keys=False)
    sftp = client.open_sftp()
    sftp.put(local, remote)
    sftp.close()

    cmd = "PYTHONIOENCODING=utf-8 python3 " + remote + " " + " ".join(sys.argv[2:])
    _stdin, stdout, stderr = client.exec_command(cmd, timeout=300)
    sys.stdout.write(stdout.read().decode("utf-8", "replace"))
    err = stderr.read().decode("utf-8", "replace")
    if err.strip():
        sys.stderr.write(err)
    rc = stdout.channel.recv_exit_status()
    client.close()
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
