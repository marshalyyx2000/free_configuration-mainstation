#!/bin/sh
# 子站 router/MaintainServer 调用：固定堆与 GC，Java 8 兼容。
JAR="${GENCONFIG_JAR:-/home/admin/router/bin/genconfig.jar}"
exec java \
  -Xms128m \
  -Xmx512m \
  -XX:+UseSerialGC \
  -XX:+ExitOnOutOfMemoryError \
  -jar "$JAR" "$@"
