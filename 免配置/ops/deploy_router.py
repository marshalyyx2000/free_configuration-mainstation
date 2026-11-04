#!/usr/bin/env python3
"""把本地改过的子站源码同步到装置并重新编译（凭证走环境变量）。

    set SUBST_PASS=...
    python ops/deploy_router.py
    python ops/deploy_router.py --skip-build
"""
from __future__ import annotations

import argparse
import hashlib
import os
import posixpath
import sys

import paramiko

HOST = os.environ.get("SUBST_HOST", "192.168.0.10")
USER = os.environ.get("SUBST_USER", "admin")
PASS = os.environ.get("SUBST_PASS", "")
REMOTE_ROOT = "/home/admin/src/configuration-free/iec61850_router"
LOCAL_ROOT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                          "configuration-free", "iec61850_router")

FILES = [
    "src/server/config/config_generator.cpp",
    "src/server/config/config_generator.h",
    "src/server/server_controller.cpp",
    "src/server/server_controller.h",
]


def md5(data: bytes) -> str:
    return hashlib.md5(data).hexdigest()


def connect() -> paramiko.SSHClient:
    if not PASS:
        raise SystemExit("请先设置 SUBST_PASS")
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(HOST, username=USER, password=PASS, timeout=15,
                   allow_agent=False, look_for_keys=False)
    return client


def run(client: paramiko.SSHClient, cmd: str, timeout: int = 300) -> int:
    _stdin, stdout, stderr = client.exec_command(cmd, timeout=timeout)
    for line in stdout:
        sys.stdout.write(line)
    err = stderr.read().decode("utf-8", "replace")
    if err.strip():
        sys.stderr.write(err)
    return stdout.channel.recv_exit_status()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--install", action="store_true", help="build 后覆盖到 ~/router/bin")
    args = parser.parse_args()

    client = connect()
    sftp = client.open_sftp()
    changed = []
    try:
        for rel in FILES:
            local = os.path.join(LOCAL_ROOT, rel.replace("/", os.sep))
            remote = posixpath.join(REMOTE_ROOT, rel)
            if not os.path.isfile(local):
                print(f"跳过（本地不存在）: {rel}")
                continue
            with open(local, "rb") as f:
                data = f.read()
            try:
                with sftp.open(remote, "rb") as f:
                    remote_data = f.read()
            except IOError:
                remote_data = b""
            if md5(data) == md5(remote_data):
                print(f"未变化: {rel}")
                continue
            sftp.put(local, remote)
            changed.append(rel)
            print(f"已上传: {rel}")
    finally:
        sftp.close()

    if not changed:
        print("没有文件需要上传")
    if args.skip_build:
        client.close()
        return 0

    print("=== 编译 ===")
    rc = run(client, f"cd {REMOTE_ROOT} && bash build.sh 2>&1 | tail -n 40", timeout=1800)
    if rc != 0:
        print(f"编译失败 rc={rc}")
        client.close()
        return rc

    if args.install:
        print("=== 安装到 ~/router/bin ===")
        run(client, "pkill -f 'iec61850router' || true; sleep 1", timeout=30)
        run(client, f"cp -f {REMOTE_ROOT}/build/bin/iec61850router /home/admin/router/bin/iec61850router "
                    f"&& cp -f {REMOTE_ROOT}/build/bin/iec61850model /home/admin/router/bin/iec61850model 2>/dev/null; "
                    f"chmod +x /home/admin/router/bin/iec61850router; "
                    f"ls -l /home/admin/router/bin/iec61850router", timeout=60)

    client.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
