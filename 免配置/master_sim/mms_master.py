"""MMS 主站 CLI 进程桥（JSONL）。"""
from __future__ import annotations

import json
import queue
import subprocess
import threading
from pathlib import Path
from typing import Any, Callable

ROOT = Path(__file__).resolve().parent
DEFAULT_CLI = ROOT / "bin" / "mms_master_cli.exe"


class MmsMasterBridge:
    def __init__(self, exe: Path | None = None) -> None:
        self.exe = exe or DEFAULT_CLI
        self._proc: subprocess.Popen[str] | None = None
        self._reader: threading.Thread | None = None
        self._evt_queue: queue.Queue[dict[str, Any]] = queue.Queue()
        self._lock = threading.Lock()

    @property
    def available(self) -> bool:
        return self.exe.is_file()

    @property
    def connected(self) -> bool:
        return self._proc is not None and self._proc.poll() is None

    def start(self) -> None:
        if not self.available:
            raise FileNotFoundError(f"未找到 {self.exe}，请先运行 build_libiec61850.ps1")
        if self.connected:
            return
        env = dict(**__import__("os").environ)
        bin_dir = str(self.exe.parent.resolve())
        env["PATH"] = bin_dir + ";" + env.get("PATH", "")
        self._proc = subprocess.Popen(
            [str(self.exe)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
            cwd=bin_dir,
            env=env,
        )
        assert self._proc.stdin and self._proc.stdout

        def read_loop() -> None:
            assert self._proc and self._proc.stdout
            for line in self._proc.stdout:
                line = line.strip()
                if not line:
                    continue
                try:
                    obj = json.loads(line)
                except json.JSONDecodeError:
                    obj = {"evt": "error", "msg": line}
                self._evt_queue.put(obj)

        self._reader = threading.Thread(target=read_loop, daemon=True)
        self._reader.start()

    def stop(self) -> None:
        if self.connected:
            try:
                self.send({"cmd": "quit"})
            except OSError:
                pass
            try:
                self._proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self._proc.kill()
        self._proc = None

    def send(self, cmd: dict[str, Any]) -> None:
        if not self.connected or not self._proc or not self._proc.stdin:
            raise RuntimeError("CLI 未启动")
        with self._lock:
            self._proc.stdin.write(json.dumps(cmd, ensure_ascii=False) + "\n")
            self._proc.stdin.flush()

    def poll_events(self) -> list[dict[str, Any]]:
        items: list[dict[str, Any]] = []
        while True:
            try:
                items.append(self._evt_queue.get_nowait())
            except queue.Empty:
                break
        return items

    def drain_until(
        self,
        predicate: Callable[[dict[str, Any]], bool],
        timeout_sec: float = 30.0,
    ) -> dict[str, Any] | None:
        import time

        deadline = time.time() + timeout_sec
        while time.time() < deadline:
            for evt in self.poll_events():
                if predicate(evt):
                    return evt
                if evt.get("evt") == "error":
                    raise RuntimeError(evt.get("msg", "unknown error"))
            time.sleep(0.05)
        return None
