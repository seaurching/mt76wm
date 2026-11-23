# AirportMtkWlan 对照分析（2026-11-08；真机运行验证 2026-11-08 补）

克隆 [Arxusion/AirportMtkWlan](https://github.com/Arxusion/AirportMtkWlan)
（本地 `/Users/miyamoto/Documents/AirportMtkWlan`）与 mt76wm 逐段对照。
该项目是 OpenBSD **mwx(4)** 的 macOS 移植（MT7921/MT7922，`pci14c3:7961` 同硬件），
原生 AirPort/Skywalk 形态。

**真机运行验证（决定性）**：`/Users/miyamoto/Downloads/AirportMtkWlan.kext`
（com.flxxxxddd.AirportMtkWlan 2.0.0）在本机 MT7921 上 **完整工作中**：
固件加载 ✅ → 扫描下发 ✅（`Scan type to FW 1`）→ 原生 AirPort 接口
**en2 / MAC `b4:b5:b6:e5:8d:ad`**（与 Linux live 的 wlp4s0 真 MAC 一致）✅。
→ 硬件/BIOS/VT-d/macOS 平台层全无罪，问题 100% 在驱动代码。

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

### D0【致命，sem 零应答根因】RX ring fill 留一格 vs 填满环 —— 已修（build 57）

`cpu_idx` 是 producer 指针（环形）。填满整个环后 `q->head` 绕回 0，
`kick_queue` 写 `cpu_idx=0 == dma_idx=0` —— WPDMA 视为**空环**，从不消费
任何 RX desc，MCU 应答无 buffer 可写 → sem 永远超时。日志里长期存在的
`mcu_rxq cpu=0 dma=0 queued=8` 即此（软件满、硬件空）。

三方语义对照：
- **Linux** `dma.c:785`：`while (q->queued < q->ndesc - 1)` 留一格 ✅
- **mwx** `mwx_queue_fill`：`last = (mq_count + mq_cons - 1) % mq_count`，
  `while (idx != last)` 留一格 ✅
- **mt76wm（旧）**：`while (q->queued < q->ndesc)` 填满 ❌（为修"边界 NULL
  DMA"时误改；填满后硬件不取 desc，"挂死消失"是假象）

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

## 2.5 架构/形态差异（血统不同，不阻塞 MCU 通路）

| 维度 | AirportMtkWlan (mwx) | mt76wm |
|---|---|---|
| 血统 | OpenBSD mwx(4) 原生移植 | Linux mt76 最小改动移植 |
| 系统形态 | **原生 AirPort/Skywalk**（IO80211Controller、apple80211 ioctl、系统 Wi-Fi 菜单） | itlwm 形态（IOEthernetController + UserClient + HeliPort） |
| DMA | bus_dmamap + PREWRITE/POSTWRITE sync、mbuf | IOBufferMemoryDescriptor（kIOMapInhibitCache 免 sync）+ skb 包装 |
| 中断源 | 普通 `IOInterruptEventSource` + trampoline（MSI edge 无需 filter） | `IOFilterInterruptEventSource`（filter 恒 true，等价） |
| MCU 等待 | `sc_mcu_wait[16]` per-seq 槽 + `tsleep_nsec(3s)`/`wakeup`，超时 `mwx_reset` | `send_and_get_msg` 同步等待（Linux 形态） |
| RX 事件 | `m_adj(m, MT7921_MCU_RXD_SIZE)` 先撕 12B WPDMA 头 | `skb_pull(sizeof(*rxd)-4)`（等价偏移，D7 复查项） |
| ring 布局 | 6 个独立 ring（TX/RX × data/MCU/FWDL） | mt76 q_tx/q_rx 索引布局（含 MT_TXQ_FWDL） |
| 固件分块 | `MWX_FW_CHUNK_SIZE = 4096 - sizeof(mcu_txd)`（4KB cluster） | max_len=4096（与 Linux 一致）✅ 无差异 |
| 寄存器访问 | bus_space 直读 + `mwx_reg_addr` fixed_map（"To be honest this is ridiculous"） | 同 fixed_map + mmio fail-safe 防越界 panic |
| 恢复链 | own 失败 → wfsys_reset → **PCIe FLR**（warm reboot 残留） | own 失败 → wfsys_reset 重试（无 FLR） |
| attach 顺序 | pmctrl → chipid → MCU2HOST_SW_INT_ENA → **无条件 wfsys_reset** → INT_ENA=0/PCIE_MAC_INT=0xff → intr_establish → dma_alloc → preinit(固件) | 中断注册 → mtk_attach（DMA+固件）；wfsys 仅 own 失败时 |
| 中断处理 | MSI edge drain 循环（清 ENA 期间到达的事件不产生新消息，重读 8 轮） | 单轮处理（靠 poll 兜底） |

## 3. 对 sem 卡点的更新判断

- 17 项排除表后新增**第 18 项假设：MSI enable 时序错误导致 MSI posted-write
  abort 拖死 WPDMA**（D1）。该假设与"TX_DONE 状态位可见但 RX 零写回"兼容，
  且 D1+D2 修复成本极低（build 56 已含）。
- 若 build 56 仍零应答：下一梯队为 D3（换 IOInterruptEventSource）、
  D5（FLR 恢复链，尤其 warm reboot 场景）、D6（固件分块粒度核对）。
- 与既有"PCIe 对齐实验"（用户已确认）合并推进：**MSI enable 时序即第一
  步**；setIOEnable 项已可跳过。

## 4. TODO（从 mwx 吸收）

- [x] **D0 RX fill 留一格（sem 零应答根因，build 57）**
- [x] D1 移除手动 MSI enable（build 56）
- [x] D2 `MT_PCIE_MAC_INT_ENABLE=0xff`（build 56）
- [ ] D3 中断源换 `IOInterruptEventSource`（若 D0-D2 后仍无中断）
- [ ] D4 MSI edge drain 循环搬入 `mt792x_irq_handler`
- [ ] D5 PCIe FLR 恢复链（warm reboot 残留）
- [x] D6 固件 4KB 分块粒度核对（与 Linux 4096 一致，无差异）
- [ ] D7 RX WPDMA 头偏移与 `MT7921_MCU_RXD_SIZE` 复查
- [ ] 真机验证 build 57：`SEMGET/RELEASE` 出现 `SEMRET ret=2/3`、
      固件 patch+RAM 四连条、对照 AirportMtkWlan 的 en2 行为

---

## 2026-11-13 更新：v2.2.x 连接链路 / 吞吐修复 / AWDL 边界

**真机验证（用户实测，2026-11-19 补）**：该驱动在**同一台本机**（MT7921
pci14c3:7961）上可正常加载、扫描 Wi-Fi、连接手机热点（WPA2-Personal）；
**WPA2-Enterprise 无法连接**（参考驱动自身的边界）。固件/MCU/sem/WPDMA 链路
在本机被完整证明可用——mt76wm 的 sem 零应答是自家移植层问题，非平台限制。
仓库演进很快
（v2.0.0 → v2.2.x，132 文件 +108k 行）；本节分析基于干净 worktree
`/Users/miyamoto/Documents/AirportMtkWlan-latest`（origin/HEAD `3526eb7`）。
原克隆里的本地未提交改动（9 文件 +220 行：WCL/MtkMwx/net80211 等）**未触碰**，
系用户自己的实验代码，合并上游前请自行处置。

### 5.1 连上手机热点的三个关键（mt76wm 直接对照项）

分工：WCL（Wireless Control Layer，`AirportMtkWlan/WCL/WCL.cpp`）接收 macOS
命令并把参数灌进 net80211 `ic_des_*` → net80211 状态机跑 scan→join→PAE 四次握手
→ `MtkMwx` HAL 只做固件编程（信道/STA_REC/密钥）。

1. **WPA2/WPA3 过渡模式 AKM 改写**（`WCL.hpp:385-450` `selectWPA2TransitionMode`，
   scan 发布 `WCL.cpp:366-407` 与 join `WCL.cpp:485-663` 各跑一次）：手机热点几乎
   都是 PSK+SAE 混合——对**发给 macOS 的 RSN IE 副本**把 SAE 套件改写为 PSK 单套件
   + 清 MFPC/MFPR（真握手仍用原始 RSN）。不做这步连密码框都不出。FT AKM 映射回
   WPA2（`WCL.hpp:555-593`，注释明言固件/PAE 无 FT）。
2. **GTK 组播软件解密兜底**（`MtkMwx.cpp:1977-2015` `mwx_set_key`）：注释直言
   MT7921 硬件 GTK 解密组播**不可靠**——不置软件 CCMP 则组播帧被 wepfail 丢弃，
   **广播 ARP/DHCP 永远到不了协议栈**（"握手成功但拿不到 DHCP"的权威答案）。
3. **PSK 四次握手全在 net80211 PAE**（`ieee80211_pae_input.c`）：WCL 的
   installWCLKey 只管企业级 PMK（`WCL.cpp:425-452` 注释明确拒绝 PTK/GTK）。
   mt76wm 若不走 net80211 PAE 需自备握手。

### 5.2 吞吐修复根因（commit 级，"能连"→"可用吞吐"的分水岭）

| commit | 根因/修复 | mt76wm 启示 |
|---|---|---|
| 3176c02 | 1x1 能力注册（streams=0）；MSI 边沿搁浅（ISR 退出前 INT_ENA 读回确认 + 1ms 兜底）；活跃流量时 bgscan 拒绝（≥200pps 重放缓存不拉离信道）；**WMM/EDCA 不编程 → 基本速率重传**；BA reorder 风暴 250ms 限速 | 能力位别漏；QoS 必编 |
| e4a304a | **ASPM/L1SS 显式关**（Ryzen 上不关 ~40Mbps 封顶）；workloop 饿死 → 异步 pull + ISR max_passes=2 让出 gate；TX doorbell 每 16 帧批写；A-MSDU 被邻居帧误 flush | 中断/输出共享 workloop 必须有界 |
| 485d6a8 | RX BA 协商漏 amsdu=1；多段 A-MSDU 单缓冲丢链式段；RX drain 预算 64×2→128×4；MSI 搁浅二次修复（余量自触发） | 聚合参数逐项核对 |
| 9bb12ae | DMA 弹性缓冲须**物理连续+cacheable+32 位**；mbuf 段长须 setlen 否则空 head 拒收；**TX WTBL_BA 必须在 STA BA 之前编程**（WTBL_BA tag 0x08）；A-MSDU 定时 flush（2ms） | 与我们 IOVA+IODMACommand 路线不同（见下），BA 顺序可抄 |
| eee2ac7 | ASPM 按 PCIe 5.4.1.3 先下游后上游，**AMD 父桥绝对不碰**（动根口寄存器一有流量整条 PCIe 死锁）；热重启无 PERST 卡 lpctl=0x4 → `mwx_ensure_drv_own` 已持有时早退、**绝不先 fw_pmctrl**（自杀操作）、轮询 50→500ms | own 抢占时序直接对照 sem 卡点 |

**DMA 路线差异注意**：该驱动要求 OpenCore `DisableIoMapper=true`（直编程物理
DMA 环，不兼容 AppleVTD remap）；mt76wm 走真 IOVA（IODMACommand）——各自成立，
但排障时注意语境不同。其 BA 窗口 256 + A-MSDU 重组（11c1bdb/570f6f9）是吞吐必需，
SAE/FT/160MHz 可后置（160 仅 MT7922，MT7921 限 80MHz，bbff0d1）。

### 5.3 MSI/probe 修复（`3526eb7`，最新一条）

`IOPCIEDeviceWrapper.cpp` probe()：`msiCap/msixCap` **未初始化**且不检查
`findPCICapability` 返回值就当能力偏移用 → MSI-X clear-and-set 写到随机配置寄存器。
修复 = 零初始化 + 返回值/非零偏移双重校验。与 D1/D3 同类教训：**能力偏移必须
探测验证后再用**。

### 5.4 AWDL/AirDrop 边界（对 [airdrop-research.md](airdrop-research.md) 的更新）

v2.2.x 的 "high-throughput AWDL/AirDrop" **是链路层，不是 AirDrop 应用层**，
旧结论基本不变：

- **AWDL 状态机仍无**：sync/election/presence/availability-window/信道序列全部
  缺失（`AirportItlwmV2.hpp:289-306` 状态字段全是死声明）；AWDL ioctl 对
  wifip2pd 全 `unsupported`；**remain-on-channel 没有**。
- **awdl0 = 接收影子接口**：RX 分流可抄（`_if_input`：dest==awdl MAC 或
  EtherType 0x88F0 → awdl0；IPv6 `33:33:*` 组播克隆到 en0+awdl0，`_mbuf.cpp:44-118`）；
  但 TX 混进 infra 队列且需 S_RUN，对端不可达。
- "high-throughput" 实为链路层优化（650b6ec）：BA 序列窗改 modulo-4096 标准
  滑窗、非关联 peer 接受 ADDBA、Apple SNAP `00:17:F2` 解封装。
- **router coexistence 只是连接管理级**（awdl 开着不漫游 `WCL.cpp:249`；
  `mwx_send_null_pm` 是**未接线死代码**）——没有射频分时，正是 Linux 侧缺的
  "驱动原生 AWDL"，本仓库同样没给出。
- **Instant Hotspot**（6223ab4）= 关联请求注入 Apple Device/Instant Hotspot
  vendor IE + 蓝牙配套（AirportMtkBt），与 AWDL 数据面无关。
- **定位**：这是路线 B（注册 awdl0 走系统栈）的**接口级先例**，但未端到端跑通；
  要真 AirDrop 仍需实现 wifip2pd 下发的 AWDL ioctl + 信道调度，或走路线 A。
- 可抄清单：createVirtualInterface role 分支 + 本地管理 MAC `^0x02` +
  `deferBSDAttach`/`registerService` 时序 + 能力位（byte0 bit0 / byte4 bit0 /
  byte9 bit0-1）+ 上述 RX 分流 + BA/SNAP。

### 5.5 部署要求修正（AirportMtkwm 变体重要）

**不需要 OCLP root patch / IO80211FamilyLegacy 降级**，也**不做 Broadcom 伪装**
（保持原生 0x14c3，禁用一切伪装 SSDT）；要求 MinKernel/MaxKernel 与系统匹配
（Sequoia 24.2-24.99 / Tahoe 25.0-25.99，vtable ABI 分离，用错构建直接崩）；
`AirportMtkWlan.kext` 与 `SimpleMtkWlan.kext` 不可同载。此前 Hi-Jiajun 仓库的
"OCLP + spoof IOName"路线不是唯一路径——**原生 IO80211 + Skywalk（macOS ≥15.2）
可行**。

### 5.6 诊断工具三件套（整体值得抄）

1. **IORegistry 持久日志环**（`MtkDebugLog` 属性 + `tools/mtklog.sh`，免 root）——
   绕过内核日志收集限制。
2. **脱敏一键归档**（`tools/mtk-grab-logs.sh`，1000 行 9 段式，用户名/序列号/
   MAC/口令全脱敏，可 CI 化）。
3. **吞吐计数器**（`MTK_PERF=1` 构建 + `tools/airport_throughput.py` 读
   `MtkThroughput`：rx/tx 描述符、A-MSDU 链路、WTBL_BA 成败、分阶段 ticks
   直方图 + iperf3 前后对比）——**先建计数器再调吞吐**。

### 5.7 TODO 追加（连热点优先级，据 5.1/5.2 重排）

> 2026-11-13 已把下列前 5 项合并进代码（`mtkwm-airport` 构建通过）；剩余见未勾选。

- [x] sem 卡点收尾：`mt7921e_driver_own` / `__mt792xe_mcu_drv_pmctrl` 加
      "已持有早退"，drv_pmctrl 轮询 50→500ms（eee2ac7；绝不先 fw_pmctrl 已确认）
- [x] GTK 组播软件解密兜底：`mtk_set_key` 总是先建软件上下文（新增
      `IEEE80211_KEY_HWCRYPTO` 区分硬件装钥；TX 优先硬件，RX 软件兜底）
- [x] AKM 过渡模式改写：WCL 侧已有 `selectWPA2TransitionMode`（publishWCLBeacon/
      startWCLJoin 已接线）；补 itl80211 `ieee80211_choose_rsnparams` PSK 优先
      （原 SAE-first 会在混合热点上选 Dragonfly 失败）
- [x] MSI/中断：ISR tasklet 加 8-pass drain 循环（MSI 边沿不重发）；
      `wfsys_reset` 后重臂（ack INT_STA + 重写 INT_ENA/PCIE_MAC_INT_ENABLE +
      `mtk_xnu_msi_rearm` MSI config 快照恢复）；probe 能力偏移守卫（3526eb7）；
      `-nvoff` 探测解耦（3907c5c）
- [x] ASPM/L1SS 关闭：`mtk_xnu_disable_aspm`（端点+上游桥、PCIe 5.4.1.3 顺序、
      AMD 父桥禁区、`-mtkallowaspm/-mtknoparentaspm/-mtkforceparentaspm`），
      `mt7921_disable_aspm` 默认 true
- [ ] MSI 读回确认 + 1ms 兜底定时（3176c02 ISR 顶层两项，drain 循环之外）
- [ ] EDCA/WMM 编程 + BA 窗 256 + A-MSDU + WTBL_BA 顺序（吞吐阶段）
- [ ] 诊断三件套（5.6；IORegistry 日志环已有，余两件未搬）
