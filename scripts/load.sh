#!/bin/sh
# load.sh - load a mtkwm kext (development).
#   usage: scripts/load.sh [mtkwm|mtkwm-airport]   (default: mtkwm-airport)
# The two kexts share OS class names (CTimeout, ItlMt76, ...) so they must
# never be loaded at once -- both bundle ids are unloaded first.

cd "$(dirname "$0")/.." || exit 1

TARGET="${1:-mtkwm-airport}"
case "$TARGET" in
    mtkwm-airport) BUNDLE_ID="com.mt76wm.mtkwm-airport" ;;
    mtkwm)         BUNDLE_ID="com.mt76wm.mtkwm" ;;
    *) echo "unknown target '$TARGET' (want mtkwm or mtkwm-airport)"; exit 1 ;;
esac

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
# Never co-load the two forms: shared OS class names would collide.
sudo kmutil unload --bundle-identifier com.mt76wm.mtkwm-airport 2>/dev/null \
    || sudo kextunload -b com.mt76wm.mtkwm-airport 2>/dev/null \
    || true
sudo kmutil unload --bundle-identifier com.mt76wm.mtkwm 2>/dev/null \
    || sudo kextunload -b com.mt76wm.mtkwm 2>/dev/null \
    || true
sudo chown -R root:wheel "$KEXT"
sudo chmod -R go-w "$KEXT"

if command -v kmutil >/dev/null 2>&1; then
    sudo kmutil load -p "$KEXT"
else
    sudo kextutil -v 6 "$KEXT"
fi

echo "--- check ---"
kextstat 2>/dev/null | grep -i mtkwm || kmutil showloaded --bundle-identifier "$BUNDLE_ID" 2>/dev/null
