/* SPDX-License-Identifier: GPL-2.0 */
/* Linux compat stub for the mt76wm XNU port. Ported for mt76wm (XNU) */
#ifndef MTK_LC_ETHERDEVICE_H
#define MTK_LC_ETHERDEVICE_H

#include <linux/mtk_core.h>
#include <linux/random.h>

static inline void ether_addr_copy(u8 *dst, const u8 *src)
{
    memcpy(dst, src, ETH_ALEN);
}

static inline int ether_addr_equal(const u8 *a, const u8 *b)
{
    return memcmp(a, b, ETH_ALEN) == 0;
}

static inline bool is_zero_ether_addr(const u8 *addr)
{
    return !(addr[0] | addr[1] | addr[2] | addr[3] | addr[4] | addr[5]);
}

static inline bool is_multicast_ether_addr(const u8 *addr)
{
    return addr[0] & 0x01;
}

static inline bool is_broadcast_ether_addr(const u8 *addr)
{
    return (addr[0] & addr[1] & addr[2] & addr[3] & addr[4] & addr[5]) == 0xff;
}

static inline bool is_valid_ether_addr(const u8 *addr)
{
    return !is_multicast_ether_addr(addr) && !is_zero_ether_addr(addr);
}

static inline void eth_broadcast_addr(u8 *addr)
{
    memset(addr, 0xff, ETH_ALEN);
}

static inline void eth_zero_addr(u8 *addr)
{
    memset(addr, 0x00, ETH_ALEN);
}

static inline void eth_random_addr(u8 *addr)
{
    get_random_bytes(addr, ETH_ALEN);
    addr[0] &= 0xfe;
    addr[0] |= 0x02;
}

static inline void eth_hw_addr_set(struct net_device *dev, const u8 *addr)
{
    ether_addr_copy(dev->dev_addr, addr);
}

static inline void random_ether_addr(u8 *addr)
{
    eth_random_addr(addr);
}

/* get_random_mask_addr(dst, base, mask): randomize selected octets */
static inline void get_random_mask_addr(u8 *dst, const u8 *base, const u8 *mask)
{
    u8 rnd[ETH_ALEN];
    int i;

    get_random_bytes(rnd, ETH_ALEN);
    for (i = 0; i < ETH_ALEN; i++)
        dst[i] = (base[i] & ~mask[i]) | (rnd[i] & mask[i]);
}

struct ethhdr {
    u8 h_dest[ETH_ALEN];
    u8 h_source[ETH_ALEN];
    __be16 h_proto;
} __packed;

#define ETH_P_IP    0x0800
#define ETH_P_IPV6  0x86DD
#define ETH_P_8021Q 0x8100
#define ETH_P_PAE   0x888E
#define ETH_P_AARP  0x80F3
#define ETH_P_IPX   0x8137
#define ETH_P_802_2 0x0004

/* RFC1042/bridge tunnel SNAP headers (Linux ethertypes) */
static const u8 rfc1042_header[6] __attribute__((unused)) =
    { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00 };
static const u8 bridge_tunnel_header[6] __attribute__((unused)) =
    { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0xf8 };
#define ETH_P_802_3 0x0001
#define ETH_P_802_3_MIN 0x0600

#define ETH_GSTRING_LEN 32
#define ETH_SS_STATS    1
#define ETH_SS_TEST     2
#define ETH_SS_PRIV_FLAGS 3

#endif
