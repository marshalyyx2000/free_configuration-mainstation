# Windows 登录（Microsoft 密码仍失败）处理说明

## 当前环境说明

- **10.237.1.16** 若 Ping 的 **TTL≈128** 且 SSH 横幅为 `OpenSSH_for_Windows`，说明当前进的是 **Windows**，不是 Ubuntu。
- 上次 **解锁 SAM / 删 NGC** 必须在 **Ubuntu 下离线改 C 盘** 才生效；若一直进 Windows，或改完盘后又从 Linux 挂载过 C 盘，都可能仍登不进。
- 我这边用 `by/by` **已无法 SSH**（Windows 上通常没有该 Linux 用户），需要你在 **Ubuntu 本机** 执行下面脚本。

## 为什么 Microsoft 密码仍可能失败

1. **登录页未连网**：清理过凭据缓存后，Microsoft 账户常需 **WiFi 已连接** 才能在线校验密码（不是 PIN）。
2. **仍在用 PIN/人脸**：必须点 **「登录选项」→「密码」**，再输入 **完整邮箱 + 密码**。
3. **密码本身不对 / 未同步**：到 https://account.microsoft.com 改密后，用 **新密码** 试（旧 PIN 与密码无关）。
4. **本地仍锁定**：若之后又输错多次，需再在 Ubuntu 下跑一次解锁脚本。
5. **仅清除了部分 NGC**：需再清 TokenBroker、IdentityCache 等（见 `fix_win_login_deep.sh`）。

## 推荐操作（请按顺序）

### 步骤 1：重启进 Ubuntu

GRUB 里选 **Ubuntu**，本机登录 `by`，打开终端。

### 步骤 2：执行深度修复脚本

把仓库里的 `fix_win_login_deep.sh` 拷到该机器（U 盘 / scp 均可），然后：

```bash
chmod +x fix_win_login_deep.sh
sudo bash fix_win_login_deep.sh
```

脚本会：

- 再次 **解锁 marshalyyx**
- **启用内置 Administrator 并清空其本地密码**（备用入口）
- 备份并移走 **PIN/微软账户本地缓存**（Ngc、TokenBroker、IdentityCache 等）
- **卸载** C 盘

### 步骤 3：重启进 Windows

1. 登录界面右下角 **网络图标** → 连上 WiFi。  
2. **优先试备用账户**（Win11 有时需点「其他用户」）：  
   - 用户名：`Administrator` 或 `.\Administrator`  
   - 密码：**留空**（脚本已清空本地密码）  
3. 若必须用主账户：「登录选项」→ **密码** → 邮箱 **marshalyyx 对应完整 Microsoft 邮箱** + **account.microsoft.com 上确认过的密码**。  
4. 进桌面后：**设置 → 账户 → 登录选项** → 删除 PIN → 重新设置；建议关闭「快速启动」：**控制面板 → 电源 → 选择电源按钮的功能 → 取消「启用快速启动」**（避免双系统 C 盘被占用）。

### 步骤 4：若 Administrator 也进不去

- 开机 **长按 Shift → 重启 → 疑难解答 → 高级选项 → 启动设置 → 重启 → 选带网络的安全模式**，再试 `Administrator` 空密码。  
- 或回到 Ubuntu 再跑一遍脚本，并把登录页 **完整英文错误**（或照片）发出来继续查。

## 从本机 Windows 拷贝脚本到 Ubuntu（示例）

在 **能 SSH 到 Ubuntu** 时（TTL 64、横幅为 OpenSSH_xxx Linux）：

```powershell
scp "C:\工程项目开发\1U装置稳定性测试\fix_win_login_deep.sh" by@10.237.1.16:~/
```
