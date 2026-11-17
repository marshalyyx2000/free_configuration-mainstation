# master_sim — Windows 仿真主站

> **产品入口只有 SimMasterQt。** Python `gui.py` / `run_lab.py` 为历史参考，不作为验收路径。

> **2026-11-09 修复**：子站 `station.SMCD` 的 `RptEnabled max=16/12/18` 曾把 RCB 实例放大到 **31,312** 条，`iec61850router` 启动和每 10ms 扫描被拖到 100% CPU，主站订阅/总召全部超时。已现场收敛为 `max=1`（2,152 RC / 404 LD），并修复 Java 8 下 `genconfig.jar` 的 `Files.readString`、`SubStationLD0` 域名和 LPHD 重复问题。排查与验证详见 [`../诊断-主站订阅召唤失败-修复记录.md`](../诊断-主站订阅召唤失败-修复记录.md)。

从 `26579.SCD` 生成免配置下装包，经装置 **2405** 运维口下装并生效 61850，再以 MMS 客户端读子站 **102** 通信工况。

架构说明：[`sim_master_qt/docs/MASTER_ARCHITECTURE.md`](sim_master_qt/docs/MASTER_ARCHITECTURE.md)

## 命令行（无窗口）

`sim_master_qt.exe` 子命令；`connect` 常驻 MMS，其它命令在另一终端发给该会话。

```powershell
.\sim_master_qt.exe deploy --scd "D:\研发\1U装置稳定性测试\免配置\子站scd\26579.SCD" --wait-102
.\sim_master_qt.exe connect --host 192.168.0.10
# 大 SCD 建议：.\sim_master_qt.exe connect --host 192.168.0.10 --prefer-smcd-fcda
# 另一终端：
.\sim_master_qt.exe status
.\sim_master_qt.exe subscribe --rcb E1Q1SB30PROT/LLN0$BR$brcbRelayDin
.\sim_master_qt.exe gi --rcb E1Q1SB30PROT/LLN0$BR$brcbRelayDin
.\sim_master_qt.exe subscribe --ied E1Q1SB30
.\sim_master_qt.exe gi --ied E1Q1SB30
.\sim_master_qt.exe subscribe --all
.\sim_master_qt.exe gi --all
.\sim_master_qt.exe disconnect
```

验收：`python sim_master_qt/tools/cli_acceptance.py --stage small`（通过后再 `--stage large`）。同一时刻不要再开 GUI 连 102。

## 快速开始（SimMasterQt）

```powershell
cd 免配置\master_sim\sim_master_qt
$env:QT6_DIR = "C:\Qt\6.7.3\msvc2019_64"
.\build_sim_master.ps1 -Configuration Release
.\build-qt\Release\sim_master_qt.exe
# 或 .\install_sim_master.ps1 安装到 %ProgramFiles%\SimMasterQt（需管理员/UAC）
```

一体化窗口 **「免配置仿真主站」**：Tab **运维/下装**（生成、104 下装、等 102、MMS 检测）+ Tab **61850 主站**（全站订阅/事件）。

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
2. 在 **Windows 本机**（不要用 WSL）编译 libiec61850 + CLI：

```powershell
cd 免配置\master_sim
powershell -ExecutionPolicy Bypass -File .\build_libiec61850.ps1
```

3. 打开 `sim_master_qt.exe` → 切到 **61850 主站** → 连接子站 102。

功能：通信工况 `dsCommState`、按装置读数据集、订阅 BRCB/URCB 看变位、录波目录列表。连接目标为 **SubStation** 合并站模型，不直连各 IED IP。

命令行总召（开发调试）：

```powershell
# JSONL：connect + read_comm_state，见 native/mms_master_cli.c
# 或
python mms_client.py --host 192.168.0.10 --port 102
```

### 验收（61850 主站页）

1. 连接 `SubStation` 成功（102 已监听、子站 router 已运行）。
2. **刷新通信工况** 日志含 `ref=SubStationLD0/LLN0$dsCommState` 与 `source=directory|dataset`（或后备 `smcd_fcda`），条目数与台账一致；`summary` 通断与 SCDPro 现场有关。
3. 选中某装置（如 `E1Q1SB30`），数据集选 `dsRelayDin`，**读数据集** 可见与 SCDPro 遥信一致的点值。
4. 报告页选 `brcbRelayDin`，**订阅** 后在 SCDPro 变位，主站表格实时更新（变位行高亮）。
5. **录波文件** 页列 `/COMTRADE/`（或台账 `wave_path`），能收到目录应答（空目录也算成功）。

## 前置

- 装置 `MaintainServer` 监听 2405；`MaintainServer.ini` 中 `p61850=/home/admin/router/bin/iec61850router`（或启动前 `PATH` 含 `~/router/bin`）
- 装置已 `apt install unzip`；路由：`10.100.100.0/24 via 192.168.0.27`（重启装置会丢，需重加）
- 下装 `station.zip` 后大 SMCD 冷启动 **102 常需 8～15 分钟**（`genconfig.jar` + 装载 `supervision.cfg`）
- SCDPro 加载同份 SCD；本机 `add_sim_ips.ps1`（管理员）添加 `10.100.100.x`

## 通信工况（dsCommState）

与《保信子站免配置技术方案》（20251120）及 **规范版 docx 附录 A** 一致：

- **读点：** `SubStationLD0/LLN0$dsCommState`（不要用 `LD0/LLN0$dsCommState`，不要扫全站 `LPHD1.Proxy` 代替数据集）。
- **SMCD：** `dsCommState` 的 FCDA 通常引用各装置 `LPHD1.Proxy`；生成器按此写入，避免用仅部分型号才有的 `GOAlmGGIO1`（否则 genconfig 会出现不存在对象，`error 10`）。
- **台账顺序：** `IEDList.xml` 中 61850 装置 `no` 与 SMCD 中 FCDA 顺序一致。
- **SimMasterQt / CLI：** `read_comm_state` 固定上述 ref；**默认先 MMS directory / 数据集**，失败且连接未断时才按本地 `station.SMCD` FCDA 顺序读（`source=smcd_fcda`）。CLI 可选 `preferSmcdFcda`（验收脚本环境变量 `SIMMASTER_PREFER_SMCD_FCDA=1`）仅供现场规避，产品 UI 默认关闭。`iedDataSetOnly` 同理：验收脚本可开，产品 UI 默认关。

下装并等 102 稳态（大 SMCD 约 **8～15 分钟**）后验证：

```powershell
cd 免配置\ops
$env:SUBST_PASS='...'
python mms_gi.py 127.0.0.1 102 'SubStationLD0/LLN0$dsCommState'
# 期望: OK … entries=N；summary 通断与现场保护网有关
```

规范版方案文档：`免配置/保信子站免配置技术方案_20251120_规范版.docx`

## 文件

| 路径 | 作用 |
|------|------|
| `sim_master_qt/` | **产品**：运维下装 + 61850 主站 |
| `native/mms_master_cli.c` | JSONL MMS 客户端（Qt 经 QProcess 调用） |
| `build_libiec61850.ps1` | 编译 libiec61850 + CLI → `bin/mms_master_cli.exe` |
| `add_sim_ips.ps1` | 本机添加 10.100.100.x（管理员） |
| `build_config.py` | 对照生成 `out/IEDList.xml`、`station.SMCD`、`station.zip` |
| `iec104_client.py` | 历史 104 运维客户端（对照） |
| `gui.py` / `run_lab.py` / `iec61850_ui.py` | **已废弃**，改用 SimMasterQt |

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
