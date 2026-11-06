/*
 * mtk_glue.c - XNU implementations of the Linux kernel primitives used by
 * the ported mt76 sources (mt76wm project).
 *
 * Ported for mt76wm (XNU) - M1 minimal semantics; TODO(M2) items marked.
 */

#include <linux/mtk_core.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/pci.h>
#include <linux/hwmon.h>
#include <net/page_pool/helpers.h>

#include <IOKit/IOLib.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IODMACommand.h>
#include <kern/thread_call.h>
#include <kern/clock.h>
#include <libkern/c++/OSData.h>

#include "FwData.h"

/* ------------------------------------------------------------------ */
/* globals                                                            */
/* ------------------------------------------------------------------ */
volatile unsigned long jiffies_global = 0;

static lck_grp_t *g_lck_grp;

lck_grp_t *mtk_lck_grp(void)
{
    if (!g_lck_grp)
        g_lck_grp = lck_grp_alloc_init("mt76wm", LCK_GRP_ATTR_NULL);
    return g_lck_grp;
}

static void mtk_tick(unsigned long ms)
{
    jiffies_global += ms;
}

/*
 * Advance the jiffies counter (HZ == 1000, == ms) from real time.  The
 * counter feeds the mt76 time_after()/TX-status timeout logic; ticking it
 * from the work/timer trampolines keeps it monotonic without a dedicated
 * clock thread.
 */
void mtk_advance_jiffies(void)
{
    static uint64_t last_uptime;
    uint64_t now = 0, ns = 0;

    clock_get_uptime(&now);
    if (last_uptime != 0 && now > last_uptime) {
        absolutetime_to_nanoseconds(now - last_uptime, &ns);
        mtk_tick(ns / 1000000ULL);
    }
    last_uptime = now;
}

/* ------------------------------------------------------------------ */
/* idr                                                                */
/* ------------------------------------------------------------------ */
void idr_init(struct idr *idr)
{
    idr->slots = NULL;
    idr->max = 0;
}

void idr_destroy(struct idr *idr)
{
    if (idr->slots) {
        IOFree(idr->slots, sizeof(void *) * idr->max);
        idr->slots = NULL;
    }
    idr->max = 0;
}

int idr_alloc(struct idr *idr, void *ptr, int start, int end, gfp_t gfp)
{
    int i;

    (void)gfp;
    if (end <= 0)
        end = 0x7fffffff;
    if (end > idr->max) {
        void **n = (void **)IOMalloc(sizeof(void *) * end);
        if (!n)
            return -ENOMEM;
        bzero(n, sizeof(void *) * end);
        if (idr->slots) {
            memcpy(n, idr->slots, sizeof(void *) * idr->max);
            IOFree(idr->slots, sizeof(void *) * idr->max);
        }
        idr->slots = n;
        idr->max = end;
    }
    for (i = start; i < end; i++) {
        if (!idr->slots[i]) {
            idr->slots[i] = ptr;
            return i;
        }
    }
    return -ENOSPC;
}

mtk_any_ptr idr_remove(struct idr *idr, int id)
{
    void *p = NULL;

    if (id >= 0 && id < idr->max && idr->slots) {
        p = idr->slots[id];
        idr->slots[id] = NULL;
    }
    return mtk_any_ptr(p);
}

mtk_any_ptr idr_find(struct idr *idr, int id)
{
    if (id >= 0 && id < idr->max && idr->slots)
        return mtk_any_ptr(idr->slots[id]);
    return mtk_any_ptr(NULL);
}

int idr_is_empty(struct idr *idr)
{
    int i;

    for (i = 0; i < idr->max; i++)
        if (idr->slots && idr->slots[i])
            return 0;
    return 1;
}

/* ------------------------------------------------------------------ */
/* RCU                                                                */
/* ------------------------------------------------------------------ */
void mtk_call_rcu(struct rcu_head *head, void (*func)(struct rcu_head *))
{
    /* single-CPU approximation: call immediately (no grace period).
     * TODO(M2): proper deferred freeing. */
    if (func)
        func(head);
}

void mtk_kfree_rcu(void *ptr, struct rcu_head *head)
{
    (void)head;
    if (ptr)
        kfree(ptr);
}

/* ------------------------------------------------------------------ */
/* thread_call lifetime tracking (thread_call_cancel_wait emulation)   */
/*                                                                      */
/* XNU does not export thread_call_cancel_wait() to kexts:              */
/* config/Mach.exports only lists thread_call_allocate / cancel /       */
/* enter / enter_delayed / free, so calling it fails to link with        */
/* "could not find a kext which exports this symbol".  Glue-managed     */
/* thread_calls therefore track their queued+executing invocations in   */
/* tc_refs and mtk_tc_cancel_wait() emulates the missing call: cancel   */
/* a pending entry (dropping its ref) and wait for a running handler    */
/* to finish.  A ref is taken BEFORE thread_call_enter*() so a call     */
/* that is about to run is never observed as idle.                      */
/*                                                                      */
/* Polarity note (2026-11): thread_call_enter()/thread_call_enter_      */
/* delayed() return TRUE when the call was ALREADY pending and FALSE    */
/* when it was newly queued (Kernel.framework kern/thread_call.h).      */
/* The old code read the result the other way round: every re-queue of  */
/* a still-pending call leaked one ref (later mtk_tc_cancel_wait() spun */
/* forever -> unload hung with "kext is in use or retained"), while     */
/* each fresh queue dropped the ref of a call that was about to run     */
/* (cancel_*_sync then returned while a handler was still executing).   */
/* A ref is now held only for a newly queued invocation.                */
/* ------------------------------------------------------------------ */
static inline void mtk_tc_queued(volatile int *refs)
{
    __sync_add_and_fetch(refs, 1);
}

static inline void mtk_tc_finished(volatile int *refs)
{
    __sync_sub_and_fetch(refs, 1);
}

static bool mtk_tc_enter(thread_call_t call, volatile int *refs)
{
    mtk_tc_queued(refs);
    if (thread_call_enter(call)) {
        /* Already pending: that invocation owns the ref taken above,
         * but it will only be dropped once.  Undo ours. */
        mtk_tc_finished(refs);
        return false;
    }
    /* Newly queued: this entry holds our ref until the trampoline
     * finishes with it. */
    return true;
}

