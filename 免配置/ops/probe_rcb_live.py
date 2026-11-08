"""验证 RCB 订阅：对在线装置做 enable_rcb / GI / disable_rcb。"""
import json
import subprocess

CLI = r"D:\研发\1U装置稳定性测试\免配置\master_sim\bin\mms_master_cli.exe"
HOST = "192.168.0.10"


def run(cmds, timeout=150):
    payload = "\n".join(json.dumps(c, ensure_ascii=False) for c in cmds) + "\n"
    t = subprocess.run([CLI], input=payload, capture_output=True, text=True,
                       timeout=timeout, encoding="utf-8", errors="replace")
    return (t.stdout or "").splitlines()


conn = {"cmd": "connect", "host": HOST, "port": 102, "timeoutMs": 45000}

# 先看该 LD 有哪些数据集，确定 RCB 名
print("== get_datasets C_B5043XLD0")
for ln in run([conn, {"cmd": "get_datasets", "ld": "C_B5043XLD0", "timeoutMs": 60000}, {"cmd": "quit"}], 90):
    print("  ", ln[:400])
