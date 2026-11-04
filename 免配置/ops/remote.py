#!/usr/bin/env python3
"""在 192.168.0.10 上执行命令的小工具（凭证从环境变量读取，不落盘）。

用法：
    set SUBST_PASS=...        # PowerShell
    python remote.py "ps -ef | grep iec61850"
"""
from __future__ import annotations

import os
import sys

import paramiko

HOST = os.environ.get("SUBST_HOST", "192.168.0.10")
USER = os.environ.get("SUBST_USER", "admin")
PASS = os.environ.get("SUBST_PASS", "")


def connect() -> paramiko.SSHClient:
    if not PASS:
        raise SystemExit("请先设置环境变量 SUBST_PASS")
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(HOST, username=USER, password=PASS, timeout=15,
                   allow_agent=False, look_for_keys=False)
    return client


def run(script: str, timeout: int = 120) -> str:
    with connect() as client:
        _stdin, stdout, stderr = client.exec_command(script, timeout=timeout)
        out = stdout.read().decode("utf-8", "replace")
        err = stderr.read().decode("utf-8", "replace")
    return out + (("\n[stderr]\n" + err) if err.strip() else "")


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    print(run(sys.argv[1], timeout=int(os.environ.get("SUBST_TIMEOUT", "120"))))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
