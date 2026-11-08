#!/usr/bin/env python3
"""部署/回滚加速 genconfig.jar（FastGenconfigMain）。

    set SUBST_PASS=...
    python ops/deploy_genconfig_fast.py
    python ops/deploy_genconfig_fast.py --rollback
    python ops/deploy_genconfig_fast.py --bench
"""
from __future__ import annotations

import argparse
import hashlib
import os
import sys
import time
from pathlib import Path

import paramiko

ROOT = Path(__file__).resolve().parents[1]
FAST_JAR = ROOT / "master_sim/third_party/libiec61850/tools/model_generator/genconfig-fast.jar"
REMOTE_BIN = "/home/admin/router/bin"
REMOTE_JAR = f"{REMOTE_BIN}/genconfig.jar"
VENDOR_JAR = f"{REMOTE_BIN}/genconfig.jar.vendor"
DEVICE_CID = "/home/admin/router/config/supervision.CID"

HOST = os.environ.get("SUBST_HOST", "192.168.0.10")
USER = os.environ.get("SUBST_USER", "admin")
PASS = os.environ.get("SUBST_PASS", "")


def md5_file(path: Path) -> str:
    return hashlib.md5(path.read_bytes()).hexdigest()


def connect() -> paramiko.SSHClient:
    if not PASS:
        raise SystemExit("请先设置 SUBST_PASS")
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(HOST, username=USER, password=PASS, timeout=15,
                   allow_agent=False, look_for_keys=False)
    return client


def run(client: paramiko.SSHClient, cmd: str, timeout: int = 600) -> tuple[int, str, str]:
    _stdin, stdout, stderr = client.exec_command(cmd, timeout=timeout)
    out = stdout.read().decode("utf-8", errors="replace")
    err = stderr.read().decode("utf-8", errors="replace")
    rc = stdout.channel.recv_exit_status()
    return rc, out, err


def remote_file_exists(sftp: paramiko.SFTPClient, path: str) -> bool:
    try:
        sftp.stat(path)
        return True
    except IOError:
        return False


def deploy() -> int:
    if not FAST_JAR.is_file():
        raise SystemExit(f"缺少加速 jar，请先运行 master_sim/tools/build_fast_genconfig.ps1: {FAST_JAR}")
    local_md5 = md5_file(FAST_JAR)
    client = connect()
    sftp = client.open_sftp()
    try:
        if not remote_file_exists(sftp, VENDOR_JAR):
            print(f"备份现网 jar -> {VENDOR_JAR}")
            rc, out, err = run(client, f"cp -a {REMOTE_JAR} {VENDOR_JAR}")
            if rc != 0 and "No such file" not in err:
                print(out, err, file=sys.stderr)
                return rc
        tmp = "/tmp/genconfig-fast.upload.jar"
        sftp.put(str(FAST_JAR), tmp)
        rc, out, err = run(
            client,
            f"cmp -s {tmp} {REMOTE_JAR} 2>/dev/null; "
            f"if [ $? -ne 0 ]; then cp -f {tmp} {REMOTE_JAR} && chmod 644 {REMOTE_JAR}; fi; "
            f"rm -f {tmp}; ls -l {REMOTE_JAR}; md5sum {REMOTE_JAR}",
        )
        print(out)
        if rc != 0:
            print(err, file=sys.stderr)
            return rc
        print(f"本地 MD5 {local_md5}")
    finally:
        sftp.close()
        client.close()
    return 0


def rollback() -> int:
    client = connect()
    rc, out, err = run(
        client,
        f"test -f {VENDOR_JAR} && cp -f {VENDOR_JAR} {REMOTE_JAR} && ls -l {REMOTE_JAR} "
        f"|| echo 'vendor backup missing'",
    )
    print(out)
    if err.strip():
        print(err, file=sys.stderr)
    client.close()
    return rc


def bench_fast() -> int:
    client = connect()
    cmd = (
        f"OUT=/tmp/bench_fast_supervision.cfg; rm -f $OUT; "
        f"ls -l {DEVICE_CID}; "
        f"T0=$(date +%s); java -jar {REMOTE_JAR} {DEVICE_CID} $OUT "
        f">/tmp/bench_fast.log 2>&1; EC=$?; T1=$(date +%s); "
        f"echo EXIT:$EC; echo WALL:$((T1-T0)); ls -l $OUT 2>/dev/null; tail -8 /tmp/bench_fast.log"
    )
    t0 = time.perf_counter()
    rc, out, err = run(client, cmd, timeout=600)
    wall = time.perf_counter() - t0
    print(out)
    if err:
        print(err, file=sys.stderr)
    for line in out.splitlines():
        if line.startswith("WALL:"):
            try:
                wall = float(line.split(":", 1)[1])
            except ValueError:
                pass
    ok = "EXIT:0" in out and wall <= 60.0
    print(f"bench_fast wall={wall:.1f}s ok={ok}")
    client.close()
    return 0 if ok else 1


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--rollback", action="store_true")
    p.add_argument("--bench", action="store_true", help="装置上跑加速 jar 计时")
    args = p.parse_args()
    if args.rollback:
        return rollback()
    if args.bench:
        return bench_fast()
    rc = deploy()
    if rc == 0 and args.bench is False:
        print("部署完成。可加 --bench 验收 ≤60s。")
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
