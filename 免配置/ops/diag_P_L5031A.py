"""诊断 P_L5031A 订阅失败：在线状态、LD/DS 列表、RCB 存在性、订阅尝试。"""
import json
import subprocess

CLI = r"D:\研发\1U装置稳定性测试\免配置\master_sim\bin\mms_master_cli.exe"
HOST = "192.168.0.10"
IED = "P_L5031A"


def run(cmds, timeout=120):
    payload = "\n".join(json.dumps(c, ensure_ascii=False) for c in cmds) + "\n"
    p = subprocess.run([CLI], input=payload, capture_output=True, text=True,
                       timeout=timeout, encoding="utf-8", errors="replace")
    return (p.stdout or "").splitlines()


conn = {"cmd": "connect", "host": HOST, "port": 102, "timeoutMs": 45000}

for ld in [IED + "LD0", IED + "PROT", IED + "MEAS", IED + "CTRL"]:
    print("=== datasets", ld)
    for ln in run([conn, {"cmd": "get_datasets", "ld": ld, "timeoutMs": 30000}, {"cmd": "quit"}], 90):
        if '"datasets"' in ln or '"error"' in ln:
            print("   ", ln[:400])

print("=== try subscribe common RCB names on", IED + "LD0")
for rcb, ds in [("brcbAlarm", "dsAlarm"), ("brcbWarning", "dsWarning"),
                ("brcbWarningA", "dsWarning1"), ("brcbRelayDin", "dsRelayDin")]:
    handler = f"{IED}LD0/LLN0$BR${rcb}"
    getref = handler + "01"
    dsref = f"{IED}LD0/LLN0${ds}"
    lines = run([conn, {"cmd": "enable_rcb", "handlerRef": handler, "rcbGetRef": getref,
                        "dsRef": dsref, "timeoutMs": 25000}, {"cmd": "quit"}], 80)
    res = [ln for ln in lines if '"rcb_enabled"' in ln or '"error"' in ln]
    print("  ", rcb, "->", res[0][:160] if res else "no output")
