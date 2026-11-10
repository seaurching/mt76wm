#!/bin/sh
# load.sh - load the native AirPort kext (mwx-based, AirportMtkWlan lineage).
#   usage: scripts/load.sh
# NOTE: every zalloc type-confusion panic so far happened while kernelmanagerd
# was unloading an already-running IO80211 kext.  Only load from a clean
# state (no WiFi kext loaded); if one is running, reboot first.

cd "$(dirname "$0")/.." || exit 1

KEXT=""
for p in \
    build/Debug/sequoia/AirportMtkWlan.kext \
    build/Release/sequoia/AirportMtkWlan.kext
do
    [ -d "$p" ] && KEXT="$p" && break
done

if [ -z "$KEXT" ]; then
    echo "AirportMtkWlan.kext not found. Build first:"
    echo "  MODULE_VERSION=2.0.0 AIRPORT_OS=sequoia ./build-macos.sh Debug AirportMtkWlan"
    exit 1
fi

BUNDLE_ID="com.flxxxxddd.AirportMtkWlan"
echo "loading $KEXT ($BUNDLE_ID)"

if kmutil showloaded 2>/dev/null | grep -q "$BUNDLE_ID"; then
    echo "REFUSING: $BUNDLE_ID is already loaded."
    echo "Runtime-unloading an IO80211 kext has panicked kernelmanagerd every time."
    echo "Reboot first, then run this from a clean state."
    exit 1
fi

sudo chown -R root:wheel "$KEXT"
sudo chmod -R go-w "$KEXT"
sudo kmutil load -p "$KEXT" 2>&1

echo "--- check ---"
kextstat 2>/dev/null | grep -i mtk || kmutil showloaded --bundle-identifier "$BUNDLE_ID" 2>/dev/null
