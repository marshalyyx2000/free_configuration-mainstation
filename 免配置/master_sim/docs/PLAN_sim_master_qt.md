# Qt6 仿真主站（sim_master_qt）

与 Cursor 计划同步；实施以本仓库 `sim_master_qt/` 源码为准。

## 目标

- Windows + Qt6 + libiec61850（经 `mms_master_cli` JSONL 桥）
- 左 IED 树、右多 Tab 报告页、全站订阅、全站变位事件列表（拼音多段搜索、装置/RCB 级展示）
- Release 编译、NSIS 安装包、VERSION 每成功打包 +0.01

## 构建

```powershell
cd 免配置\master_sim
.\build_libiec61850.ps1
cd sim_master_qt
$env:QT6_DIR = "C:\Qt\6.7.3\msvc2019_64"
.\build_sim_master.ps1 -Configuration Release
.\verify_sim_master.ps1
.\package_sim_master.ps1
```

完整设计见 [`docs/PLAN_sim_master_qt_full.md`](PLAN_sim_master_qt_full.md)（自 Cursor 计划导出）。
