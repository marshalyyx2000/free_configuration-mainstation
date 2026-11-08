#!/usr/bin/env python3
"""下装配置到子站（凭证不在脚本里，走环境变量）。

    set SUBST_PASS=...
    python ops/download.py [host] [port]
"""
from __future__ import annotations

import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "master_sim"))

from iec104_client import Iec104MaintainClient, Maintain104Config  # noqa: E402

sys.path.insert(0, str(Path(__file__).resolve().parent))
from resolve_out_dir import resolve_out_dir, warn_if_lab_ledger  # noqa: E402

OUT = resolve_out_dir()


def main() -> int:
    host = sys.argv[1] if len(sys.argv) > 1 else "192.168.0.10"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 2405

    warn_if_lab_ledger(OUT)
    ied = (OUT / "IEDList.xml").read_bytes()
    zipp = (OUT / "station.zip").read_bytes()
    print(f"输出目录: {OUT}")
    print(f"IEDList.xml {len(ied)} B, station.zip {len(zipp)} B")

    client = Iec104MaintainClient(Maintain104Config(host=host, port=port, timeout=60.0))
    try:
        client.connect()
        print("下装 IEDList.xml ...")
        client.download_file("/CONF/IEDList.xml", ied, file_inf=1)
        print("下装 station.zip ...")
        client.download_file("/SMCD/station.zip", zipp, file_inf=2)
        print("生效 61850 ...")
        client.effect_61850()
        print("已下发生效命令，router 开始生成模型（约 8 分钟）")
    finally:
        client.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
