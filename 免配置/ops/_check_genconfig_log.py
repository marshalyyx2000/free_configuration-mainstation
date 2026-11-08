import os
import paramiko

c = paramiko.SSHClient()
c.set_missing_host_key_policy(paramiko.AutoAddPolicy())
c.connect(
    os.environ.get("SUBST_HOST", "192.168.0.10"),
    username="admin",
    password=os.environ["SUBST_PASS"],
    timeout=15,
    allow_agent=False,
    look_for_keys=False,
)
for path in (
    "/home/admin/router/log/iec61850Lifetime.log",
    "/home/admin/router/router/iec61850Lifetime.log",
):
    cmd = f"grep -E 'failed: 256|generate server config failed' {path} 2>/dev/null | tail -3 || true"
    _, o, _ = c.exec_command(cmd, timeout=30)
    out = o.read().decode("utf-8", errors="replace").strip()
    if out:
        print(path, out)
    else:
        print(path, "(no failed:256)")
c.close()
