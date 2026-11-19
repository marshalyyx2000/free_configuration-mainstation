"""Integration: a whole-FC reference such as FunEna1[ST] is stVal, quality, time.

The device returns that CDC as a JSON array. The GUI labels it from the
data type; this check only proves the array still has all three members.
"""
import json
import subprocess
import sys
import threading
import time
from pathlib import Path

CLI = Path(r"C:\Temp\mms-cli-build\Release\mms_master_cli.exe")
HOST = "192.168.0.10"
PORT = 102
DS = "E1Q1SB26LD0/LLN0$dsRelayEna"


def main() -> int:
    if not CLI.is_file():
        print("missing", CLI)
        return 2
    proc = subprocess.Popen(
        [str(CLI)],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    lines = []
    lock = threading.Lock()

    def reader():
        assert proc.stdout is not None
        for line in proc.stdout:
            with lock:
                lines.append(line.rstrip("\n"))

    threading.Thread(target=reader, daemon=True).start()

    def send(obj):
        assert proc.stdin is not None
        proc.stdin.write(json.dumps(obj, ensure_ascii=False) + "\n")
        proc.stdin.flush()

    def wait_for(pred, timeout):
        end = time.time() + timeout
        while time.time() < end:
            with lock:
                snap = list(lines)
            if pred(snap):
                return snap
            time.sleep(0.05)
        with lock:
            return list(lines)

    try:
        send({"cmd": "connect", "host": HOST, "port": PORT})
        got = wait_for(lambda xs: any('"evt":"connected"' in x or '"evt":"error"' in x for x in xs), 20)
        if not any('"evt":"connected"' in x for x in got):
            print("connect failed", got[-5:])
            return 1
        send({"cmd": "read_dataset", "ref": DS})
        got = wait_for(
            lambda xs: any('"evt":"dataset"' in x and DS in x for x in xs) or any('"evt":"error"' in x for x in xs),
            30,
        )
        dataset = None
        for line in got:
            if '"evt":"dataset"' not in line or DS not in line:
                continue
            try:
                dataset = json.loads(line)
            except json.JSONDecodeError:
                continue
        if dataset is None:
            print("no dataset", got[-8:])
            return 1
        rows = dataset.get("rows") or []
        if not rows:
            print("empty dataset", dataset)
            return 1
        valued = 0
        for row in rows:
            value = row.get("value")
            if not isinstance(value, list) or len(value) < 3:
                print("row missing quality/time", row)
                return 1
            if value[0] in (None, "") or value[1] in (None, "") or value[2] in (None, ""):
                print("blank member", row)
                return 1
            valued += 1
        print(f"rows={len(rows)} valued={valued} sample={rows[0].get('value')}")
        return 0
    finally:
        try:
            send({"cmd": "quit"})
        except Exception:
            pass
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()


if __name__ == "__main__":
    sys.exit(main())
