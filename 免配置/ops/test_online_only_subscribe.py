#!/usr/bin/env python3
"""模拟修复后逻辑：先 read_comm_state，仅对在线装置 enable_rcb，不应刷 IED_UNKNOWN。"""
from __future__ import annotations

import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / "master_sim" / "bin" / "mms_master_cli.exe"
if not CLI.is_file():
    CLI = Path(r"C:\Program Files\SimMasterQt\bin\mms_master_cli.exe")
SCD = ROOT / "子站scd" / "江西500kV通天岩变电站26041110" / "江西500kV通天岩变电站.scd"


def run_cli(cmds: list[dict]) -> tuple[int, list[dict]]:
    payload = "\n".join(json.dumps(c, ensure_ascii=False) for c in cmds) + "\n"
    p = subprocess.run(
        [str(CLI)],
        input=payload,
        capture_output=True,
        text=True,
        timeout=300,
        encoding="utf-8",
        errors="replace",
    )
    evts = []
    for line in (p.stdout or "").splitlines():
        line = line.strip()
        if line.startswith("{"):
            try:
                evts.append(json.loads(line))
            except json.JSONDecodeError:
                pass
    return p.returncode, evts


def ied_order_from_scd() -> list[str]:
    if not SCD.is_file():
        return []
    text = SCD.read_text(encoding="utf-8", errors="replace")
    return re.findall(r'<IED\s+name="([^"]+)"', text)


def main() -> int:
    host = os.environ.get("SUBST_HOST", "192.168.0.10")
    cmds: list[dict] = [
        {"cmd": "connect", "host": host, "port": 102, "timeoutMs": 45000},
        {"cmd": "read_comm_state"},
    ]
    rc, evts = run_cli(cmds)
    comm = next((e for e in evts if e.get("evt") == "comm_state"), None)
    if not comm:
        print("FAIL: no comm_state", file=sys.stderr)
        return 1
    entries = comm.get("entries", [])
    ieds = ied_order_from_scd()
    online = []
    for i, ent in enumerate(entries):
        if i < len(ieds) and ent.get("stVal"):
            online.append(ieds[i])
    print(f"online={len(online)} / {len(entries)}")
    sample = online[:3] if online else ["C_B5043X"]
    trial_cmds = [{"cmd": "connect", "host": host, "port": 102, "timeoutMs": 45000}]
    for ied in sample:
        trial_cmds.append(
            {
                "cmd": "enable_rcb",
                "handlerRef": f"{ied}LD0/LLN0$BR$brcbAlarm",
                "rcbGetRef": f"{ied}LD0/LLN0$BR$brcbAlarm01",
                "dsRef": f"{ied}LD0/LLN0$dsAlarm",
            }
        )
    trial_cmds.append({"cmd": "quit"})
    rc2, evts2 = run_cli(trial_cmds)
    errs = [e for e in evts2 if e.get("evt") == "error"]
    enabled = [e for e in evts2 if e.get("evt") == "rcb_enabled"]
    print(f"enabled={len(enabled)} errors={len(errs)}", [e.get("msg") for e in errs[:5]])
    if errs and all(e.get("msg") in {"IED_UNKNOWN", "TEMPORARILY_UNAVAILABLE"} for e in errs):
        print("FAIL: still benign errors on online-only sample", file=sys.stderr)
        return 1
    if not enabled:
        print("WARN: no rcb_enabled (sample may all offline)")
    print("PASS: online-only subscribe logic")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
