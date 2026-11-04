# master_sim — Windows 仿真主站

从 `26579.SCD` 生成免配置下装包，经装置 **2405** 运维口下装并生效 61850。

## 快速开始

**图形界面（推荐）**

```powershell
cd 免配置\master_sim\sim_master_qt
$env:QT6_DIR = "C:\Qt\6.7.3\msvc2019_64"
.\build_sim_master.ps1 -Configuration Release
.\build-qt\Release\sim_master_qt.exe
# 或 .\install_sim_master.ps1 安装到 %LOCALAPPDATA%\Programs\SimMasterQt
```

一体化窗口 **「免配置仿真主站」**：Tab **运维/下装**（生成、104 下装、等 102、MMS 检测）+ Tab **61850 主站**（全站订阅/事件）。仍可用 Python 后备：`python gui.py`。

**命令行**

```powershell
cd 免配置\master_sim
python build_config.py
python run_lab.py --skip-upload   # 仅生成 out/
python run_lab.py                 # 下装到 192.168.0.10
```

## 文件

| 脚本 | 作用 |
|------|------|
| `build_config.py` | `out/IEDList.xml`、`out/station.SMCD`、`out/station.zip` |
| `iec104_client.py` | IEC104 + 南网运维 103 下装 |
| `gui.py` | 图形界面：生成 / 下装 / 一键流程 / 端口探测 |
| `lab_ops.py` | 实验室操作库（GUI 与 CLI 共用） |
| `run_lab.py` | 生成 + 下装 + 可选 MMS 探测 |
| `mms_client.py` | 102 端口探测 + 通信工况总召（需 `bin/mms_master_cli.exe`） |
| `mms_master.py` / `native/mms_master_cli.c` | JSONL MMS 客户端进程 |
| `iec61850_ui.py` | GUI「61850 主站」页 |
| `build_libiec61850.ps1` | 编译 libiec61850 + CLI |
| `add_sim_ips.ps1` | 本机添加 10.100.100.x（管理员） |

## Qt6 免配置仿真主站（sim_master_qt）

Windows 原生 **免配置仿真主站**（Qt6）：**运维/下装** + **61850 主站** 同一程序；连接免配置子站 `192.168.0.10`（2405 运维 / 102 MMS），含装置树、多 Tab 报告、**全站订阅**、**全站事件**（拼音多段搜索）。配置生成与 104 下装为 **纯 C++**，不依赖 Python 运行时。

**依赖**：VS 2022 Build Tools、CMake、Qt 6.7+（MSVC 套件）、先运行 `build_libiec61850.ps1`。

```powershell
cd 免配置\master_sim\sim_master_qt
$env:QT6_DIR = "C:\Qt\6.7.3\msvc2019_64"
.\build_sim_master.ps1 -Configuration Release
.\verify_sim_master.ps1
.\package_sim_master.ps1   # 生成 NSIS 包，成功后 VERSION +0.01
```

安装包默认输出：`C:\Temp\SimMasterQt_cpack\SimMasterQt-<版本>-win64.exe`（并尝试复制到 `sim_master_qt\dist\`）。

计划文档：[`docs/PLAN_sim_master_qt.md`](docs/PLAN_sim_master_qt.md)、[`docs/PLAN_sim_master_qt_full.md`](docs/PLAN_sim_master_qt_full.md)

## 61850 主站界面

1. 安装 **CMake 4.x**（已可用：`winget install Kitware.CMake`）与 **Visual Studio 2022 Build Tools**（勾选 **使用 C++ 的桌面开发 / VCTools**）。
2. 在 **Windows 本机**（不要用 WSL）编译：

```powershell
cd 免配置\master_sim
git clone --depth 1 --branch v1.5.3 https://github.com/mz-automation/libiec61850.git third_party\libiec61850
powershell -ExecutionPolicy Bypass -File .\build_libiec61850.ps1
```

3. `python gui.py` → 切到 **61850 主站** → 连接子站 102。

功能：通信工况 `dsCommState`、按装置读数据集、订阅 BRCB/URCB 看变位、录波目录列表。连接目标为 **SubStation** 合并站模型，不直连各 IED IP。

命令行总召：

```powershell
python mms_client.py --host 192.168.0.10 --port 102
```

### 验收（61850 主站页）

1. 连接 `SubStation` 成功（102 已监听、子站 router 已运行）。
2. **刷新通信工况** 显示 `summary: N/N connected`，与 SCDPro 装置连接数一致。
3. 选中某装置（如 `E1Q1SB30`），数据集选 `dsRelayDin`，**读数据集** 可见与 SCDPro 遥信一致的点值。
4. 报告页选 `brcbRelayDin`，**订阅** 后在 SCDPro 变位，主站表格实时更新（变位行高亮）。
5. **录波文件** 页列 `/COMTRADE/`（或台账 `wave_path`），能收到目录应答（空目录也算成功）。

## 前置

- 装置 `MaintainServer` 监听 2405；`MaintainServer.ini` 中 `p61850=/home/admin/router/bin/iec61850router`（或启动前 `PATH` 含 `~/router/bin`）
- 装置已 `apt install unzip`；路由：`10.100.100.0/24 via 192.168.0.27`（重启装置会丢，需重加）
- 下装 `station.zip` 后首次生成模型在 RK3568 上约 **2 分钟内**可监听 102（`genconfig.jar` 约 20 秒；SMCD 未变时会复用已有 `supervision.cfg`）
- SCDPro 加载同份 SCD；本机 `add_sim_ips.ps1`（管理员）添加 `10.100.100.x`

## 通信工况（dsCommState）

数据集用各装置都存在的 `LPHD1.Proxy.stVal` 表示通信状态。SCD 里各型号装置的
`GOAlmGGIO1` 只有 13 台有，早期版本用它做数据集，会让 genconfig 生成不存在
的条目（读出来是 `error 10`）。

下装并等 102 起来后验证：

```powershell
cd 免配置\ops
$env:SUBST_PASS='...'
python mms_gi.py 127.0.0.1 102 'SubStationLD0/LLN0$dsCommState'
# 期望: summary: 30/30 connected
```

## ops 目录（装置运维脚本）

凭证统一从环境变量 `SUBST_PASS` 读取，不写进仓库。

| 脚本 | 作用 |
|------|------|
| `ops/remote.py "<cmd>"` | 在装置上执行一条命令 |
| `ops/download.py` | 下装 `out/` 里的包并发生效命令 |
| `ops/wait_and_gi.py` | 等 102 起来后做通信工况总召 |
| `ops/mms_gi.py [host] [port] [ds]` | 在装置上编译并运行 MMS 总召客户端 |
| `ops/deploy_router.py [--install]` | 同步改过的子站源码并重新编译 |
| `ops/verify_link.py` | 黑洞一台装置验证通信工况会变 false |

