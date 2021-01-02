#!/system/bin/sh

MODDIR=${0%/*}
touch /data/local/tmp/working_mark

[ -f $MODDIR/sepolicy.rule ] && exit 0

magiskpolicy --live "allow zygote labeledfs filesystem { unmount }"
