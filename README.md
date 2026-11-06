# mt76wm

**A MediaTek (MT7921/MT7922) Wi-Fi adapter kernel extension for macOS, developed on top of [itlwm](https://github.com/OpenIntelWireless/itlwm) (architecture, HAL, itl80211 net80211 stack, firmware pipeline and HeliPort client protocol are inherited from itlwm) and ported from the Linux [mt76](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/drivers/net/wireless/mediatek/mt76) driver.**

> ⚠️ Work in progress. See `docs/porting-notes.md` for the milestone status.
> Current stage: **M0 — project skeleton**.

## Architecture

```
HeliPort (userspace GUI, ClientKit protocol)
      │  IOConnectCallStructMethod
MtkNetworkUserClient ──── mtkwm (IOEthernetController)
      └────────┬────────┘
   ItlMt76 : ItlHalService, ItlDriverInfo, ItlDriverController
        struct mt76_softc (mt792x_dev → mt76_dev + ieee80211com)
   ┌──────────────┬──────────────┬─────────────────────┐
   mt76core/      mt7921/        glue/
   (ported mt76   (chip driver   (net80211 glue,
    dma/mcu/connac init/mcu/mac/  linuxcompat, napi shim,
    core)          pci)           DMA helpers)
   └──────────────┴──────────────┴─────────────────────┘
        itl80211/ (OpenBSD net80211 stack, from itlwm)
```

## Requirements（黑苹果依赖，承袭 itlwm）

本驱动与 itlwm 共享同一套 macOS 运行环境要求（架构即基于 itlwm 开发）：

| 依赖 | 说明 |
|---|---|
| **平台** | Intel macOS（Apple Silicon 不支持，同 itlwm） |
| **macOS 版本** | 与 itlwm 相同的兼容范围（10.13+，实测 15.8.2 / Darwin 24.6） |
| **SIP / kext 加载** | 同 itlwm：需要降低 SIP（csrutil）或 OpenCore 注入。黑苹果推荐 `csr-active-config = 0xA7F`（含 `CSR_ALLOW_UNAPPROVED_KEXTS`）；`OSBundleRequired=Network-Root` |
| **引导器** | OpenCore（推荐，同 itlwm 用户群）：kext 放 `EFI/OC/Kexts/` + `Kernel → Add` 注入。开发迭代用 `scripts/load.sh`（kmutil，需系统设置批准） |
| **客户端** | [HeliPort](https://github.com/OpenIntelWireless/HeliPort)（直接复用 itlwm 的 ClientKit 协议；其 `isSupportService` 白名单已含 `mtkwm`） |
| **构建依赖** | Xcode、[XcodeGen](https://github.com/yonaskolb/XcodeGen)、[MacKernelSDK](https://github.com/acidanthera/MacKernelSDK)（同 itlwm） |
| **硬件** | MT7921/MT7922 等 M.2 PCIe MediaTek Wi-Fi（PCI vendor `0x14c3`；ITTIM `0x0b48` 变体）；USB 家族 personality 已预留 |

黑苹果注意事项：

- 无 T2 机型不涉及 Secure Boot/用户批准硬件；kext 加载批准走系统设置（UAKEL）。
- **VT-d**：实测关闭与否对当前固件加载阶段无影响（详见 `docs/testing-log.md`）；
  Linux 侧同硬件在 VT-d 全开下工作正常。
- 同 itlwm：**不要与其它 MediaTek Wi-Fi 驱动（如旧版 kext）同时加载**。

参考来源：[itlwm 文档](https://OpenIntelWireless.github.io/itlwm)（Intro / Compatibility / FAQ）。

## Building

Requirements: Xcode, [XcodeGen](https://github.com/yonaskolb/XcodeGen) (for regenerating the project), [MacKernelSDK](https://github.com/acidanthera/MacKernelSDK).

```sh
git clone <this-repo> mt76wm && cd mt76wm
# MacKernelSDK: either clone as submodule or symlink, e.g.
ln -s /path/to/MacKernelSDK ./MacKernelSDK
# firmware (optional, needed to actually run the driver):
./scripts/extract_fw.sh
rm include/FwBinary.cpp && ./scripts/fw_gen.sh -P firmware/mt7921/
# generate project & build:
xcodegen generate
xcodebuild -project mtkwm.xcodeproj -scheme mtkwm -configuration Debug build
```

## Versioning

Version and build number are managed in `version.xcconfig` and bumped on
every build — see [docs/versioning.md](docs/versioning.md).

## Firmware

Firmware blobs are not committed (see `firmware/mt7921/README.md`).
`scripts/extract_fw.sh` copies them from a linux-firmware checkout.

## Loading (development)

Use `scripts/load.sh` / `scripts/unload.sh` (kextutil/kextunload). On modern
macOS you need reduced SIP or OpenCore injection, same as itlwm.

## Credits

- [itlwm / OpenIntelWireless](https://github.com/OpenIntelWireless/itlwm) — project model, itl80211 (OpenBSD net80211), HAL abstraction, firmware pipeline
- [OpenBSD](https://openbsd.org) — net80211 stack
- [Linux kernel mt76](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/drivers/net/wireless/mediatek/mt76) — the MediaTek hardware driver being ported
- [linux-firmware](https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git) — MT7921/MT7922 firmware blobs
- [Acidanthera](https://github.com/acidanthera) — MacKernelSDK

## License

GPL-2.0 — see `LICENSE`.
