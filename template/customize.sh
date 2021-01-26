RIRU_OLD_PATH="/data/misc/riru"
RIRU_NEW_PATH="/data/adb/riru"
RIRU_MODULE_ID="isolatedmagiskhider"

ui_print "- This is an open source project"
ui_print "- You can find its source code at https://github.com/canyie/Riru-IsolatedMagiskHider"

if [ $ARCH != "arm" ] && [ $ARCH != "arm64" ] && [ $ARCH != "x86" ] && [ $ARCH != "x64" ]; then
  abort "! Unsupported platform: $ARCH"
else
  ui_print "- Device platform: $ARCH"
fi

if [ $API -lt 24 ]; then
  abort "! Unsupported Android API level $API"
else
  ui_print "- Android API level: $API"
fi

if [ -f $RIRU_OLD_PATH/api_version.new ] || [ -f $RIRU_OLD_PATH/api_version ]; then
  RIRU_PATH=$RIRU_OLD_PATH
elif [ -f $RIRU_NEW_PATH/api_version.new ] || [ -f $RIRU_NEW_PATH/api_version ]; then
  RIRU_PATH=$RIRU_NEW_PATH
else
  abort "! Requirement module 'Riru - Core' is not installed"
fi

ui_print "- Extracting module files"
unzip -o $ZIPFILE module.prop uninstall.sh sepolicy.rule post-fs-data.sh -d $MODPATH >&2 || abort "! Can't extract module files: $?"

if [ $MAGISK_VER_CODE -lt 20200 ]; then
  ui_print "- Removing sepolicy.rule for Magisk $MAGISK_VER"
  rm $MODPATH/sepolicy.rule
fi

if [ $ARCH = "x86" ] || [ $ARCH = "x64" ]; then
  ui_print "- Extracting x86 libraries"
  unzip -o $ZIPFILE system_x86/* -d $MODPATH >&2 || abort "! Can't extract system/: $?"
  mv $MODPATH/system_x86 $MODPATH/system
else
  ui_print "- Extracting arm libraries"
  unzip -o $ZIPFILE system/* -d $MODPATH >&2 || abort "! Can't extract system/: $?"
fi

if [ $IS64BIT == false ]; then
  ui_print "- Removing 64-bit libraries"
  rm -rf $MODPATH/system/lib64
fi

RIRU_MODULE_PATH="$RIRU_PATH/modules/$RIRU_MODULE_ID"

ui_print "- Extracting riru files"

[ -d $RIRU_MODULE_PATH ] || mkdir -p $RIRU_MODULE_PATH || abort "! Can't create $RIRU_MODULE_PATH: $?"
cp -f $MODPATH/module.prop $RIRU_MODULE_PATH/module.prop || abort "! Can't copy module.prop to $RIRU_MODULE_PATH: $?"

ui_print "- Setting permissions"
set_perm_recursive $MODPATH 0 0 0755 0644
