"""Probe one RCB on the substation with mms_master_cli and print its JSON events."""
import json
import subprocess
import sys
import threading
import time

CLI = r"C:\Program Files\SimMasterQt\bin\mms_master_cli.exe"

IED = "E1Q1SB49"
LD = "E1Q1SB49PROT"
DS = f"{LD}/LLN0$dsRelayEna"
HANDLER = f"{LD}/LLN0$BR$brcbRelayEna"
RCB = f"{LD}/LLN0$BR$brcbRelayEna01"


def main(duration=12.0, rcb=RCB, ds=DS, handler=HANDLER):
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

    def send(obj):
        line = json.dumps(obj, ensure_ascii=False)
        print(f">>> {line}", flush=True)
        proc.stdin.write(line + "\n")
        proc.stdin.flush()

    send({"cmd": "connect", "host": "192.168.0.10", "port": 102})
    deadline = time.time() + duration
    connected = False
    enabled = False
    while time.time() < deadline:
        line = proc.stdout.readline()
        if not line:
            break
        line = line.strip()
        if not line:
            continue
        print(line, flush=True)
        try:
            evt = json.loads(line)
        except json.JSONDecodeError:
            continue
        if evt.get("evt") == "connected" and not connected:
            connected = True
            send({"cmd": "enable_rcb", "handlerRef": handler, "rcbGetRef": rcb, "dsRef": ds})
        if evt.get("evt") == "rcb_enabled" and not enabled:
            enabled = True
            print("### RCB enabled; waiting for reports", flush=True)
    send({"cmd": "quit"})
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()


if __name__ == "__main__":
    dur = float(sys.argv[1]) if len(sys.argv) > 1 else 12.0
    main(dur)