static bool mtk_tc_enter_delayed(thread_call_t call, uint64_t deadline,
                                 volatile int *refs)
{
    mtk_tc_queued(refs);
    if (thread_call_enter_delayed(call, deadline)) {
        /* Already pending (deadline updated in place): the existing
         * entry holds the ref.  Undo ours. */
        mtk_tc_finished(refs);
        return false;
    }
    return true;
}

static bool mtk_tc_cancel(thread_call_t call, volatile int *refs)
{
    if (!thread_call_cancel(call))
        return false;
    mtk_tc_finished(refs);      /* pending entry removed before it ran */
    return true;
}

/*
 * Bounded cancel-and-wait.  A wedged handler must never pin the caller
 * (teardown paths rely on this returning), so give up after
 * MTK_TC_CANCEL_WAIT_MS and report failure instead of spinning forever.
 * The first cancel drops a pending entry's ref; a handler that is
 * already running drops its own ref when it returns.  Re-check in case
 * the handler re-armed the call before finishing.
 */
#define MTK_TC_CANCEL_WAIT_MS   30000

static bool mtk_tc_cancel_wait(thread_call_t call, volatile int *refs)
{
    int i;

    mtk_tc_cancel(call, refs);
    for (i = 0; i < MTK_TC_CANCEL_WAIT_MS; i++) {
        if (__sync_add_and_fetch(refs, 0) <= 0)
            return true;
        mtk_tc_cancel(call, refs);   /* handler may have re-queued */
        IOSleep(1);
    }
    XYLog("mt76: thread_call cancel_wait timed out (refs %d)\n",
          __sync_add_and_fetch(refs, 0));
    return false;
}

/* ------------------------------------------------------------------ */
/* teardown drain: once begun, work/timers/tasklets refuse re-arming   */
/* so a "cancel_*_sync" on the unload path can never race with a       */
/* self-(re)scheduling callback and leave a live thread_call behind     */
/* ("取消后回调不再入队" guarantee).                                    */
/* ------------------------------------------------------------------ */
static volatile int g_mtk_draining;

void mtk_quiesce_work(void)
{
    g_mtk_draining = 1;
}

/* Re-arm after a detach/attach cycle in the same kext load. */
void mtk_quiesce_end(void)
{
    g_mtk_draining = 0;
}

static inline bool mtk_draining(void)
{
    return g_mtk_draining != 0;
}

/* ------------------------------------------------------------------ */
/* workqueues (thread_call based)                                     */
/* ------------------------------------------------------------------ */
static void mtk_work_trampoline(thread_call_param_t owner,
                                thread_call_param_t param)
{
    struct work_struct *w = (struct work_struct *)owner;

    (void)param;
    mtk_advance_jiffies();
    w->state = 0;
    if (w->func && !w->dead)
        w->func(w);
    mtk_tc_finished(&w->tc_refs);
}

void mtk_init_work(struct work_struct *w, work_func_t f)
{
    w->func = f;
    w->state = 0;
    w->dead = 0;
    w->tc_refs = 0;
    w->call = thread_call_allocate(mtk_work_trampoline, w);
}

void mtk_destroy_work(struct work_struct *w)
{
    if (w->call) {
        w->dead = 1;
        if (mtk_tc_cancel_wait(w->call, &w->tc_refs))
            thread_call_free(w->call);
        else
            XYLog("mt76: destroy_work %p: handler busy, leaking call\n", w);
        w->call = NULL;
    }
}

static void mtk_delayed_trampoline(thread_call_param_t owner,
                                   thread_call_param_t param)
{
    struct delayed_work *d = (struct delayed_work *)owner;

    (void)param;
    if (d->work.call && !d->work.dead)
        mtk_tc_enter(d->work.call, &d->work.tc_refs);
    mtk_tc_finished(&d->timer_refs);
}

void mtk_init_delayed_work(struct delayed_work *w, work_func_t f)
{
    mtk_init_work(&w->work, f);
    w->delay = 0;
    w->timer_refs = 0;
    w->timer = thread_call_allocate(mtk_delayed_trampoline, w);
}

void mtk_destroy_delayed_work(struct delayed_work *w)
{
    w->work.dead = 1;
    if (w->timer) {
        if (mtk_tc_cancel_wait(w->timer, &w->timer_refs))
            thread_call_free(w->timer);
        else
            XYLog("mt76: destroy_delayed_work %p: timer busy, leaking call\n", w);
        w->timer = NULL;
    }
    mtk_destroy_work(&w->work);
}

bool mtk_queue_work(struct work_struct *w)
{
    if (!w->call || w->dead || mtk_draining())
        return false;
    return mtk_tc_enter(w->call, &w->tc_refs);
}

bool mtk_queue_delayed_work(struct delayed_work *w, unsigned long delay)
{
    uint64_t deadline = 0;

    if (!w->timer || w->work.dead || mtk_draining())
        return false;
    if (!delay)
        return mtk_queue_work(&w->work);
    clock_absolutetime_interval_to_deadline(
        (uint64_t)delay * 1000000ULL, &deadline);
    return mtk_tc_enter_delayed(w->timer, deadline, &w->timer_refs);
}

bool mtk_cancel_work(struct work_struct *w)
{
    if (!w->call)
        return false;
    return mtk_tc_cancel(w->call, &w->tc_refs);
}

void mtk_flush_work(struct work_struct *w)
{
    /* wait for a running handler to finish */
    if (w->call)
        mtk_tc_cancel_wait(w->call, &w->tc_refs);
}

void mtk_cancel_delayed_work_sync(struct delayed_work *w)
{
    if (w->timer)
        mtk_tc_cancel_wait(w->timer, &w->timer_refs);
    mtk_flush_work(&w->work);
}

/* ------------------------------------------------------------------ */
/* timers (thread_call based)                                         */
/* ------------------------------------------------------------------ */
static void mtk_timer_trampoline(thread_call_param_t owner,
                                 thread_call_param_t param)
{
    struct timer_list *t = (struct timer_list *)owner;

    (void)param;
    mtk_advance_jiffies();
    t->pending = false;
    if (t->function && !t->dead)
        t->function(t);
    mtk_tc_finished(&t->tc_refs);
}

void mtk_timer_setup(struct timer_list *t, void (*fn)(struct timer_list *),
                     unsigned int flags)
{
    (void)flags;
    t->function = fn;
    t->expires = 0;
    t->pending = false;
    t->dead = 0;
    t->tc_refs = 0;
    t->call = thread_call_allocate(mtk_timer_trampoline, t);
}

