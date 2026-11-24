# AirportMtk-macOS 对照分析（2026-11-13）

> **权威内部参考（2026-11-23 补）**：`/Users/miyamoto/kc-skywalk/` 存有用户用 Ghidra
> 反编译的 **IO80211Family（50 万行）/ IOSkywalkFamily** 源码 + 协议头 dump
> （`IO80211InfraProtocol.h` / `IO80211SkywalkInterface.h`）。排查 apple80211 ioctl
> 契约、Skywalk 槽位、kernel_task 扫描断链、AWDL 接口时，先查这里再猜。

克隆 [Hi-Jiajun/AirportMtk-macOS](https://github.com/Hi-Jiajun/AirportMtk-macOS)
（本地 `/Users/miyamoto/Documents/AirportMtk-macOS`）。

**项目定位**：MT792x 的 **IO80211 家族驱动** = AirportItlwm 前端（IO80211Controller
子类）+ laobamac/SimpleMtkWlan 的 OpenBSD **if_mwx** 驱动体（`SimpleMtkWlan/hal_mwx/`，
~6.7k 行）。与 [airportmtkwlan-analysis.md](airportmtkwlan-analysis.md)
（Arxusion/AirportMtkWlan）**同属 mwx 血统**，本文只记**增量**（E 系列），不重复 D 系列。

**自报实测状态**（macOS 15.8.1 + MT7922/RZ616 `14c3:0616`）：

| 项 | 状态 |
|---|---|
| 固件下载（ROM patch + WM image 全量） | ✅ |
| 关联 / WPA2 4-way / DHCP / IPv4+IPv6+TCP | ✅（48.4/18.5 Mbit/s） |
| 驱动侧扫描 | ✅ 21 轮全通（`scan done #N complete_ch=41`） |
| AWDL 接口发布 | ✅（数据面不支持，见 §5） |
| 扫描结果进系统 Wi-Fi 列表 | ❌ kernel_task 断链（见 §4） |

---

## 1. 与 D 系列的增量对照（速查）

| # | 新增项 | 与 D 系列关系 | 出处 |
|---|---|---|---|
| E1 | 同 workloop ISR 阻塞 + command gate 门控自排空 | D 系列未覆盖，**最高价值** | `MtkMwx.cpp:8965-9120` |
| E2 | DRV_OWN 前先读 `MT_TOP_LPCR_HOST_BAND0` | D 系列未覆盖 | `MtkMwx.cpp:9447-9448` |
| E3 | WFSYS reset 清 MSI capability，恢复需三件套 | 扩展 D1/D2 | `MtkMwx.cpp:2926,5661,5721` |
| E4 | 固件-驱动版本耦合（2024 驱动 + 2026 固件 → own failed） | D 系列未覆盖 | `docs/rounds/round93` |
| E5 | 有界等待三级 + boot latch 退役教训 | 方法论 | `README.md:188-203`、`AirportMtk.cpp:121-165` |
| E6 | scatter/mbuf 尺寸算术 panic（2048+64>2048） | 对应我们 D6 复查项 | `docs/rounds/round31` |
| E7 | wait slot **入队前**注册（上游 fd5e4436 修复） | 扩展 D8 | `MtkMwx.cpp:8547` |
| E8 | PCI command bit10（Interrupt Disable 误置）轮次 | D1 邻域 | `docs/rounds/INDEX.md` |
| S 系列 | 扫描实现 + 上报断链取证（§3/§4） | 全新 | round73/74/75 等 |
| A 系列 | AWDL 发布配方（§5） | 全新 | `AirportMtk.cpp:1415-1500` 等 |

## 2. MCU/固件启动增量（E 系列，sem 卡点对照用）

### E1【最高价值】同 workloop ISR 阻塞 → 应答堆环

`MtkMwx.cpp:8965-9081` 长注释 + 实证：IOInterruptEventSource 注册在驱动
workloop 上，而发命令链（`enable()` → `mwx_init()`）**跑在同一个 workloop**——
等待期睡眠时中断 action 无法运行，应答只能堆在 MCU RX 环里。启动日志实证：
五次 MAC_INIT_CTRL 发送全部超时，init 放弃后应答**批量到达**。

- **解法**：command gate 门控自排空——等待方每步执行
  `mwx_mcu_drain_gated`（`MtkMwx.cpp:8994`）主动排空 MCU RX 环，
  `mwx_mcu_await`（:9029）tsleep 10 ms 步进 + 每步 drain，全程有界
  （超时返 EWOULDBLOCK，**超时不 reset**——reset 会与在飞命令重叠造成超时级联，:9107-9120）。
- **判别签名**：`SEMSEND ret=-60` 超时后是否有**迟到应答批量涌入**。
  我们的 `mtk_mcu_wait_poll` 强制轮询（§5 "设备中断从不投递"）部分掩盖了此形态，
  但若 D3 换回普通中断路径，必须同步引入门控自排空。

### E2【高】DRV_OWN 前先读 `MT_TOP_LPCR_HOST_BAND0`

`MtkMwx.cpp:9447-9448`："this read is needed to make interrupts work"——
写该寄存器拉 DRV_OWN 之前**必须先读一次同寄存器**，否则中断不工作，
后续 MCU 命令全部零应答。随后 5000 ms 有界轮询 FW_OWN 清除（:9449）。
对照点：`mt7921e_driver_own` / `__mt792xe_mcu_drv_pmctrl`（testing-log §5）
若 macOS 移植丢了这个先读后写，症状与 sem 零应答完全一致，且发生在第一条命令上。

### E3【高】WFSYS reset 清 MSI capability → 恢复三件套

- `mwx_pci_msi_enable`（:5661）：仅当 MSI addr 未编程（0xfee00000）时写
  addr/data 并置 Enable 位；**WFSYS reset 会清掉 capability，reset 后必须重调**（:2926）。
- `mwx_pci_irq_state`（:5721）：发现 Enable 位被清时恢复——实测"只恢复位"不够，
  还要 **ack `MT_WFDMA0_HOST_INT_STA` + 重写 `HOST_INT_ENA` + `PCIE_MAC_INT_ENABLE`**。
- 我们的 wfsys_reset 重试链（testing-log §5）执行后是否重设 MSI/中断使能，需核对。

### E4【中】固件-驱动版本耦合

round93 实测：2024 mt76 配 2026 固件 → `WM Firmware Version: ____000000` →
`driver own failed`；macOS 侧驱动配最新固件正常。**"零应答/不起机"可由固件版本
错配单独造成**。建议 A/B：linux-firmware 20231211 那对 blob
（`WIFI_MT7922_patch_mcu_1_1_hdr` + `WIFI_RAM_CODE_MT7922_1`，7921 同理）。
方法学：round93 含 Linux kprobe 抓 MCU 流量的对照法。

### E5【中】有界等待 + boot latch 退役教训

三级上限（MCU 应答 3 s / 描述符槽 10 s / 固件就绪轮询 1.5 s）+ 轮询步进 10 ms。
boot latch（`MT_NVRAM_ATTEMPT`）**已退役**：APFS 关机卡顿致 latch 清除不生效
（一天四次误拒），且当时 `mwx_poll()` 有单位 bug（标称 5 s 实跑 ~500 s）——
**修好单位 bug + 全路径有界等待后，硬闩的价值不成立**。教训可直接吸收。

### E6/E7/E8【低-中】杂项

- round31：`MT_FW_SCATTER_MAX(2048) + sizeof(mcu_txd)(64) > MCLBYTES(2048)` →
  单 cluster 分配器 panic 而非 ENOBUFS；修复 = 1792 上限 + 分配前拒绝 + static_assert。
  我们固件分块 4096 与 Linux 一致（D6 已核对无差异），但**分配器算术断言**值得加。
- `sc_mcu_wait[seq]` **入队前**注册（上游 fd5e4436，round36 核对表）。
- INDEX 轮次 "PCI Command bit10（Interrupt Disable 位误置）"——command 寄存器
  逐位管理的又一实证（与我们"PCI command `0002` 对齐"候选 A 呼应）。

## 3. 扫描实现（驱动侧已验证，参数可直接抄）

**机制：MCU 硬件扫描**，非 net80211 软扫描。命令 `MCU_CE_CMD_START_HW_SCAN` +
`mt76_connac_hw_scan_req`（`MtkMwx.cpp:10274-10646`）——与 Linux mt76 的
`mt76_connac_mcu_hw_scan`（mt7921/main.c、mt76_connac_mcu.c）同族协议。

**数据面（重要，勿误解）**：扫描结果不走特殊上报通道——beacon / probe response
以**普通帧从 RX 数据环**回来，固件只在整轮结束发一个 scan-done 事件
（MT7921：`MCU_EVENT_SCAN_DONE`，connac2 legacy eid，mt7921/mcu.c:366-368；
MT7925 才是 UNI `MCU_UNI_EVENT_SCAN_DONE` 0x0e——AirportMtk 一度怀疑的
"UNI 语义"疑云即两代命令集之别）。**结果集 = 现有 RX 半部已经能收的帧**；
真正缺的只是命令后端 + 完成事件消费。

**mt76wm 现状（2026-11-13 核对）**：MT7921 用的是较老的 **connac2 命令集**
（相对 MT7925 的 UNI/connac3），**该后端尚未写**——net80211 ops 走软扫描（M4），
hw_scan 未接线。移植树里已有未接线的死代码可作起点：命令构建器
`mt76_connac_mcu_hw_scan`（`mt76core/mt76_connac_mcu.c:1823`）、scan-done
事件分发（`mt7921/mcu.c:366-368` → `mt7921_mcu_scan_event` → `scan_event_list`
+ `MT792x_HW_SCAN_TIMEOUT`，队列已在 `mt7921/init.c:220` 初始化）；
但 glue 侧调用/取消/超时链与 ops 接线未写。if_mwx 的发送代码无需搬——
**协议相同，参数与工程细节可抄**：

| 参数 | 值 | 出处 |
|---|---|---|
| 扫描类型 | 默认被动（不发 probe req）；boot-arg `mtkascan` 切主动 | `MtkMwx.cpp:10566` 一带 |
| 驻留时间 | min=dwell=120 ms/信道 | :10566-10567 |
| 整轮超时 | nchan × 120 ms（41 信道 ≈ 4.9 s） | :10564 |
| 信道遍历 | `ic_channels` 全量；≤32 只填 primary，33-64 拆 primary+ext | :10475-10490 |
| 完成判定 | `MCU_EVENT_SCAN_DONE`(0x0d) 按 **seq 匹配**（陈旧丢弃） | :10692-10750 |
| 看门狗 | seq 守卫；超时**只 cancel 不 reset**（reset 级联超时） | :5392, 7220-7284 |

**已踩坑（前车之鉴）**：

1. **`MWX_FLAG_SCANNING` 泄漏楔死**：Wi-Fi 电源开关走 stop/init 时
   `mwx_stop` 在 `was_running==false` 下不清标志，SCAN→SCAN 守卫 return
   又在所有日志之前 → 扫描永久静默吞掉。修复：stop 无条件清 + init 进门清 +
   守卫前加日志（`docs/rounds/stuck-scan-flag-brief.md`）。
2. **假 scan-done 掩蔽真断链**：100 ms 定时 `fakeScanDone` 喂 airportd，
   掩蔽了 §4 的上报断链；换成真实固件完成后 bug 才显影（round73→74）。
   **不要用假完成喂上层**。
3. **ioctl 路径分配内存会 panic**（round90）：`mwx_log_flush` 在 ioctl 路径
   分配 → 双重错误 panic。教训：**apple80211 ioctl 处理路径禁止分配**。
4. 固件曾不发 scan-done（疑 UNI 语义 `MCU_UNI_EVENT_SCAN_DONE` 0x0e vs legacy
   eid switch），全靠 15.76 s 看门狗兜底；后自行消失（round74 起 21 轮全健康）。

## 4. 扫描结果上报断链（kernel_task）——AirportMtkwm 变体必读

**现象**：驱动结果集填充正确、每条都被读走（scanRes 340 = 34 轮 × 9 条 + 结束
标记），但 airportd 永远 `results=0`。探针定性（round75）：

- `req=86`（SCAN_REQ_MULTIPLE）→ caller=**airportd**（请求侧正常）；
- `req=11`（SCAN_RESULT）→ 12/12 caller=**kernel_task**（IO80211FamilyLegacy
  内核桥接层），桥接层读走结果 + 结束标记后**不转发给现代扫描缓存**。

**已证伪**（round74/75，省弯路）：字段畸形、verb 缓冲太小（4132B ≥ 1164B）、
结构常量与 Apple SDK 不一致、ni_age_ts 过滤、asr_age 语义、信道子集/SSID 列表
不匹配、100 ms 假完成——全部排除。

**历史证据**：`__IO80211_TARGET=__MAC_13_0` 时代**曾通过**（`airport -s`/
CoreWLAN 列出过真实 AP）→ 契约曾经对上过，后断（join-and-awdl-audit.md
2026-10-11 也记录过 CoreWLAN 拿到真实 AP）。

**嫌疑与候选方向**（round75 §4）：

1. **缺接口侧扫描路径**：IO80211ScanManager 走现代接口侧协议，与老控制器
   ioctl 分派并存；旁证——CURRENT_NETWORK(103)/CHANNELS_INFO(207) 一次都没进
   驱动（"case 103 是死代码"），同样走接口侧。**优先接现代通道，勿在 legacy
   通道继续修字段**。
2. v2-port（macOS 26 Skywalk 前端）整体替换——从未部署，31+110 槽位不匹配，仅作研究。
3. 试探 legacy 桥接契约：`scanDone(scanSource,int)` 回调形态（上游也从未调用），
   可能缺某个回调/属性让桥接层把结果转正。

**诊断基建（第一天就该带）**：

- IOKit 计数属性 `AirportMtkScanCounters`：r10/r11/scanReq/scanRes/ret0/5/12
  （0=返回一条、5=列表结束标记、12=空表）+ "ioctl req=%d" 面包屑环
  （`tools/read-breadcrumb.py` 读）。
- `tools/cwscan.sh/.swift`：CoreWLAN 扫描探针（**与 Wi-Fi 菜单同路径**，无法
  绕开 airportd）；陷阱——airportd 常判 "daemon cache (no scan)" 根本不下发
  SCAN_REQ，**"0 networks" 不能当证据**（entitlement/定位 SSID 过滤）。
- **硬前提**：`ieeeChanFlag2appleScanFlagVentura`——Ventura+ 频道标志转换，
  没有它即使结果通了菜单也不刷新（`AirportSTAIOCTL.cpp:2211`）。

## 5. AWDL 接口发布配方（对 [airdrop-research.md](airdrop-research.md) 路线 B）

已验证的发布路径（wifip2pd 能看到接口）：

1. `createVirtualInterface` 覆写（`AirportMtk.cpp:1415-1500`）：super 对
   role 6（`APPLE80211_VIF_AWDL`）返回 NULL，需自建 `IO80211P2PInterface`，
   `init(this, ether, role, "awdl")`。
2. **attach 后必须 `registerService()`**（`AirportSTAIOCTL.cpp:2342-2560`）——
   最初只 attach 不发布 → `!registered, !matched`，wifip2pd "Timed out while
   waiting for interface AWDL"，第二次请求走"已存在"捷径假成功。修复 = 两步发布
   （`fAWDLRegistered` + 属性 `AirportMtkVirtIfPublished`）。
3. 单飞按 **role** 而非 BSD 名（否则空 bsd_name 导致 awdl1 重复）。
4. boot-arg 开关：`mtknoawdl` 关 AWDL、`mtkp2p` 开 P2P role 3/4/5
   （P2P_DEVICE/P2P_GO 分支仍是旧写法，同 attach-不发布 之病）。

**功能边界**：AirDrop/接力**未支持**，且非改宏可得——缺 remain-on-channel（ROC）
整套硬件能力（上游 if_mwx 与本移植皆无）。与 airdrop-research.md 的判断一致：
真正的 AWDL 时间片调度（信道序列分时）是未做部分。

## 6. TODO（吸收清单）

- [ ] **E1**：核对 MCU 等待路径是否同 workloop 阻塞形态；若有中断投递恢复计划，
      同步实现 command gate 门控自排空（判别：超时后迟到应答批量到达）
- [ ] **E2**：核对 `mt7921e_driver_own` 是否先读 `MT_TOP_LPCR_HOST_BAND0` 再写
- [ ] **E3**：wfsys_reset 后重调 MSI enable + ack `HOST_INT_STA` + 重写
      `HOST_INT_ENA`/`PCIE_MAC_INT_ENABLE`
- [ ] **E4**：固件 blob 版本 A/B（linux-firmware 20231211 对）
- [ ] E6：固件分块分配前加尺寸断言（防 scatter+txd 越 cluster）
- [ ] 扫描（mtkwm/HeliPort 路线）：M4 sw scan 已有；若上 hw_scan，先补
      **connac2 命令后端**（MT7921 用老一代 connac2 命令集；构建器
      `mt76_connac_mcu_hw_scan` 与 scan-done 分发已在树中但未接线，见 §3）。
      结果数据面免费（beacon/probe resp 走现有 RX 数据环）；吸收参数
      （120 ms 驻留、seq 看门狗、只 cancel 不 reset）；防 `MWX_FLAG_SCANNING`
      类标志泄漏
- [ ] 扫描（AirportMtkwm 路线）：抄 `AirportSTAIOCTL.cpp` 扫描面（0/5/12 游标
      语义、ni_age_ts 新鲜度、**Ventura 频道标志**）；第一天带计数属性 + 面包屑 +
      `req=11 caller` 探针；按接口侧扫描路径设计
- [ ] AWDL 发布：照 §5 配方（attach+registerService 两步、role 单飞）
