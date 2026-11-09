# AirportMtkWlan 对照分析（2026-11-08）

克隆 [Arxusion/AirportMtkWlan](https://github.com/Arxusion/AirportMtkWlan)
（本地 `/Users/miyamoto/Documents/AirportMtkWlan`）与 mt76wm 逐段对照。
该项目是 OpenBSD **mwx(4)** 的 macOS 移植（MT7921/MT7922，`pci14c3:7961` 同硬件），
原生 AirPort/Skywalk 形态，**README 声明固件加载 ✅、MSI 中断注册工作中 ✅**
——这是同硬件 macOS 上已验证工作的参考实现，价值极高。

---

## 1. 协议层三方一致（再次证明驱动逻辑无罪）

| 项 | Linux mt76 | openwrt/mt76 | AirportMtkWlan (mwx) | mt76wm |
|---|---|---|---|---|
| sem GET/RELEASE 值 | 1 / 0 | 同 | 同（`mwx_mcu_patch_sem_ctrl`） | 同 |
| sem 成功返回值 | 2=NOT_DL、3=REL | 同 | 同 | 同 |
| sem 值提取位置 | rxd `ext_eid`（offset 32） | 同 | `mcu_int = rxd_ext_eid` | 同 |
| mcu_txd 帧格式 | 68B LONG_FORMAT | 同 | `txd[0]=TX_BYTES\|TYPE_CMD\|Q_IDX`、`txd[1]=LONG_FORMAT\|HDR_FORMAT_CMD` | 同 |
| seq 语义 | 4 位 `&0x0f` | 同 | 同 | 同 |

**结论不变**：sem/固件/MCU 命令协议四家一致，卡点不在协议层。

## 2. 关键差异（macOS 传输层实操，按优先级）

### D1【最高】probe 手动置 MSI enable 位 —— 已修（build 56）

mwx 在 probe 里**显式禁止**手动置位（`SimpleMtkWlan.cpp:106` 注释）：

> Do not set the MSI enable bit by hand: nothing has programmed the MSI
> address/data yet, and with MSI enabled the device stops raising INTx.
> IOPCIFamily enables MSI when the PCIMessaged vector is registered.

**mt76wm 原先 `mtkwm.cpp:90` 调 `pciMsiSetEnable(msiCap, 1)` 手动置位**——
后果与我们的现象完全吻合：
1. MSI address/data 尚未编程，设备发出的 MSI 消息落空，且 **INTx 同时停发**
   → IOFilterInterruptEventSource **从不投递**（历史观察）；
2. 更重的副作用：发往未编程地址的 posted MSI write 可能在 PCIe 层 abort，
   **拖死 WPDMA 引擎** → 与"MCU 收走命令、RX 环零写回"（sem 零应答）兼容——
   这是 17 项排除后的新假设（第 18 项）。

**修复**：移除手动置位，改为注释说明；IOPCIFamily 在注册 PCIMessaged vector
（attach 的 `IOFilterInterruptEventSource` 于 `ItlMt76.cpp:176` 找到
`kIOInterruptTypePCIMessaged` index）时自行编程地址/数据并置 enable。

### D2【高】`MT_PCIE_MAC_INT_ENABLE = 0xff` —— 已补（build 56）

mwx 在 **attach（中断建立前）** 与 **dma_enable 尾部** 两处写
`MT_PCIE_MAC_INT_ENABLE(0x10188) = 0xff`（PCIe MAC 层中断转发到 host）。
Linux mt7921 不写（仅 mt7915 写；mt7921 依赖 boot 值）——**mwx 是 macOS 平台
适配补丁**。mt76wm 原先没写。已在 `mt792x_dma_enable` 尾部补上
（`MT_MCU2HOST_SW_INT_ENA` 之后），宏 `MT_PCIE_MAC_INT_ENABLE` 补入
`mt792x_regs.h`（与 `mt7921/regs.h` 同值重定义，token 相同合法）。

### D3【中】中断源类型：`IOInterruptEventSource` vs `IOFilterInterruptEventSource`

mwx 的 `pci_intr_establish`（`mtk80211/compat.cpp:127`）用**普通
`IOInterruptEventSource`** + trampoline 到 C handler。理由：MSI 是
edge-triggered，无需 filter 确认（filter 返回 false 会把 MSI 判 spurious
甚至卡死 source）。mt76wm 用 `IOFilterInterruptEventSource` 但 filter
**恒返 true**（`ItlMt76.cpp:248`），功能等价；若 D1/D2 修复后中断仍不投递，
换普通 `IOInterruptEventSource` 是下一候选。

### D4【中】MSI edge drain 循环

mwx `mwx_intr`（L2247-2275）：处理完一轮后**循环最多 8 次**重读
`HOST_INT_STA`——"MSI is edge triggered: events that arrived while the host
interrupt enable was cleared do not raise a new message"。我们的
`mt792x_irq_handler` 无 drain 循环（依赖下一次 poll）。可吸收。

### D5【中】attach 动作顺序与 warm-reboot 恢复链

mwx attach 顺序（`MtkMwx.cpp:2440-2550`）：

```
BAR map → pci_intr_map(msix→msi→intx) →
  fw_pmctrl/drv_pmctrl(driver_own)
    失败 → mwx_wfsys_reset → 再试
    仍失败 → mwx_pci_flr (PCIe FLR，保存/恢复 config) → 再试 →
  chipid → MT_MCU2HOST_SW_INT_ENA=WAKE_RX_PCIE → wfsys_reset →
  INT_ENA=0、MT_PCIE_MAC_INT_ENABLE=0xff → pci_intr_establish →
  txwi_alloc → dma_alloc → preinit(固件)
```

要点：
- **PCIe FLR 恢复**（`mwx_pci_flr` L4227）：warm reboot 不断电 M.2 卡，
  上一 OS 会话可能留卡死芯片——先 WFSYS reset 再 FLR。mt76wm 无 FLR。
- 固件加载在 `attach_hook`（M2 阶段）**晚于**中断建立——先建中断再传固件，
  patch/RAM 下载的 TX_DONE/RX 事件能实时收。

### D6【中】DMA：bus_dma sync 纪律 + 4KB clusters

mwx `mwx_dma_tx_enqueue`（L3375）：`bus_dmamap_load_mbuf` 后
`BUS_DMASYNC_PREWRITE`、完成后 `POSTWRITE`；desc
`{buf0, buf1, info, ctrl=SD_LEN0|SD_LEN1|LAST_SEC0/1}`，kick 写
`MT_DMA_CPU_IDX`。固件下载用 **4KB DMA cluster 分块**（README 实测点）。
mt76wm 用 IOBufferMemoryDescriptor + IODMACommand（kIOMapInhibitCache
uncached，等价无 sync 需求），但**固件分块粒度**值得核对（我们整块/页？）。

### D7【低】RX 撕 WPDMA 头

mwx `mwx_mcu_rx_event`：`m_adj(m, MT7921_MCU_RXD_SIZE)` 先撕 12B WPDMA 头
再解析 mcu rxd。我们 Linux 语义 `skb_pull(sizeof(*rxd)-4)` 等价（rx 描述符
格式一致），但值得复查偏移是否与 mwx 的 `MT7921_MCU_RXD_SIZE` 完全对齐。

### D8【低】`sc_mcu_wait[16]` seq 等待表

mwx 用 per-seq 槽（`mcu_cmd`/`mcu_int`/`mcu_m`）+ `tsleep_nsec(3s)`/`wakeup`，
超时→`mwx_reset`。我们用 send_and_get_msg 同步等待（Linux 形态），语义等价。

### 附：`setIOEnable(true)` 无罪（重要数据点）

mwx start 同样 `setBusMasterEnable/setIOEnable(true)/setMemoryEnable(true)`
+ `configWrite8(0x41, 0)`（L336-339）——**与我们完全相同**且其驱动工作。
→ PCIe 对齐实验中"移除 setIOEnable（command=0x0006 对齐 Linux）"优先级
**降级**：IO Space 位不是差异因子。

## 3. 对 sem 卡点的更新判断

- 17 项排除表后新增**第 18 项假设：MSI enable 时序错误导致 MSI posted-write
  abort 拖死 WPDMA**（D1）。该假设与"TX_DONE 状态位可见但 RX 零写回"兼容，
  且 D1+D2 修复成本极低（build 56 已含）。
- 若 build 56 仍零应答：下一梯队为 D3（换 IOInterruptEventSource）、
  D5（FLR 恢复链，尤其 warm reboot 场景）、D6（固件分块粒度核对）。
- 与既有"PCIe 对齐实验"（用户已确认）合并推进：**MSI enable 时序即第一
  步**；setIOEnable 项已可跳过。

## 4. TODO（从 mwx 吸收）

- [x] D1 移除手动 MSI enable（build 56）
- [x] D2 `MT_PCIE_MAC_INT_ENABLE=0xff`（build 56）
- [ ] D3 中断源换 `IOInterruptEventSource`（若 D1/D2 不够）
- [ ] D4 MSI edge drain 循环搬入 `mt792x_irq_handler`
- [ ] D5 PCIe FLR 恢复链（warm reboot 残留）
- [ ] D6 固件 4KB 分块粒度核对
- [ ] D7 RX WPDMA 头偏移与 `MT7921_MCU_RXD_SIZE` 复查
- [ ] 真机验证 build 56：`SEMGET/RELEASE` 是否有 `SEMRET`、
      中断 log 是否出现 `interrupt status=...`（对照 mwx 的 intr 计数）