bool mtk_mod_timer(struct timer_list *t, unsigned long expires)
{
    uint64_t deadline = 0;
    long delay = (long)expires - (long)jiffies_global;

    if (!t->call || t->dead || mtk_draining())
        return false;
    if (delay < 0)
        delay = 0;
    clock_absolutetime_interval_to_deadline((uint64_t)delay * 1000000ULL,
                                           &deadline);
    t->expires = expires;
    t->pending = true;
    return mtk_tc_enter_delayed(t->call, deadline, &t->tc_refs);
}

int mtk_del_timer_sync(struct timer_list *t)
{
    bool was = t->pending;

    if (t->call) {
        mtk_tc_cancel_wait(t->call, &t->tc_refs);
        t->pending = false;
    }
    return was ? 1 : 0;
}

int mtk_del_timer(struct timer_list *t)
{
    bool was = t->pending;

    if (t->call) {
        mtk_tc_cancel(t->call, &t->tc_refs);
        t->pending = false;
    }
    return was ? 1 : 0;
}

void mtk_timer_shutdown_sync(struct timer_list *t)
{
    if (t->call) {
        t->dead = 1;
        if (mtk_tc_cancel_wait(t->call, &t->tc_refs))
            thread_call_free(t->call);
        else
            XYLog("mt76: timer_shutdown %p: handler busy, leaking call\n", t);
        t->call = NULL;
        t->pending = false;
    }
}

/* ------------------------------------------------------------------ */
/* tasklets (thread_call based)                                       */
/* ------------------------------------------------------------------ */
static void mtk_tasklet_trampoline(thread_call_param_t owner,
                                   thread_call_param_t param)
{
    struct tasklet_struct *t = (struct tasklet_struct *)owner;

    (void)param;
    mtk_advance_jiffies();
    t->state = 0;
    if (t->func && !t->dead)
        t->func(t->data);
    mtk_tc_finished(&t->tc_refs);
}

void mtk_tasklet_init(struct tasklet_struct *t, void (*fn)(unsigned long),
                      unsigned long data)
{
    t->func = fn;
    t->data = data;
    t->state = 0;
    t->dead = 0;
    t->tc_refs = 0;
    t->call = thread_call_allocate(mtk_tasklet_trampoline, t);
}

void mtk_tasklet_destroy(struct tasklet_struct *t)
{
    if (t->call) {
        t->dead = 1;
        if (mtk_tc_cancel_wait(t->call, &t->tc_refs))
            thread_call_free(t->call);
        else
            XYLog("mt76: tasklet_destroy %p: handler busy, leaking call\n", t);
        t->call = NULL;
    }
}

void mtk_tasklet_schedule(struct tasklet_struct *t)
{
    if (t->call && !t->dead && !mtk_draining())
        mtk_tc_enter(t->call, &t->tc_refs);
}

/* ------------------------------------------------------------------ */
/* kernel threads (thread_call based)                                 */
/* ------------------------------------------------------------------ */
static struct task_struct *g_current_task;   /* single worker at a time:
                                              * TODO(M2) per-thread state */

static void mtk_kthread_trampoline(thread_call_param_t owner,
                                   thread_call_param_t param)
{
    struct task_struct *t = (struct task_struct *)owner;

    (void)param;
    g_current_task = t;
    t->fn(t->arg);
    t->finished = 1;
    g_current_task = NULL;
    mtk_tc_finished(&t->tc_refs);
}

struct task_struct *mtk_kthread_run(int (*fn)(void *), void *data,
                                    const char *name)
{
    struct task_struct *t = (struct task_struct *)kzalloc(sizeof(*t), 0);

    if (!t)
        return NULL;
    t->fn = fn;
    t->arg = data;
    strscpy(t->name, name);
    t->call = thread_call_allocate(mtk_kthread_trampoline, t);
    if (!t->call) {
        kfree(t);
        return NULL;
    }
    mtk_tc_enter(t->call, &t->tc_refs);
    return t;
}

int mtk_kthread_stop(struct task_struct *t)
{
    if (!t)
        return 0;
    t->should_stop = 1;
    /* give the loop a chance to exit (it polls) */
    for (int i = 0; i < 100 && !t->finished; i++)
        IOSleep(10);
    if (t->call) {
        if (mtk_tc_cancel_wait(t->call, &t->tc_refs))
            thread_call_free(t->call);
        else
            XYLog("mt76: kthread_stop %s: still busy, leaking call\n", t->name);
    }
    kfree(t);
    return 0;
}

void mtk_kthread_park(struct task_struct *t)
{
    if (t)
        t->should_park = 1;
}

void mtk_kthread_unpark(struct task_struct *t)
{
    if (t)
        t->should_park = 0;
}

int mtk_kthread_should_stop(void)
{
    return g_current_task ? g_current_task->should_stop : 1;
}

int mtk_kthread_should_park(void)
{
    return g_current_task ? g_current_task->should_park : 0;
}

void mtk_kthread_parkme(void)
{
    while (g_current_task && g_current_task->should_park &&
           !g_current_task->should_stop)
        IOSleep(1);
}

void wake_up_process(struct task_struct *t)
{
    (void)t;    /* polling workers */
}

void mtk_set_task_state(int state)
{
    if (g_current_task)
        g_current_task->state = state;
}

void schedule(void)
{
    /* worker idle poll (TODO(M2): real wait/wake) */
    IOSleep(1);
}

/* ------------------------------------------------------------------ */
/* NAPI                                                               */
/* ------------------------------------------------------------------ */
static struct napi_struct *g_napi_list;
static lck_mtx_t *g_napi_lock;

static void mtk_napi_lock(void)
{
    if (!g_napi_lock)
        g_napi_lock = lck_mtx_alloc_init(mtk_lck_grp(), LCK_ATTR_NULL);
    if (g_napi_lock)
        lck_mtx_lock(g_napi_lock);
}

static void mtk_napi_unlock(void)
{
    if (g_napi_lock)
        lck_mtx_unlock(g_napi_lock);
}

void netif_napi_add(struct net_device *dev, struct napi_struct *napi,
                    int (*poll)(struct napi_struct *, int))
{
    napi->dev = dev;
    napi->poll = poll;
    napi->weight = 64;
    napi->state = 0;
    napi->call = NULL;

    mtk_napi_lock();
    napi->next = g_napi_list;
    g_napi_list = napi;
    napi->on_list = true;
    mtk_napi_unlock();
}

void netif_napi_add_tx(struct net_device *dev, struct napi_struct *napi,
                       int (*poll)(struct napi_struct *, int))
{
    netif_napi_add(dev, napi, poll);
}

