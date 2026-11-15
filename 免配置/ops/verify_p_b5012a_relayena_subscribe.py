#!/usr/bin/env python3
"""P_B5012A 软压板 brcbRelayEna：主站订阅 + 子站 RptEna 与 FZ 订阅状况对照。

用法（实验室）:
  set SUBST_HOST=192.168.0.10
  python ops/verify_p_b5012a_relayena_subscribe.py

输出说明:
  - CLI rcb_enabled 应含 verified 未 false，且 handler/getRef/dsRef 与下表一致
  - 若子站 RptEna 已 true 而 FZ「订阅状况」仍为 -，多为 router 未把 RCB 写落到 ser501
    （见 诊断-主站订阅召唤失败-修复记录.md、ops/_remote/write_mech.py）
"""
from __future__ import annotations

import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
CLI = os.path.join(ROOT, "master_sim", "bin", "mms_master_cli.exe")
HOST = os.environ.get("SUBST_HOST", "192.168.0.10")
IED = os.environ.get("VERIFY_IED", "P_B5012A")
LD = f"{IED}PROT"
HANDLER = f"{LD}/LLN0$BR$brcbRelayEna"
GET_REF = f"{LD}/LLN0$BR$brcbRelayEna01"
DS_REF = f"{LD}/LLN0$dsRelayEna"
FZ_UNIX = os.environ.get("FZ_UNIX_SOCK", r"\\.\pipe\fz_ser501_P_B5012A")


def run_cli(cmds: list[dict], timeout: int = 120) -> list[str]:
    if not os.path.isfile(CLI):
        raise SystemExit(f"missing CLI: {CLI}")
    payload = "\n".join(json.dumps(c, ensure_ascii=False) for c in cmds) + "\n"
    p = subprocess.run(
        [CLI],
        input=payload,
        capture_output=True,
        text=True,
        timeout=timeout,
        encoding="utf-8",
        errors="replace",
    )
    lines = (p.stdout or "").splitlines()
    if p.returncode != 0:
        print(p.stderr or "", file=sys.stderr)
    return lines


def parse_events(lines: list[str]) -> list[dict]:
    out = []
    for ln in lines:
        ln = ln.strip()
        if not ln.startswith("{"):
            continue
        try:
            out.append(json.loads(ln))
        except json.JSONDecodeError:
            pass
    return out


def read_fz_subscr_body() -> str:
    """Windows 上经 scdpro unix_connect 同源管道读 ser501 订阅列表（可选）。"""
    try:
        import socket

        s = socket.socket(socket.AF_UNIX if hasattr(socket, "AF_UNIX") else socket.AF_INET, socket.SOCK_STREAM)
        if hasattr(socket, "AF_UNIX"):
            s.connect(FZ_UNIX.replace("\\\\.\\pipe\\", "/") if FZ_UNIX.startswith("\\\\") else FZ_UNIX)
        else:
            return ""
        s.sendall(b"RPT_SUBSCR\n")
        data = s.recv(65536)
        s.close()
        return data.decode("utf-8", errors="replace")
    except Exception as exc:
        return f"(FZ body unreadable: {exc})"


def main() -> int:
    print("Target:", HANDLER)
    print("  getRef:", GET_REF)
    print("  dsRef:", DS_REF)
    lines = run_cli(
        [
            {"cmd": "connect", "host": HOST, "port": 102, "timeoutMs": 60000},
            {
                "cmd": "enable_rcb",
                "handlerRef": HANDLER,
                "rcbGetRef": GET_REF,
                "dsRef": DS_REF,
                "timeoutMs": 90000,
            },
            {"cmd": "disable_rcb", "handlerRef": HANDLER, "rcbGetRef": GET_REF},
            {"cmd": "quit"},
        ],
        180,
    )
    evts = parse_events(lines)
    enabled = [e for e in evts if e.get("evt") == "rcb_enabled"]
    errors = [e for e in evts if e.get("evt") == "error"]
    datasets = [e for e in evts if e.get("evt") == "dataset" and e.get("source") == "subscribe_snapshot"]

    print("\n-- CLI enable_rcb --")
    for e in enabled:
        print(json.dumps(e, ensure_ascii=False))
    for e in errors:
        print("ERROR:", json.dumps(e, ensure_ascii=False))
    if datasets:
        print("subscribe_snapshot rows:", len(datasets[0].get("rows", [])))

    ok = bool(enabled) and enabled[-1].get("verified") is not False and not any(
        e for e in errors if "RptEna verify" in str(e.get("msg", ""))
    )
    print("\n-- FZ / ser501 subscription body (optional) --")
    body = read_fz_subscr_body()
    print(body[:2000] if body else "(empty)")
    want = "dsRelayEna"
    if want in body and "brcbRelayEna" in body:
        print("FZ: found dsRelayEna|brcbRelayEna in body")
    else:
        print("FZ: no matching RelayEna subscription line (check router forward)")

    if ok:
        print("\nPASS: CLI subscribe verified")
        return 0
    print("\nFAIL: enable not verified or errors present", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())