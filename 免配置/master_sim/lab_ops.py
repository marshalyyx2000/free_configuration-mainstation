"""仿真主站实验室操作（供 CLI / GUI 共用）。"""
from __future__ import annotations

import socket
import subprocess
import sys
import time
from pathlib import Path
from typing import Callable

from build_config import DEFAULT_SCD, OUT_DIR, generate
from iec104_client import Iec104MaintainClient, Maintain104Config

ROOT = Path(__file__).resolve().parent
LogFn = Callable[[str], None]

# 装置侧 genconfig + 模型装载（RK3568 实测，约 15 MB 站级模型）
GENCONFIG_HINT_SEC = 120
GENCONFIG_HINT_TEXT = "约 2 分钟内（含 genconfig.jar + 装载 supervision.cfg）"


def _noop(_: str) -> None:
    pass


def tcp_reachable(host: str, port: int, timeout: float = 3.0) -> bool:
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except OSError:
        return False


def model_size_bytes(out: Path | None = None) -> int:
    """站级模型体积：优先 station.SMCD（与装置 genconfig 输入同量级）。"""
    out = out or OUT_DIR
    for name in ("station.SMCD", "station.zip"):
        path = out / name
        if path.is_file():
            return path.stat().st_size
    return 0


def format_mb(num_bytes: int) -> str:
    if num_bytes <= 0:
        return "—"
    return f"{num_bytes / (1024 * 1024):.1f} MB"


def format_duration(seconds: float) -> str:
    if seconds < 0:
        seconds = 0
    if seconds < 10:
        return f"{seconds:.1f} 秒"
    seconds_i = int(round(seconds))
    m, s = divmod(seconds_i, 60)
    if m <= 0:
        return f"{s} 秒"
    return f"{m} 分 {s:02d} 秒"


class StepTimer:
    """记录并打印各步骤耗时。"""

    def __init__(self, log: LogFn = _noop) -> None:
        self.log = log
        self.steps: list[tuple[str, float]] = []
        self._t0 = time.monotonic()

    def run(self, name: str, fn):
        self.log(f"▶ {name} …")
        t = time.monotonic()
        result = fn()
        dt = time.monotonic() - t
        self.steps.append((name, dt))
        self.log(f"✔ {name} 完成，耗时 {format_duration(dt)}")
        return result

    def add(self, name: str, seconds: float) -> None:
        self.steps.append((name, seconds))
        self.log(f"✔ {name} 完成，耗时 {format_duration(seconds)}")

    def summary(self, title: str = "步骤耗时汇总") -> None:
        total = time.monotonic() - self._t0
        self.log(f"—— {title} ——")
        for name, dt in self.steps:
            self.log(f"  · {name}: {format_duration(dt)}")
        self.log(f"  · 合计: {format_duration(total)}")