void netif_napi_del(struct napi_struct *napi)
{
    struct napi_struct **pp;

    mtk_napi_lock();
    for (pp = &g_napi_list; *pp; pp = &(*pp)->next) {
        if (*pp == napi) {
            *pp = napi->next;
            napi->on_list = false;
            break;
        }
    }
    mtk_napi_unlock();
}

void napi_enable(struct napi_struct *napi)
{
    clear_bit(NAPI_STATE_DISABLE, &napi->state);
}

void napi_disable(struct napi_struct *napi)
{
    set_bit(NAPI_STATE_DISABLE, &napi->state);
}

void napi_schedule(struct napi_struct *napi)
{
    if (test_bit(NAPI_STATE_DISABLE, &napi->state))
        return;
    if (!test_and_set_bit(NAPI_STATE_SCHED, &napi->state)) {
        /* run one polling round immediately (called from the glue
         * bottom half / thread context).  TODO(M2): budgeting. */
        int work = 0;

        if (napi->poll)
            work = napi->poll(napi, napi->weight);
        if (work < napi->weight)
            napi_complete(napi);
    }
}

bool napi_schedule_prep(struct napi_struct *napi)
{
    return !test_and_set_bit(NAPI_STATE_SCHED, &napi->state);
}

int napi_complete(struct napi_struct *napi)
{
    clear_bit(NAPI_STATE_SCHED, &napi->state);
    return 1;
}

int napi_complete_done(struct napi_struct *napi, int work_done)
{
    (void)work_done;
    return napi_complete(napi);
}

void mtk_napi_process(void)
{
    struct napi_struct *n;
    struct napi_struct *copy[64];
    int n_copy = 0, i;

    mtk_napi_lock();
    for (n = g_napi_list; n && n_copy < 64; n = n->next)
        copy[n_copy++] = n;
    mtk_napi_unlock();

    for (i = 0; i < n_copy; i++) {
        n = copy[i];
        if (test_bit(NAPI_STATE_SCHED, &n->state) && n->poll) {
            int work = n->poll(n, n->weight);

            if (work < n->weight)
                napi_complete(n);
        }
    }
}

/* ------------------------------------------------------------------ */
/* net_device                                                         */
/* ------------------------------------------------------------------ */
struct net_device *alloc_netdev_dummy(int sizeof_priv)
{
    size_t size = sizeof(struct net_device) +
                  ALIGN(sizeof_priv, NETDEV_ALIGN) + NETDEV_ALIGN;
    struct net_device *dev = (struct net_device *)kzalloc(size, 0);

    return dev;
}

void free_netdev(struct net_device *dev)
{
    if (dev)
        kfree(dev);
}

/* ------------------------------------------------------------------ */
/* skb                                                                */
/* ------------------------------------------------------------------ */
void mtk_skb_release(struct sk_buff *skb)
{
    struct skb_shared_info *sh;
    int i;

    if (!skb)
        return;
    /*
     * M5 TX completion choke point: every TX'd skb carries a net80211
     * cookie (mbuf + node ref).  Whatever path frees the skb (TXS status,
     * DMA/token cleanup, error path, flush), report the completion to the
     * net80211 world exactly once.
     */
    if (skb->mtk_ck.mdev || skb->mtk_ck.m || skb->mtk_ck.ni) {
        struct mtk_tx_cookie ck = skb->mtk_ck;
        unsigned int status = skb->mtk_tx_status;

        skb->mtk_ck.mdev = NULL;
        skb->mtk_ck.m = NULL;
        skb->mtk_ck.ni = NULL;
        mtk_net80211_tx_done(&ck, status);
    }

    /*
     * M6: head_frag skbs are built by napi_build_skb() over a page-pool
     * RX buffer; their frags (multi-buffer RX) hold pool pages as well.
     * Recycle those back to the pool instead of leaking them.  M7:
     * alloc_skb() heads are linear DMA blocks (mtk_skb_alloc_data) and
     * return to the TX buffer cache.
     */
    sh = skb_shinfo(skb);
    for (i = 0; i < sh->nr_frags && i < MAX_SKB_FRAGS; i++)
        mtk_page_pool_recycle_page((struct page *)sh->frags[i].page);
    if (skb->head_frag)
        mtk_page_pool_recycle_buf(skb->head);
    else if (skb->head)
        mtk_skb_free_data(skb->head);
    kfree(skb);
}

/* ------------------------------------------------------------------ */
/* DMA (IOBufferMemoryDescriptor backed, physically contiguous)       */
/* Mirrors itlwm's allocDmaMemory2 pattern (hal_iwn/ItlIwn.cpp).      */
/*                                                                      */
/* M7 (real IOVA for TX): every block allocated here is registered in  */
/* a dynamic VA->IOVA table keyed by page frame number, so              */
/* mtk_dma_map_single() can translate ANY interior pointer (e.g.       */
/* skb->data after reserve/push) into the true bus address:            */
/*                                                                      */
/*     dma = block->dma + (vaddr - block->va)                          */
/*                                                                      */
/* The table is a chained hash (unbounded: "按需扩容"), NOT the old     */
/* fixed 512-slot array, so txwi cache blocks and TX skb heads (up to  */
/* token_size = 8192 live blocks) never overflow it.  RX page-pool     */
/* blocks stay OUT of this table (they are self-describing via their   */
/* embedded struct page header and are never passed to map_single).    */
/* ------------------------------------------------------------------ */
#define MTK_DMA_HASH_BITS   11
#define MTK_DMA_HASH_SIZE   (1U << MTK_DMA_HASH_BITS)

/* one hash node per page covered by a block (blocks span 1..n pages) */
struct mtk_dma_pg {
    struct mtk_dma_pg *next;
    unsigned long pfn;
    struct mtk_dma_block *blk;
};

struct mtk_dma_block {
    void *va;                       /* payload base (block base)        */
    size_t size;                    /* usable size of the block         */
    dma_addr_t dma;                 /* IOVA of va                       */
    IOBufferMemoryDescriptor *bmd;
    IODMACommand *cmd;
    struct mtk_dma_block *cache_next; /* mtk_skb_alloc_data freelist    */
    unsigned int npages;
    struct mtk_dma_pg *pgs;         /* one node per covered page        */
};

static struct mtk_dma_pg *g_dma_hash[MTK_DMA_HASH_SIZE];
static lck_spin_t *g_dma_lock;

