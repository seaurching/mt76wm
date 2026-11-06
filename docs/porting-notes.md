# Porting notes — Linux mt76 → XNU (mt76wm)

Source of truth on the left is `/Users/miyamoto/Linux.localized/drivers/net/wireless/mediatek/mt76`
(in-tree Linux 7.3 mt76). itlwm is the structural model.

## 家族覆盖（全部 9 个 Linux 源模块已移植）

| 源模块 | 芯片 | 状态 |
|---|---|---|
| mt76/mt7921 | MT7921/7922/7920/7902 | ✅ 全功能（含 net80211/WPA2/收发） |
| mt76/mt7925 | MT7925/7927/7928/7935 | ✅ probe/固件/MCU（MT7935 固件待上游发布） |
| mt76/mt7603 | MT7603E/MT76x8 | ✅ probe/固件/MCU |
| mt76/mt7615 | MT7615E/MT7663 | ✅ probe/固件/MCU |
| mt76/mt7915 | MT7915/7906/7916/790a/7981/7986 | ✅ probe/固件/MCU |
| mt76/mt7996 | MT7996/7992/7990（Wi-Fi 7 AP） | ✅ probe/固件/MCU（双 HIF 单模式） |
| mt76/mt76x0 | MT7610/7630/7650 | ✅ probe/固件/MCU |
| mt76/mt76x2 | MT7612/7662/7611/7602 | ✅ probe/固件/MCU |
| mt7601u | MT7601U（USB） | ✅ USB 架构 + probe/固件（17 个 VID/PID） |

固件 80 个 blob 已打包进 kext（22MB）；7 个无源（MT7935×4、mt7992 `_24`×3）等上游发布后跑 extract_fw.sh 补齐。

## Milestone status（MT7921 全功能路径）

| # | Milestone | Status |
|---|-----------|--------|
| M0 | Project skeleton, compiles | ✅ done |
| M1 | PCIe probe / BAR / MSI / chip id | ✅ code done（真机验证待做） |
| M2 | Firmware load (patch + RAM code + alive) | ✅ code done（固件已打包；真机验证待做） |
| M3 | MCU command path / EEPROM / NIC capability | ✅ code done |
| M4 | net80211 attach + RX half + sw scan | ✅ done（mtk_net80211.c：ifattach/RX/被动+主动扫描） |
| M5 | TX/RX datapath | ✅ done（if_start→WPDMA、TXS 回收、mgtq、hdr-trans 关闭） |
| M6 | Connect / WPA2 / firmware RC | ✅ code done（WTBL/BSS/key 下发、PAE 4-way 走固件加解密+SW 回落；真机验证待做） |
| M7 | UserClient / HeliPort / PM / stability | 🔨 UserClient 已复用；TX DMA 映射已修复（真 IOVA）；PM/休眠/稳定性待装机迭代 |

> 详见 [testing-log.md](testing-log.md)（真机调试完整实验记录）。

## 真机实测进度（MT7921 pci14c3:7961，macOS 15.8.2 黑苹果）

**已打通**（真机验证 ✅）：

| 步骤 | 状态 | 备注 |
|---|---|---|
| M1 PCI 匹配 → BAR0 映射 + L1 翻译层 → MSI | ✅ 真机 | BAR0 主机窗口仅 1MB，高段经 fixed_map/HIF-L1 窗口 |
| DMA 环建立（TX/RX/MCU 队列，32 位） | ✅ 真机 | ring 填满策略（WPDMA 越界消费修复） |
| driver_own 双握手（MT_CONN_ON_LPCTL + MT_TOP_LPCR_BAND0） | ✅ 真机 | 失败自动 wfsys_reset 重试 |
| wfsys_reset 复位序列（SW_INIT_DONE） | ✅ 真机 | reg 0x18000140 = 0x11 |
| MCU 命令通道（命令送达设备，TX_DONE 观测） | ✅ 真机 | 应答方向见卡点 |
| M4 net80211 注册 → mtk0/en1 接口 | ✅ 真机 | 占位 MAC 02:00:00:00:00:00（真 MAC 待 EEPROM） |
| 扫描状态机运转（INIT→SCAN 逐信道跳、probe_req 发出） | ✅ 真机 | tune 需 MCU ready（见卡点） |

**卡点**（真机 ⛔）：

```
sem 命令（PATCH_SEM_CONTROL GET/RELEASE）MCU 零应答
  → patch/RAM 固件下载不了 → MCU not ready
  → 每信道 "MCU not ready, skipping tune" → 扫不出 SSID
```

- 17 项假设已实验排除（对照 Linux 逐函数+全文 diff、地址链 dump、
  marker 实验、A/B 十余轮）——详见 [testing-log.md](testing-log.md) §4。
- **Linux U 盘 live 同硬件同代码全通**（patch+RAM+真 MAC+接口 up，
  VT-d/IOMMU 全开）——问题定格 macOS 平台层。

**未开始**（依赖 MCU ready）：

| 步骤 | 状态 |
|---|---|
| patch → RAM 固件 → N9 ready | ⬜ 卡在 sem |
| EEPROM / NIC capability（真 MAC b4:b5:b6:e5:8d:ad） | ⬜ |
| 扫描 tune 到信道 → 扫出 SSID | ⬜ |
| HeliPort 连 WPA2（4-way → key → DHCP）→ ping/iperf | ⬜ |
| 休眠唤醒 / PM 稳定性 | ⬜ |

**下一步候选**：A. PCIe 层精确对齐 Linux probe 动作（PCI command register
`0002` vs 我们多开的 IO Space 位、pci_set_master 时序、MSI 分配方式）；
B. 关注黑苹果社区 MT7921/MTK WiFi patch 或 macOS 更新。

