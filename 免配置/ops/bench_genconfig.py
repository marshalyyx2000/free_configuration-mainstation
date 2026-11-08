#!/usr/bin/env python3
"""记录/跑 genconfig 基准（本机或 SSH 到子站）。

    set SUBST_PASS=...
    python ops/bench_genconfig.py --remote
    python ops/bench_genconfig.py --local SMCD supervision.cfg
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MASTER = ROOT / "master_sim"
sys.path.insert(0, str(MASTER))

DEFAULT_JAR = MASTER / "third_party/libiec61850/tools/model_generator/genconfig.jar"
DEVICE_JAR = Path(os.environ.get("GENCONFIG_JAR", "/home/admin/router/bin/genconfig.jar"))
DEVICE_CID = "/home/admin/router/config/supervision.CID"
OUT_BASELINE = ROOT / "ops" / ".genconfig_baseline.json"


def run_genconfig(java: str, jar: Path, scl: Path, cfg: Path, timeout: float) -> dict:
    cfg.parent.mkdir(parents=True, exist_ok=True)
    if cfg.is_file():
        cfg.unlink()
    t0 = time.perf_counter()
    try:
        r = subprocess.run(
            [java, "-jar", str(jar), str(scl), str(cfg)],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout,
        )
        wall = time.perf_counter() - t0
        size = cfg.stat().st_size if cfg.is_file() else 0
        return {
            "ok": r.returncode == 0 and size > 0,
            "exit_code": r.returncode,
            "wall_sec": round(wall, 2),
            "cfg_bytes": size,
            "stderr_tail": (r.stderr or "")[-500:],
            "stdout_tail": (r.stdout or "")[-500:],
        }
    except subprocess.TimeoutExpired:
        return {
            "ok": False,
            "exit_code": -1,
            "wall_sec": round(time.perf_counter() - t0, 2),
            "cfg_bytes": 0,
            "error": f"timeout after {timeout}s",
        }


def remote_full_bench() -> dict:
    if not os.environ.get("SUBST_PASS"):
        raise SystemExit("需要 SUBST_PASS")
    import paramiko

    host = os.environ.get("SUBST_HOST", "192.168.0.10")
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(
        host, username=os.environ.get("SUBST_USER", "admin"),
        password=os.environ["SUBST_PASS"], timeout=15,
        allow_agent=False, look_for_keys=False,
    )
    cmd = (
        f"CID={DEVICE_CID}; OUT=/tmp/bench_supervision.cfg; rm -f $OUT; "
        f"ls -l $CID 2>/dev/null; "
        f"T0=$(date +%s); java -jar {DEVICE_JAR} $CID $OUT >/tmp/bench_gen.log 2>&1; "
        f"EC=$?; T1=$(date +%s); echo EXIT:$EC; echo WALL:$((T1-T0)); "
        f"ls -l $OUT 2>/dev/null; tail -5 /tmp/bench_gen.log 2>/dev/null"
    )
    _stdin, stdout, stderr = client.exec_command(cmd, timeout=900)
    out = stdout.read().decode("utf-8", errors="replace")
    err = stderr.read().decode("utf-8", errors="replace")
    client.close()
    wall = None
    for line in (out + err).splitlines():
        if line.startswith("WALL:"):
            try:
                wall = float(line.split(":", 1)[1])
            except ValueError:
                pass
    return {
        "mode": "remote_full",
        "host": host,
        "cid_path": DEVICE_CID,
        "jar": str(DEVICE_JAR),
        "raw_output": out,
        "raw_stderr": err,
        "wall_sec": wall,
        "noted": "全量 CID；大站常 >300s 或失败 exit 256",
    }


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--remote", action="store_true", help="SSH 上跑全量 genconfig（最长 15 分钟）")
    p.add_argument("--local", nargs=2, metavar=("SCL", "OUT_CFG"))
    p.add_argument("--jar", type=Path, default=DEFAULT_JAR)
    p.add_argument("--timeout", type=float, default=600)
    p.add_argument("--write-baseline", action="store_true", default=True)
    args = p.parse_args()

    if args.remote:
        report = remote_full_bench()
    elif args.local:
        smcd, cfg = Path(args.local[0]), Path(args.local[1])
        report = run_genconfig("java", args.jar, smcd, cfg, args.timeout)
        report["mode"] = "local"
        report["scl"] = str(smcd)
    else:
        p.print_help()
        return 1

    print(json.dumps(report, ensure_ascii=False, indent=2))
    if args.write_baseline:
        OUT_BASELINE.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(f"baseline -> {OUT_BASELINE}", file=sys.stderr)
    return 0 if report.get("ok") else 1


if __name__ == "__main__":
    raise SystemExit(main())
