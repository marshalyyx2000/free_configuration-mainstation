# LED 灯控接口文档

## 一、软件接口说明

- 头文件及库文件：`kemyond.h`、`libkemyond.so`
- 动态库位置：`/usr/local/kemyond/lib`
- 头文件位置：`/usr/local/kemyond/include`

以上目录为 SDK 安装包的默认安装目录。程序包含 `kemyond.h` 并链接接口库后，可直接调用以下接口，无需初始化。

### 权限（普通用户点灯）

接口通过 `libgpiod` 访问 `/dev/gpiochip*`，默认仅 root 可用。部署本目录下的 `99-kemyond-gpiochip.rules` 并将运行用户加入 `gpio` 组后，**无需 sudo** 即可点灯：

```bash
sudo cp 99-kemyond-gpiochip.rules /etc/udev/rules.d/
sudo groupadd -f gpio
sudo usermod -aG gpio <用户名>
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=gpio --action=add
# 重新登录 SSH 后生效
export LD_LIBRARY_PATH=/usr/local/kemyond/lib:$LD_LIBRARY_PATH
```

### 1. 状态指示灯接口

#### 1.1 设置指示灯状态：SetLEDValue

```c
int SetLEDValue(int line, bool value);
```

| 项目 | 内容 |
| --- | --- |
| 功能 | 设置指示灯状态。 |
| 参数 | `int line`：`LED_RUN`（0）—运行灯；`LED_ALARM`（1）—告警灯；`LED_BACKUP`（2）—备用灯。<br>`bool value`：`true`（1）—亮；`false`（0）—灭。 |
| 返回值 | `1`：设置成功。<br>`-1`：设置失败。 |
| 说明 | 设置指定状态指示灯亮灭，点亮和熄灭成功均返回 `1`。<br>调用示例：`SetLEDValue(LED_RUN, true);` 点亮运行灯；`SetLEDValue(LED_RUN, false);` 熄灭运行灯。 |

#### 1.2 获取指示灯状态：GetLEDValue

```c
int GetLEDValue(int line);
```

| 项目 | 内容 |
| --- | --- |
| 功能 | 获取指示灯状态。 |
| 参数 | `int line`：`LED_RUN`（0）—运行灯；`LED_ALARM`（1）—告警灯；`LED_BACKUP`（2）—备用灯。 |
| 返回值 | `0`：读取成功，灯灭。<br>`1`：读取成功，灯亮。<br>`-1`：读取失败。 |
| 说明 | 读取指定状态指示灯亮灭状态。<br>调用示例：`int state = GetLEDValue(LED_RUN);` 读取运行灯状态。先判断 `state < 0` 是否失败，再判断亮灭。 |