def wait_for_mms_port(
    host: str = "192.168.0.10",
    port: int = 102,
    timeout_sec: float = 20 * 60,
    poll_sec: float = 5.0,
    log: LogFn = _noop,
) -> float | None:
    """轮询直至 MMS 端口监听，返回从调用起的耗时秒数；超时返回 None。

    若一开始 102 已通，说明旧进程未退出，计时无意义：先等到端口关闭，
    再从关闭时刻起重新计时，直到再次监听。
    """
    model = format_mb(model_size_bytes())
    already_up = tcp_reachable(host, port, timeout=2.0)
    if already_up:
        log(
            f"注意: {host}:{port} 此刻已通，多半是旧 router 未退出；"
            f"先等 102 关闭后再计 genconfig/装载（模型 {model}）…"
        )
        drop_deadline = time.monotonic() + 90
        while time.monotonic() < drop_deadline and tcp_reachable(host, port, timeout=1.5):
            time.sleep(1.0)
        if tcp_reachable(host, port, timeout=1.5):
            log(
                "102 一直未关闭，本次「装置耗时」会接近 0，不能代表 genconfig。"
                "请先在装置上删除 supervision.cfg/.stamp 并 kill iec61850router 后再测。"
            )
            return 0.0
        log("102 已关闭，开始计时等待重新生成/装载…")

    t0 = time.monotonic()
    deadline = t0 + timeout_sec
    last_report = -1
    log(
        f"等待装置 genconfig/装载完成（模型 {model}，"
        f"预期 {GENCONFIG_HINT_TEXT}）…"
    )
    while time.monotonic() < deadline:
        elapsed = time.monotonic() - t0
        if tcp_reachable(host, port, timeout=2.0):
            log(f"MMS {host}:{port} 已监听，装置侧耗时 {format_duration(elapsed)}")
            return elapsed
        bucket = int(elapsed // 15)
        if bucket != last_report:
            last_report = bucket
            log(f"  …仍在生成/装载模型，已等待 {format_duration(elapsed)}")
        time.sleep(poll_sec)
    log(f"超时：{timeout_sec:.0f} 秒内 {host}:{port} 仍未监听")
    return None


def run_generate(scd: Path | None = None, out: Path | None = None, log: LogFn = _noop) -> int:
    scd = scd or DEFAULT_SCD
    out = out or OUT_DIR
    timer = StepTimer(log)
    log(f"从 SCD 生成配置: {scd}")

    def _gen():
        return generate(scd, out)

    devices = timer.run("生成本地配置(IEDList/SMCD/zip)", _gen)
    size = model_size_bytes(out)
    log(f"完成: {len(devices)} 台装置 -> {out}")
    log(
        f"站级模型: {format_mb(size)}（station.SMCD）；"
        f"装置 genconfig.jar 预期 {GENCONFIG_HINT_TEXT}"
    )
    timer.summary("生成配置耗时")
    return len(devices)


def run_upload(
    host: str = "192.168.0.10",
    port: int = 2405,
    out: Path | None = None,
    log: LogFn = _noop,
    wait_genconfig: bool = True,
    mms_port: int = 102,
) -> float | None:
    """下装并生效。若 wait_genconfig，则等到 102 打开并返回装置侧耗时秒数。"""
    out = out or OUT_DIR
    ied_path = out / "IEDList.xml"
    zip_path = out / "station.zip"
    if not ied_path.is_file() or not zip_path.is_file():
        raise FileNotFoundError(f"缺少 {ied_path} 或 {zip_path}，请先生成配置")

    ied = ied_path.read_bytes()
    zipp = zip_path.read_bytes()
    model_mb = format_mb(model_size_bytes(out))
    timer = StepTimer(log)

    def _connect():
        log(f"连接运维口 {host}:{port} ...")
        if not tcp_reachable(host, port):
            log(f"警告: {host}:{port} TCP 暂不可达，仍将尝试 104 握手")
        cfg = Maintain104Config(host=host, port=port)
        client = Iec104MaintainClient(cfg)
        client.connect()
        return client

    client = timer.run("连接运维口 2405", _connect)
    try:
        timer.run(
            "下装 IEDList.xml",
            lambda: client.download_file("/CONF/IEDList.xml", ied, file_inf=1),
        )
        timer.run(
            f"下装 station.zip（模型 {model_mb}）",
            lambda: client.download_file("/SMCD/station.zip", zipp, file_inf=2),
        )
        timer.run(
            "配置生效 ASDU0xCA INF=2",
            lambda: client.effect_61850(),
        )
        log("下装与生效完成（COT=0x14）")
        log(
            f"装置将跑 genconfig.jar（模型约 {model_mb}），"
            f"预期 {GENCONFIG_HINT_TEXT} 后 102 才监听"
        )
    finally:
        client.close()

    elapsed: float | None = None
    if wait_genconfig:
        t_wait = time.monotonic()
        elapsed = wait_for_mms_port(host=host, port=mms_port, log=log)
        if elapsed is not None:
            timer.add(f"装置 genconfig.jar+装载（至 102，模型 {model_mb}）", elapsed)
        else:
            timer.add("装置 genconfig.jar+装载（超时未开 102）", time.monotonic() - t_wait)

    timer.summary("下装/生效/genconfig 耗时")
    return elapsed


def run_mms_check(host: str = "192.168.0.10", port: int = 102, log: LogFn = _noop) -> int:
    timer = StepTimer(log)

    def _check():
        if tcp_reachable(host, port):
            log(f"MMS 端口 {host}:{port} 可达")
        else:
            log(f"MMS 端口 {host}:{port} 不可达（router 可能仍在 genconfig）")
            return 1
        r = subprocess.run(
            [sys.executable, str(ROOT / "mms_client.py"), "--host", host, "--port", str(port)],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        if r.stdout:
            log(r.stdout.strip())
        if r.stderr:
            log(r.stderr.strip())
        return r.returncode

    rc = timer.run("检测 MMS(102)", _check)
    timer.summary("MMS 检测耗时")
    return rc


def run_full_pipeline(
    host: str = "192.168.0.10",
    port: int = 2405,
    scd: Path | None = None,
    mms_check: bool = False,
    wait_genconfig: bool = True,
    log: LogFn = _noop,
) -> int:
    t0 = time.monotonic()
    run_generate(scd=scd, log=log)
    run_upload(host=host, port=port, wait_genconfig=wait_genconfig, log=log)
    rc = 0
    if mms_check:
        rc = run_mms_check(host=host, log=log)
    log(f"—— 一键全流程合计: {format_duration(time.monotonic() - t0)} ——")
    return rc
