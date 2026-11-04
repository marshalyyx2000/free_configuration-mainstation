#!/usr/bin/env python3
"""连接子站 102：端口探测；若已编译 mms_master_cli 则总召 dsCommState。"""
from __future__ import annotations

import argparse
import socket
import sys

from mms_master import MmsMasterBridge

DEFAULT_HOST = "192.168.0.10"
DEFAULT_PORT = 102
DEFAULT_DS = "SubStationLD0/LLN0$dsCommState"


def tcp_probe(host: str, port: int) -> bool:
    try:
        s = socket.create_connection((host, port), timeout=5)
        s.close()
        return True
    except OSError:
        return False


def comm_state_via_cli(host: str, port: int, ds_ref: str) -> int:
    bridge = MmsMasterBridge()
    if not bridge.available:
        print("未找到 bin/mms_master_cli.exe，请先运行 build_libiec61850.ps1", file=sys.stderr)
        return 2
    bridge.start()
    bridge.drain_until(lambda e: e.get("evt") == "ready", timeout_sec=5)
    bridge.send({"cmd": "connect", "host": host, "port": port})
    if not bridge.drain_until(lambda e: e.get("evt") == "connected", timeout_sec=15):
        print("MMS 连接失败", file=sys.stderr)
        bridge.stop()
        return 1
    bridge.send({"cmd": "read_comm_state", "ref": ds_ref})
    evt = bridge.drain_until(lambda e: e.get("evt") == "comm_state", timeout_sec=30)
    bridge.send({"cmd": "quit"})
    bridge.stop()
    if not evt:
        print("读通信工况超时", file=sys.stderr)
        return 1
    entries = evt.get("entries") or []
    connected = sum(1 for e in entries if e.get("valid") and e.get("stVal"))
    total = len(entries)
    print(f"OK {ds_ref} entries={total}")
    print(f"summary: {connected}/{total} connected")
    return 0


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--host", default=DEFAULT_HOST)
    p.add_argument("--port", type=int, default=DEFAULT_PORT)
    p.add_argument("--ds", default=DEFAULT_DS)
    p.add_argument("--probe-only", action="store_true")
    args = p.parse_args()

    if not tcp_probe(args.host, args.port):
        print(f"子站 MMS {args.host}:{args.port} 不可达", file=sys.stderr)
        return 1
    print(f"子站 MMS 端口 {args.host}:{args.port} 已监听")
    if args.probe_only:
        return 0
    return comm_state_via_cli(args.host, args.port, args.ds)


if __name__ == "__main__":
    raise SystemExit(main())
