"""诊断 C_B5013X 订阅：LD 数据集与 RCB enable 探测。"""
import json
import subprocess

CLI = r"D:\研发\1U装置稳定性测试\免配置\master_sim\bin\mms_master_cli.exe"
HOST = "192.168.0.10"
IED = "C_B5013X"


def run(cmds, timeout=120):
    payload = "\n".join(json.dumps(c, ensure_ascii=False) for c in cmds) + "\n"
    p = subprocess.run([CLI], input=payload, capture_output=True, text=True,
                       timeout=timeout, encoding="utf-8", errors="replace")
    return (p.stdout or "").splitlines()


conn = {"cmd": "connect", "host": HOST, "port": 102, "timeoutMs": 45000}

print("=== comm_state tail (online idx>=4)")
for ln in run([conn, {"cmd": "read_comm_state", "timeoutMs": 20000}, {"cmd": "quit"}], 60):
    if '"comm_state"' in ln:
        print(ln[:200], "...")

for ld in [IED + "LD0", IED + "CTRL", IED + "MEAS", IED + "PROT"]:
    print("=== datasets", ld)
    for ln in run([conn, {"cmd": "get_datasets", "ld": ld, "timeoutMs": 25000}, {"cmd": "quit"}], 60):
        if '"datasets"' in ln or '"error"' in ln:
            print("   ", ln[:500])

trials = [
    ("LD0", "brcbAlarm", "dsAlarm"),
    ("LD0", "brcbWarning", "dsWarning"),
    ("LD0", "brcbCommState", "dsCommState"),
    ("CTRL", "brcbDin", "dsDin1"),
    ("CTRL", "brcbDin08", "dsDin08"),
    ("CTRL", "brcbDin1", "dsDin1"),
]
for ld, rcb, ds in trials:
    lds = IED + ld
    handler = f"{lds}/LLN0$BR${rcb}"
    get01 = handler + "01"
    get08 = handler + "08"
    dsref = f"{lds}/LLN0${ds}"
    for label, getref in [("01", get01), ("08", get08)]:
        lines = run([conn, {"cmd": "enable_rcb", "handlerRef": handler, "rcbGetRef": getref,
                            "dsRef": dsref, "timeoutMs": 20000}, {"cmd": "quit"}], 50)
        res = [ln for ln in lines if '"rcb_enabled"' in ln or '"error"' in ln or '"evt"' in ln]
        print(f"  {ld}/{rcb} inst={label} ds={ds} ->", res[-1][:180] if res else "no output")
