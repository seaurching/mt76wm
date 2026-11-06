# MT7921/MT7922 firmware blobs

Required files (names must match `mt792x.h` in the Linux mt76 driver):

| File | Chip |
|---|---|
| `WIFI_MT7961_patch_mcu_1_2_hdr.bin` | MT7921 ROM patch |
| `WIFI_RAM_CODE_MT7961_1.bin` | MT7921 RAM code |
| `WIFI_MT7922_patch_mcu_1_1_hdr.bin` | MT7922 ROM patch |
| `WIFI_RAM_CODE_MT7922_1.bin` | MT7922 RAM code |

## Why aren't they in the repo?

The blobs are licensed by MediaTek under the terms found in the
[linux-firmware](https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git)
repository (`WHENCE`, `LICENCE.mediatek_*`) — redistribution is permitted but
with conditions; keeping them out of the source tree is the safe default.

## How to get them

```sh
# from the repository root: pulls every family (mt7921/mt7925/mt7927/mt7928)
# into firmware/<family>/
./scripts/extract_fw.sh /path/to/linux-firmware   # or no arg to auto-locate/clone
rm include/FwBinary.cpp
./scripts/fw_gen.sh -P firmware/    # or just rebuild (preBuild script)
```

The blobs are then zlib-packed into `include/FwBinary.cpp` and compiled into
the kext (same pipeline as itlwm).  MT7925/MT7927/MT7928 blob lists live in
`firmware/mt7925/`, `firmware/mt7927/`, `firmware/mt7928/` (see
`scripts/extract_fw.sh` for the exact file names).
