"""分步探测 MMS：每步独立进程 + 独立超时，定位卡在哪条命令。"""
from __future__ import annotations

import json
import os
import subprocess
import sys
import time
from pathlib import Path

CLI = Path(__file__).resolve().parents[1] / "master_sim" / "bin" / "mms_master_cli.exe"
HOST = os.environ.get("SUBST_HOST", "192.168.0.10")


def step(name: str, cmds: list[dict], timeout: float) -> None:
    payload = "\n".join(json.dumps(c, ensure_ascii=False) for c in cmds) + "\n"
    t0 = time.perf_counter()
    try:
        proc = subprocess.run(
            [str(CLI)], input=payload, capture_output=True, text=True,
            timeout=timeout, encoding="utf-8", errors="replace",
        )
        lines = (proc.stdout or "").splitlines()
        rc = proc.returncode
    except subprocess.TimeoutExpired as exc:
        lines = [f"<TIMEOUT after {timeout:.0f}s>"]
        raw = exc.stdout
        if raw:
            if isinstance(raw, bytes):
                raw = raw.decode("utf-8", "replace")
            lines += raw.splitlines()
        rc = -1
    print(f"--- {name} ({time.perf_counter() - t0:.1f}s, rc={rc})")
    for ln in lines[-12:]:
        print("   ", ln[:300])
    sys.stdout.flush()


def main() -> int:
    conn = {"cmd": "connect", "host": HOST, "port": 102, "timeoutMs": 30000}
    step("connect", [conn, {"cmd": "quit"}], 60)
    step("get_ld_list", [conn, {"cmd": "get_ld_list", "timeoutMs": 60000}, {"cmd": "quit"}], 120)
    step("get_datasets", [conn, {"cmd": "get_datasets", "ld": "P_X5071APROT", "timeoutMs": 60000}, {"cmd": "quit"}], 120)
    step("read_dsWarning", [conn, {"cmd": "read_dataset", "ref": "P_X5071APROT/LLN0$dsWarning", "timeoutMs": 60000}, {"cmd": "quit"}], 120)
    step("read_comm_state", [conn, {"cmd": "read_comm_state", "timeoutMs": 60000}, {"cmd": "quit"}], 120)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
