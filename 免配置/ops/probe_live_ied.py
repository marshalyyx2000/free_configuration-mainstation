"""对真实在报数据的装置（C_B5043X）验证代理读与 RCB 订阅。"""
import json
import subprocess

CLI = r"D:\研发\1U装置稳定性测试\免配置\master_sim\bin\mms_master_cli.exe"
HOST = "192.168.0.10"


def run(cmds, timeout=120):
    payload = "\n".join(json.dumps(c, ensure_ascii=False) for c in cmds) + "\n"
    t = subprocess.run([CLI], input=payload, capture_output=True, text=True,
                       timeout=timeout, encoding="utf-8", errors="replace")
    return (t.stdout or "").splitlines()


conn = {"cmd": "connect", "host": HOST, "port": 102, "timeoutMs": 45000}

print("== read C_B5043XLD0/LLN0$dsAin")
for ln in run([conn, {"cmd": "read_dataset", "ref": "C_B5043XLD0/LLN0$dsAin", "timeoutMs": 60000}, {"cmd": "quit"}], 120):
    print("  ", ln[:260])

print("== read C_B5043XMEAS/LLN0$dsAin")
for ln in run([conn, {"cmd": "read_dataset", "ref": "C_B5043XMEAS/LLN0$dsAin", "timeoutMs": 60000}, {"cmd": "quit"}], 120):
    print("  ", ln[:260])
