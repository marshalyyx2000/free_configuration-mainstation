#!/usr/bin/env python3
"""DEPRECATED: use sim_master_qt. 仿真主站图形界面（tkinter），仅作历史参考。"""
from __future__ import annotations

import queue
import subprocess
import sys
import threading
import time
import tkinter as tk
from datetime import datetime
from pathlib import Path
from tkinter import filedialog, messagebox, scrolledtext, ttk

from build_config import DEFAULT_SCD, OUT_DIR
from iec61850_ui import Iec61850Panel
from lab_ops import (
    GENCONFIG_HINT_TEXT,
    format_duration,
    format_mb,
    model_size_bytes,
    run_generate,
    run_mms_check,
    run_upload,
    tcp_reachable,
)

ROOT = Path(__file__).resolve().parent


class MasterSimApp(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("免配置仿真主站")
        self.minsize(960, 620)
        self._log_queue: queue.Queue[str] = queue.Queue()
        self._worker: threading.Thread | None = None
        self._last_genconfig_sec: float | None = None

        self._build_ui()
        self._refresh_model_info()
        self.after(100, self._drain_log)

    def _build_ui(self) -> None:
        pad = {"padx": 8, "pady": 4}
        nb = ttk.Notebook(self)
        nb.pack(fill=tk.BOTH, expand=True)

        maintain = ttk.Frame(nb, padding=8)
        nb.add(maintain, text="运维 / 下装")
        mms_tab = ttk.Frame(nb, padding=0)
        nb.add(mms_tab, text="61850 主站")

        self.var_host = tk.StringVar(value="192.168.0.10")
        self.var_maintain_port = tk.StringVar(value="2405")
        self.var_mms_port = tk.StringVar(value="102")

        Iec61850Panel(mms_tab, self.var_host, self.var_mms_port).pack(
            fill=tk.BOTH, expand=True
        )

        frm = maintain

        conn = ttk.LabelFrame(frm, text="连接", padding=8)
        conn.pack(fill=tk.X, **pad)

        ttk.Label(conn, text="子站 IP").grid(row=0, column=0, sticky=tk.W)
        ttk.Entry(conn, textvariable=self.var_host, width=18).grid(
            row=0, column=1, sticky=tk.W, padx=4
        )

        ttk.Label(conn, text="运维端口").grid(row=0, column=2, sticky=tk.W, padx=(12, 0))
        ttk.Entry(conn, textvariable=self.var_maintain_port, width=8).grid(
            row=0, column=3, sticky=tk.W, padx=4
        )

        ttk.Label(conn, text="MMS 端口").grid(row=0, column=4, sticky=tk.W, padx=(12, 0))
        ttk.Entry(conn, textvariable=self.var_mms_port, width=8).grid(
            row=0, column=5, sticky=tk.W, padx=4
        )

        ttk.Button(conn, text="探测端口", command=self._on_probe).grid(
            row=0, column=6, padx=(12, 0)
        )

        scd_frm = ttk.LabelFrame(frm, text="SCD / 输出", padding=8)
        scd_frm.pack(fill=tk.X, **pad)

        self.var_scd = tk.StringVar(value=str(DEFAULT_SCD))
        ttk.Entry(scd_frm, textvariable=self.var_scd).pack(
            side=tk.LEFT, fill=tk.X, expand=True, padx=(0, 4)
        )
        ttk.Button(scd_frm, text="浏览…", command=self._pick_scd).pack(side=tk.LEFT)
        ttk.Label(scd_frm, text=f"  输出: {OUT_DIR}").pack(side=tk.LEFT, padx=8)

        info = ttk.LabelFrame(frm, text="装置 genconfig.jar（模型生成）", padding=8)
        info.pack(fill=tk.X, **pad)

        self.var_model_size = tk.StringVar(value="模型大小: —")
        self.var_genconfig_time = tk.StringVar(
            value=f"装置耗时: 未测（预期 {GENCONFIG_HINT_TEXT}）"
        )
        self.lbl_model_size = ttk.Label(
            info, textvariable=self.var_model_size, font=("Microsoft YaHei UI", 11, "bold")
        )
        self.lbl_model_size.pack(anchor=tk.W)
        self.lbl_genconfig_time = ttk.Label(
            info,
            textvariable=self.var_genconfig_time,
            font=("Microsoft YaHei UI", 11, "bold"),
        )
        self.lbl_genconfig_time.pack(anchor=tk.W, pady=(4, 0))
        ttk.Label(
            info,
            text="说明: 下装生效后，装置用 genconfig.jar 把站级 SCL 展开为 supervision.cfg；"
            "「装置耗时」从生效计到 102 监听。日志中每一步会标 ▶/✔ 与耗时，结尾有汇总。",
            wraplength=680,
        ).pack(anchor=tk.W, pady=(6, 0))

        actions = ttk.LabelFrame(frm, text="操作", padding=8)
        actions.pack(fill=tk.X, **pad)

        self.btn_gen = ttk.Button(actions, text="1. 生成配置", command=self._on_generate)
        self.btn_gen.pack(side=tk.LEFT, padx=4)
        self.btn_up = ttk.Button(actions, text="2. 下装并生效", command=self._on_upload)
        self.btn_up.pack(side=tk.LEFT, padx=4)
        self.btn_full = ttk.Button(actions, text="一键全流程", command=self._on_full)
        self.btn_full.pack(side=tk.LEFT, padx=4)
        self.btn_mms = ttk.Button(actions, text="检测 MMS(102)", command=self._on_mms)
        self.btn_mms.pack(side=tk.LEFT, padx=4)
        ttk.Button(actions, text="打开输出目录", command=self._open_out).pack(
            side=tk.LEFT, padx=4
        )

        opts = ttk.Frame(frm)
        opts.pack(fill=tk.X, **pad)
        self.var_wait_genconfig = tk.BooleanVar(value=True)
        ttk.Checkbutton(
            opts,
            text="下装后等待并计时装置 genconfig（至 102 监听）",
            variable=self.var_wait_genconfig,
        ).pack(side=tk.LEFT)
        self.var_mms_after = tk.BooleanVar(value=True)
        ttk.Checkbutton(
            opts,
            text="一键流程后自动检测 MMS 端口",
            variable=self.var_mms_after,
        ).pack(side=tk.LEFT, padx=(16, 0))

        log_frm = ttk.LabelFrame(frm, text="日志", padding=4)
        log_frm.pack(fill=tk.BOTH, expand=True, **pad)
        log_bar = ttk.Frame(log_frm)
        log_bar.pack(fill=tk.X, pady=(0, 4))
        ttk.Label(log_bar, text="运行日志").pack(side=tk.LEFT)
        ttk.Button(log_bar, text="清除日志", command=self._clear_log).pack(side=tk.RIGHT)
        self.log_text = scrolledtext.ScrolledText(
            log_frm, height=16, state=tk.DISABLED, font=("Consolas", 10)
        )
        self.log_text.pack(fill=tk.BOTH, expand=True)

        self.status = ttk.Label(frm, text="就绪")
        self.status.pack(anchor=tk.W, **pad)

    def _refresh_model_info(self, measured_sec: float | None = None) -> None:
        size = model_size_bytes(OUT_DIR)
        self.var_model_size.set(f"模型大小: {format_mb(size)}（station.SMCD，供装置 genconfig.jar）")
        if measured_sec is not None:
            self._last_genconfig_sec = measured_sec
            self.var_genconfig_time.set(
                f"装置耗时: {format_duration(measured_sec)}（本次实测，模型 {format_mb(size)}）"
            )
        elif self._last_genconfig_sec is not None:
            self.var_genconfig_time.set(
                f"装置耗时: {format_duration(self._last_genconfig_sec)}（上次实测，"
                f"模型 {format_mb(size)}；预期 {GENCONFIG_HINT_TEXT}）"
            )
        else:
            self.var_genconfig_time.set(
                f"装置耗时: 未测（预期 {GENCONFIG_HINT_TEXT}，模型 {format_mb(size)}）"
            )

    def _pick_scd(self) -> None:
        path = filedialog.askopenfilename(
            title="选择 SCD",
            filetypes=[("SCD/SCL", "*.SCD *.scd *.xml"), ("所有文件", "*.*")],
            initialdir=str(DEFAULT_SCD.parent),
        )
        if path:
            self.var_scd.set(path)

    def _open_out(self) -> None:
        OUT_DIR.mkdir(parents=True, exist_ok=True)
        subprocess.Popen(["explorer", str(OUT_DIR)])

    def _log(self, msg: str) -> None:
        stamp = datetime.now().strftime("%H:%M:%S")
        self._log_queue.put(f"[{stamp}] {msg}")

    def _clear_log(self) -> None:
        while True:
            try:
                self._log_queue.get_nowait()
            except queue.Empty:
                break
        self.log_text.configure(state=tk.NORMAL)
        self.log_text.delete("1.0", tk.END)
        self.log_text.configure(state=tk.DISABLED)
        self._log("日志已清除")

    def _drain_log(self) -> None:
        while True:
            try:
                line = self._log_queue.get_nowait()
            except queue.Empty:
                break
            self.log_text.configure(state=tk.NORMAL)
            self.log_text.insert(tk.END, line + "\n")
            self.log_text.see(tk.END)
            self.log_text.configure(state=tk.DISABLED)
        self.after(100, self._drain_log)

    def _set_busy(self, busy: bool) -> None:
        state = tk.DISABLED if busy else tk.NORMAL
        for w in (self.btn_gen, self.btn_up, self.btn_full, self.btn_mms):
            w.configure(state=state)
        self.status.configure(text="运行中…" if busy else "就绪")

    def _run_async(self, target, on_done=None) -> None:
        if self._worker and self._worker.is_alive():
            messagebox.showwarning("忙", "请等待当前任务结束")
            return

        def wrapper() -> None:
            err: Exception | None = None
            try:
                target()
            except Exception as exc:
                err = exc
                self._log(f"错误: {exc}")
            finally:
                self.after(0, lambda: self._finish_worker(err, on_done))

        self._set_busy(True)
        self._worker = threading.Thread(target=wrapper, daemon=True)
        self._worker.start()

    def _finish_worker(self, err: Exception | None, on_done) -> None:
        self._set_busy(False)
        if err:
            self.status.configure(text="失败")
            messagebox.showerror("失败", str(err))
        else:
            self.status.configure(text="完成")
        if on_done:
            on_done(err)

    def _host_port(self) -> tuple[str, int]:
        return self.var_host.get().strip(), int(self.var_maintain_port.get().strip())

    def _on_probe(self) -> None:
        host = self.var_host.get().strip()
        try:
            mp = int(self.var_maintain_port.get())
            mms = int(self.var_mms_port.get())
        except ValueError:
            messagebox.showerror("参数", "端口必须为数字")
            return
        self._log(f"探测 {host} …")
        maint_ok = tcp_reachable(host, mp, timeout=6.0)
        mms_ok = tcp_reachable(host, mms, timeout=15.0)
        self._log(f"  运维 {mp}: {'通' if maint_ok else '不通'}")
        self._log(f"  MMS {mms}: {'通' if mms_ok else '不通'}")
        if maint_ok and not mms_ok:
            self._log("  提示：运维已通而 MMS 未通时，可能在装载大模型，请 5–15 分钟后重试。")
        if not maint_ok and not mms_ok:
            self._log("  提示：请确认 PC 与子站同网段且子站服务已启动。")

    def _log_total(self, title: str, t0: float) -> None:
        self._log(f"【总耗时】{title}: {format_duration(time.monotonic() - t0)}")

    def _on_generate(self) -> None:
        scd = Path(self.var_scd.get().strip())

        def job() -> None:
            t0 = time.monotonic()
            run_generate(scd=scd, log=self._log)
            self.after(0, self._refresh_model_info)
            self._log_total("生成配置", t0)

        self._run_async(job)

    def _on_upload(self) -> None:
        host, port = self._host_port()
        wait = self.var_wait_genconfig.get()
        try:
            mms = int(self.var_mms_port.get())
        except ValueError:
            messagebox.showerror("参数", "MMS 端口必须为数字")
            return

        def job() -> None:
            t0 = time.monotonic()
            elapsed = run_upload(
                host=host,
                port=port,
                wait_genconfig=wait,
                mms_port=mms,
                log=self._log,
            )
            self.after(0, lambda: self._refresh_model_info(elapsed))
            self._log_total("下装并生效", t0)

        self._run_async(job)

    def _on_mms(self) -> None:
        host = self.var_host.get().strip()
        try:
            mms = int(self.var_mms_port.get())
        except ValueError:
            messagebox.showerror("参数", "MMS 端口必须为数字")
            return

        def job() -> None:
            t0 = time.monotonic()
            run_mms_check(host=host, port=mms, log=self._log)
            self._log_total("检测 MMS(102)", t0)

        self._run_async(job)

    def _on_full(self) -> None:
        host, port = self._host_port()
        scd = Path(self.var_scd.get().strip())
        mms = self.var_mms_after.get()
        wait = self.var_wait_genconfig.get()

        def job() -> None:
            t0 = time.monotonic()
            run_generate(scd=scd, log=self._log)
            self.after(0, self._refresh_model_info)
            elapsed = run_upload(
                host=host,
                port=port,
                wait_genconfig=wait,
                mms_port=int(self.var_mms_port.get()),
                log=self._log,
            )
            self.after(0, lambda: self._refresh_model_info(elapsed))
            if mms:
                run_mms_check(host=host, port=int(self.var_mms_port.get()), log=self._log)
            self._log_total("一键全流程", t0)

        self._run_async(job)


def main() -> int:
    app = MasterSimApp()
    app.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
