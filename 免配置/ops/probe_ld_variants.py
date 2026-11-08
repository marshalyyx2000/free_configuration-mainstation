"""对多个 LD/DS 组合逐一走代理读，定位装置实际使用的 LD 命名。"""
import json
import subprocess

CLI = r"D:\研发\1U装置稳定性测试\免配置\master_sim\bin\mms_master_cli.exe"
HOST = "192.168.0.10"

REFS = [
    "P_X5071ALD0/LLN0$dsAlarm",
    "P_X5071ALD0/LLN0$dsDeviceState",
    "P_X5071ALD0/LLN0$dsRelayDin",
    "P_X5071APROT/LLN0$dsRelayEna",
    "P_X5071APROT/LLN0$dsParameter",
]


def run(cmds, timeout=120):
    payload = "\n".join(json.dumps(c, ensure_ascii=False) for c in cmds) + "\n"
    p = subprocess.run([CLI], input=payload, capture_output=True, text=True,
                       timeout=timeout, encoding="utf-8", errors="replace")
    return (p.stdout or "").splitlines()


conn = {"cmd": "connect", "host": HOST, "port": 102, "timeoutMs": 45000}
for ref in REFS:
    lines = run([conn, {"cmd": "read_dataset", "ref": ref, "timeoutMs": 45000}, {"cmd": "quit"}], 90)
    ev = [ln for ln in lines if '"dataset"' in ln or '"error"' in ln]
    print("==", ref)
    for ln in ev[:2]:
        print("   ", ln[:200])
