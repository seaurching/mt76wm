#!/bin/sh
# extract_fw.sh - copy MediaTek firmware blobs from a linux-firmware
# checkout into firmware/<family>/. The blobs are NOT redistributed with
# this repository (MediaTek license, see README in firmware/mt7921/).
#
# Families and blobs (mt792x names must match mt792x.h in the Linux mt76
# driver, mt7603 names the firmware constants in mtkwm/mt7603/mt7603.h):
#   firmware/mt7921/  MT7921/MT7922 (WIFI_MT7961/MT7922 patch + RAM code)
#   firmware/mt7925/  MT7925        (WIFI_MT7925 patch + RAM code)
#   firmware/mt7927/  MT7927/MT6639 (WIFI_MT6639 patch + RAM code)
#   firmware/mt7928/  MT7928/MT7935 (WIFI_MT7935 patch + RAM code +
#                                    CBMCU code + PHY RAM code)
#   firmware/mt7603/  MT7603E/MT76x8 (mt7603_e1/e2 + mt7628_e1/e2;
#                                    mt7603_e2_patch.bin is the e2 ROM
#                                    patch shipped in the same directory)
#   firmware/mt7615/  MT7615E/MT7663/MT7622 (mt7615_n9 + rom_patch +
#                                    mt7615_cr4 for the MT7622 WMAC;
#                                    mt7622_n9 + mt7622_rom_patch;
#                                    mt7663pr2h/mt7663_n9_v3 offload set +
#                                    mt7663pr2h_rebb/mt7663_n9_rebb REBB
#                                    set - names are the firmware
#                                    constants in mtkwm/mt7615/mt7615.h)
#   firmware/mt7996/  MT7996/MT7992/MT7990 (Wi-Fi 7 tri-band; WA/WM/DSP
#                                    RAM code + ROM patch + default eeprom
#                                    bins for the 23/24/233/2i5i/2i5e
#                                    variants - names are the firmware
#                                    constants in mtkwm/mt7996/mt7996.h)
#   firmware/mt7915/  MT7915/MT7906/MT7916/MT790a (and the MT7981/MT7986
#                                    SoC ids sharing the mcu.c fw_name
#                                    table): WA/WM RAM code + ROM patch +
#                                    default eeprom bins - names are the
#                                    firmware constants in
#                                    mtkwm/mt7915/mt7915.h
#   firmware/mt76x0/  MT7610E/MT7630/MT7650 + MT7610U (USB): mt7610e.bin +
#                                    mt7650e.bin (PCIe, MT7610E_FIRMWARE/
#                                    MT7650E_FIRMWARE in mtkwm/mt76x0/mt76x0.h)
#                                    + mt7610u.bin (MT7610U_FIRMWARE)
#   firmware/mt76x2/  MT7612E/MT7662/MT7611/MT7602 + MT7662U (USB):
#                                    mt7662.bin + mt7662_rom_patch.bin
#                                    (MT7662_FIRMWARE/MT7662_ROM_PATCH in
#                                    mtkwm/mt76x2/mt76x2.h) + the USB
#                                    mt7662u.bin/mt7662u_rom_patch.bin set
#
# Usage:
#   ./scripts/extract_fw.sh [path-to-linux-firmware]
# If no path is given, ../linux-firmware or ~/linux-firmware is used if present,
# otherwise the script clones linux-firmware (shallow) into a temp dir.

set -e

SRC="$1"

if [ -z "$SRC" ]; then
    for cand in ../linux-firmware ~/linux-firmware /usr/lib/firmware; do
        if [ -d "$cand/mediatek" ]; then SRC="$cand"; break; fi
    done
fi

