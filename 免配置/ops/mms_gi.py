#!/usr/bin/env python3
"""把 ops/mms_gi.c 传到装置、编译并做一次通信工况总召。

    set SUBST_PASS=...
    python ops/mms_gi.py [host] [port] [datasetRef]
"""
from __future__ import annotations

import os
import sys

import paramiko

HOST = os.environ.get("SUBST_HOST", "192.168.0.10")
USER = os.environ.get("SUBST_USER", "admin")
PASS = os.environ.get("SUBST_PASS", "")
LOCAL = os.path.join(os.path.dirname(os.path.abspath(__file__)), "mms_gi.c")
INC = "/home/admin/src/configuration-free/iec61850_router/third_party/libiec61850-1.5.3"
LIB = "/home/admin/src/configuration-free/iec61850_router/build/libiec61850/src"


def main() -> int:
    if not PASS:
        raise SystemExit("请先设置 SUBST_PASS")
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    port = sys.argv[2] if len(sys.argv) > 2 else "102"
    dsref = sys.argv[3] if len(sys.argv) > 3 else "SubStationLD0/LLN0$dsCommState"

    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(HOST, username=USER, password=PASS, timeout=15,
                   allow_agent=False, look_for_keys=False)
    sftp = client.open_sftp()
    sftp.put(LOCAL, "/tmp/mms_gi.c")
    sftp.close()

    build = (
        f"gcc -O2 -o /tmp/mms_gi /tmp/mms_gi.c "
        f"-I{INC}/src/iec61850/inc -I{INC}/src/mms/inc -I{INC}/src/common/inc "
        f"-I{INC}/hal/inc -I{INC}/src/logging "
        f"-L{LIB} -Wl,-rpath,{LIB} -liec61850 -lpthread && echo BUILD_OK"
    )
    for cmd in (build, f"/tmp/mms_gi {host} {port} '{dsref}'"):
        _stdin, stdout, stderr = client.exec_command(cmd, timeout=120)
        out = stdout.read().decode("utf-8", "replace")
        err = stderr.read().decode("utf-8", "replace")
        rc = stdout.channel.recv_exit_status()
        sys.stdout.write(out)
        if err.strip():
            sys.stderr.write(err)
        if rc != 0:
            client.close()
            return rc

    client.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
