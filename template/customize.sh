RIRU_OLD_PATH="/data/misc/riru"
RIRU_NEW_PATH="/data/adb/riru"
RIRU_MODULE_ID="momohider"
DATA_DIR="/data/adb/momohider"
OLD_DATA_DIR="/data/misc/isolatedmagiskhider/"
RIRU_API=0

ui_print "- This is an open source project"
ui_print "- You can find its source code at https://github.com/canyie/Riru-MomoHider"

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

MAGISK_TMP=$(magisk --path) || MAGISK_TMP="/sbin"
MAGISK_CURRENT_RIRU_MODULE_PATH=$MAGISK_TMP/.magisk/modules/riru-core

if [ -f $MAGISK_CURRENT_RIRU_MODULE_PATH/util_functions.sh ]; then
  # Riru V24+, api version is provided in util_functions.sh
  # I don't like this, but I can only follow this change
  RIRU_PATH=$MAGISK_CURRENT_RIRU_MODULE_PATH
  ui_print "- Load $MAGISK_CURRENT_RIRU_MODULE_PATH/util_functions.sh"
  # shellcheck disable=SC1090
  . $MAGISK_CURRENT_RIRU_MODULE_PATH/util_functions.sh

  # Pre Riru 25, as a old module
  if [ "$RIRU_API" -lt 25 ]; then
    ui_print "- Riru API version $RIRU_API is lower than v25"
    RIRU_PATH=$RIRU_NEW_PATH
  fi
elif [ -f $RIRU_OLD_PATH/api_version.new ] || [ -f $RIRU_OLD_PATH/api_version ]; then
  RIRU_PATH=$RIRU_OLD_PATH
elif [ -f $RIRU_NEW_PATH/api_version.new ] || [ -f $RIRU_NEW_PATH/api_version ]; then
  RIRU_PATH=$RIRU_NEW_PATH
else
  abort "! Requirement module 'Riru' is not installed"
fi

if [ $MAGISK_VER_CODE -lt 20200 ]; then
  ui_print "- Removing sepolicy.rule for Magisk $MAGISK_VER"
  rm $MODPATH/sepolicy.rule
fi

if [ $ARCH = "x86" ] || [ $ARCH = "x64" ]; then
  ui_print "- Removing arm libraries for x86 device"
  rm -rf "$MODPATH/riru"
  mv -f "$MODPATH/riru_x86" "$MODPATH/riru"
else
  ui_print "- Removing x86 libraries for arm device"
  rm -rf "$MODPATH/riru_x86"
fi

if [ $IS64BIT == false ]; then
  ui_print "- Removing 64-bit libraries"
  rm -rf $MODPATH/riru/lib64
fi

if [ "$RIRU_API" -lt 25 ]; then
  ui_print "- Old riru api version $RIRU_API"
  mv "$MODPATH/riru" "$MODPATH/system"
  RIRU_MODULE_PATH="$RIRU_PATH/modules/$RIRU_MODULE_ID"
  [ -d $RIRU_MODULE_PATH ] || mkdir -p $RIRU_MODULE_PATH || abort "! Can't create $RIRU_MODULE_PATH: $?"
  cp -f $MODPATH/module.prop $RIRU_MODULE_PATH/module.prop
else
  # Riru v25+, maybe the user upgrade from old module without uninstall
  # Remove the Riru v22's module path to make sure riru knews we're a new module
  RIRU_22_MODULE_PATH="$RIRU_NEW_PATH/modules/$RIRU_MODULE_ID"
  ui_print "- Removing $RIRU_22_MODULE_PATH for new Riru $RIRU_API"
  rm -rf "$RIRU_22_MODULE_PATH"
fi

ui_print "- Preparing data directory"
[ -d $DATA_DIR ] || mkdir -p $DATA_DIR || abort "! Can't create $DATA_DIR"
if [ -d $OLD_DATA_DIR ]; then
  echo "- Migrating previous configures"
  mv -f "$OLD_DATA_DIR/*" "$DATA_DIR"
  rm -rf "$OLD_DATA_DIR"
fi

ui_print "- Setting permissions"
set_perm_recursive $MODPATH 0 0 0755 0644
set_perm_recursive $DATA_DIR 0 0 0700 0600 u:object_r:system_data_file:s0