# <dest subdir>:<linux-firmware relative path> list
BLOBS="
mt7601u:mediatek/mt7601u.bin
mt7921:mediatek/WIFI_MT7961_patch_mcu_1_2_hdr.bin
mt7921:mediatek/WIFI_RAM_CODE_MT7961_1.bin
mt7921:mediatek/WIFI_MT7922_patch_mcu_1_1_hdr.bin
mt7921:mediatek/WIFI_RAM_CODE_MT7922_1.bin
mt7925:mediatek/mt7925/WIFI_MT7925_PATCH_MCU_1_1_hdr.bin
mt7925:mediatek/mt7925/WIFI_RAM_CODE_MT7925_1_1.bin
mt7927:mediatek/mt7927/WIFI_MT6639_PATCH_MCU_2_1_hdr.bin
mt7927:mediatek/mt7927/WIFI_RAM_CODE_MT6639_2_1.bin
mt7928:mediatek/mt7928/WIFI_MT7935_PATCH_MCU_1_1_hdr.bin
mt7928:mediatek/mt7928/WIFI_RAM_CODE_MT7935_1_1.bin
mt7928:mediatek/mt7928/CBMCU_CODE_MT7935_1_1.bin
mt7928:mediatek/mt7928/WIFI_MT7935_PHY_RAM_CODE_1_1.bin
mt7603:mediatek/mt7603/mt7603_e1.bin
mt7603:mediatek/mt7603/mt7603_e2.bin
mt7603:mediatek/mt7603/mt7603_e2_patch.bin
mt7603:mediatek/mt7603/mt7628_e1.bin
mt7603:mediatek/mt7603/mt7628_e2.bin
mt7615:mediatek/mt7615_cr4.bin
mt7615:mediatek/mt7615_n9.bin
mt7615:mediatek/mt7615_rom_patch.bin
mt7615:mediatek/mt7622_n9.bin
mt7615:mediatek/mt7622_rom_patch.bin
mt7615:mediatek/mt7663pr2h.bin
mt7615:mediatek/mt7663_n9_v3.bin
mt7615:mediatek/mt7663pr2h_rebb.bin
mt7615:mediatek/mt7663_n9_rebb.bin
mt7915:mediatek/mt7915_wa.bin
mt7915:mediatek/mt7915_wm.bin
mt7915:mediatek/mt7915_rom_patch.bin
mt7915:mediatek/mt7915_eeprom.bin
mt7915:mediatek/mt7915_eeprom_dbdc.bin
mt7915:mediatek/mt7916_wa.bin
mt7915:mediatek/mt7916_wm.bin
mt7915:mediatek/mt7916_rom_patch.bin
mt7915:mediatek/mt7916_eeprom.bin
mt7915:mediatek/mt7981_wa.bin
mt7915:mediatek/mt7981_wm.bin
mt7915:mediatek/mt7981_rom_patch.bin
mt7915:mediatek/mt7981_eeprom_mt7976_dbdc.bin
mt7915:mediatek/mt7986_wa.bin
mt7915:mediatek/mt7986_wm.bin
mt7915:mediatek/mt7986_wm_mt7975.bin
mt7915:mediatek/mt7986_rom_patch.bin
mt7915:mediatek/mt7986_rom_patch_mt7975.bin
mt7915:mediatek/mt7986_eeprom_mt7975.bin
mt7915:mediatek/mt7986_eeprom_mt7975_dual.bin
mt7915:mediatek/mt7986_eeprom_mt7976.bin
mt7915:mediatek/mt7986_eeprom_mt7976_dbdc.bin
mt7915:mediatek/mt7986_eeprom_mt7976_dual.bin
mt76x0:mediatek/mt7610e.bin
mt76x0:mediatek/mt7650e.bin
mt76x0:mediatek/mt7610u.bin
mt76x2:mediatek/mt7662.bin
mt76x2:mediatek/mt7662_rom_patch.bin
mt76x2:mediatek/mt7662u.bin
mt76x2:mediatek/mt7662u_rom_patch.bin
mt7996:mediatek/mt7996/mt7996_wa.bin
mt7996:mediatek/mt7996/mt7996_wm.bin
mt7996:mediatek/mt7996/mt7996_dsp.bin
mt7996:mediatek/mt7996/mt7996_rom_patch.bin
mt7996:mediatek/mt7996/mt7996_wa_233.bin
mt7996:mediatek/mt7996/mt7996_wm_233.bin
mt7996:mediatek/mt7996/mt7996_rom_patch_233.bin
mt7996:mediatek/mt7996/mt7992_wa.bin
mt7996:mediatek/mt7996/mt7992_wm.bin
mt7996:mediatek/mt7996/mt7992_dsp.bin
mt7996:mediatek/mt7996/mt7992_rom_patch.bin
mt7996:mediatek/mt7996/mt7992_wa_23.bin
mt7996:mediatek/mt7996/mt7992_wm_23.bin
mt7996:mediatek/mt7996/mt7992_dsp_23.bin
mt7996:mediatek/mt7996/mt7992_rom_patch_23.bin
mt7996:mediatek/mt7996/mt7992_wa_24.bin
mt7996:mediatek/mt7996/mt7992_wm_24.bin
mt7996:mediatek/mt7996/mt7992_dsp_24.bin
mt7996:mediatek/mt7996/mt7992_rom_patch_24.bin
mt7996:mediatek/mt7996/mt7990_wm.bin
mt7996:mediatek/mt7996/mt7990_rom_patch.bin
mt7996:mediatek/mt7996/mt7996_eeprom.bin
mt7996:mediatek/mt7996/mt7996_eeprom_2i5i6i.bin
mt7996:mediatek/mt7996/mt7996_eeprom_233.bin
mt7996:mediatek/mt7996/mt7996_eeprom_233_2i5i6i.bin
mt7996:mediatek/mt7996/mt7992_eeprom.bin
mt7996:mediatek/mt7996/mt7992_eeprom_2i5i.bin
mt7996:mediatek/mt7996/mt7992_eeprom_2i5e.bin
mt7996:mediatek/mt7996/mt7992_eeprom_23.bin
mt7996:mediatek/mt7996/mt7992_eeprom_23_2i5i.bin
mt7996:mediatek/mt7996/mt7992_eeprom_24_2i5i.bin
mt7996:mediatek/mt7996/mt7990_eeprom.bin
mt7996:mediatek/mt7996/mt7990_eeprom_2i5i.bin
"

