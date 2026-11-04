# LED 灯控接口与测试报告

| 项目 | 内容 |
| --- | --- |
| 文档版本 | 1.0 |
| 编制日期 | 2026-10-31 |
| 目标装置 | 192.168.0.10（用户 admin） |
| 测试目录 | `免配置/驱动/test` |
| SDK 路径 | `/usr/local/kemyond/include`、`/usr/local/kemyond/lib` |

---

## 一、概述

本文档说明 KEMYOND 状态指示灯（运行 / 告警 / 备用）软件接口，记录在目标装置上的编译、功能验证及普通用户点灯权限配置方法。

---

## 二、软件接口说明

- 头文件及库文件：`kemyond.h`、`libkemyond.so`
- 动态库位置：`/usr/local/kemyond/lib`
- 头文件位置：`/usr/local/kemyond/include`

程序包含 `kemyond.h` 并链接 `libkemyond` 后，可直接调用接口，**无需额外初始化**。

### 2.1 LED 编号定义

| 宏 | 值 | 含义 |
| --- | --- | --- |
| `LED_RUN` | 0 | 运行灯 |
| `LED_ALARM` | 1 | 告警灯 |
| `LED_BACKUP` | 2 | 备用灯 |

### 2.2 设置指示灯：SetLEDValue

```c
int SetLEDValue(int line, bool value);
```

| 项目 | 内容 |
| --- | --- |
| 功能 | 设置指定指示灯亮灭。 |
| 参数 | `line`：上表 LED 编号；`value`：`true`(1) 亮，`false`(0) 灭。 |
| 返回值 | `1`：设置成功；`-1`：设置失败。 |
| 说明 | 点亮与熄灭成功均返回 `1`。示例：`SetLEDValue(LED_RUN, true);` |

### 2.3 读取指示灯：GetLEDValue

```c
int GetLEDValue(int line);
```

| 项目 | 内容 |
| --- | --- |
| 功能 | 读取指定指示灯状态。 |
| 参数 | `line`：上表 LED 编号。 |
| 返回值 | `0`：读取成功，灯灭；`1`：读取成功，灯亮；`-1`：读取失败。 |
| 说明 | 须先判断 `state < 0` 是否为失败，再解析亮灭。 |

---

## 三、普通用户点灯（权限配置）

接口底层通过 **libgpiod** 访问 `/dev/gpiochip*`。系统默认仅 root 可访问，普通用户调用会返回 `-1`。

部署本目录 `99-kemyond-gpiochip.rules` 并将业务用户加入 `gpio` 组后，**无需 sudo** 即可点灯：

```bash
sudo cp 99-kemyond-gpiochip.rules /etc/udev/rules.d/
sudo groupadd -f gpio
sudo usermod -aG gpio <用户名>
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=gpio --action=add
# 重新登录 SSH 后 supplementary 组生效
export LD_LIBRARY_PATH=/usr/local/kemyond/lib:$LD_LIBRARY_PATH
```

配置成功后，设备节点权限示例：

```text
crw-rw---- 1 root gpio ... /dev/gpiochip0
```

---

## 四、测试程序说明

| 程序 | 源文件 | 用途 |
| --- | --- | --- |
| `test_setled` | `test_setled.c` | 设置单路 LED：`./test_setled <id> <0\|1>` |
| `test_getled` | `test_getled.c` | 读取 LED：`./test_getled <id>` 或 `./test_getled all` |

### 4.1 编译命令（装置上）

```bash
mkdir -p /tmp/kemyond_led_test
# 上传 test_setled.c、test_getled.c 后：
cd /tmp/kemyond_led_test
export LD_LIBRARY_PATH=/usr/local/kemyond/lib:$LD_LIBRARY_PATH
gcc -Wall -Wextra -O2 -I/usr/local/kemyond/include \
  -o test_setled test_setled.c -L/usr/local/kemyond/lib -lkemyond -lgpiod -lpthread
gcc -Wall -Wextra -O2 -I/usr/local/kemyond/include \
  -o test_getled test_getled.c -L/usr/local/kemyond/lib -lkemyond -lgpiod -lpthread
```

### 4.2 运行示例

```bash
export LD_LIBRARY_PATH=/usr/local/kemyond/lib
./test_getled all
./test_setled 0 1    # 运行灯亮
./test_getled 0
./test_setled 0 0    # 运行灯灭
```

---

## 五、测试结果（192.168.0.10）

**环境：** Linux aarch64，内核 4.19.232-rt104，`libkemyond.so` 已安装。

### 5.1 编译

| 项 | 结果 |
| --- | --- |
| `test_setled` | 编译成功 |
| `test_getled` | 编译成功 |

### 5.2 权限未配置时（仅 admin，无 gpio 组）

| 接口 | 结果 |
| --- | --- |
| `GetLEDValue` | 三路均读取失败（`-1`） |
| `SetLEDValue` | 设置失败（`-1`） |
| 原因 | `/dev/gpiochip*` 为 `root:root` 600 |

### 5.3 配置 udev + gpio 组后（普通 admin，无 sudo）

| 用例 | 结果 |
| --- | --- |
| `SetLEDValue(LED_RUN, ON/OFF)` | 成功，读回 ON/OFF 一致 |
| `SetLEDValue(LED_ALARM, ON/OFF)` | 成功，读回一致 |
| `SetLEDValue(LED_BACKUP, ON/OFF)` | 成功，读回一致 |
| `test_getled all` 收尾 | 三路均为 OFF |

**结论：** 接口行为与文档一致；完成 GPIO 权限配置后，普通用户可正常点灯。

---

## 六、附录

### 6.1 udev 规则全文（`99-kemyond-gpiochip.rules`）

```udev
SUBSYSTEM=="gpio", KERNEL=="gpiochip*", GROUP="gpio", MODE="0660"
```

### 6.2 相关文件

- `LED灯控接口文档.md` — 接口原文摘要
- `99-kemyond-gpiochip.rules` — 权限规则
- `test/test_setled.c`、`test/test_getled.c` — 测试源码
