# 测试与调试记录（testing log）

真机调试（MT7921 `pci14c3:7961`，macOS 15.8.2 / Darwin 24.6，黑苹果 MacBookPro16,2 SMBIOS）
的完整实验记录。构建/加载细节另见 [macOS kext 调试心法](../README.md) 与 `scripts/load.sh`。

---

## 0. 环境与流程

| 项 | 值 |
|---|---|
| 硬件 | MT7921 M.2（14c3:7961），Lenovo 主板（DMAR 表 LENOVO TP-R17） |
| 系统 | macOS 15.8.2（Darwin 24.6.0），Intel，无 T2，OpenCore |
| 构建 | `xcodebuild -project mtkwm.xcodeproj -scheme mtkwm -configuration Debug SYMROOT=Build build` |
| 加载 | `bash scripts/load.sh`（kmutil unload/load；**不要改 EFI**——用户要求） |
| 日志 | `sudo dmesg`（kprintf 通道）+ `log show --predicate 'eventMessage CONTAINS "itlwm"'` |
| panic 符号化 | 基址偏移 + `nm -n` 定位（见每次 panic 记录） |

**UAKEL 流程**：每个新二进制需 系统设置点"允许"（Code=27）→ 首次加载 Code=28 需重启（进启动集合）。
同一二进制重复 load.sh 不再要批准。`kmutil unload` 卡死时 `sudo kmutil clear-staging` 清启动集合。

**工具教训（重要）**：
- macOS 的 BSD `diff` **不支持 `--text`/`-a`**，对含特殊字符文件报
  `illegal option` 并让 stdout 为空——曾导致"与 Linux 零 diff"的假结论。
  **用 `git diff --no-index --text` 做文本比对**。
- `defaults read` 对相对路径会失败；`kmutil load` 与 `load.sh` 的产物路径
  以 `Build/Debug/mtkwm.kext` 为准（SYMROOT=Build）。
- 产物被 `load.sh` chown root 后再构建报 `ld: can't write` →
  `sudo chown -R miyamoto:staff Build` 后重建。

---

## 1. MMIO page fault 六连 panic（已根治）

**症状**：`Kernel trap type=14 page fault`，`readl` 内 `__OSReadInt32`，栈固定为
`mtkwm::start → ItlMt76::attach → mtk_attach → mt7921_xnu_probe → mt7921_dma_init
→ mt792x_dma_enable → mt76_mmio_rmw → readl`。6 次 panic 页内偏移恒 `0x120`。

**根因链**（逐层实验证实）：

1. **`MT_WFDMA_DUMMY_CR = 0x54000120`**（`MT_MCU_WPDMA0(0x120)`）是
   `mt792x_dma_enable` 的第一条 rmw——`0x54000120 & 0xfff = 0x120` 与 panic 偏移吻合。
2. **MT7921 寄存器文件 ~2GB**（段：`0x18060000` TOP、`0x54000000` WPDMA0、
   `0x70010200` CHIPID、`0x74030000` PCIe MAC、`0x7c060000` CONN_ON），
   但 **macOS 的 BAR0 主机窗口只有 1MB**（`BAR0 size=0x100000`，掩码法探测证实）。
   直读 `regs+0x54000120` 越界 → 页表无映射 → fault。
3. **Linux 上为什么没事**：Linux 内核同样的 1MB BAR 下也有同样的地址，但
   `__mt7921_reg_addr()` 翻译层（fixed_map：`0x54000000→0x2000`、
   `0x7c060000→0xe0000`、`0x74030000→0x10000`，其余经 `mt7921_reg_map_l1`
   HIF L1 窗口 `0xe0000+off`）把所有访问折进低 1MB。**chipid 能读（走 L1）而
   DUMMY_CR 直读越界**的怪象由此解释。
4. "手动 load 好、boot 崩"是假象：差异不在加载机制，而在 **build 11 时代
   `mt792x_dma_enable` 还没有 DUMMY_CR 写**（后加的），一来就越界。

**修复**：
- `mt76core/mmio.c`：mmio 层 fail-safe（偏移 ≥0x100000 读返 `0xffffffff`、
  写丢弃 + 限流告警）——page fault 从机制上不可能再发生。
- 翻译层照常挂载（`mt7921_rr/wr/rmw` → fixed_map/L1 → 底层 mmio）。
- 中途试过"分窗映射"（regwin）——窗口读回 `0xffffffff`（物理在 BAR 外），
  已废弃；试过 mmio 层通用 L1——已回退（翻译层覆盖后为死代码）。