static void mtk_dma_guard(void)
{
    if (!g_dma_lock)
        g_dma_lock = lck_spin_alloc_init(mtk_lck_grp(), LCK_ATTR_NULL);
    if (g_dma_lock)
        lck_spin_lock(g_dma_lock);
}

static void mtk_dma_unguard(void)
{
    if (g_dma_lock)
        lck_spin_unlock(g_dma_lock);
}

static inline unsigned int mtk_dma_pfn_hash(unsigned long pfn)
{
    return (unsigned int)((pfn * 2654435761u) >> (32 - MTK_DMA_HASH_BITS)) &
           (MTK_DMA_HASH_SIZE - 1);
}

/* Link blk under every page it covers.  Pure pointer work: the node
 * array ships inside the block allocation, so nothing can block here.
 * Called under mtk_dma_guard(). */
static void mtk_dma_block_register(struct mtk_dma_block *blk)
{
    unsigned long first = (unsigned long)blk->va >> PAGE_SHIFT;
    unsigned long pfn;
    unsigned int i;

    for (pfn = first, i = 0; i < blk->npages; pfn++, i++) {
        unsigned int h = mtk_dma_pfn_hash(pfn);
        struct mtk_dma_pg *n = &blk->pgs[i];

        n->pfn = pfn;
        n->blk = blk;
        n->next = g_dma_hash[h];
        g_dma_hash[h] = n;
    }
}

/* Unlink blk's page nodes.  Called under mtk_dma_guard(). */
static void mtk_dma_block_unregister(struct mtk_dma_block *blk)
{
    unsigned int i;

    for (i = 0; i < blk->npages; i++) {
        struct mtk_dma_pg *n = &blk->pgs[i];
        unsigned int h = mtk_dma_pfn_hash(n->pfn);
        struct mtk_dma_pg **pp = &g_dma_hash[h];

        while (*pp) {
            if (*pp == n) {
                *pp = n->next;
                break;
            }
            pp = &(*pp)->next;
        }
    }
}

/* Find the block containing vaddr.  Called under mtk_dma_guard(). */
static struct mtk_dma_block *mtk_dma_block_lookup(void *vaddr)
{
    unsigned long pfn = (unsigned long)vaddr >> PAGE_SHIFT;
    unsigned int h = mtk_dma_pfn_hash(pfn);
    struct mtk_dma_pg *n;

    for (n = g_dma_hash[h]; n; n = n->next) {
        struct mtk_dma_block *b = n->blk;

        if (n->pfn == pfn &&
            (u8 *)vaddr >= (u8 *)b->va &&
            (u8 *)vaddr < (u8 *)b->va + b->size)
            return b;
    }
    return NULL;
}

static void mtk_dma_block_free(struct mtk_dma_block *blk)
{
    IODMACommand *cmd = blk->cmd;
    IOBufferMemoryDescriptor *bmd = blk->bmd;
    size_t recsz = sizeof(*blk) + blk->npages * sizeof(struct mtk_dma_pg);

    if (cmd) {
        cmd->clearMemoryDescriptor();
        cmd->release();
    }
    if (bmd) {
        bmd->complete();
        bmd->release();
    }
    IOFree(blk, recsz);
}

mtk_any_ptr mtk_dma_alloc_coherent(size_t size, dma_addr_t *dma_handle,
                                   gfp_t gfp)
{
    IOBufferMemoryDescriptor *bmd;
    IODMACommand *cmd;
    IODMACommand::Segment64 seg;
    UInt64 ofs = 0;
    UInt32 numSegs = 1;
    struct mtk_dma_block *blk;
    void *va;

    (void)gfp;
    if (!size)
        size = 1;
    /* DMA_BIT_MASK(32): the MT7921 WPDMA descriptors and the connac2
     * hardware TXP pointers are 32-bit (mt7921_pci_probe() also claims a
     * 32-bit dma mask), so every block must live below 4 GB or the
     * stored address would be silently truncated. */
    bmd = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
        kernel_task,
        kIODirectionInOut | kIOMemoryPhysicallyContiguous | kIOMapInhibitCache,
        size, DMA_BIT_MASK(32));
    if (!bmd) {
        XYLog("mt76: DMA alloc failed (size %u)\n", (unsigned)size);
        return mtk_any_ptr(NULL);
    }
    bmd->prepare();
    cmd = IODMACommand::withSpecification(kIODMACommandOutputHost64, 64, 0,
                                          IODMACommand::kMapped, 0, 64);
    if (!cmd) {
        bmd->complete();
        bmd->release();
        return mtk_any_ptr(NULL);
    }
    cmd->setMemoryDescriptor(bmd);
    if (cmd->gen64IOVMSegments(&ofs, &seg, &numSegs) != kIOReturnSuccess) {
        cmd->clearMemoryDescriptor();
        cmd->release();
        bmd->complete();
        bmd->release();
        return mtk_any_ptr(NULL);
    }
    va = bmd->getBytesNoCopy();
    bzero(va, size);

    /* One allocation for the bookkeeping record plus one hash node per
     * page the block covers, so the registry never needs to allocate
     * while the spinlock is held. */
    {
        unsigned long first = (unsigned long)va >> PAGE_SHIFT;
        unsigned long last = ((unsigned long)va + size - 1) >> PAGE_SHIFT;
        unsigned int npages = (unsigned int)(last - first + 1);
        size_t recsz = sizeof(*blk) + npages * sizeof(struct mtk_dma_pg);

        blk = (struct mtk_dma_block *)IOMalloc(recsz);
        if (!blk) {
            cmd->clearMemoryDescriptor();
            cmd->release();
            bmd->complete();
            bmd->release();
            return mtk_any_ptr(NULL);
        }
        blk->va = va;
        blk->size = size;
        blk->dma = (dma_addr_t)seg.fIOVMAddr;
        blk->bmd = bmd;
        blk->cmd = cmd;
        blk->cache_next = NULL;
        blk->npages = npages;
        blk->pgs = (struct mtk_dma_pg *)(blk + 1);
    }

    mtk_dma_guard();
    mtk_dma_block_register(blk);
    mtk_dma_unguard();

    if (dma_handle)
        *dma_handle = blk->dma;
    return mtk_any_ptr(va);
}

void mtk_dma_free_coherent(void *vaddr, size_t size, dma_addr_t dma_handle)
{
    (void)size; (void)dma_handle;
    if (!vaddr)
        return;
    mtk_dma_guard();
    struct mtk_dma_block *blk = mtk_dma_block_lookup(vaddr);

    if (blk && blk->va == vaddr) {
        mtk_dma_block_unregister(blk);
        mtk_dma_unguard();
        mtk_dma_block_free(blk);
        return;
    }
    mtk_dma_unguard();
    XYLog("mt76: dma_free_coherent on unknown block %p\n", vaddr);
}

