#!/system/bin/sh
MODDIR=${0%/*}

rm -f /data/adb/service.d/qg-otomatik.sh
pkill -f qg-daemon 2>/dev/null

chmod 755 "$MODDIR/qg-daemon"
exec "$MODDIR/qg-daemon"
