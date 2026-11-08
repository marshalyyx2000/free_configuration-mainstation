"""验证代理读：用 items（多变量读）绕开 readDataSetValues 的装置兼容问题。"""
import json
import subprocess

CLI = r"D:\研发\1U装置稳定性测试\免配置\master_sim\bin\mms_master_cli.exe"
HOST = "192.168.0.10"


def run(cmds, timeout=150):
    payload = "\n".join(json.dumps(c, ensure_ascii=False) for c in cmds) + "\n"
    p = subprocess.run([CLI], input=payload, capture_output=True, text=True,
                       timeout=timeout, encoding="utf-8", errors="replace")
    return (p.stdout or "").splitlines()


conn = {"cmd": "connect", "host": HOST, "port": 102, "timeoutMs": 60000}

print("== read via items (dsRelayDin1)")
for ln in run([
    conn,
    {"cmd": "read_dataset", "ref": "P_X5071APROT/LLN0$dsRelayDin1",
     "items": ["P_X5071APROT/BinInGGIO1.Ind1[ST]"], "timeoutMs": 60000},
    {"cmd": "quit"},
]):
    print("  ", ln[:400])

print("== read via items (dsWarning first 5)")
for ln in run([
    conn,
    {"cmd": "read_dataset", "ref": "P_X5071APROT/LLN0$dsWarning",
     "items": [f"P_X5071APROT/AlmGGIO1.Alm{i}[ST]" for i in range(1, 6)],
     "timeoutMs": 60000},
    {"cmd": "quit"},
]):
    print("  ", ln[:400])
