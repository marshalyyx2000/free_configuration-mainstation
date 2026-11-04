#!/usr/bin/env python3
"""等子站模型生成完成（102 监听）并做一次通信工况总召。

    set SUBST_PASS=...
    python ops/wait_and_gi.py
"""
from __future__ import annotations

import os
import sys
import time

import paramiko

HOST = os.environ.get("SUBST_HOST", "192.168.0.10")
USER = os.environ.get("SUBST_USER", "admin")
PASS = os.environ.get("SUBST_PASS", "")
DSREF = "SubStationLD0/LLN0$dsCommState"


def run(client: paramiko.SSHClient, cmd: str, timeout: int = 120) -> tuple[int, str]:
    _stdin, stdout, stderr = client.exec_command(cmd, timeout=timeout)
    out = stdout.read().decode("utf-8", "replace")
    err = stderr.read().decode("utf-8", "replace")
    rc = stdout.channel.recv_exit_status()
    return rc, out + (("\n" + err) if err.strip() else "")


def main() -> int:
    if not PASS:
        raise SystemExit("请先设置 SUBST_PASS")

    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(HOST, username=USER, password=PASS, timeout=15,
                   allow_agent=False, look_for_keys=False)

    deadline = time.time() + 20 * 60
    pid = None
    while time.time() < deadline:
        _rc, out = run(client, "pgrep -x iec61850router || true")
        pid = out.strip().splitlines()[0] if out.strip() else None
        _rc, port = run(client, "ss -lnt 2>/dev/null | grep ':102 ' || true")
        _rc, java = run(client, "pgrep -x java || true")
        state = "genconfig" if java.strip() else ("listening" if ":102" in port else "starting")
        print(f"[{time.strftime('%H:%M:%S')}] router={pid} state={state}")
        if ":102" in port:
            break
        time.sleep(20)
    else:
        print("超时：102 未开")
        _rc, out = run(client, "tail -n 20 /home/admin/router/logs/iec61850Lifetime.log")
        print(out)
        client.close()
        return 1

    print("模型已加载，等 20 秒让 A 网装置连上 ...")
    time.sleep(20)

    _rc, out = run(client, "grep -c 'Connected to' /home/admin/router/logs/iec61850Connection.log || true")
    print("累计 Connected 日志行:", out.strip())
    _rc, out = run(client, "grep 'Connected to' /home/admin/router/logs/iec61850Connection.log | tail -n 5")
    print(out)

    print("=== 通信工况总召 ===")
    rc, out = run(client, f"/tmp/mms_gi 127.0.0.1 102 '{DSREF}'", timeout=180)
    print(out.strip())

    _rc, log = run(client, "grep -c 'Updated common state' /home/admin/router/logs/iec61850Connection.log || true")
    print("Updated common state 次数:", log.strip())
    _rc, log = run(client, "grep -c 'Could not find common state' /home/admin/router/logs/iec61850Connection.log || true")
    print("Could not find common state 次数:", log.strip())

    client.close()
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
