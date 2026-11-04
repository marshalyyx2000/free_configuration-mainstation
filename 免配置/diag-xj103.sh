#!/bin/bash
set -u
BIN=/home/admin/maintain/bin
cd "$BIN"
sed -i 's/\r$//' MaintainServer.ini XJCommuServer103.ini
sed -i 's/^loglevel=.*/loglevel=3/' MaintainServer.ini XJCommuServer103.ini

echo "===== before file exists ====="
rm -f Xj_Log/0_STServer.txt
LD_LIBRARY_PATH=/home/admin/maintain/lib:/home/admin/maintain/bin \
  timeout 3 ./XJCommuServer103 >/tmp/xj-stdout.txt 2>&1 || true
echo "-- stdout --"
cat /tmp/xj-stdout.txt
echo "-- STServer log --"
cat Xj_Log/0_STServer.txt 2>/dev/null || echo "(no STServer log)"
echo "-- listen --"
ss -lntp | grep 2404 || echo "2404 not listening"

echo "===== create ledger and retry ====="
mkdir -p /home/ipofas/stdown/maintain/CONF /home/ipofas/stdown/maintain/SMCD
cp -f /CONF/IEDList.xml /home/ipofas/stdown/maintain/CONF/IEDList.xml
printf 'placeholder\n' > /home/ipofas/stdown/maintain/SMCD/station.SMCD
rm -f Xj_Log/0_STServer.txt
LD_LIBRARY_PATH=/home/admin/maintain/lib:/home/admin/maintain/bin \
  ./XJCommuServer103 >/tmp/xj-stdout2.txt 2>&1 &
PID=$!
sleep 2
echo "-- stdout2 --"
cat /tmp/xj-stdout2.txt
echo "-- STServer log2 --"
cat Xj_Log/0_STServer.txt 2>/dev/null || echo "(no STServer log)"
echo "-- process/port --"
ps -p "$PID" -o pid,stat,cmd || echo "process exited"
ss -lntp | grep 2404 || echo "2404 not listening"
# leave it running if it stayed up