## 2. 卸载卡死与 unload UAF（已修）

- **`kmutil unload` 报 "class CTimeout has 2 instances"**：`g_n.scan_to` 只
  `timeout_del` 不 `timeout_free`；全仓库无人调 `ieee80211_ifdetach`（itlwm
  各 HAL 都在 detach 里调）。两处已修，unload 干净通过。
- **unload 时 `set_bit(MT76_REMOVED)` UAF panic**（`mt7921_pci_remove` 写已释放
  设备）：完整 probe 失败路径释放了 dev#0 但 `pci_set_drvdata` 的指针悬挂，
  M1 兜底没重设。补 `pci_set_drvdata(&g_xnu_pdev, mdev)` + remove 里
  `g_xnu_dev` 防御。
- **unload 时 `NULL->drv_own`（+0x18）panic**：M1 兜底没设 `hif_ops`/`pcie_reg`
  （`__mt792x_mcu_drv_pmctrl` 宏解引用 NULL）。补齐 + 宏改 NULL-safe inline。
- 一度 `kmutil` 全命令死锁（kernel collection 锁被半死 stop 持有）→ 重启解锁；
  `mtk_pci_deinit`/`mt7921e_unregister_device` 已加阶段日志（`mtk-deinit:`）。

## 3. 固件 patch 信号量（曾通后阻塞）

- sem 值解码：MCU 应答值在 rxd **offset 32**（`skb_pull(sizeof(*rxd)-4)`），
  GET 成功 = `2 (PATCH_NOT_DL_SEM_SUCCESS)`、RELEASE 成功 = `3`。
  历史成功记录：`SEMRET ret=2/3`、`WM Firmware Version: ____010000`、
  patch+RAM 均 loaded——**sem 链路本身走得通**。
- 后期 sem 全超时（`ret=-60`）的排查见 §4。
- `MT_SWDEF_MODE=0`（`__mt7921_init_hardware` 首句）两版一致，无差异。

## 4. sem 命令 MCU 零应答（当前卡点，全链排查完毕）

**现象**：`SEMSEND GET/RELEASE ret=-60`（4s 超时循环）；MCU 收走命令
（TX_DONE 中断 `HOST_INT_STA=0x04000000` 曾观测到），但 RX 环**零写回**
（`mcu_rxq` dma_idx 不动、无 RXD、buffer marker 完好）。

**Linux U 盘 live 验证（决定性）**——同硬件、同驱动代码、VT-d 开启：

```
mt7921e 0000:04:00.0: ASIC revision: 79610010
mt7921e: HW/SW Version: 0x8a108a10        ← patch sem+下载成功
mt7921e: WM Firmware Version: ____010000  ← RAM 固件成功
wlp4s0 (b4:b5:b6:e5:8d:ad)                ← 真 MAC，接口 up
DMAR: VT-d enabled, iommu group 12 含 04:00.0   ← IOMMU 全开下也通
```

**已排除**（实验 → 结果）：

