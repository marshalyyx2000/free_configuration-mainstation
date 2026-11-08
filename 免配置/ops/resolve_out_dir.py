"""与 SimMasterQt GUI 一致的配置输出目录（避免误用 master_sim/out 里的旧 26579 台账）。"""
from __future__ import annotations

import os
from pathlib import Path


def resolve_out_dir() -> Path:
    env = os.environ.get("SIMMASTER_OUT_DIR", "").strip()
    if env:
        p = Path(env).expanduser()
        if p.is_dir():
            return p
    local = os.environ.get("LOCALAPPDATA", "").strip()
    if local:
        p = Path(local) / "SimMasterQt" / "out"
        if p.is_dir() and (p / "IEDList.xml").is_file():
            return p
    repo = Path(__file__).resolve().parents[1] / "master_sim" / "out"
    return repo


def warn_if_lab_ledger(path: Path) -> None:
    if not (path / "IEDList.xml").is_file():
        return
    text = (path / "IEDList.xml").read_text(encoding="utf-8", errors="replace")
    n10 = text.count("10.100.100.")
    n172 = text.count("172.16.")
    if n10 >= 5 and n172 == 0:
        print(
            f"警告: {path} 仍是仿真站 10.100.100 网段台账（{n10} 处）。"
            "请先对江西 SCD 在 GUI「生成配置」，或设置 SIMMASTER_OUT_DIR 指向 %LOCALAPPDATA%\\SimMasterQt\\out",
            file=__import__("sys").stderr,
        )