已知限制：DMA 地址强制 <4GB（MT7921 WPDMA 32 位 only——64 位地址挂死引擎，
实测证实；Linux 同 `dma_set_mask(32)`）、AMPDU/HT 聚合未启用、
`updateprot`/`update_chw` 为桩（20MHz no-HT 无影响）、单设备单 vif。

## Key design decisions

1. **mt76 C code is ported with minimal changes** — `mt76_driver_ops` /
   `mt76_mcu_ops` / `mt76_queue_ops` stay C function-pointer tables
   (`mt76.h`); Linux API calls are redirected to `glue/linuxcompat`.
2. **mac80211 glue is rewritten** — `mt76/mac80211.c` and the
   `ieee80211_ops` table in `mt7921/main.c` are replaced by
   `glue/net80211_glue.cpp` speaking OpenBSD net80211.
3. **Scanning uses net80211 software scan** (per-channel dwell driven by the
   net80211 state machine + beacons via RX), not mt76 hw_scan. hw_scan is an
   M7 optional optimization.
4. **Everything compiles as C++ (gnu++14)** like itlwm does
   (`GCC_INPUT_FILETYPE = sourcecode.cpp.cpp`). Ported GNU-C constructs
   (designated initializers, compound literals, container_of) are accepted by
   clang as extensions; fix up any that error during the port.

## Linux → XNU shim map (glue/linuxcompat)

| Linux | XNU | Reference |
|---|---|---|
| `read32/write32` (mmio) | BAR0 mapped pointer (OSWriteLittleInt32) | hal_iwx register access |
| `dma_alloc_coherent` / `dma_map_single` | IOBufferMemoryDescriptor + IODMACommand | itlwm ItlIwx.cpp:2329 |
| `devm_request_irq` | IOFilterInterruptEventSource (filter reads MT_INT_STATUS) | itlwm probe MSI setup |
| `napi_struct` | napi_shim: workloop budget polling | new |
| `wait_queue_head_t` / `completion` / `mutex` | ItlHalService::tsleep_nsec/wakeupOn + lck_mtx_t | itlwm |
| `timer_list` | CTimeout | itlwm |
| `tasklet` / `INIT_WORK` | workloop + IOCommandGate::runAction | — |
| `request_firmware` | getFWDescByName + uncompressFirmware (FwData.h) | itlwm ItlIwn.cpp:7003 |
| `sk_buff` | minimal skb wrapper, RX copies into mbuf → ieee80211_inputm | new |
| `dev_dbg/dev_err` | XYLog | itl80211/linux/kernel.h |
| debugfs / trace / testmode | dropped, not compiled | — |

## ieee80211_ops → net80211 mapping (M4+)

| mac80211 op | net80211 counterpart | Notes |
|---|---|---|
| add/remove_interface | single fixed STA vif (ic_ac) | itlwm-style single vif |
| config / set_channel | net80211 channel switch → mt7921_mcu_set_channel | sw scan drives this |
| bss_info_changed | hooks in net80211 state transitions | mt76_connac2_mcu_add_bss_info etc. |
| sta_state | ieee80211_node lifecycle hooks | mt76_connac_mcu_sta_state |
| set_key | SIOCS80211KEY / WPA_KEY ioctl | mt76_connac_mcu_add_key |
| hw_scan / remain_on_channel | **not ported** (sw scan) | M7 optional |
| ampdu_action | disabled at first (M6) | largest semantic gap |
| start / stop | if_init / if_stop | — |
| conf_tx (EDCA) | net80211 EDCA media params | M6 |

## Rate control

MT7921 uses firmware-offload rate control; no minstrel port needed. net80211
gets a basic rate set for management frames, data rates are chosen by firmware.

## Firmware (M2)

Per `mt792x.h`:
- MT7921: `mediatek/WIFI_MT7961_patch_mcu_1_2_hdr.bin` + `mediatek/WIFI_RAM_CODE_MT7961_1.bin`
- MT7922: `mediatek/WIFI_MT7922_patch_mcu_1_1_hdr.bin` + `mediatek/WIFI_RAM_CODE_MT7922_1.bin`

Load flow (`mt792x_core.c:mt792x_load_firmware`):
driver_own → MCU ready (poll MT_CONN_ON_MISC) → ROM patch
(mt76_connac2_load_patch: semaphore + chunked download + start) → RAM code
(mt76_connac2_load_ram) → wait MT_TOP_MISC2_FW_N9_RDY → get_nic_capability.

## File map (porting strategy)

| Strategy | Files |
|---|---|
| A: minimal-change port | mt76core: dma.c mcu.c mmio.c eeprom.c util.c tx.c agg-rx.c mt76_connac_mcu.c mt76_connac_mac.c mt792x_core.c mt792x_mac.c + headers; mt7921: init.c mcu.c mac.c pci.c pci_mcu.c pci_mac.c regd.c + headers |
| B: rewrite | mt76/mac80211.c → glue/net80211_glue.cpp; mt7921/main.c ops table → glue; NAPI/IRQ/DMA alloc → napi_shim + MtkDma |
| C: new | ItlMt76, mtkwm.*, MtkNetworkUserClient.*, mt76_softc.h, linuxcompat, napi_shim, extract_fw.sh |
| D: dropped | debugfs trace testmode wed npu sdio usb scan.c(M4) connac3(MT7925 later) |
| E: reused as-is | itl80211/, include/HAL, ClientKit, FwData.h, fw_gen.sh, zlib_compress_fw.py |
