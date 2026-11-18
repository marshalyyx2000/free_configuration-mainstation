"""Integration: enable several RCBs, burst GI, and require a report with values.

A stalled stdout pipe used to block the report thread and the GI write never
left the PC, so a capture showed no MMS packets.
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
RCBS = [
    ("E1Q1SB20LD0/LLN0$BR$brcbWarning", "E1Q1SB20LD0/LLN0$dsWarning"),
    ("E1Q1SB20LD0/LLN0$BR$brcbAlarm", "E1Q1SB20LD0/LLN0$dsAlarm"),
    ("E1Q1SB20LD0/LLN0$BR$brcbCommState", "E1Q1SB20LD0/LLN0$dsCommState"),
]


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
        got = wait_for(lambda xs: any('"evt":"connected"' in x for x in xs), 20)
        if not any('"evt":"connected"' in x for x in got):
            print("connect failed", got[-5:])
            return 1
        for handler, ds in RCBS:
            send({
                "cmd": "enable_rcb",
                "handlerRef": handler,
                "rcbGetRef": handler + "01",
                "dsRef": ds,
            })
            got = wait_for(
                lambda xs, h=handler: any('"evt":"rcb_enabled"' in x and h in x for x in xs)
                or any('"evt":"error"' in x for x in xs),
                30,
            )
            if not any('"evt":"rcb_enabled"' in x and handler in x for x in got):
                print("enable failed", handler, got[-6:])
                return 1
        for handler, _ds in RCBS:
            send({"cmd": "gi", "handlerRef": handler})
        got = wait_for(
            lambda xs: sum(1 for x in xs if '"cmd":"gi"' in x and '"evt":"ok"' in x) >= len(RCBS)
            and any('"evt":"report"' in x and '"value":' in x for x in xs),
            40,
        )
        oks = [x for x in got if '"cmd":"gi"' in x and '"evt":"ok"' in x]
        reports = []
        valued = 0
        for x in got:
            if '"evt":"report"' not in x:
                continue
            try:
                obj = json.loads(x)
            except json.JSONDecodeError:
                continue
            reports.append(obj)
            for it in obj.get("items") or []:
                if it.get("value") not in (None, "", []):
                    valued += 1
        print(f"gi_ok={len(oks)} reports={len(reports)} valued_items={valued}")
        if len(oks) < len(RCBS) or valued < 1:
            print("last", got[-8:])
            return 1
        print("ok gi_wire")
        return 0
    finally:
        try:
            send({"cmd": "quit"})
        except Exception:
            pass
        try:
            proc.wait(timeout=8)
        except subprocess.TimeoutExpired:
            proc.kill()


if __name__ == "__main__":
    sys.exit(main())
