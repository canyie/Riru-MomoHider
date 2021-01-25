#!/system/bin/sh

MODDIR=${0%/*}

[ -f $MODDIR/sepolicy.rule ] && exit 0

magiskpolicy --live "allow zygote * filesystem { unmount }"
