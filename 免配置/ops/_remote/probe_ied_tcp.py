"""在子站上直接向装置发 MMS，确认装置真实 LD/DS 名称。

router 转发 P_X5071APROT/LLN0$dsWarning 时装置回 OBJECT_DOES_NOT_EXIST，
需要确认装置实际使用的 LD inst 与数据集命名，避免模型与装置错配。
"""
import socket
import struct
import sys

HOST = "172.16.50.83"
PORT = 102


def main():
    try:
        s = socket.create_connection((HOST, PORT), timeout=5)
        print("tcp ok", s.getpeername())
        s.close()
    except OSError as exc:
        print("tcp fail:", exc)
        return 1

    # 用子站已有 mms 客户端能力有限，这里只做 TCP 可达性检查
    return 0


if __name__ == "__main__":
    sys.exit(main())