dma_addr_t mtk_dma_map_single(void *vaddr, size_t size,
                              enum dma_data_direction dir)
{
    struct mtk_dma_block *blk;
    dma_addr_t dma = 0;

    (void)dir;
    if (!vaddr || !size)
        return 0;

    /* Registered DMA blocks (TX skb heads, txwi cache, rings): return the
     * true IOVA of the mapped range.  Interior pointers (skb->data after
     * reserve/push) translate via the block base. */
    mtk_dma_guard();
    blk = mtk_dma_block_lookup(vaddr);
    if (blk) {
        if ((u8 *)vaddr + size <= (u8 *)blk->va + blk->size)
            dma = blk->dma + (dma_addr_t)((u8 *)vaddr - (u8 *)blk->va);
        else
            XYLog("mt76: map_single range %p+%u escapes block\n",
                  vaddr, (unsigned)size);
    }
    mtk_dma_unguard();

    if (!dma)
        XYLog("mt76: map_single on unregistered buffer %p (%u bytes)\n",
              vaddr, (unsigned)size);
    return dma;
}

void mtk_dma_unmap_single(dma_addr_t dma_addr, size_t size,
                          enum dma_data_direction dir)
{
    /* All glue buffers are allocated in IOBufferMemoryDescriptor blocks
     * that stay mapped for their whole lifetime (kIOMapInhibitCache
     * coherent memory), so translation-only unmap is a no-op.  Blocks
     * are released by their owner (skb free / txwi cache free / pool
     * recycle), never here. */
    (void)dma_addr; (void)size; (void)dir;
}

/* ------------------------------------------------------------------ */
/* TX skb data buffers (M7)                                            */
/*                                                                      */
/* alloc_skb() heads must live in DMA-reachable memory so that          */
/* mt76_dma_tx_queue_skb()/_raw() can hand WPDMA a real IOVA.  Each    */
/* head is one registered mtk_dma_block (see above), so                */
/* mtk_dma_map_single(skb->data, ...) translates correctly even for    */
/* interior pointers (after skb_reserve()/skb_push()).                  */
/*                                                                      */
/* Freed heads are recycled through a bounded per-size-class cache      */
/* (this is the TX-side "pool"; every recycled buffer is counted in     */
/* g_skb_cache_recycled and every eviction in g_skb_cache_evicted).     */
/* Blocks handed out from the cache stay registered, so map translation */
/* keeps working across reuse.                                          */
/* ------------------------------------------------------------------ */
#define MTK_SKB_CLASS_SHIFT 8                       /* 256 B classes    */
#define MTK_SKB_CLASS_SIZE  (1U << MTK_SKB_CLASS_SHIFT)
#define MTK_SKB_CLASS_MAX   32                      /* up to 8 KB       */
#define MTK_SKB_CACHE_MAX   128                     /* per class        */

static struct mtk_dma_block *g_skb_cache[MTK_SKB_CLASS_MAX + 1];
static unsigned int g_skb_cache_n[MTK_SKB_CLASS_MAX + 1];
static bool g_skb_cache_dead;      /* teardown: stop recycling          */
static unsigned int g_skb_cache_hits;      /* served from cache         */
static unsigned int g_skb_cache_misses;    /* fresh blocks allocated    */
static unsigned int g_skb_cache_recycled;  /* returned into the cache   */
static unsigned int g_skb_cache_evicted;   /* cache full -> really freed*/

static inline unsigned int mtk_skb_class_of(unsigned int bytes)
{
    unsigned int cls = (bytes + MTK_SKB_CLASS_SIZE - 1) >> MTK_SKB_CLASS_SHIFT;

    if (!cls)
        cls = 1;
    if (cls > MTK_SKB_CLASS_MAX)
        cls = MTK_SKB_CLASS_MAX;
    return cls;
}

/* Only exact class-sized blocks are cacheable; oversized allocations are
 * allocated at need and freed for real. */
static bool mtk_skb_class_fit(unsigned int size, unsigned int *cls_out)
{
    unsigned int cls = (size + MTK_SKB_CLASS_SIZE - 1) >> MTK_SKB_CLASS_SHIFT;

    if (!cls)
        cls = 1;
    if (cls > MTK_SKB_CLASS_MAX || cls * MTK_SKB_CLASS_SIZE != size)
        return false;
    *cls_out = cls;
    return true;
}

void *mtk_skb_alloc_data(unsigned int size)
{
    struct mtk_dma_block *blk;
    unsigned int cls = mtk_skb_class_of(size);
    unsigned int want = cls * MTK_SKB_CLASS_SIZE;

    if (want < size)
        want = size;            /* request above the cacheable range */

    mtk_dma_guard();
    blk = (!g_skb_cache_dead && want == cls * MTK_SKB_CLASS_SIZE) ?
          g_skb_cache[cls] : NULL;
    if (blk) {
        g_skb_cache[cls] = blk->cache_next;
        blk->cache_next = NULL;
        g_skb_cache_n[cls]--;
        g_skb_cache_hits++;
        mtk_dma_unguard();
        /* kzalloc() semantics for the reused block */
        bzero(blk->va, want);
        return blk->va;
    }
    g_skb_cache_misses++;
    mtk_dma_unguard();

    /* Fresh block: same allocator as dma_alloc_coherent (registered,
     * zeroed).  mtk_dma_alloc_coherent() returns the payload VA; the
     * block record is reachable again via mtk_dma_block_lookup(). */
    return mtk_dma_alloc_coherent(want, NULL, 0).get();
}

void mtk_skb_free_data(void *head)
{
    struct mtk_dma_block *blk;
    unsigned int cls;

    if (!head)
        return;

    mtk_dma_guard();
    blk = mtk_dma_block_lookup(head);
    if (blk && blk->va == head && !g_skb_cache_dead &&
        mtk_skb_class_fit((unsigned int)blk->size, &cls) &&
        g_skb_cache_n[cls] < MTK_SKB_CACHE_MAX) {
        blk->cache_next = g_skb_cache[cls];
        g_skb_cache[cls] = blk;
        g_skb_cache_n[cls]++;
        g_skb_cache_recycled++;
        mtk_dma_unguard();
        return;
    }
    if (blk && blk->va == head) {
        mtk_dma_block_unregister(blk);
        mtk_dma_unguard();
        mtk_dma_block_free(blk);
        g_skb_cache_evicted++;
        return;
    }
    mtk_dma_unguard();

    /* Foreign head (not from mtk_skb_alloc_data): keep the historical
     * kfree() behaviour so nothing leaks, but make the anomaly loud. */
    XYLog("mt76: skb data free on unknown block %p (kfree fallback)\n",
          head);
    kfree(head);
}

