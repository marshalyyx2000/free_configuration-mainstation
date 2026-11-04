#!/bin/bash
set -e
export WORK_HOME="${HOME}/router"
export PATH="${HOME}/router/bin:${HOME}/maintain/bin:${PATH}"
export LD_LIBRARY_PATH="${HOME}/router/lib:${HOME}/maintain/lib:${HOME}/maintain/bin:${LD_LIBRARY_PATH}"

mkdir -p "${HOME}/maintain/Xj_Log" "${HOME}/maintain/files" "${HOME}/router/router/logs"

cd "${HOME}/maintain/bin"
nohup ./XJCommuServer103 > "${HOME}/maintain/Xj_Log/xj103.log" 2>&1 &
nohup ./MaintainServer > "${HOME}/maintain/Xj_Log/maintain.log" 2>&1 &

# iec61850router requires主站下装的台账与 SMCD，示例：
# nohup iec61850router -d /CONF/IEDList.xml -c /SMCD/station.SMCD > "${HOME}/router/router/logs/iec61850.log" 2>&1 &

echo "Started MaintainServer and XJCommuServer103"
