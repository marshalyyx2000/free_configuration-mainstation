"""在子站上执行一段 python 脚本（上传到 /tmp 后运行），避免 PowerShell 转义问题。"""
from __future__ import annotations

import os
import sys
from pathlib import Path

import paramiko

HOST = os.environ.get("SUBST_HOST", "192.168.0.10")
USER = os.environ.get("SUBST_USER", "admin")
PASS = os.environ.get("SUBST_PASS", "")


def main() -> int:
    if not PASS:
        print("请先设置 SUBST_PASS", file=sys.stderr)
        return 1
    script_path = Path(sys.argv[1])
    code = script_path.read_text(encoding="utf-8")
    remote = f"/tmp/_remote_{script_path.stem}.py"

    c = paramiko.SSHClient()
    c.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    c.connect(HOST, username=USER, password=PASS, timeout=15,
              allow_agent=False, look_for_keys=False)
    sftp = c.open_sftp()
    with sftp.open(remote, "w") as fh:
        fh.write(code.encode("utf-8"))
    sftp.close()

    cmd = f"python3 {remote} " + " ".join(sys.argv[2:])
    _in, out, err = c.exec_command(cmd, timeout=int(os.environ.get("SUBST_TIMEOUT", "900")))
    o = out.read().decode("utf-8", "replace")
    e = err.read().decode("utf-8", "replace")
    rc = out.channel.recv_exit_status()
    print(o)
    if e.strip():
        print("[stderr]", e)
    c.close()
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
