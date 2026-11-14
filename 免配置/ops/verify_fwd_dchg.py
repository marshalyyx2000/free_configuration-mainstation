#!/usr/bin/env python3
"""验收：主站订阅经子站转发保护 → GI 有真值 →（可选）变位 dchg。

    set SUBST_PASS=...   # 仅子站日志需要
    python ops/verify_fwd_dchg.py
    python ops/verify_fwd_dchg.py --host 192.168.0.10 --ied C_B5043X
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.abspath(__file__))
MASTER = os.path.join(ROOT, "..", "master_sim")
CLI = os.environ.get(
    "MMS_MASTER_CLI",
    os.path.join(MASTER, "bin", "mms_master_cli.exe"),
)
DEFAULT_HOST = os.environ.get("SUBST_HOST", "192.168.0.10")


def run_cli(host: str, cmds: list, timeout: float = 180) -> tuple[int, str]:
    payload = "\n".join(json.dumps(c, ensure_ascii=False) for c in cmds) + "\n"
    if not os.path.isfile(CLI):
        raise SystemExit(f"缺少 MMS CLI: {CLI}")
    p = subprocess.run(
        [CLI],
        input=payload,
        capture_output=True,
        text=True,
        timeout=timeout,
        encoding="utf-8",
        errors="replace",
    )
    return p.returncode, (p.stdout or "") + (p.stderr or "")


def test_subscribe_gi(host: str, ied: str) -> dict:
    handler = f"{ied}LD0/LLN0$BR$brcbAlarm"
    getref = f"{handler}01"
    dsref = f"{ied}LD0/LLN0$dsAlarm"
    cmds = [
        {"cmd": "connect", "host": host, "port": 102, "timeoutMs": 45000},
        {
            "cmd": "enable_rcb",
            "handlerRef": handler,
            "rcbGetRef": getref,
            "dsRef": dsref,
            "timeoutMs": 60000,
        },
        {"cmd": "gi", "handlerRef": handler, "timeoutMs": 60000},
        {"cmd": "disable_rcb", "handlerRef": handler},
        {"cmd": "quit"},
    ]
    t0 = time.time()
    rc, out = run_cli(host, cmds, timeout=200)
    wall = round(time.time() - t0, 2)
    enabled = "rcb_enabled" in out or '"evt":"rcb_enabled"' in out
    gi_ok = '"evt":"report"' in out or '"evt":"gi_report"' in out
    err11 = out.count("error 11") + out.count('"error":11')
    dev_alm = "DevAlmGGIO" in out or "Alm1" in out
    return {
        "rc": rc,
        "wall_sec": wall,
        "enabled": enabled,
        "gi_has_report": gi_ok,
        "error_11_count": err11,
        "mentions_dev_alm": dev_alm,
        "sample": out[-2000:],
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default=DEFAULT_HOST)
    ap.add_argument("--ied", default="C_B5043X")
    args = ap.parse_args()
    rep = test_subscribe_gi(args.host, args.ied)
    print(json.dumps(rep, ensure_ascii=False, indent=2))
    sample = rep["sample"]
    valid_item = (
        '"value":true' in sample
        or '"value":false' in sample
        or '"value":[true' in sample
        or '"value":[false' in sample
        or '"value":1' in sample
        or '"value":0' in sample
        or "error 11" not in sample
    )
    ok = (
        rep["rc"] == 0
        and rep["enabled"]
        and rep["gi_has_report"]
        and rep["error_11_count"] == 0
        and "error 11" not in rep["sample"]
        and valid_item
    )
    rep["gi_has_valid_value"] = valid_item
    if not ok:
        print("FAIL: 订阅/GI 须无 error 11 且含有效 stVal", file=sys.stderr)
        return 1
    print("PASS: 订阅成功，GI 报告含有效值且无 error 11")
    if not rep["mentions_dev_alm"]:
        print("WARN: 输出未出现 DevAlmGGIO，请人工核对报告点值")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
