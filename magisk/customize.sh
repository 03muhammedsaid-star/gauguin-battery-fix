ui_print "*****************************************"
ui_print "*   Gauguin Battery Gauge Fix (Native C)*"
ui_print "*   Author: muhammed                    *"
ui_print "*****************************************"
ui_print "- Installing native QG battery daemon..."
set_perm "$MODPATH/qg-daemon" 0 0 0755
set_perm "$MODPATH/service.sh" 0 0 0755
rm -f /data/adb/service.d/qg-otomatik.sh
ui_print "- Installation successful! Please reboot your device."
