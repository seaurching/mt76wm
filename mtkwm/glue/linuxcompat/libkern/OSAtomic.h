/*
 * libkern/OSAtomic.h - self-contained OSAtomic shim for the mt76wm port.
 *
 * The SDK's OSAtomic macros rely on declarations that are not reliably
 * visible in every TU (macro/declaration interplay in the MacKernelSDK
 * copy), so this shadow header (found first via -isystem) provides the
 * classic OSAtomic API as inline functions built on __atomic builtins.
 *
 * Ported for mt76wm (XNU).
 */
#ifndef MTK_OSATOMIC_H
#define MTK_OSATOMIC_H

#include <libkern/OSTypes.h>

#if defined(__cplusplus)
extern "C" {
#endif

static inline SInt32 OSIncrementAtomic(volatile SInt32 *address)
{
    return __atomic_add_fetch(address, 1, __ATOMIC_SEQ_CST) - 1;
}

static inline SInt32 OSDecrementAtomic(volatile SInt32 *address)
{
    return __atomic_sub_fetch(address, 1, __ATOMIC_SEQ_CST) + 1;
}

static inline SInt32 OSAddAtomic(SInt32 amount, volatile SInt32 *address)
{
    return __atomic_fetch_add(address, amount, __ATOMIC_SEQ_CST);
}

static inline SInt32 OSBitAndAtomic(UInt32 mask, volatile UInt32 *address)
{
    return (SInt32)__atomic_fetch_and(address, mask, __ATOMIC_SEQ_CST);
}

static inline SInt32 OSBitOrAtomic(UInt32 mask, volatile UInt32 *address)
{
    return (SInt32)__atomic_fetch_or(address, mask, __ATOMIC_SEQ_CST);
}

static inline SInt32 OSBitXorAtomic(UInt32 mask, volatile UInt32 *address)
{
    return (SInt32)__atomic_fetch_xor(address, mask, __ATOMIC_SEQ_CST);
}

static inline Boolean OSCompareAndSwap(UInt32 oldValue, UInt32 newValue,
                                       volatile UInt32 *address)
{
    return __atomic_compare_exchange_n(address, &oldValue, newValue, false,
                                       __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static inline Boolean OSCompareAndSwapPtr(void *oldValue, void *newValue,
                                          void *volatile *address)
{
    return __atomic_compare_exchange_n(address, &oldValue, newValue, false,
                                       __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static inline SInt64 OSAddAtomic64(SInt64 amount, volatile SInt64 *address)
{
    return __atomic_fetch_add(address, amount, __ATOMIC_SEQ_CST);
}

static inline Boolean OSCompareAndSwap64(UInt64 oldValue, UInt64 newValue,
                                         volatile UInt64 *address)
{
    return __atomic_compare_exchange_n(address, &oldValue, newValue, false,
                                       __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static inline SInt8 OSIncrementAtomic8(volatile SInt8 *address)
{
    return __atomic_add_fetch(address, 1, __ATOMIC_SEQ_CST) - 1;
}

static inline SInt8 OSDecrementAtomic8(volatile SInt8 *address)
{
    return __atomic_sub_fetch(address, 1, __ATOMIC_SEQ_CST) + 1;
}

static inline SInt16 OSIncrementAtomic16(volatile SInt16 *address)
{
    return __atomic_add_fetch(address, 1, __ATOMIC_SEQ_CST) - 1;
}

static inline SInt16 OSDecrementAtomic16(volatile SInt16 *address)
{
    return __atomic_sub_fetch(address, 1, __ATOMIC_SEQ_CST) + 1;
}

static inline void OSMemoryBarrier(void)
{
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

#if defined(__cplusplus)
}
#endif

#endif /* MTK_OSATOMIC_H */
