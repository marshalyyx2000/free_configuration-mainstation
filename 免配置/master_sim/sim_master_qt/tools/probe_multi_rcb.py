"""Enable several RCBs sharing one RptID; collect CLI JSON events without blocking."""
import json
import queue
import subprocess
import sys
import threading
import time

import os

CLI = os.environ.get(
    "MMS_CLI",
    r"C:\Program Files\SimMasterQt\bin\mms_master_cli.exe",
)
LD = "E1Q1SB49PROT"
RCBS = [
    (f"{LD}/LLN0$BR$brcbAlarm", f"{LD}/LLN0$BR$brcbAlarm01", f"{LD}/LLN0$dsAlarm"),
    (f"{LD}/LLN0$BR$brcbRelayEna", f"{LD}/LLN0$BR$brcbRelayEna01", f"{LD}/LLN0$dsRelayEna"),
    (f"{LD}/LLN0$BR$brcbWarning", f"{LD}/LLN0$BR$brcbWarning01", f"{LD}/LLN0$dsWarning"),
]


def main(duration=8.0):
    proc = subprocess.Popen(
        [CLI],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        bufsize=1,
    )
    q = queue.Queue()

    def reader():
        for line in proc.stdout:
            q.put(line.rstrip("\n"))

    threading.Thread(target=reader, daemon=True).start()

    def send(obj):
        line = json.dumps(obj, ensure_ascii=False)
        print(f">>> {line}", flush=True)
        proc.stdin.write(line + "\n")
        proc.stdin.flush()

    def wait_for(evt_name, timeout=8.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                line = q.get(timeout=0.2)
            except queue.Empty:
                continue
            print(line, flush=True)
            try:
                evt = json.loads(line)
            except json.JSONDecodeError:
                continue
            if evt.get("evt") == evt_name:
                return True
        return False

    send({"cmd": "connect", "host": "192.168.0.10", "port": 102})
    if not wait_for("connected"):
        print("### no connected event", flush=True)
    for handler, rcb, ds in RCBS:
        send({"cmd": "enable_rcb", "handlerRef": handler, "rcbGetRef": rcb, "dsRef": ds})
        wait_for("rcb_enabled", 5.0)
        time.sleep(0.4)

    deadline = time.time() + duration
    while time.time() < deadline:
        try:
            line = q.get(timeout=0.3)
        except queue.Empty:
            continue
        print(line, flush=True)

    send({"cmd": "quit"})
    try:
        proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()


if __name__ == "__main__":
    main(float(sys.argv[1]) if len(sys.argv) > 1 else 8.0)
