"""直连装置（P_X5071A 172.16.50.83:102）确认实际数据集与 RCB 名称。

子站 router 转发 `P_X5071APROT/LLN0$dsWarning` 时装置回 OBJECT_DOES_NOT_EXIST，
需要确认装置真实 LD/DS 结构，判断模型名与装置名是否错配。
"""
import json
import subprocess

CLI = r"D:\研发\1U装置稳定性测试\免配置\master_sim\bin\mms_master_cli.exe"


def run(host, port, cmds, timeout=120):
    payload = "\n".join(json.dumps(c) for c in cmds) + "\n"
    p = subprocess.run([CLI], input=payload, capture_output=True, text=True,
                       timeout=timeout, encoding="utf-8", errors="replace")
    return (p.stdout or "").splitlines()


for host, port in [("172.16.50.83", 102)]:
    print("==== direct", host)
    conn = {"cmd": "connect", "host": host, "port": port, "timeoutMs": 20000}
    for ln in run(host, port, [conn, {"cmd": "get_ld_list", "timeoutMs": 30000}, {"cmd": "quit"}], 90):
        print("  ", ln[:600])
