#!/bin/sh
# load.sh - load the mtkwm-airport kext (native AirPort form).
#   usage: scripts/load.sh
# The old HeliPort form is gone (it shared OS class names with this kext and
# alternating the two provoked zalloc type-confusion panics).

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
# Unload ours and the community AirportMtkWlan (same PCI device) first.
for bid in com.mt76wm.mtkwm-airport com.mt76wm.mtkwm com.flxxxxddd.AirportMtkWlan; do
    sudo kmutil unload --bundle-identifier "$bid" 2>/dev/null \
        || sudo kextunload -b "$bid" 2>/dev/null \
        || true
done
sudo chown -R root:wheel "$KEXT"
sudo chmod -R go-w "$KEXT"

if command -v kmutil >/dev/null 2>&1; then
    sudo kmutil load -p "$KEXT"
else
    sudo kextutil -v 6 "$KEXT"
fi

echo "--- check ---"
kextstat 2>/dev/null | grep -i mtkwm || kmutil showloaded --bundle-identifier "$BUNDLE_ID" 2>/dev/null
