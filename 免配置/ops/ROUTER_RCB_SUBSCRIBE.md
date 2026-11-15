# 主站订阅 → ser501「订阅状况」透传排查

## 现象

- SimMasterQt「主站会话」= 已订阅（且 CLI `verified` 为真）
- FZ 装置页「订阅状况」仍为 `-`
- 主站下表「前值」与 FZ 不一致，且用户未点总召唤

## 结论分层

1. **主站 UI**：`已订阅` 仅表示本端 `mms_master_cli` 在**子站合并模型**上 `RptEna` 回读成功（1.26+）。
2. **FZ**：`-` 表示 `ser501` 进程内无 `RptEna=true` 的活动连接（`fz_build_rpt_subscription_body`）。
3. 中间 **iec61850router** 须把对 `P_*PROT/LLN0$BR$*` 的写操作落到装置 MMS。

## 快速对照

```text
python ops/verify_p_b5012a_relayena_subscribe.py
```

- CLI 输出应含 `"verified":` 无 false，`handlerRef`/`rcbGetRef`/`dsRef` 与软压板行一致（勿订错 `brcbRelayBlk`）。
- body 中期望行：`PROT/dsRelayEna|brcbRelayEna|1|...`

## 若 CLI verified 但 FZ 仍 `-`

1. 子站 `supervision.cfg` 是否存在 `P_B5012A` **PROT** LD 的 `RC(... brcbRelayEna ...)`，实例 **01**（对上订阅恒为 01）。
2. router 日志写 RCB / ProxyWrite（`ops/_remote/write_mech.py`、`ops/_remote/log_writes.py`）。
3. 装置 `ser501` MMS 连接应来自子站网关 IP（FZ 左侧「MMS 连接」），非主站 PC 直连。

修复一般在 **子站 router / supervision.cfg 生成**，非 SimMasterQt；模型生成需保留 PROT/CTRL/MEAS 代理 LD（见 `CollapsedLDevicePruner` 与 `诊断-主站订阅召唤失败-修复记录.md`）。