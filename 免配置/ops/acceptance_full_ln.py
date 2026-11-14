#!/usr/bin/env python3
"""装置 full-ln 一条龙：部署 jar → 清除 stamp → 生成生产 cfg → 重启 router → bench → verify。

    set SUBST_PASS=...
    python ops/acceptance_full_ln.py
    python ops/acceptance_full_ln.py --skip-deploy
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEVICE_CID = "/home/admin/router/config/supervision.CID"
PROD_CFG = "/home/admin/router/config/supervision.cfg"
STAMP = PROD_CFG + ".stamp"
OUT_BASELINE = ROOT / "ops" / ".genconfig_baseline.json"


def main() -> int:
    if not os.environ.get("SUBST_PASS"):
        print("需要 SUBST_PASS（部署与 bench）", file=sys.stderr)
        return 1

    ap = argparse.ArgumentParser()
    ap.add_argument("--skip-deploy", action="store_true")
    args = ap.parse_args()

    ops = ROOT / "ops"
    steps: list[tuple[str, list[str]]] = []
    if not args.skip_deploy:
        steps.append(("deploy", [sys.executable, str(ops / "deploy_genconfig_fast.py")]))
    steps.extend(
        [
            ("regen+restart", [sys.executable, str(ops / "_remote" / "regen_cfg_restart.py")]),
            ("bench", [sys.executable, str(ops / "deploy_genconfig_fast.py"), "--bench-full-ln"]),
            ("verify", [sys.executable, str(ops / "verify_fwd_dchg.py")]),
        ]
    )

    report: dict = {}
    for name, cmd in steps:
        print(f"=== {name} ===")
        r = subprocess.run(cmd, cwd=str(ROOT), env=os.environ.copy())
        report[name] = {"exit_code": r.returncode}
        if r.returncode != 0:
            print(f"FAIL at step {name}", file=sys.stderr)
            _write_report(report)
            return r.returncode

    _write_report(report)
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0


def _write_report(report: dict) -> None:
    data = {}
    if OUT_BASELINE.is_file():
        data = json.loads(OUT_BASELINE.read_text(encoding="utf-8"))
    data["acceptance_full_ln"] = report
    OUT_BASELINE.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


if __name__ == "__main__":
    raise SystemExit(main())