REGEN_MSG='done. Now: rm include/FwBinary.cpp && (re)build - the preBuild
script repacks firmware/ via scripts/fw_gen.sh (or run it by hand:
./scripts/fw_gen.sh -P firmware/)'

if [ -z "$SRC" ] || [ ! -d "$SRC/mediatek" ]; then
    # No local linux-firmware checkout: try fetching the individual blobs
    # directly from git.kernel.org before falling back to a full clone.
    BASE="https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain/mediatek"
    for entry in $BLOBS; do
        fam="${entry%%:*}"
        rel="${entry#*:}"
        f="${rel#mediatek/}"
        url="$BASE/$f"
        DEST="$(dirname "$0")/../firmware/$fam"
        mkdir -p "$DEST"
        [ -f "$DEST/$(basename "$f")" ] && continue
        if curl -fsSL -o "$DEST/$(basename "$f")" "$url"; then
            echo "downloaded $f"
            continue
        fi
        # Fallback: the OpenWrt mt76 repo hosts the blobs that never made it
        # into linux-firmware (mt7603/mt7628 etc.); looked up by basename.
        alt="https://raw.githubusercontent.com/openwrt/mt76/master/firmware/$(basename "$f")"
        if curl -fsSL -o "$DEST/$(basename "$f")" "$alt"; then
            echo "downloaded $(basename "$f") (openwrt/mt76)"
        fi
    done
    ok=1
    for entry in $BLOBS; do
        fam="${entry%%:*}"
        rel="${entry#*:}"
        DEST="$(dirname "$0")/../firmware/$fam"
        [ -f "$DEST/$(basename "$rel")" ] || { ok=0; echo "MISSING: $fam/$(basename "$rel")" >&2; }
    done
    if [ "$ok" = 1 ]; then
        echo "$REGEN_MSG"
    else
        echo "some blobs unavailable (e.g. MT7935/MT7928 firmware is not in"
        echo "linux-firmware yet) -- those families fall back to no-firmware"
        echo "mode until the blobs are added manually. Others are ready."
    fi
    exit 0
fi

for entry in $BLOBS; do
    fam="${entry%%:*}"
    rel="${entry#*:}"
    DEST="$(dirname "$0")/../firmware/$fam"
    mkdir -p "$DEST"
    if [ -f "$SRC/$rel" ]; then
        cp "$SRC/$rel" "$DEST/"
        echo "copied $rel"
    else
        echo "WARNING: $rel not found in $SRC" >&2
    fi
done

echo "$REGEN_MSG"