/* Drop every cached TX buffer (device teardown).  Blocks still owned by
 * in-flight skbs come back through mtk_skb_free_data() later and are
 * freed for real because the cache stops accepting returns. */
void mtk_skb_data_cache_purge(void)
{
    struct mtk_dma_block *dead = NULL, *blk, *next;
    unsigned int cls, kept = 0, freed = 0;

    /* Splice every cache out under the lock, release the DMA blocks
     * (blocking IOKit calls) outside of it. */
    mtk_dma_guard();
    g_skb_cache_dead = true;
    for (cls = 1; cls <= MTK_SKB_CLASS_MAX; cls++) {
        for (blk = g_skb_cache[cls]; blk; blk = next) {
            next = blk->cache_next;
            mtk_dma_block_unregister(blk);
            blk->cache_next = dead;
            dead = blk;
        }
        g_skb_cache[cls] = NULL;
        kept += g_skb_cache_n[cls];
        g_skb_cache_n[cls] = 0;
    }
    mtk_dma_unguard();

    for (blk = dead; blk; blk = next) {
        next = blk->cache_next;
        mtk_dma_block_free(blk);
        freed++;
    }

    XYLog("mt76: TX buf cache: %u alloc (%u hits / %u misses), "
          "%u recycled, %u evicted, %u purged (%u accounted)\n",
          g_skb_cache_hits + g_skb_cache_misses, g_skb_cache_hits,
          g_skb_cache_misses, g_skb_cache_recycled, g_skb_cache_evicted,
          freed, kept);
}

/* ------------------------------------------------------------------ */
/* page pool: linear DMA-capable RX buffers                            */
/*                                                                      */
/* M6 (RX ring fix): the mt76 RX fill path draws its buffers from a    */
/* page pool.  There is no Linux page allocator here, so each pool      */
/* buffer is a standalone physically contiguous DMA block              */
/* (IOBufferMemoryDescriptor + IODMACommand, see mtk_dma_alloc_        */
/* coherent above and hal_mt76/MtkDma.cpp).  Layout:                   */
/*                                                                      */
/*     [ struct page | pad | payload ... ]   (MTK_PP_HDR_SIZE bytes)   */
/*                                                                      */
/* virt_to_head_page() (net/page_pool/helpers.h) subtracts the header  */
/* size to recover the struct page, so no lookup table is needed.      */
/* Recycled buffers stay on a per-pool freelist (bounded by the ring   */
/* size); a dead pool frees every returned block immediately.          */
/* ------------------------------------------------------------------ */
struct page_pool {
    spinlock_t lock;
    struct page *freelist;
    unsigned int max_len;       /* payload size per buffer             */
    unsigned int pool_size;     /* freelist bound (== ring size)       */
    unsigned int nfree;
    unsigned int outstanding;   /* buffers held by the ring / skbs     */
    bool dead;                  /* destroy ran: free instead of reuse  */
};

static void mtk_page_pool_free_block(struct page *page)
{
    IODMACommand *cmd = (IODMACommand *)page->cmd;
    IOBufferMemoryDescriptor *bmd =
        (IOBufferMemoryDescriptor *)page->bmd;

    if (cmd) {
        cmd->clearMemoryDescriptor();
        cmd->release();
    }
    if (bmd) {
        bmd->complete();
        bmd->release();
    } else {
        /* coherent-backed block (mtk_dma_alloc_coherent) */
        mtk_dma_free_coherent(page, page->alloc_size, page->alloc_dma);
    }
}

static struct page *mtk_page_pool_alloc_new(struct page_pool *pp,
                                            unsigned int size);

struct page_pool *mtk_page_pool_create(unsigned int pool_size,
                                       unsigned int max_len)
{
    struct page_pool *pp;
    unsigned int i;

    if (!max_len)
        max_len = 2048;
    pp = (struct page_pool *)kzalloc(sizeof(*pp), 0);
    if (!pp)
        return NULL;
    spin_lock_init(&pp->lock);
    pp->max_len = max_len;
    pp->pool_size = pool_size ? pool_size : 1;

    /* Pre-seed the freelist here, in process context where blocking is
     * allowed.  Runtime refills (NAPI/tasklet, under queue spinlocks)
     * must never allocate -- they only pop from this freelist. */
    for (i = 0; i < pp->pool_size; i++) {
        struct page *pg = mtk_page_pool_alloc_new(pp, max_len);

        if (!pg)
            break;
        spin_lock(&pp->lock);
        pg->next = pp->freelist;
        pp->freelist = pg;
        pp->nfree++;
        pp->outstanding--;
        spin_unlock(&pp->lock);
    }
    return pp;
}

void mtk_page_pool_destroy(struct page_pool *pp)
{
    struct page *list, *p;
    bool last;

    if (!pp)
        return;

    spin_lock(&pp->lock);
    pp->dead = true;
    list = pp->freelist;
    pp->freelist = NULL;
    pp->nfree = 0;
    last = (pp->outstanding == 0);
    spin_unlock(&pp->lock);

    while (list) {
        p = list;
        list = p->next;
        mtk_page_pool_free_block(p);
    }
    if (last)
        kfree(pp);
    /* else: freed by mtk_page_pool_recycle_page() on the last return */
}

struct page *mtk_page_pool_alloc(struct page_pool *pp, unsigned int size)
{
    IOBufferMemoryDescriptor *bmd;
    IODMACommand *cmd;
    IODMACommand::Segment64 seg;
    UInt64 ofs = 0;
    UInt32 numSegs = 1;
    struct page *page;
    void *va;
    size_t total;

    if (!pp)
        return NULL;
    if (!size)
        size = pp->max_len;

    /* Recycle a previously used block when one is available. */
    spin_lock(&pp->lock);
    page = pp->freelist;
    if (page) {
        pp->freelist = page->next;
        page->next = NULL;
        pp->nfree--;
        pp->outstanding++;
        spin_unlock(&pp->lock);
        return page;
    }
    spin_unlock(&pp->lock);

