#!/usr/bin/env python3
"""记录/跑 genconfig 基准（本机或 SSH 到子站）。

    set SUBST_PASS=...
    python ops/bench_genconfig.py --remote
    python ops/bench_genconfig.py --local SMCD supervision.cfg
    python ops/bench_genconfig.py --remote-baseline
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MASTER = ROOT / "master_sim"
sys.path.insert(0, str(MASTER))

DEFAULT_JAR = MASTER / "third_party/libiec61850/tools/model_generator/genconfig-fast.jar"
DEVICE_JAR = Path(os.environ.get("GENCONFIG_JAR", "/home/admin/router/bin/genconfig.jar"))
DEVICE_CID = "/home/admin/router/config/supervision.CID"
OUT_BASELINE = ROOT / "ops" / ".genconfig_baseline.json"
JAVA_FLAGS = "-Xms128m -Xmx512m -XX:+UseSerialGC -XX:+ExitOnOutOfMemoryError"


def run_genconfig(java: str, jar: Path, scl: Path, cfg: Path, timeout: float, env: dict | None = None) -> dict:
    cfg.parent.mkdir(parents=True, exist_ok=True)
    if cfg.is_file():
        cfg.unlink()
    t0 = time.perf_counter()
    cmd = java.split() + ["-jar", str(jar), str(scl), str(cfg)]
    try:
        r = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout,
            env=env,
        )
        wall = time.perf_counter() - t0
        size = cfg.stat().st_size if cfg.is_file() else 0
        return {
            "ok": r.returncode == 0 and size > 0,
            "exit_code": r.returncode,
            "wall_sec": round(wall, 2),
            "cfg_bytes": size,
            "devalm_ln_count": count_cfg_ln_devalm(cfg),
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


def count_cfg_ln_devalm(cfg: Path) -> int:
    if not cfg.is_file():
        return 0
    try:
        text = cfg.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return 0
    return len(re.findall(r"LN\([^)]*DevAlmGGIO", text))


def remote_full_bench(jar_path: str, genconfig_mode: str | None = None) -> dict:
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
    mode_env = f"GENCONFIG_MODE={genconfig_mode} " if genconfig_mode else ""
    cmd = (
        f"CID={DEVICE_CID}; OUT=/tmp/bench_supervision.cfg; rm -f $OUT $OUT.stamp; "
        f"ls -l $CID 2>/dev/null; "
        f"T0=$(date +%s); {mode_env}java {JAVA_FLAGS} -jar {jar_path} $CID $OUT "
        f">/tmp/bench_gen.log 2>&1; EC=$?; T1=$(date +%s); "
        f"echo EXIT:$EC; echo WALL:$((T1-T0)); "
        f"ls -l $OUT 2>/dev/null; "
        f"grep -c 'LN(DevAlmGGIO' $OUT 2>/dev/null || echo DEVALM:0; "
        f"tail -8 /tmp/bench_gen.log 2>/dev/null"
    )
    _stdin, stdout, stderr = client.exec_command(cmd, timeout=900)
    out = stdout.read().decode("utf-8", errors="replace")
    err = stderr.read().decode("utf-8", errors="replace")
    client.close()
    wall = None
    exit_code = None
    devalm = 0
    cfg_bytes = 0
    for line in (out + err).splitlines():
        if line.startswith("WALL:"):
            try:
                wall = float(line.split(":", 1)[1])
            except ValueError:
                pass
        if line.startswith("EXIT:"):
            try:
                exit_code = int(line.split(":", 1)[1])
            except ValueError:
                pass
        if line.startswith("DEVALM:"):
            try:
                devalm = int(line.split(":", 1)[1])
            except ValueError:
                pass
        if "bench_supervision.cfg" in line and " " in line:
            parts = line.split()
            for p in parts:
                if p.isdigit() and int(p) > 1000:
                    cfg_bytes = int(p)
                    break
    ok = exit_code == 0 and wall is not None and cfg_bytes > 0
    return {
        "host": host,
        "cid_path": DEVICE_CID,
        "jar": jar_path,
        "genconfig_mode": genconfig_mode or "full-ln",
        "raw_output": out,
        "raw_stderr": err,
        "wall_sec": wall,
        "exit_code": exit_code,
        "cfg_bytes": cfg_bytes,
        "devalm_ln_count": devalm,
        "ok": ok,
        "ok_under_300s": bool(ok and wall is not None and wall <= 300),
    }


def write_baseline(report: dict, key: str) -> None:
    data = {}
    if OUT_BASELINE.is_file():
        data = json.loads(OUT_BASELINE.read_text(encoding="utf-8"))
    data[key] = report
    OUT_BASELINE.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--remote", action="store_true", help="SSH 上跑 fast jar（full-ln）")
    p.add_argument("--remote-vendor", action="store_true", help="SSH 上跑原厂 jar（对照）")
    p.add_argument("--remote-baseline", action="store_true", help="full-ln / collapse / vendor 写入 baseline")
    p.add_argument("--local", nargs=2, metavar=("SCL", "OUT_CFG"))
    p.add_argument("--jar", type=Path, default=DEFAULT_JAR)
    p.add_argument("--timeout", type=float, default=600)
    args = p.parse_args()

    if args.remote:
        report = remote_full_bench(DEVICE_JAR.as_posix())
    elif args.remote_vendor:
        report = remote_full_bench(VENDOR_JAR.as_posix() if False else str(DEVICE_JAR))
        report["note"] = "vendor jar on device path"
    elif args.remote_baseline:
        fast = remote_full_bench(DEVICE_JAR.as_posix())
        collapse = remote_full_bench(DEVICE_JAR.as_posix(), "collapse")
        write_baseline(fast, "remote_full_ln_target")
        write_baseline(collapse, "remote_collapse_baseline")
        report = {"remote_full_ln_target": fast, "remote_collapse_baseline": collapse}
        write_baseline(
            {
                "host": fast.get("host"),
                "supervision_CID_bytes": 75730544,
                "note": "见 remote_full_ln_target / collapse",
            },
            "remote_device_collapsed_cid",
        )
    elif args.local:
        smcd, cfg = Path(args.local[0]), Path(args.local[1])
        env = os.environ.copy()
        env.setdefault("GENCONFIG_MODE", "full-ln")
        java = f"java {JAVA_FLAGS}"
        report = run_genconfig(java, args.jar, smcd, cfg, args.timeout, env=env)
        report["mode"] = "local_full_ln"
        report["scl"] = str(smcd)
    else:
        p.print_help()
        return 1

    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0 if report.get("ok", True) else 1


if __name__ == "__main__":
    raise SystemExit(main())
