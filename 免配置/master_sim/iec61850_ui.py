#!/usr/bin/env python3
"""IEC 61850 MMS 主站展示页（连接 SubStation:102）。"""
from __future__ import annotations

import json
import tkinter as tk
from datetime import datetime
from tkinter import messagebox, scrolledtext, ttk
from typing import Any

from build_config import OUT_DIR
from ied_list import (
    IedDevice,
    dataset_reference,
    load_ied_list,
    logical_device_name,
    rcb_get_reference,
    rcb_handler_reference,
)
from mms_master import MmsMasterBridge, ROOT
from smcd_model import ReportInfo, get_ied_model


def _fmt_value(val: Any) -> str:
    if val is None:
        return ""
    if isinstance(val, bool):
        return "true" if val else "false"
    if isinstance(val, (list, dict)):
        return json.dumps(val, ensure_ascii=False)
    return str(val)


class Iec61850Panel(ttk.Frame):
    def __init__(self, master, host_var: tk.StringVar, port_var: tk.StringVar) -> None:
        super().__init__(master, padding=4)
        self.var_host = host_var
        self.var_port = port_var
        self.bridge = MmsMasterBridge()
        self.devices: list[IedDevice] = load_ied_list()
        self._comm: dict[str, bool] = {}
        self._selected_ied: IedDevice | None = None
        self._selected_ds: str | None = None
        self._enabled_rcbs: set[str] = set()
        self._row_values: dict[int, str] = {}

        self._build()
        self.after(150, self._poll_events)

    def _build(self) -> None:
        top = ttk.Frame(self)
        top.pack(fill=tk.X, pady=4)

        if not self.bridge.available:
            ttk.Label(
                top,
                text=f"未找到 {ROOT / 'bin' / 'mms_master_cli.exe'}，请先运行 build_libiec61850.ps1",
                foreground="red",
            ).pack(anchor=tk.W)

        self.lbl_link = ttk.Label(top, text="未连接")
        self.lbl_link.pack(side=tk.LEFT)

        ttk.Button(top, text="连接 MMS", command=self._on_connect).pack(side=tk.LEFT, padx=6)
        ttk.Button(top, text="断开", command=self._on_disconnect).pack(side=tk.LEFT, padx=2)
        ttk.Button(top, text="刷新通信工况", command=self._refresh_comm).pack(side=tk.LEFT, padx=6)

        paned = ttk.Panedwindow(self, orient=tk.HORIZONTAL)
        paned.pack(fill=tk.BOTH, expand=True, pady=4)

        left = ttk.LabelFrame(paned, text="装置列表", padding=4)
        paned.add(left, weight=1)

        cols = ("no", "ied", "desc", "state")
        self.tree_dev = ttk.Treeview(left, columns=cols, show="headings", height=18)
        for c, w, t in (
            ("no", 40, "序号"),
            ("ied", 110, "IED"),
            ("desc", 160, "描述"),
            ("state", 60, "通断"),
        ):
            self.tree_dev.heading(c, text=t)
            self.tree_dev.column(c, width=w, anchor=tk.W)
        self.tree_dev.pack(fill=tk.BOTH, expand=True)
        self.tree_dev.bind("<<TreeviewSelect>>", self._on_dev_select)
        self.tree_dev.tag_configure("up", foreground="green")
        self.tree_dev.tag_configure("down", foreground="gray")

        right = ttk.Notebook(paned)
        paned.add(right, weight=3)

        self._build_comm_tab(right)
        self._build_dataset_tab(right)
        self._build_report_tab(right)
        self._build_file_tab(right)

        log_frm = ttk.LabelFrame(self, text="61850 日志", padding=4)
        log_frm.pack(fill=tk.X, pady=4)
        self.log = scrolledtext.ScrolledText(log_frm, height=6, font=("Consolas", 9))
        self.log.pack(fill=tk.X)

        self._reload_device_tree()

    def _build_comm_tab(self, nb: ttk.Notebook) -> None:
        tab = ttk.Frame(nb, padding=4)
        nb.add(tab, text="通信工况")
        cols = ("no", "ied", "desc", "stVal")
        self.tree_comm = ttk.Treeview(tab, columns=cols, show="headings")
        for c, w, t in (
            ("no", 50, "序号"),
            ("ied", 120, "IED"),
            ("desc", 220, "描述"),
            ("stVal", 80, "通断"),
        ):
            self.tree_comm.heading(c, text=t)
            self.tree_comm.column(c, width=w)
        self.tree_comm.pack(fill=tk.BOTH, expand=True)
        self.lbl_comm_summary = ttk.Label(tab, text="—")
        self.lbl_comm_summary.pack(anchor=tk.W, pady=4)

    def _build_dataset_tab(self, nb: ttk.Notebook) -> None:
        tab = ttk.Frame(nb, padding=4)
        nb.add(tab, text="数据集 / 遥信遥测")

        bar = ttk.Frame(tab)
        bar.pack(fill=tk.X)
        ttk.Label(bar, text="数据集").pack(side=tk.LEFT)
        self.cmb_ds = ttk.Combobox(bar, width=40, state="readonly")
        self.cmb_ds.pack(side=tk.LEFT, padx=4)
        self.cmb_ds.bind("<<ComboboxSelected>>", self._on_ds_select)
        ttk.Button(bar, text="读数据集", command=self._read_dataset).pack(side=tk.LEFT, padx=4)
        ttk.Button(bar, text="MMS 刷新列表", command=self._fetch_datasets_mms).pack(
            side=tk.LEFT, padx=4
        )

        cols = ("idx", "name", "value", "time")
        self.tree_ds = ttk.Treeview(tab, columns=cols, show="headings")
        for c, w, t in (
            ("idx", 40, "#"),
            ("name", 280, "61850 成员"),
            ("value", 120, "值"),
            ("time", 160, "时间"),
        ):
            self.tree_ds.heading(c, text=t)
            self.tree_ds.column(c, width=w)
        self.tree_ds.pack(fill=tk.BOTH, expand=True, pady=4)
        self.tree_ds.tag_configure("changed", background="#4a3728", foreground="white")

    def _build_report_tab(self, nb: ttk.Notebook) -> None:
        tab = ttk.Frame(nb, padding=4)
        nb.add(tab, text="报告")

        bar = ttk.Frame(tab)
        bar.pack(fill=tk.X)
        ttk.Label(bar, text="报告控制块").pack(side=tk.LEFT)
        self.cmb_rcb = ttk.Combobox(bar, width=36, state="readonly")
        self.cmb_rcb.pack(side=tk.LEFT, padx=4)
        ttk.Button(bar, text="订阅", command=self._enable_rcb).pack(side=tk.LEFT, padx=2)
        ttk.Button(bar, text="退订", command=self._disable_rcb).pack(side=tk.LEFT, padx=2)
        ttk.Button(bar, text="总召 GI", command=self._gi_rcb).pack(side=tk.LEFT, padx=2)
        self.lbl_rcb = ttk.Label(bar, text="")
        self.lbl_rcb.pack(side=tk.LEFT, padx=8)

        cols = ("idx", "name", "value", "reason", "time")
        self.tree_rpt = ttk.Treeview(tab, columns=cols, show="headings")
        for c, w, t in (
            ("idx", 40, "#"),
            ("name", 260, "成员"),
            ("value", 100, "值"),
            ("reason", 60, "原因"),
            ("time", 140, "时间"),
        ):
            self.tree_rpt.heading(c, text=t)
            self.tree_rpt.column(c, width=w)
        self.tree_rpt.pack(fill=tk.BOTH, expand=True, pady=4)
        self.tree_rpt.tag_configure("changed", background="#4a3728", foreground="white")

    def _build_file_tab(self, nb: ttk.Notebook) -> None:
        tab = ttk.Frame(nb, padding=4)
        nb.add(tab, text="录波文件")
        bar = ttk.Frame(tab)
        bar.pack(fill=tk.X)
        ttk.Label(bar, text="目录").pack(side=tk.LEFT)
        self.var_file_path = tk.StringVar(value="/COMTRADE/")
        ttk.Entry(bar, textvariable=self.var_file_path, width=50).pack(side=tk.LEFT, padx=4)
        ttk.Button(bar, text="列目录", command=self._list_files).pack(side=tk.LEFT)
        cols = ("name", "size", "mtime")
        self.tree_files = ttk.Treeview(tab, columns=cols, show="headings")
        for c, w, t in (("name", 360, "文件名"), ("size", 80, "大小"), ("mtime", 140, "修改时间")):
            self.tree_files.heading(c, text=t)
            self.tree_files.column(c, width=w)
        self.tree_files.pack(fill=tk.BOTH, expand=True, pady=4)

    def _log(self, msg: str) -> None:
        stamp = datetime.now().strftime("%H:%M:%S")
        self.log.insert(tk.END, f"[{stamp}] {msg}\n")
        self.log.see(tk.END)

    def _reload_device_tree(self) -> None:
        self.devices = load_ied_list()
        self.tree_dev.delete(*self.tree_dev.get_children())
        self.tree_comm.delete(*self.tree_comm.get_children())
        seen: set[str] = set()
        for d in self.devices:
            if d.ied_name in seen:
                continue
            seen.add(d.ied_name)
            st = self._comm.get(d.ied_name)
            st_txt = "通" if st else ("分" if st is False else "—")
            tag = "up" if st else ("down" if st is False else "")
            self.tree_dev.insert(
                "",
                tk.END,
                iid=d.ied_name,
                values=(d.no, d.ied_name, d.desc, st_txt),
                tags=(tag,) if tag else (),
            )
            self.tree_comm.insert("", tk.END, values=(d.no, d.ied_name, d.desc, st_txt))

    def _on_connect(self) -> None:
        if not self.bridge.available:
            messagebox.showerror("缺少 CLI", "请先编译 mms_master_cli.exe")
            return
        try:
            host = self.var_host.get().strip()
            port = int(self.var_port.get().strip())
        except ValueError:
            messagebox.showerror("参数", "MMS 端口无效")
            return
        try:
            if not self.bridge.connected:
                self.bridge.start()
                self.bridge.drain_until(lambda e: e.get("evt") == "ready", timeout_sec=5)
            self.bridge.send({"cmd": "connect", "host": host, "port": port})
            evt = self.bridge.drain_until(lambda e: e.get("evt") == "connected", timeout_sec=15)
            if not evt:
                raise RuntimeError("连接超时")
            self.lbl_link.configure(text=f"已连接 {host}:{port}")
            self._log(f"已连接 {host}:{port}")
            self._refresh_comm()
        except Exception as exc:
            messagebox.showerror("连接失败", str(exc))
            self._log(f"连接失败: {exc}")

    def _on_disconnect(self) -> None:
        try:
            if self.bridge.connected:
                self.bridge.send({"cmd": "disconnect"})
            self.bridge.stop()
        except OSError:
            pass
        self.lbl_link.configure(text="未连接")
        self._enabled_rcbs.clear()
        self.lbl_rcb.configure(text="")
        self._log("已断开")

    def _refresh_comm(self) -> None:
        if not self.bridge.connected:
            messagebox.showinfo("提示", "请先连接 MMS")
            return
        self.bridge.send({"cmd": "read_comm_state", "ref": "SubStationLD0/LLN0$dsCommState"})

    def _on_dev_select(self, _evt=None) -> None:
        sel = self.tree_dev.selection()
        if not sel:
            return
        ied_name = sel[0]
        self._selected_ied = next((d for d in self.devices if d.ied_name == ied_name), None)
        if not self._selected_ied:
            return
        self.var_file_path.set(self._selected_ied.wave_path or "/COMTRADE/")
        self._load_model_nav(self._selected_ied.ied_name)

    def _load_model_nav(self, ied_name: str) -> None:
        model = get_ied_model(ied_name, OUT_DIR / "station.SMCD")
        ds_names = [d.name for d in model.datasets] if model else []
        rcb_items = (
            [f"{r.name} ({r.desc})" for r in model.reports] if model else []
        )
        self.cmb_ds["values"] = ds_names
        if ds_names:
            self.cmb_ds.current(0)
            self._on_ds_select()
        else:
            self.cmb_ds.set("")
        self.cmb_rcb["values"] = rcb_items
        if rcb_items:
            self.cmb_rcb.current(0)

    def _fetch_datasets_mms(self) -> None:
        if not self._selected_ied or not self.bridge.connected:
            return
        ld = logical_device_name(self._selected_ied.ied_name)
        self.bridge.send({"cmd": "get_datasets", "ld": ld})

    def _on_ds_select(self, _evt=None) -> None:
        name = self.cmb_ds.get().strip()
        if not name or not self._selected_ied:
            return
        self._selected_ds = name

    def _read_dataset(self) -> None:
        if not self._selected_ied or not self._selected_ds:
            return
        if not self.bridge.connected:
            messagebox.showinfo("提示", "请先连接")
            return
        ref = dataset_reference(self._selected_ied.ied_name, self._selected_ds)
        self.bridge.send({"cmd": "read_dataset", "ref": ref})

    def _current_report(self) -> ReportInfo | None:
        if not self._selected_ied:
            return None
        model = get_ied_model(self._selected_ied.ied_name, OUT_DIR / "station.SMCD")
        if not model or not self.cmb_rcb.get():
            return None
        name = self.cmb_rcb.get().split(" ", 1)[0]
        for r in model.reports:
            if r.name == name:
                return r
        return None

    def _enable_rcb(self) -> None:
        rpt = self._current_report()
        if not rpt or not self._selected_ied or not self.bridge.connected:
            return
        ds_ref = dataset_reference(self._selected_ied.ied_name, rpt.dat_set)
        from ied_list import rcb_get_reference_upstream

        get_ref = rcb_get_reference_upstream(
            self._selected_ied.ied_name, rpt.name, rpt.buffered
        )
        handler = rcb_handler_reference(
            self._selected_ied.ied_name, rpt.name, rpt.buffered
        )
        self.bridge.send(
            {
                "cmd": "enable_rcb",
                "rcbGetRef": get_ref,
                "handlerRef": handler,
                "dsRef": ds_ref,
            }
        )
        self._log(f"订阅 {handler}")

    def _disable_rcb(self) -> None:
        rpt = self._current_report()
        if not rpt or not self._selected_ied:
            return
        handler = rcb_handler_reference(
            self._selected_ied.ied_name, rpt.name, rpt.buffered
        )
        self.bridge.send({"cmd": "disable_rcb", "handlerRef": handler})
        self._enabled_rcbs.discard(handler)
        self.lbl_rcb.configure(text="")

    def _gi_rcb(self) -> None:
        rpt = self._current_report()
        if not rpt or not self._selected_ied:
            return
        handler = rcb_handler_reference(
            self._selected_ied.ied_name, rpt.name, rpt.buffered
        )
        self.bridge.send({"cmd": "gi", "handlerRef": handler})

    def _list_files(self) -> None:
        if not self.bridge.connected:
            return
        path = self.var_file_path.get().strip() or "/COMTRADE/"
        self.bridge.send({"cmd": "list_files", "path": path})

    def _apply_comm_state(self, evt: dict[str, Any]) -> None:
        entries = evt.get("entries") or []
        connected = 0
        for item in entries:
            idx = int(item.get("idx", 0))
            if idx <= 0 or idx > len(self.devices):
                continue
            dev = self.devices[idx - 1]
            ok = item.get("valid", False)
            st = bool(item.get("stVal")) if ok else False
            if ok and st:
                connected += 1
            self._comm[dev.ied_name] = st if ok else False
        self._reload_device_tree()
        total = len(self.devices)
        self.lbl_comm_summary.configure(text=f"summary: {connected}/{total} connected")

    def _fill_dataset_tree(self, evt: dict[str, Any]) -> None:
        self.tree_ds.delete(*self.tree_ds.get_children())
        self._row_values.clear()
        now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        for row in evt.get("rows") or []:
            idx = row.get("idx", 0)
            name = row.get("name", "")
            val = _fmt_value(row.get("value"))
            self._row_values[int(idx)] = val
            self.tree_ds.insert("", tk.END, iid=str(idx), values=(idx, name, val, now))

    def _apply_report(self, evt: dict[str, Any]) -> None:
        ts = evt.get("ts_ms")
        time_str = datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]
        if ts:
            try:
                time_str = datetime.fromtimestamp(int(ts) / 1000.0).strftime(
                    "%Y-%m-%d %H:%M:%S.%f"
                )[:-3]
            except (ValueError, OSError):
                pass
        for item in evt.get("items") or []:
            idx = item.get("idx", 0)
            name = item.get("name", "")
            val = _fmt_value(item.get("value"))
            reason = item.get("reason", "")
            iid = str(idx)
            old = self._row_values.get(int(idx))
            tags = ("changed",) if old is not None and old != val else ()
            self._row_values[int(idx)] = val
            if self.tree_rpt.exists(iid):
                self.tree_rpt.item(iid, values=(idx, name, val, reason, time_str), tags=tags)
            else:
                self.tree_rpt.insert("", tk.END, iid=iid, values=(idx, name, val, reason, time_str), tags=tags)
            if self.tree_ds.exists(iid):
                self.tree_ds.item(iid, values=(idx, name, val, time_str), tags=tags)

    def _fill_files(self, evt: dict[str, Any]) -> None:
        self.tree_files.delete(*self.tree_files.get_children())
        for ent in evt.get("entries") or []:
            mtime = ent.get("mtime", 0)
            try:
                mt = datetime.fromtimestamp(int(mtime)).strftime("%Y-%m-%d %H:%M:%S")
            except (ValueError, OSError):
                mt = str(mtime)
            self.tree_files.insert(
                "",
                tk.END,
                values=(ent.get("name", ""), ent.get("size", 0), mt),
            )

    def _poll_events(self) -> None:
        for evt in self.bridge.poll_events():
            kind = evt.get("evt")
            if kind == "error":
                self._log(f"错误: {evt.get('msg')}")
            elif kind == "comm_state":
                self._apply_comm_state(evt)
            elif kind == "dataset":
                self._fill_dataset_tree(evt)
            elif kind == "report":
                self._apply_report(evt)
            elif kind == "files":
                self._fill_files(evt)
            elif kind == "datasets":
                names = evt.get("names") or []
                self.cmb_ds["values"] = names
                if names:
                    self.cmb_ds.current(0)
                    self._on_ds_select()
                self._log(f"MMS 数据集 {len(names)} 个")
            elif kind == "rcb_enabled":
                href = evt.get("handlerRef", "")
                self._enabled_rcbs.add(href)
                self.lbl_rcb.configure(text=f"已订阅: {href}")
                self._log(f"RCB 已启用 {href}")
            elif kind == "connected":
                self.lbl_link.configure(text="已连接")
            elif kind == "disconnected":
                self.lbl_link.configure(text="未连接")
        self.after(150, self._poll_events)
