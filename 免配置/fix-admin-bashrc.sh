#!/bin/bash
# 修复 admin ~/.bashrc 中被误写入的 Windows PATH
set -e
BC="$HOME/.bashrc"
cp -a "$BC" "$BC.bak.fix" 2>/dev/null || true
grep -v 'CF-FV' "$BC" | grep -v 'WORK_HOME=.*router' | grep -v 'LD_LIBRARY_PATH=.*maintain' > /tmp/bashrc.clean || true
# 去掉末尾空行后追加
sed -i '/^$/d' /tmp/bashrc.clean 2>/dev/null || true
cat /tmp/bashrc.clean > "$BC"
cat >> "$BC" << 'ENDPATH'

# configuration-free (Linux paths)
export WORK_HOME="$HOME/router"
export PATH="$HOME/router/bin:$HOME/maintain/bin:/usr/local/bin:/usr/bin:/bin:/sbin"
export LD_LIBRARY_PATH="$HOME/router/lib:$HOME/maintain/lib:$HOME/maintain/bin"
ENDPATH
echo "Fixed $BC"