    /* Atomic context (refill runs in NAPI/tasklet under queue spinlocks):
     * never allocate here -- the freelist was pre-seeded at pool creation.
     * Returning NULL simply pauses refill until buffers are recycled. */
    return NULL;
}

static struct page *mtk_page_pool_alloc_new(struct page_pool *pp,
                                            unsigned int size)
{
    IOBufferMemoryDescriptor *bmd;
    IODMACommand *cmd;
    IODMACommand::Segment64 seg;
    UInt64 ofs = 0;
    UInt32 numSegs = 1;
    struct page *page;
    void *va;
    size_t total;

    if (!size)
        size = pp->max_len;

    /* Fresh DMA block: [ struct page | payload ].  Same 32-bit
     * constraint as mtk_dma_alloc_coherent(): the WPDMA descriptor
     * address fields are 32-bit wide. */
    total = MTK_PP_HDR_SIZE + size;
    bmd = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,
            kIODirectionInOut | kIOMemoryPhysicallyContiguous |
            kIOMapInhibitCache, total, DMA_BIT_MASK(32));
    if (!bmd) {
        XYLog("mt76: page pool alloc failed (size %u)\n", (unsigned)size);
        return NULL;
    }
    if (bmd->prepare() != kIOReturnSuccess) {
        bmd->release();
        return NULL;
    }
    cmd = IODMACommand::withSpecification(kIODMACommandOutputHost64,
            64, 0, IODMACommand::kMapped, 0, 64);
    if (!cmd) {
        bmd->complete();
        bmd->release();
        return NULL;
    }
    if (cmd->setMemoryDescriptor(bmd) != kIOReturnSuccess ||
        cmd->gen64IOVMSegments(&ofs, &seg, &numSegs) != kIOReturnSuccess) {
        cmd->clearMemoryDescriptor();
        cmd->release();
        bmd->complete();
        bmd->release();
        return NULL;
    }

    va = bmd->getBytesNoCopy();
    page = (struct page *)va;
    page->virt = (u8 *)va + MTK_PP_HDR_SIZE;
    page->alloc_dma = (dma_addr_t)seg.fIOVMAddr;
    page->dma_addr = page->alloc_dma + MTK_PP_HDR_SIZE;
    page->bmd = bmd;
    page->cmd = cmd;
    page->alloc_size = total;
    page->next = NULL;
    page->pp = pp;

    spin_lock(&pp->lock);
    if (pp->dead) {
        spin_unlock(&pp->lock);
        mtk_page_pool_free_block(page);
        return NULL;
    }
    pp->outstanding++;
    spin_unlock(&pp->lock);
    return page;
}

void mtk_page_pool_recycle_page(struct page *page)
{
    struct page_pool *pp;
    bool last;

    if (!page)
        return;
    pp = page->pp;
    if (!pp) {
        mtk_page_pool_free_block(page);
        return;
    }

    spin_lock(&pp->lock);
    if (pp->outstanding > 0)
        pp->outstanding--;
    if (!pp->dead && pp->nfree < pp->pool_size) {
        page->next = pp->freelist;
        pp->freelist = page;
        pp->nfree++;
        spin_unlock(&pp->lock);
        return;
    }
    last = pp->dead && (pp->outstanding == 0);
    spin_unlock(&pp->lock);

    mtk_page_pool_free_block(page);
    if (last)
        kfree(pp);
}

void mtk_page_pool_recycle_buf(void *buf)
{
    if (!buf)
        return;
    mtk_page_pool_recycle_page(virt_to_head_page(buf));
}

/* ------------------------------------------------------------------ */
/* firmware (from the embedded, zlib-compressed fwList)               */
/* ------------------------------------------------------------------ */
static const char *mtk_basename(const char *name)
{
    const char *slash = name, *p;

    for (p = name; *p; p++)
        if (*p == '/')
            slash = p + 1;
    return slash;
}

int request_firmware(const struct firmware **fw, const char *name,
                     struct device *dev)
{
    OSData *data;
    struct firmware *f;
    const char *base = mtk_basename(name);
    unsigned char *out;
    uint out_len;

    (void)dev;
    *fw = NULL;

    data = getFWDescByName(base);
    if (!data) {
        /* try the full path as well */
        data = getFWDescByName(name);
    }
    if (!data) {
        XYLog("mt76: firmware %s not found in fwList\n", name);
        return -ENOENT;
    }

    f = (struct firmware *)kzalloc(sizeof(*f), 0);
    if (!f) {
        data->release();
        return -ENOMEM;
    }

    /* firmware blobs are zlib compressed; guess a generous output size */
    out_len = (uint)data->getLength() * 16 + 65536;
    out = (unsigned char *)kzalloc(out_len, 0);
    if (!out) {
        data->release();
        kfree(f);
        return -ENOMEM;
    }
    if (!uncompressFirmware(out, &out_len,
                            (unsigned char *)data->getBytesNoCopy(),
                            (uint)data->getLength())) {
        XYLog("mt76: failed to uncompress firmware %s\n", name);
        kfree(out);
        data->release();
        kfree(f);
        return -EINVAL;
    }

    f->data = out;
    f->size = out_len;
    f->priv = NULL;
    data->release();
    *fw = f;
    XYLog("mt76: loaded firmware %s (%u bytes)\n", name, out_len);
    return 0;
}

int request_firmware_nowarn(const struct firmware **fw, const char *name,
                            struct device *dev)
{
    return request_firmware(fw, name, dev);
}

int firmware_request_nowarn(const struct firmware **fw, const char *name,
                            struct device *dev)
{
    return request_firmware(fw, name, dev);
}

void release_firmware(const struct firmware *fw)
{
    if (!fw)
        return;
    if (fw->data)
        kfree(fw->data);
    kfree(fw);
}

/* ------------------------------------------------------------------ */
/* misc device helpers                                                */
/* ------------------------------------------------------------------ */
void __iomem *mtk_pcim_iomap_region(struct pci_dev *pdev, int bar,
                                    const char *name)
{
    (void)bar; (void)name;
    return (__iomem void *)pdev->regs;
}

struct device *devm_hwmon_device_register_with_groups(struct device *dev,
                                                      const char *name,
                                                      void *drvdata,
                                                      const struct attribute_group **groups)
{
    /* no sysfs hwmon in the kext: return a dummy device (never freed) */
    static struct device dummy;

    (void)drvdata; (void)groups;
    dummy.name = name;
    dummy.parent = dev;
    return &dummy;
}
