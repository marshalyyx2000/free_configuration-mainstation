#!/usr/bin/env python3
"""下装生效后若 102 仍由旧 router 占用，通过 SSH 结束进程并清除 supervision 缓存，再下发一次 61850 生效。

    set SUBST_PASS=...
    python ops/reload_61850_router.py
"""
from __future__ import annotations

import os
import socket
import sys
import time
from pathlib import Path

import paramiko

HOST = os.environ.get("SUBST_HOST", "192.168.0.10")
USER = os.environ.get("SUBST_USER", "admin")
PASS = os.environ.get("SUBST_PASS", "")
MAINTAIN_PORT = int(os.environ.get("SUBST_MAINTAIN_PORT", "2405"))
MMS_PORT = int(os.environ.get("SUBST_MMS_PORT", "102"))

ROUTER_LOG_DIR = os.environ.get("SUBST_ROUTER_DIR", "/home/admin/router/router")
IEDLIST = "/home/ipofas/stdown/maintain/CONF/IEDList.xml"


def run(client: paramiko.SSHClient, cmd: str, timeout: int = 120) -> tuple[int, str]:
    _stdin, stdout, stderr = client.exec_command(cmd, timeout=timeout)
    out = stdout.read().decode("utf-8", "replace")
    err = stderr.read().decode("utf-8", "replace")
    rc = stdout.channel.recv_exit_status()
    return rc, out + (("\n" + err) if err.strip() else "")


def tcp_up(host: str, port: int) -> bool:
    try:
        with socket.create_connection((host, port), timeout=2.0):
            return True
    except OSError:
        return False


def kill_router_and_clear_cfg(client: paramiko.SSHClient) -> None:
    run(client, "pkill -f 'iec61850router' || true", timeout=30)
    time.sleep(2)
    run(
        client,
        f"rm -f {ROUTER_LOG_DIR}/supervision.cfg "
        f"{ROUTER_LOG_DIR}/supervision.cfg.stamp "
        f"{ROUTER_LOG_DIR}/.stamp "
        f"/home/admin/router/config/supervision.cfg.stamp 2>/dev/null || true",
        timeout=30,
    )


def effect_via_maintain(host: str, port: int) -> None:
    root = Path(__file__).resolve().parents[1] / "master_sim"
    sys.path.insert(0, str(root))
    from iec104_client import Iec104MaintainClient, Maintain104Config  # noqa: WPS433

    client = Iec104MaintainClient(Maintain104Config(host=host, port=port))
    client.connect()
    client.effect_61850()
    client.close()


def main() -> int:
    if not PASS:
        print("请先设置 SUBST_PASS", file=sys.stderr)
        return 1

    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(
        HOST, username=USER, password=PASS, timeout=15, allow_agent=False, look_for_keys=False
    )

    rc, ledger = run(client, f"test -f {IEDLIST} && head -n 3 {IEDLIST} || echo 'missing IEDList'")
    print(ledger.strip())
    rc, n172 = run(client, f"grep -c '172\\.16\\.' {IEDLIST} 2>/dev/null || echo 0")
    rc, n10 = run(client, f"grep -c '10\\.100\\.100\\.' {IEDLIST} 2>/dev/null || echo 0")
    print(f"装置台账: 172.16.x 约 {n172.strip()} 处, 10.100.100.x 约 {n10.strip()} 处")

    print("结束旧 iec61850router 并清除 supervision 缓存 …")
    kill_router_and_clear_cfg(client)
    client.close()

    print("运维口再次下发 61850 生效 …")
    effect_via_maintain(HOST, MAINTAIN_PORT)

    print(f"等待 {HOST}:{MMS_PORT} 重新监听（最长 20 分钟）…")
    deadline = time.time() + 20 * 60
    while time.time() < deadline:
        if tcp_up(HOST, MMS_PORT):
            print(f"102 已监听（耗时约 {20 * 60 - (deadline - time.time()):.0f}s 内检测到）")
            return 0
        time.sleep(5)
    print("超时：102 未重新监听", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
