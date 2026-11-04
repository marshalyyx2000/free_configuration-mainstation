#!/usr/bin/env python3
"""断链验证：在子站上对该 IED 加黑洞路由，确认通信工况变 false，再恢复。

    set SUBST_PASS=...
    python ops/verify_link.py
"""
from __future__ import annotations

import os
import subprocess
import sys
import time

import paramiko

HOST = os.environ.get("SUBST_HOST", "192.168.0.10")
USER = os.environ.get("SUBST_USER", "admin")
PASS = os.environ.get("SUBST_PASS", "")
TARGET = "10.100.100.30"
ENTRY = 11  # E1Q1SB30 在 IEDList 里的序号
HERE = os.path.dirname(os.path.abspath(__file__))


def sudo(client: paramiko.SSHClient, cmd: str, timeout: int = 60) -> str:
    full = f"echo {PASS} | sudo -S bash -lc '{cmd}' 2>&1"
    _stdin, stdout, _stderr = client.exec_command(full, timeout=timeout)
    return stdout.read().decode("utf-8", "replace")


def gi() -> str:
    r = subprocess.run([sys.executable, os.path.join(HERE, "mms_gi.py"),
                        "127.0.0.1", "102", "SubStationLD0/LLN0$dsCommState"],
                       capture_output=True, text=True, encoding="utf-8",
                       errors="replace", env=os.environ.copy())
    for line in (r.stdout or "").splitlines():
        if line.strip().startswith(f"{ENTRY} "):
            return line.strip()
    return "no result"


def main() -> int:
    if not PASS:
        raise SystemExit("请先设置 SUBST_PASS")

    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(HOST, username=USER, password=PASS, timeout=15,
                   allow_agent=False, look_for_keys=False)

    print("baseline      :", gi())
    print("blackhole     :", sudo(client, f"ip route add blackhole {TARGET}/32 && echo ok").strip())
    for _ in range(9):
        time.sleep(15)
        print("  down ->", gi())

    print("remove        :", sudo(client, f"ip route del blackhole {TARGET}/32 && echo ok").strip())
    for _ in range(10):
        time.sleep(15)
        print("  up   ->", gi())

    client.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
