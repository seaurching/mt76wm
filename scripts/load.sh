#!/bin/sh
# load.sh - load the mtkwm kext (development).
# Product location differs across Xcode versions; probe the known paths.

cd "$(dirname "$0")/.." || exit 1

KEXT=""
for p in \
    Build/Debug/mtkwm.kext \
    Build/Products/Debug/mtkwm.kext \
    Build/Products/Release/mtkwm.kext
do
    [ -d "$p" ] && KEXT="$p" && break
done

if [ -z "$KEXT" ]; then
    echo "mtkwm.kext not found. Build first:"
    echo "  xcodebuild -project mtkwm.xcodeproj -scheme mtkwm -configuration Debug SYMROOT=Build build"
    exit 1
fi

echo "loading $KEXT"
# Unload any previous instance first (kmutil is the modern interface;
# kextunload is kept as a legacy fallback).
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
kextstat 2>/dev/null | grep -i mtkwm || kmutil showloaded --bundle-identifier com.mt76wm.mtkwm 2>/dev/null