| # | 假设 | 实验 | 结果 |
|---|------|------|------|
| 1 | 值解码位置错（eid vs offset32） | semframe 整帧 dump | offset 32 正确 |
| 2 | patch→RAM 时序（Linux 磁盘 IO 模拟） | IOSleep(100) | 无影响 |
| 3 | wfsys_reset 空桩/真序列 | A/B（真序列 0x18000140 复位+SW_INIT_DONE=0x11 成功） | 均零应答 |
| 4 | 电源拉满（PMCSR 强制 D0）扰动 MCU | A/B | 无影响（boot 场景 own 需要它，后由 wfsys 重试覆盖） |
| 5 | deferred attach 时序 | 回同步（build 11 形态） | 无影响 |
| 6 | mmio L1 重写 / fail-safe | A/B | 无影响 |
| 7 | 命令帧格式（mcu_txd/seq/cid） | TX 帧 dump 与 Linux 结构逐字比对 | 一致（68 字节、txd[8] 布局） |
| 8 | RX 消费链 5 层 | dequeue DDONE 位/位定义/rx_check/rx_skb 签名/poll 链逐层对照 | 全一致 |
| 9 | 地址链断裂 | alloc/fill/deq 三段 dump + marker(0xA5) 实验 | IOVA==物理一致；**MCU 从不写 buffer**（A5 完好） |
| 10 | 分配器（32 位 pool vs dma_alloc_coherent） | A/B | 均零应答 |
| 11 | 探针 kprintf 时序扰动 | 全探针撤除最小化 A/B | 零应答 |
| 12 | 64 位 DMA（CONFIG_ARCH_DMA_ADDR_T_64BIT） | 定义后 buf1 高半传递（dump 证实 buf1=0x1） | **WPDMA 引擎对 64 位地址直接挂死**——MT7921 WPDMA 32 位 only（Linux `dma_set_mask(32)`），不可用 |
| 13 | BIOS 保留洞（0x23xxxxxx 写被桥拦） | coherent 分配器换区（0x14xxxxxx） | 无影响 |
| 14 | ring 越界 NULL 写（WPDMA 取 buf0=0 desc 挂起） | rx_fill 填满环（<ndesc） | 修掉挂死（`cpu=dma` 对齐），应答仍不来 |
| 15 | 中断不投递导致帧没人收 | mtk_mcu_wait_poll 无条件 napi_schedule MCU RX/WA | 消费链活着（rx_check 有输出），但 MCU 无数据 |
| 16 | AppleVTD/VT-d 拦设备写 | BIOS 关 VT-d 重启验证 | **仍零应答**——排除 |
| 17 | 探针显示"MCU 写了 36 字节" | ring dump：`ctrl=0xc0240000(len=36)` 为 RXD 写回；marker 证明数据未落 buffer | "len=36" 是 RXD 元数据非数据本体 |

**当前定位**：驱动逻辑无罪（逐函数对照 + `git diff --no-index` 全文比对确认差异
仅为移植等价改动）、硬件/BIOS/VT-d 无罪（Linux 全通）、平台写路径无罪（marker/
coherent A/B）。**剩余差异在 macOS 内核 PCIe 事务层与 Linux 的行为**（同代码
同硬件，MCU 命令处理器不应答）。

**下一步候选**：
- A. PCIe 层精确对齐 Linux probe 动作（PCI command register 值
  `0002`（仅 Memory Space）vs 我们多开的 IO Space 位；`pci_set_master` 时序；
  MSI 分配方式）。
- B. 关注黑苹果社区 MT7921 / MediaTek WiFi 相关 patch 或 macOS 更新。

## 5. 其它已修复（bring-up 期积累）

- kfree 尺寸不匹配 panic → 尺寸头分配（`mtk_alloc.h`）。
- skb 链表 NULL 写 panic（`wcid->tx_pending`、`phy->tx_list`）→ 懒初始化守卫。
- PCI vendor ID：**0x14c3 是 PCIe vendor**（0x0e8d 是 USB vendor，勿混）。
- `mt792x_wfsys_reset` 原为空桩 → 补 Linux 真序列
  （`0x18000140`：清 `WFSYS_SW_RST_B`→50ms→置位→poll `WFSYS_SW_INIT_DONE(0x10)` 500ms）。
- `mt7921e_driver_own`（MT_TOP_LPCR_BAND0 经 L1）3 次重试 +
  `__mt792xe_mcu_drv_pmctrl`（MT_CONN_ON_LPCTL=0x7c060010→fixed_map 0xe0010）
  失败后 wfsys_reset 重试（macOS 电源管理把闲置 slot 收进 fw_own 深睡）。
- RX ring 越界消费（`dma_idx` 走过 `cpu_idx` 取到未填 desc）→
  **rx_fill 填满环**（`while (queued < ndesc)`）。
- 强制轮询消费（设备中断从不投递到 IOFilterInterruptEventSource——
  driver 历史注释已明示）→ `mtk_mcu_wait_poll` 无条件 `napi_schedule`。

## 6. 平台验证记录（存档）

- **Linux U 盘 live**（Ubuntu 24.04）：固件四连条全通、wlp4s0 起、真 MAC；
  VT-d 全开（DMAR 表、iommu group 12）下正常。
- **BIOS**：VT-d 关闭测试（macOS 侧 sem 仍超时）；Above 4G Decoding/ReBAR
  已开（BAR0 物理 0x7fa1000000 高位）。
- **RMRR**：0x9824e000-0x9826dfff、0x9b000000-0x9f7fffff（均不在 DMA buffer 区）。


## 7. openwrt/mt76 对照分析（2026-11-06）

经 gh-proxy 克隆 `github.com/openwrt/mt76`（out-of-tree 演进分支）与主线 Linux mt76
逐文件 diff（`git diff --no-index --text`）：

