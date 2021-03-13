#!/system/bin/sh
MODDIR=${0%/*}
DATA_DIR="/data/misc/isolatedmagiskhider/"

[ -f "$DATA_DIR/initrc" ] || exit 0

MAGISK_TMP=$(magisk --path) || MAGISK_TMP="/sbin"
INITRC_NAME="init.rc"

# Android 11's new init.rc
[ -f "/init.rc" ] || INITRC_NAME="system/etc/init/hw/init.rc"
INITRC="$MAGISK_TMP/.magisk/rootdir/$INITRC_NAME"

trim() {
    trimmed=$1
    trimmed=${trimmed%% }
    trimmed=${trimmed## }
    echo $trimmed
}

grep_service_name() {
  ARG=$1
  LINE=$(< "$INITRC" grep "service .* $MAGISK_TMP/magisk --$ARG")
  LINE=${LINE#*"service "}
  LINE=${LINE%" $MAGISK_TMP"*}
  trim "$LINE"
}

del_service_name() {
  resetprop --delete "init.svc.$1"
}

hide_initrc() {
  # Wait for boot to complete
  while [ "$(getprop sys.boot_completed)" != "1" ]
  do
    sleep 1
  done

  # Delete Magisk's service in system properities
  POST_FS_DATA=$(grep_service_name "post-fs-data")
  LATE_START_SERVICE=$(grep_service_name "service")
  BOOT_COMPLETED=$(grep_service_name "boot-complete")
  del_service_name "$POST_FS_DATA"
  del_service_name "$LATE_START_SERVICE"
  del_service_name "$BOOT_COMPLETED"
}

hide_initrc &
