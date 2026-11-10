#!/bin/sh
# load.sh - load the mtkwm-airport kext (native AirPort form).
#   usage: scripts/load.sh
# NOTE: every zalloc type-confusion panic so far happened while kernelmanagerd
# was unloading an already-running IO80211 kext.  Only load from a clean
# state (no WiFi kext loaded); if one is running, reboot first.

cd "$(dirname "$0")/.." || exit 1

TARGET="mtkwm-airport"
BUNDLE_ID="com.mt76wm.mtkwm-airport"

KEXT=""
for p in \
    "Build/Debug/$TARGET.kext" \
    "Build/Products/Debug/$TARGET.kext" \
    "Build/Products/Release/$TARGET.kext"
do
    [ -d "$p" ] && KEXT="$p" && break
done

if [ -z "$KEXT" ]; then
    echo "$TARGET.kext not found. Build first:"
    echo "  xcodebuild -project mtkwm.xcodeproj -scheme $TARGET -configuration Debug SYMROOT=Build build"
    exit 1
fi

echo "loading $KEXT ($BUNDLE_ID)"

# Never yank an already-running IO80211 kext at runtime (zalloc panics).
if kmutil showloaded 2>/dev/null | grep -qE "com\.flxxxxddd\.AirportMtkWlan|$BUNDLE_ID"; then
    echo "REFUSING: an mt76wm/AirportMtkWlan kext is already loaded."
    echo "Runtime-unloading an IO80211 kext has panicked kernelmanagerd every time."
    echo "Reboot first, then run this from a clean state."
    exit 1
fi

sudo chown -R root:wheel "$KEXT"
sudo chmod -R go-w "$KEXT"
sudo kmutil load -p "$KEXT" 2>&1

echo "--- check ---"
kextstat 2>/dev/null | grep -i mtkwm || kmutil showloaded --bundle-identifier "$BUNDLE_ID" 2>/dev/null