| 文件 | 差异 | 内容 |
|---|---|---|
| mt7921/pci_mcu.c | 0 行 | sem/mcu_init 序列**完全一致** |
| mcu.c（get_response） | 0 行 | 应答等待逻辑一致 |
| mt792x_dma.c | 0 行 | dma_enable 一致 |
| mt7921/pci.c | 2 行 | ITTIM vendor 字面量化（无实质） |
| mt7921/init.c | 2 行 | init_work 用 `system_wq` vs `system_percpu_wq` |
| mt7921/mcu.c | 88 行 | **防御性加固**：parse_response 短帧校验（sem 应答最短 33B）、REG_READ 应答 `valid` 检查（-ENODATA 容错）、CLC 解析边界 |
| mt76_connac_mcu.c | 59 行 | 新增 `mt76_connac2_fw_check()` 固件 trailer/region 越界校验；assign_bit 内核兼容 |
| mt792x_core.c | 36 行 | load_ram 集成 fw_check + fw_end 边界 |

**结论**：openwrt/mt76 是"内核 API 兼容 + 健壮性加固"分支，**sem/固件加载逻辑与
主线一致——对当前 macOS sem 零应答卡点无直接线索**；其防御性校验（短帧、fw_check、
valid 位）可作为后续 mt76wm 加固的吸收素材。

### 7.1 openwrt vs mt76wm 移植版——792 系列逐文件分析

**文件覆盖**：openwrt 多出 `sdio_*/usb.c/testmode.c/debugfs.c`（其它形态，与
MT7921 PCIe 无关）；核心 792 文件两边齐全。mt792x_dma.c 在 mt76wm 中合并进
`glue/mtk_port_funcs.c`（dma_enable/wfsys_reset 等）。

| 文件（vs 我们的移植） | 差异行 | 差异性质 |
|---|---|---|
| mt7921/pci.c | 303 | 主要是 XNU 移植层（M1 兜底/xnu_probe/drvdata/hif_ops 补齐）+ openwrt ITTIM 字面量 |
| mt7921/pci_mcu.c | 23 | 我们的 SEMSEND/诊断探针（逻辑零差异） |
| mt7921/mcu.c | 229 | **openwrt 健壮性加固 88 行** + 我们的探针/casts |
| mt7921/init.c | 50 | XNU workqueue 垫片差异 + openwrt `system_wq` |
| mt7921/mac.c | 48 | 我们的 casts/探针（逻辑同） |
| mt792x_core.c | 67 | openwrt fw_check/边界 + 我们 LPCTL 诊断/IOSleep |
| mt792x_mac.c | 37 | casts 差异 |
| mt792x_regs.h | 1 | 几乎全同（寄存器定义一致） |
| mt792x.h | 27 | 我们的 hif_ops NULL-safe 等补充 |

**openwrt 独有的功能性内容（值得 mt76wm 后续吸收的清单）**：

1. **`mt76_connac2_fw_check()`**：固件 trailer/region 表越界校验（load_ram/
   load_patch 入口），防坏固件。
2. **parse_response 短帧防御**：sem/FINISH 应答最短 33B（`sizeof(rxd)-4+1`）、
   其余应答 36B 起——低于即 `-EINVAL`（防坏长度越界读）。
3. **rx_event 全分支长度校验**：tx_resource / phy_cap / macaddr(≥6) /
   chip_cap(≥8) / CLC TLV 各自 `len` 下限检查（主线直接 memcpy，短帧越界）。
4. **`REG_READ` 应答 `valid` 位检查**：无效返 `-ENODATA`，调用方容错跳过。
5. **CLC 解析加固**：`clc->idx >= ARRAY_SIZE(phy->clc)` 由"continue 跳过"
   改为"拒绝"（防 idx 越界写）。
6. **monitor/offchannel 修复**：monitor 判定补 `!phy->mt76->offchannel`
   （monitor 与 offchannel 并存时的误判 bug）。
7. **内核 API 兼容**：`assign_bit→set/clear_bit`、`system_percpu_wq→system_wq`
   （对 XNU 无意义，已由垫片覆盖）。

**对 sem 卡点的结论不变**：sem/固件/MCU 逻辑三方（主线 / openwrt / mt76wm）
**完全一致**——macOS 平台层问题的定位不受影响。上述 1-6 项列入后续加固 TODO
（移植成本低：均为局部防御性代码）。

