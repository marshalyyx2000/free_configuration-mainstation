"""南网运维 103 over IEC 60870-5-104（MaintainServer / CMH_104_Len2_Cpu 布局）。

装置 BufManager ID=10421，ASDU 头为 Type VSQ COT Addr CPU FUN INF RII。
"""
from __future__ import annotations

import socket
import struct
import time
from dataclasses import dataclass
from typing import Optional


P104F_STARTDT_ACT = 0x07
P104F_STARTDT_CON = 0x0B
P104F_TESTFR_ACT = 0x43
P104F_TESTFR_CON = 0x83

DEFAULT_CHUNK = 1200
DEFAULT_MES_COUNT = 1


@dataclass
class Maintain104Config:
    host: str = "192.168.0.10"
    port: int = 2405
    asdu_addr: int = 1
    timeout: float = 30.0


class Iec104MaintainClient:
    def __init__(self, cfg: Maintain104Config):
        self.cfg = cfg
        self.sock: Optional[socket.socket] = None
        self.send_seq = 0
        self.recv_seq = 0
        self._rii = 1

    def connect(self, retries: int = 3) -> None:
        last_err: Optional[Exception] = None
        for attempt in range(retries):
            try:
                self.sock = socket.create_connection(
                    (self.cfg.host, self.cfg.port), timeout=self.cfg.timeout
                )
                self.sock.settimeout(self.cfg.timeout)
                self._send_u(P104F_STARTDT_ACT)
                self._wait_u(P104F_STARTDT_CON)
                return
            except (TimeoutError, OSError) as exc:
                last_err = exc
                self.close()
                time.sleep(1.0)
        raise ConnectionError(
            f"无法在 {self.cfg.host}:{self.cfg.port} 完成 STARTDT（{retries} 次）"
        ) from last_err

    def close(self) -> None:
        if self.sock:
            try:
                self.sock.close()
            finally:
                self.sock = None

    def _send_u(self, ctrl1: int) -> None:
        """CMH_104_Len2：68 + 2 字节长度(=4) + 4 字节控制域。"""
        assert self.sock
        self.sock.sendall(bytes([0x68, 0x04, 0x00, ctrl1, 0x00, 0x00, 0x00]))

    def _wait_u(self, expected_ctrl1: int) -> None:
        assert self.sock
        data = self._recv_exact(7)
        if (
            data[0] != 0x68
            or data[1] != 0x04
            or data[2] != 0x00
            or data[3] != expected_ctrl1
        ):
            raise RuntimeError(f"unexpected U-frame: {data.hex()}")

    def _recv_exact(self, n: int) -> bytes:
        assert self.sock
        buf = b""
        while len(buf) < n:
            chunk = self.sock.recv(n - len(buf))
            if not chunk:
                raise ConnectionError("connection closed")
            buf += chunk
        return buf

    def _build_i_frame(self, asdu: bytes) -> bytes:
        apdu_len = 4 + len(asdu)
        ctrl = struct.pack("<HH", self.send_seq << 1, self.recv_seq << 1)
        self.send_seq = (self.send_seq + 1) % 32768
        return (
            bytes([0x68, apdu_len & 0xFF, (apdu_len >> 8) & 0xFF])
            + ctrl
            + asdu
        )

    def _read_apdu(self) -> tuple[int, bytes]:
        assert self.sock
        start = self._recv_exact(1)
        if start[0] != 0x68:
            raise RuntimeError(f"bad APCI start: {start.hex()}")
        length = struct.unpack("<H", self._recv_exact(2))[0]
        if length == 4:
            rest = self._recv_exact(4)
            c0 = rest[0]
            if (c0 & 0x03) == 0x03:
                if c0 == P104F_TESTFR_ACT:
                    self._send_u(P104F_TESTFR_CON)
                return self._read_apdu()
            return 0, b""
        body = self._recv_exact(length)
        if len(body) < 4:
            return 0, body
        peer_ns = struct.unpack("<H", body[0:2])[0] >> 1
        self.recv_seq = (peer_ns + 1) % 32768
        return 1, body[4:]

    def _next_rii(self) -> int:
        v = self._rii
        self._rii = (self._rii + 1) & 0xFF
        if self._rii == 0:
            self._rii = 1
        return v

    def _build_asdu(
        self,
        typ: int,
        inf: int,
        payload: bytes,
        cot: int = 0x14,
        fun: int = 0xFF,
        rii: Optional[int] = None,
    ) -> bytes:
        if rii is None:
            rii = self._next_rii()
        # CMH_104_Len2_Cpu：CPU 在 Addr 与 FUN 之间。旧代码把 0x00 追加在
        # RII 之后，装置把 RII 读成 INF，导致 0xCA INF=8 实际变成 INF=2。
        head = bytes(
            [
                typ,
                0x81,
                cot,
                self.cfg.asdu_addr & 0xFF,
                0x00,
                fun,
                inf,
                rii,
            ]
        )
        return head + payload

    def _send_s_ack(self) -> None:
        assert self.sock
        nr = self.recv_seq
        c2 = ((nr & 0x7F) << 1) & 0xFF
        c3 = ((nr & 0x3F80) >> 7) & 0xFF
        self.sock.sendall(bytes([0x68, 0x04, 0x00, 0x01, 0x00, c2, c3]))

    def _recv_until_asdu(self, expect_typ: int, timeout: float) -> bytes:
        deadline = time.time() + timeout
        while time.time() < deadline:
            self.sock.settimeout(max(0.2, deadline - time.time()))
            try:
                kind, payload = self._read_apdu()
            except TimeoutError:
                continue
            if kind == 1 and payload and payload[0] == expect_typ:
                self._send_s_ack()
                return payload
            if kind == 1 and payload:
                self._send_s_ack()
        raise TimeoutError(f"等待 ASDU 0x{expect_typ:02x} 超时")

    def _drain_link(self, timeout: float = 1.0) -> None:
        deadline = time.time() + timeout
        while time.time() < deadline:
            self.sock.settimeout(max(0.1, deadline - time.time()))
            try:
                kind, payload = self._read_apdu()
            except TimeoutError:
                break
            if kind == 1 and payload:
                self._send_s_ack()

    def _send_i_and_recv_asdu(self, asdu: bytes, expect_typ: int) -> bytes:
        assert self.sock
        self.sock.sendall(self._build_i_frame(asdu))
        return self._recv_until_asdu(expect_typ, self.cfg.timeout)

    def _send_asdu(self, asdu: bytes, expect_typ: int) -> bytes:
        return self._send_i_and_recv_asdu(asdu, expect_typ)

    def _send_i_no_reply(self, asdu: bytes) -> None:
        assert self.sock
        self.sock.sendall(self._build_i_frame(asdu))
        self._drain_link(1.5)

    def _file_info_payload(self, path: str, mes_count: int) -> bytes:
        block = bytearray(112)
        block[0] = mes_count & 0xFF
        name = path.encode("ascii", errors="replace")[:100]
        block[1 : 1 + len(name)] = name
        return bytes(block)

    def download_file(
        self,
        remote_path: str,
        data: bytes,
        file_inf: int = 0,
        chunk_size: int = DEFAULT_CHUNK,
        mes_count: int = DEFAULT_MES_COUNT,
    ) -> None:
        rii = self._next_rii()
        info_asdu = self._build_asdu(
            0x69,
            file_inf,
            self._file_info_payload(remote_path, mes_count),
            cot=0x14,
            rii=rii,
        )
        resp = self._send_i_and_recv_asdu(info_asdu, 0x6A)
        if not resp or resp[0] != 0x6A or resp[2] != 0x14:
            raise RuntimeError(
                f"file info rejected for {remote_path}: asdu={resp.hex() if resp else 'empty'}"
            )

        pos = 0
        frames_since_ack = 0
        total = len(data)
        while pos < total:
            end = min(pos + chunk_size, total)
            chunk = data[pos:end]
            more = 0 if end >= total else 1
            body = bytearray()
            body.append(more)
            body.extend(struct.pack("<I", pos))
            body.extend(chunk)
            content_asdu = self._build_asdu(0x6B, file_inf, bytes(body), cot=0x14, rii=rii)
            is_last = end >= total
            need_ack = is_last or (frames_since_ack + 1) >= mes_count
            if need_ack:
                resp = self._send_i_and_recv_asdu(content_asdu, 0x6C)
                if not resp or resp[0] != 0x6C:
                    raise RuntimeError(
                        f"file chunk failed at {pos}: {resp.hex() if resp else 'empty'}"
                    )
                if resp[2] == 0x15:
                    raise RuntimeError(f"file chunk denied at {pos}")
                frames_since_ack = 0
            else:
                self._send_i_no_reply(content_asdu)
                frames_since_ack += 1
            pos = end

    def effect_configure(self, inf: int) -> None:
        rii = self._next_rii()
        asdu = self._build_asdu(0xCA, inf, bytes([rii]), cot=0x14, rii=rii)
        resp = self._send_asdu(asdu, 0xCB)
        if not resp or resp[0] != 0xCB or resp[2] != 0x14:
            raise RuntimeError(
                f"配置命令 INF={inf} 失败: {resp.hex() if resp else 'empty'}"
            )

    def stop_iec61850_router(self) -> None:
        # 装置 INF=8 执行 path/SYS/*.sh。INF=2 的 StartProgram 杀不掉看门狗拉起的 router。
        script = (
            b"#!/bin/sh\n"
            b"killall -9 /home/admin/router/bin/iec61850router >/dev/null 2>&1 || true\n"
            b"killall -9 iec61850router >/dev/null 2>&1 || true\n"
            b"pkill -9 -f /home/admin/router/bin/iec61850router >/dev/null 2>&1 || true\n"
            b"rm -f /home/admin/router/router/supervision.cfg.stamp "
            b"/home/admin/router/router/.stamp "
            b"/home/admin/router/config/supervision.cfg.stamp\n"
            b"exit 0\n"
        )
        self.download_file("/SYS/stop_iec61850router.sh", script, file_inf=0)
        time.sleep(1.0)
        self.effect_configure(8)

    def effect_61850(self) -> None:
        self.effect_configure(2)

    def upload_config(self, ied_list: bytes, station_zip: bytes) -> None:
        self.download_file("/CONF/IEDList.xml", ied_list, file_inf=1)
        time.sleep(0.2)
        self.download_file("/SMCD/station.zip", station_zip, file_inf=2)
        time.sleep(0.2)
        self.stop_iec61850_router()
        time.sleep(0.2)
        self.effect_61850()
