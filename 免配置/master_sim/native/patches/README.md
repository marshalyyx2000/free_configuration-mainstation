# libiec61850 本地补丁

## libiec61850-report-dataset-size.patch

**问题**：部分服务端（含免配置子站）把同一个 IED 下所有 RCB 的 `rptID="NULL"` 统一改写成
`<IED>NULL`。libiec61850 客户端按 RptID 分派报告时，同一 RptID 只会命中第一个安装的
报告处理器；当后续报告的数据集条目数与第一个处理器缓存的不一致时，`client_report.c`
会把报告当成 malformed 直接丢弃，回调不会执行，主站因此既没有变位事件也没有日志。

**补丁**：数据集条目数变化时，释放并按新长度重建 `dataSetValues` / `dataReferences` /
`reasonForInclusion`，不再丢弃报告；配合 `mms_master_cli.c` 按报告 DatSet 反查正确的 RCB，
即可在 RptID 复用时仍把报告归属到正确的数据集。

同一文件里还按 RCB 引用和 `<IED>NULL` 匹配报告标识（`native/rpt_id_match.h`）。
只按本地对象引用登记处理器时，子站上送的 `<IED>NULL` 对不上，总召唤回调不会执行，点表后值保持为空。

**应用**（全新 clone 上游 v1.5.3 时；本仓库已 vendored 并含此修改，一般无需再 apply）：

```powershell
cd master_sim\third_party\libiec61850
git apply ..\..\native\patches\libiec61850-report-dataset-size.patch
```

应用后再运行 `build_libiec61850.ps1`。如果补丁未应用，构建脚本会给出警告。详见 `third_party/libiec61850/VENDOR.md`。

> 根本修复在子站侧（`configuration-free/iec61850_router/src/server/config/config_generator.cpp`），
> 为每个 RCB 生成唯一 RptID；本补丁用于兼容已经部署、仍复用 RptID 的子站。
