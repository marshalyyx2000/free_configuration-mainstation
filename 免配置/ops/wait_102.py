"""轮询等待 102 端口监听，返回耗时。"""
import socket
import time

HOST = "192.168.0.10"
PORT = 102
LIMIT = 900


def up():
    try:
        with socket.create_connection((HOST, PORT), timeout=2):
            return True
    except OSError:
        return False


t0 = time.time()
while time.time() - t0 < LIMIT:
    if up():
        print("102 UP after %.0fs" % (time.time() - t0))
        raise SystemExit(0)
    time.sleep(10)
print("102 NOT up after %ds" % LIMIT)
raise SystemExit(1)
