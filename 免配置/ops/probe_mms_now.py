#!/usr/bin/env python3
"""等待 102 监听并跑一组 MMS CLI 命令。"""
from __future__ import annotations

import json
import os
import socket
import subprocess
import sys
import time
from pathlib import Path

CLI = Path(__file__).resolve().parents[1] / "master_sim" / "bin" / "mms_master_cli.exe"
HOST = os.environ.get("SUBST_HOST", "192.168.0.10")
PORT = 102


def tcp_up(timeout: float = 2.0) -> bool:
    try:
        with socket.create_connection((HOST, PORT), timeout=timeout):
            return True
    except OSError:
        return False


def run(cmds: list[dict], timeout_sec: float = 240.0) -> list[str]:
    payload = "\n".join(json.dumps(c, ensure_ascii=False) for c in cmds) + "\n"
    proc = subprocess.run(
        [str(CLI)],
        input=payload,
        capture_output=True,
        text=True,
        timeout=timeout_sec,
        encoding="utf-8",
        errors="replace",
    )
    out = (proc.stdout or "").splitlines()
    if proc.stderr:
        out += ["STDERR:" + s for s in proc.stderr.splitlines()]
    return out


def wait_102(max_wait: float = 600.0) -> bool:
    t0 = time.time()
    while time.time() - t0 < max_wait:
        if tcp_up():
            return True
        time.sleep(5)
    return False


def main() -> int:
    max_wait = float(os.environ.get("WAIT_SEC", "600"))
    print(f"waiting {HOST}:{PORT} up to {max_wait:.0f}s ...", flush=True)
    if not wait_102(max_wait):
        print("102 not up")
        return 1
    print("102 up", flush=True)
    cmds = [
        {"cmd": "connect", "host": HOST, "port": PORT, "timeoutMs": 180000},
        {"cmd": "read_comm_state", "timeoutMs": 180000},
        {"cmd": "get_datasets", "ld": "P_X5071APROT", "timeoutMs": 180000},
        {"cmd": "read_dataset", "ref": "P_X5071APROT/LLN0$dsWarning", "timeoutMs": 180000},
        {"cmd": "enable_rcb",
         "handlerRef": "P_X5071APROT/LLN0$BR$brcbWarning",
         "rcbGetRef": "P_X5071APROT/LLN0$BR$brcbWarning01",
         "dsRef": "P_X5071APROT/LLN0$dsWarning",
         "timeoutMs": 180000},
        {"cmd": "gi", "handlerRef": "P_X5071APROT/LLN0$BR$brcbWarning"},
        {"cmd": "disable_rcb", "handlerRef": "P_X5071APROT/LLN0$BR$brcbWarning"},
        {"cmd": "quit"},
    ]
    for ln in run(cmds):
        print(ln[:400])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