## 8. AirportMtkWlan 对照分析（2026-11-08）

克隆 `github.com/Arxusion/AirportMtkWlan`（OpenBSD mwx(4) 的 macOS 移植，
**同硬件 14c3:7961 已验证固件加载 + MSI 中断工作中**）对照，详见
[airportmtkwlan-analysis.md](airportmtkwlan-analysis.md)。要点：

- sem 协议 / mcu_txd 帧格式 / sem 值提取（rxd_ext_eid）**四方一致**（再证无罪）。
- **差异 D1（最高优先，已修 build 56）**：mwx 明确禁止 probe 手动置 MSI enable
  位（"nothing has programmed the MSI address/data yet"）——我们原
  `mtkwm.cpp:90` 手动置位 → MSI 发往未编程地址 + INTx 停发 → **中断从不投递**，
  且 posted-write abort 可能拖死 WPDMA（sem 零应答**第 18 项假设**）。已移除。
- **差异 D2（已补 build 56）**：`MT_PCIE_MAC_INT_ENABLE=0xff`（PCIe MAC 中断
  转发，mwx 在 attach + dma_enable 两处写；Linux mt7921 依赖 boot 值故无此步）
  已补进 `mt792x_dma_enable`。
- **重要无罪数据点**：mwx 同样 `setIOEnable(true)` 且工作 → PCIe 对齐实验中
  "移除 IO Space 位"一项**可跳过**。
- 后续梯队：D3 换普通 `IOInterruptEventSource`（MSI edge-triggered 无需 filter）、
  D4 MSI edge drain 循环、D5 PCIe FLR 恢复链（warm reboot 残留）、D6 固件
  4KB 分块、D7 RX WPDMA 头偏移复查。

## 9. AirportMtk-macOS 对照分析（2026-11-13）

克隆 `github.com/Hi-Jiajun/AirportMtk-macOS`（mwx 血统另一实现：AirportItlwm
前端 + laobamac/SimpleMtkWlan if_mwx 驱动体；macOS 15.8.1 + MT7922 实测固件/
关联/扫描全通），增量对照详见
[airportmtk-macos-analysis.md](airportmtk-macos-analysis.md)（E 系列）。要点：

- **E1 同 workloop ISR 阻塞**（sem 零应答新判别项）：中断源与发命令链同挂一条
  IOWorkLoop → 等待期应答堆环、**超时后批量到达**；解法 = command gate 门控
  自排空 MCU RX 环（`MtkMwx.cpp:8965-9120`）。判别签名：`SEMSEND ret=-60` 后
  是否有迟到应答涌入。
- **E2** DRV_OWN 前必须**先读一次** `MT_TOP_LPCR_HOST_BAND0`（否则中断不工作，
  症状同 sem 零应答）；**E3** WFSYS reset 清 MSI capability，恢复需
  ack `HOST_INT_STA` + 重写 `HOST_INT_ENA`/`PCIE_MAC_INT_ENABLE`；
  **E4** 固件-驱动版本耦合（2024 驱动+2026 固件 → own failed，建议 blob A/B）。
- 扫描：MCU hw scan（`MCU_CE_CMD_START_HW_SCAN`，与 mt76
  `mt76_connac_mcu_hw_scan` 同族协议）参数已验证可抄（120 ms 驻留、seq 看门狗、
  只 cancel 不 reset）。**数据面**：结果 = 普通 beacon/probe resp 走 RX 数据环，
  固件只发 scan-done 终止事件（MT7921 = `MCU_EVENT_SCAN_DONE` legacy eid）；
  MT7921 的 **connac2 命令后端尚未写**（构建器/事件分发在树中未接线，详见
  主文档 §3）。防 `MWX_FLAG_SCANNING` 泄漏、ioctl 路径禁分配（round90 panic）。
- **kernel_task 上报断链**（round73-75）：SCAN_RESULT 读取方是 kernel_task 而非
  airportd、结果不转发现代缓存 → 系统 Wi-Fi 列表空。已证伪清单 + 接口侧路径
  候选方向已归档——**AirportMtkwm 变体的前置必读**。
- AWDL 发布配方：createVirtualInterface role-6 覆写 + **attach 后必须
  registerService** + 按 role 单飞；ROC 缺失 → AirDrop 数据面未做
  （呼应 [airdrop-research.md](airdrop-research.md) 路线 B 硬边界）。
