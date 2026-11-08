"""检查子站模型：SMCD 中 SubStation 的 LDevice inst 与 cfg 中 LD 命名、dsCommState 归属。"""
from __future__ import annotations

import os
import re
import sys

import paramiko

HOST = os.environ.get("SUBST_HOST", "192.168.0.10")
USER = os.environ.get("SUBST_USER", "admin")
PASS = os.environ.get("SUBST_PASS", "")
SMCD = "/home/ipofas/stdown/maintain/SMCD/station.SMCD"
CFG = "/home/admin/router/config/supervision.cfg"


def ssh() -> paramiko.SSHClient:
    c = paramiko.SSHClient()
    c.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    c.connect(HOST, username=USER, password=PASS, timeout=15,
              allow_agent=False, look_for_keys=False)
    return c


def main() -> int:
    if not PASS:
        print("请先设置 SUBST_PASS", file=sys.stderr)
        return 1
    c = ssh()
    sftp = c.open_sftp()

    # SMCD: 找 SubStation IED 的 LDevice inst
    with sftp.open(SMCD, "rb") as fh:
        data = fh.read(40 * 1024 * 1024).decode("utf-8", "replace")
    i = data.find('name="SubStation"')
    print("SubStation IED found at", i)
    if i >= 0:
        seg = data[i:i + 4000]
        print(re.findall(r'<LDevice[^>]*>', seg)[:4])
        print(re.findall(r'<LN0[^>]*>', seg)[:2])

    # cfg: LD 与 dsCommState 归属
    with sftp.open(CFG, "rb") as fh:
        cfg = fh.read().decode("utf-8", "replace")
    print("cfg MODEL:", re.findall(r"^MODEL\((\w+)\)", cfg, re.M)[:3])
    lds = re.findall(r"^LD\((\w+)\)", cfg, re.M)
    print("cfg LD count:", len(lds), "first:", lds[:5])
    print("cfg has SubStationLD0:", "LD(SubStationLD0)" in cfg)
    print("cfg has LD(LD0):", "LD(LD0)" in cfg)

    # dsCommState 出现位置
    for m in re.finditer(r"^DS\(dsCommState\)\{", cfg, re.M):
        start = m.start()
        # 向上找最近的 LD(
        head = cfg[:start]
        ld = re.findall(r"^LD\((\w+)\)", head, re.M)
        ln = re.findall(r"^LN\((\w+)\)", head, re.M)
        print(f"dsCommState at {start}: LD={ld[-1] if ld else None} LN={ln[-1] if ln else None}")

    sftp.close()
    c.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
