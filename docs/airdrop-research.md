# AirDrop 调研总结 — airdrop-mt7921 借鉴评估

> 调研对象：[jedbillyb/airdrop-mt7921](https://github.com/jedbillyb/airdrop-mt7921)（Linux + 内建 MT7921/MT7922 实现 iPhone AirDrop）。
> 本地克隆：`/Users/miyamoto/Documents/airdrop-mt7921`。
> 日期：2026-11-13。

## 一句话结论

**能借鉴，但借鉴的是"协议层 + 经验"，不是主体代码。** 该项目 80% 的复杂度在对抗
Linux cfg80211/mt76 的缺陷（monitor 双接口 trick、iw 假报告、hostapd 借信道上下文），
这些在 kext 里天然不存在；真正可移植的资产是 AWDL/AirDrop 协议栈、iOS 26 补丁、
MT7921 硬件行为知识和诊断方法论。

## 项目本体（速览）

纯用户态方案，无内核补丁：

```
iPhone ⇄ AWDL 802.11 帧 ⇄ mon0/mon1（mt76 monitor vif）
        ⇄ owl（AWDL 协议栈，建 awdl0 tun）⇄ OpenDrop（AirDrop：mDNS + HTTPS）
```

- **owl**（jedbillyb/owl fork）：AWDL 链路层（选举、同步、16 槽信道序列），libpcap 收发。
- **OpenDrop**（补丁版，钉 commit `03b4adc`）：AirDrop 应用层（/Discover → /Ask → /Upload）。
- **airdrop.sh / airdropd**：编排器（扫信道、BLE 唤醒手机、拉起/拆除栈）。
- 核心 trick：MT7921 active monitor 固件钉死信道 36 → **先建 plain monitor 调信道，
  再叠加 active monitor 共享信道上下文**（`airdrop.sh:388-414`），active 负责 ACK。
- 现状：收发均可用（iOS 26），速度 40–67 kB/s，仅限 AirDrop **Everyone** 模式。

## 可直接借鉴（跨平台协议层）

| 资产 | 内容 | 出处 |
|---|---|---|
| AWDL 协议状态机 | action 帧格式、16 槽序列 TLV、master 选举、可用性窗口 | owl `src/`（rx.c / election.c / state.c） |
| 序列策略引擎 | verbatim / widen / intersect / rotate（16 槽数组运算） | `airdrop.sh:561-582` |
| iOS 26 补丁 | chunked request body、dvzip 编解码（4B BE 头 + zlib 块 → ODC cpio）、**TransferID/TransferType 绑定**（发送方向决定性修复）、fail-closed 同意 | `patches/README.md`（19 个） |
| BLE 唤醒字节 | Continuity type 0x05 广播逐字节格式（Everyone 模式哈希全 0 即可唤醒手机 AWDL） | `tools/blewake.sh:28-41, 117-118` |
| 操作知识 | 信道白名单 6/36/44/132/149、扫后定 + 赢家缓存、"Everyone 10 分钟"静默过期、分享面板开=手机是 sender、BLE 唤醒两道门 | FINDINGS §22/§27 |
| 吞吐模型 | 45 kB/s = 照抄手机 2/16 槽占空比；widen 填槽预测 110–250 kB/s（未验证） | FINDINGS §21 |
| 诊断方法论 | 只信抓包频率；每层独立证伪（帧→同步→IP→mDNS→HTTP）；trap + 分离 watchdog 恢复现场 | `tools/README.md` |
| 分析工具 | `bursts.py` / `slotmap.py`（只读 pcap，跨平台） | `tools/` |

## 不用借鉴（Linux 绕行方案，kext 里消解）

- **双 monitor 接口 trick**：只是绕 Linux mt76 "active monitor 钉信道 36" 的补丁式绕行；
  kext 自己调度射频，ACK 是驱动天然行为，"active monitor" 概念不需要。
- **runtime-pm/deep-sleep 旋钮、iw/nl80211 语义、hostapd P2P-GO 借 chanctx、GRUB 钉内核**：
  全是对抗 Linux 缺陷的记录，仅警示价值。
- FINDINGS §41 的结论恰好是本项目的目标形态：Linux 缺的是"**驱动原生 AWDL + 射频分时**"，
  而这正是 macOS IO80211 架构本来的样子。

## 对到 mt76wm：两条路线

### 路线 A — raw 管道 + 移植 owl/OpenDrop（借整套上层）

- **现成基础好于预期**：RX 已关 hdr-trans（`mtkwm/mt7921/init.c:85`），802.11 帧头 +
  mactime 保留；TX 管理/action 帧走 `ic_mgtq → MTK_TX_FLAG_MGMT` 已通
  （`mtkwm/glue/mtk_net80211.c:813-837`）。
- **要补**：monitor 模式端到端接线（itl80211 的 `IEEE80211_M_MONITOR` 与 mt76 core 的
  monitor 机器都在，但 glue 未接、monitor 帧被丢弃 `mtk_net80211.c:528-534`）、
  raw TX 注入、用户态通道（user client / BPF tap）、owl 平台层移植（libnl→自有 ioctl、
  tun→utun/自有 ifnet、BlueZ→CoreBluetooth）、OpenDrop 基本原样跑（纯 Python）。
- **效果**：iPhone ⇄ 黑苹果 AirDrop，**Everyone-only**，CLI/守护进程形态（非 Finder 集成）。
- **工作量**：中等偏上，每层可独立分层验证。

### 路线 B — 注册 awdl0，系统 AirDrop 原生工作

- **Apple80211 私有契约在头文件里相当完备**：60+ AWDL ioctl + 结构体（含
  `sync_channel_sequence` static_assert）、事件号、`IO80211P2PInterface` 生命周期
  （awdlInit/awdlConfigureIfnet/awdlAttachToBpf）、`outputActionFrame`/`inputMonitorPacket`/
  `createVirtualInterface` 虚函数（`include/Airport/apple80211_ioctl.h:116-351`）。
- AirportMtkwm 已是 Apple80211 可加载的 IO80211Controller；AWDL 目前全 stub
  （`AirportMtkwm.hpp:281-295`）——airportitlwm 参考也止步于此。**2026-11-13 更新**：
  Arxusion/AirportMtkWlan v2.2.x 是路线 B 的**接口级先例**（awdl0 已注册、RX 分流、
  链路层 BA/SNAP 加速、Instant Hotspot IE），但 **AWDL 状态机/ROC/AirDrop 应用层
  均未实现，未端到端跑通**（详
  [airportmtkwlan-analysis.md](airportmtkwlan-analysis.md) §5.4）。AWDL 接口
  **发布配方**（createVirtualInterface role-6 覆写 + attach 后必须
  registerService + 按 role 单飞）已有可抄模板，见
  [airportmtk-macos-analysis.md](airportmtk-macos-analysis.md) §5。
- **要补**：路线 A 的 raw 底座 + multi-vif 重构（现单 vif `g_m6`，`mtk_port_funcs.c:1206`）+
  ioctl 实现 + 内核态信道调度器（AWDL 时隙与 STA 分时）+ 事件回抛。
- **效果上限高**：Finder/共享面板原生 AirDrop、与 Mac 互通；黑苹果 Apple ID/IDS 身份真实，
  **contacts-only 有机会工作**（对 Linux 项目的硬超越）。
- **最大不确定性**：airportd/wifid 是否接受非 Apple 驱动的 awdl0 —— 头文件回答不了，
  需真机试。工作量大，属逆向工程项目。

## 硬边界（任何路线）

1. **吞吐**：上限由手机槽占空比决定，与平台无关。45 kB/s（2/16 槽）是 baseline；
   提速关键是吃下手机传输中加宽的 11–15/16 槽（FINDINGS §21 预测 110–250 kB/s）。
2. **contacts-only**：走 OpenDrop（路线 A）必然 Everyone-only（Apple 签名三件套不可得，
   FINDINGS §50）；仅路线 B 用系统身份栈可能突破。

## 建议与前置

1. **前置不变**：真机 MCU semaphore（PATCH_SEM_CONTROL 零应答，见
   [testing-log.md](testing-log.md) §4）是硬阻塞——固件不通则 STA 不通，AirDrop 无从验证。
2. **路线 A 先行**：先打通 raw 收发管道，移植 owl 跑通 iPhone 收发；BLE 唤醒用
   CoreBluetooth 发同样的 type 0x05 字节。协议层直接抄，风险集中在驱动层。
3. **路线 B 做低成本 spike**：在 AirportMtkwm 里试注册最小 AWDL vif，真机看 airportd
   是否认它——一次实验验证最大不确定性，再决定是否投入 60 个 ioctl。
