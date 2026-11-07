# Vendored libiec61850

| 项 | 说明 |
|---|---|
| 上游 | [mz-automation/libiec61850](https://github.com/mz-automation/libiec61850) |
| 版本 | **v1.5.3**（与上游 tag 一致） |
| 纳入仓库 | 自独立 clone 改为随 `1U装置稳定性测试` 一并版本管理，便于 0.10 及他机完整复现编译 |

## 相对上游的本地修改

仅 **`src/iec61850/client/client_report.c`**：数据集条目数变化时不丢弃报告（兼容多 RCB 复用同一 RptID 的子站）。

- 补丁原文：`免配置/master_sim/native/patches/libiec61850-report-dataset-size.patch`
- 构建前校验：`build_libiec61850.ps1` 会检查源码中是否含 `report data set size changed`

## 编译

在 `免配置/master_sim` 下：

```powershell
.\build_libiec61850.ps1
```

依赖：Visual Studio 2022（C++）、CMake；产物在 `build-win/`、`bin/mms_master_cli.exe`（目录仍被 gitignore，不提交）。
