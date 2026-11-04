#!/bin/bash
# 在 10.237.1.16 的 Ubuntu 上执行（需 sudo）。Windows 需已关机或未挂载 C:。
set -euo pipefail

WIN=/mnt/winos
DEV=/dev/nvme0n1p3
SAM="$WIN/Windows/System32/config/SAM"
USER_NAME=marshalyyx
USER_DIR="$WIN/Users/$USER_NAME"

if [[ $EUID -ne 0 ]]; then
  echo "请: sudo bash $0"
  exit 1
fi

mkdir -p "$WIN"
if ! mountpoint -q "$WIN"; then
  mount -t ntfs-3g "$DEV" "$WIN"
fi

echo "=== 1) 解锁 $USER_NAME，清零失败次数 ==="
printf '2\nq\ny\n' | chntpw -u "$USER_NAME" "$SAM" || true

echo "=== 2) 启用内置 Administrator，清空本地密码（备用登录）==="
printf '2\n1\nq\ny\n' | chntpw -u Administrator "$SAM" || true

echo "=== 3) 清理 PIN / Hello / 微软账户本地凭据缓存 ==="
TS=$(date +%Y%m%d_%H%M%S)
BK="$USER_DIR/AppData/Local/Microsoft/_login_fix_$TS"
mkdir -p "$BK"
for rel in \
  Ngc \
  Windows/TokenBroker \
  Windows/CloudAPCache \
  IdentityCache \
  Credentials \
  Vault \
  ; do
  src="$USER_DIR/AppData/Local/Microsoft/$rel"
  if [[ -e "$src" ]]; then
    mv "$src" "$BK/$(echo "$rel" | tr / _)"
    echo "  moved $rel"
  fi
done
SVC_NGC="$WIN/Windows/ServiceProfiles/LocalService/AppData/Local/Microsoft/Ngc"
[[ -d "$SVC_NGC" ]] && mv "$SVC_NGC" "${BK}_LocalService_Ngc" && echo "  moved LocalService Ngc"

# Web 账户 broker 包缓存（若存在）
PKG="$USER_DIR/AppData/Local/Packages"
if [[ -d "$PKG" ]]; then
  find "$PKG" -maxdepth 1 -type d \( -iname '*Account*' -o -iname '*AAD*' -o -iname '*WebAccount*' \) 2>/dev/null | while read -r d; do
    mv "$d" "$BK/pkg_$(basename "$d")" && echo "  moved package $(basename "$d")"
  done
fi

echo "=== 4) 登录页显示内置 Administrator（离线 SOFTWARE）==="
# 需 hivex 或手动；此处用 reged/chntpw 较繁琐，仅提示 Safe Mode 可用 Administrator

echo "=== 5) 状态 ==="
chntpw -l "$SAM" | tail -10
echo "备份目录: $BK"

sync
umount "$WIN" || true
echo ""
echo "完成。请重启进 Windows："
echo "  A) 先连 WiFi，登录页选「密码」，用 Microsoft 账户完整邮箱 + 最新密码；"
echo "  B) 若仍失败：选「其他用户」→ 用户名 Administrator → 密码留空（或你本机习惯的空密码）；"
echo "     进系统后在「设置→账户」里修复 marshalyyx 或改密。"
