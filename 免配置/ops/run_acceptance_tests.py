#!/usr/bin/env python3
"""装置 full-ln 验收一键脚本（exit 0 = 全部通过）。

    set SUBST_PASS=...   # 校验 cfg 尺寸时需要
    python ops/run_acceptance_tests.py
"""
from __future__ import annotations

import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OPS = ROOT / "ops"


def run(cmd: list[str], timeout: float = 200) -> tuple[int, str]:
    p = subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=timeout,
        cwd=str(ROOT),
        env=os.environ.copy(),
    )
    return p.returncode, (p.stdout or "") + (p.stderr or "")


def check_cfg_remote() -> dict:
    if not os.environ.get("SUBST_PASS"):
        return {"ok": False, "skip": True, "reason": "no SUBST_PASS"}
    import paramiko

    host = os.environ.get("SUBST_HOST", "192.168.0.10")
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(
        host,
        username=os.environ.get("SUBST_USER", "admin"),
        password=os.environ["SUBST_PASS"],
        timeout=15,
        allow_agent=False,
        look_for_keys=False,
    )
    cmd = (
        "CFG=/home/admin/router/config/supervision.cfg; "
        "ls -l $CFG; grep -c '^RC(' $CFG; grep -c 'LN(DevAlmGGIO' $CFG"
    )
    _stdin, stdout, _stderr = client.exec_command(cmd, timeout=60)
    out = stdout.read().decode("utf-8", "replace")
    client.close()
    size = 0
    rc = devalm = 0
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 5 and parts[-1].endswith("supervision.cfg"):
            try:
                size = int(parts[4])
            except ValueError:
                pass
        elif parts and parts[0].isdigit():
            n = int(parts[0])
            if n > 1000 and size == 0:
                size = n
            elif rc == 0 and n < 10000:
                rc = n
            else:
                devalm = n
    ok = size >= 5_000_000 and devalm >= 1 and rc >= 2000
    return {"ok": ok, "cfg_bytes": size, "rc_count": rc, "devalm_ln_count": devalm, "raw": out.strip()}


def check_fwd_dchg() -> dict:
    rc, out = run([sys.executable, str(OPS / "verify_fwd_dchg.py")])
    rep = {}
    for line in out.splitlines():
        line = line.strip()
        if line.startswith("{") and line.endswith("}"):
            try:
                rep = json.loads(line)
                break
            except json.JSONDecodeError:
                continue
    ok = (
        rc == 0
        and bool(rep.get("enabled"))
        and rep.get("error_11_count", 99) == 0
        and rep.get("gi_has_report")
    )
    if rc == 0 and "PASS:" in out:
        ok = True
    return {"ok": ok, "exit_code": rc, "report": rep}


def check_mms_smoke() -> dict:
    cli = ROOT / "master_sim" / "bin" / "mms_master_cli.exe"
    if not cli.is_file():
        return {"ok": False, "reason": "missing mms_master_cli.exe"}
    host = os.environ.get("SUBST_HOST", "192.168.0.10")
    cmds = [
        {"cmd": "connect", "host": host, "port": 102, "timeoutMs": 45000},
        {"cmd": "get_ld_list"},
        {"cmd": "read_comm_state"},
        {"cmd": "quit"},
    ]
    payload = "\n".join(json.dumps(c, ensure_ascii=False) for c in cmds) + "\n"
    p = subprocess.run(
        [str(cli)],
        input=payload,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=120,
    )
    out = p.stdout or ""
    ok = "connected" in out.lower() and "error" not in out.lower().split("quit")[0]
    ld_ok = '"evt":"ld_list"' in out or "ld_list" in out
    comm = "read_comm_state" in out or "comm" in out
    return {"ok": ok and ld_ok and comm, "exit_code": p.returncode}


def main() -> int:
    report = {
        "cfg_remote": check_cfg_remote(),
        "fwd_dchg": check_fwd_dchg(),
        "mms_smoke": check_mms_smoke(),
    }
    all_ok = all(
        v.get("ok")
        for v in report.values()
        if not v.get("skip")
    )
    report["all_ok"] = all_ok
    print(json.dumps(report, ensure_ascii=False, indent=2))
    if all_ok:
        print("ACCEPTANCE PASS")
        return 0
    print("ACCEPTANCE FAIL", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
