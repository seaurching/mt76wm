/*
 * mtk_core.h - central Linux kernel primitive shims for the mt76wm port.
 *
 * Part of the mt76wm XNU port of the Linux mt76 driver (MT7921/MT7922).
 * Provides the kernel object model (locks, waits, work, timers, tasks,
 * NAPI, idr, RCU, device/netdev) on top of XNU KPIs so that the ported
 * Linux sources compile and run as a kext.
 *
 * Ported for mt76wm (XNU) - minimal-semantics shim layer (M1).
 */
#ifndef MTK_CORE_H
#define MTK_CORE_H

#include <linux/types.h>
#include <linux/kernel.h>

#include <IOKit/IOLib.h>

/* MT7921 WPDMA is 32-bit only (Linux: dma_set_mask(32)); do NOT define
 * CONFIG_ARCH_DMA_ADDR_T_64BIT -- 64-bit addresses hang the RX engine. */
#include <IOKit/IOTypes.h>
#include <kern/locks.h>
#include <libkern/OSTypes.h>
#include <kern/thread_call.h>
#include <kern/clock.h>
#include <mach/clock_types.h>
#include <pexpert/pexpert.h>

/* ------------------------------------------------------------------ */
/* misc constants                                                     */
/* ------------------------------------------------------------------ */
#ifndef ETH_ALEN
#define ETH_ALEN 6
#endif
#ifndef ETH_HLEN
#define ETH_HLEN 14
#endif
#ifndef IFNAMSIZ
#define IFNAMSIZ 16
#endif

#define GFP_KERNEL  0
#define GFP_ATOMIC  0
#define GFP_NOWAIT  0
#define GFP_DMA32   0
#define __GFP_NOWARN 0

#ifndef PAGE_SHIFT
#define PAGE_SHIFT 12
#endif
#ifndef PAGE_SIZE
#define PAGE_SIZE (1UL << PAGE_SHIFT)
#endif

#define KBUILD_MODNAME "mtkwm"

struct mac_address {
    u8 addr[ETH_ALEN];
} __aligned(2);

/* ------------------------------------------------------------------ */
/* logging                                                            */
/* ------------------------------------------------------------------ */
/* dev_err/dev_warn/dev_info -> XYLog; dev_dbg -> silent (M1). */
#define dev_err(dev, fmt, ...)   XYLog("mt76: " fmt, ##__VA_ARGS__)
#define dev_warn(dev, fmt, ...)  XYLog("mt76: " fmt, ##__VA_ARGS__)
#define dev_info(dev, fmt, ...)  XYLog("mt76: " fmt, ##__VA_ARGS__)
#define dev_notice(dev, fmt, ...) XYLog("mt76: " fmt, ##__VA_ARGS__)
#define dev_dbg(dev, fmt, ...)   do { } while (0)
#define dev_err_ratelimited(dev, fmt, ...)  XYLog("mt76: " fmt, ##__VA_ARGS__)
#define dev_warn_ratelimited(dev, fmt, ...) XYLog("mt76: " fmt, ##__VA_ARGS__)

#ifndef pr_err
#define pr_err(fmt, ...)  XYLog("mt76: " fmt, ##__VA_ARGS__)
#endif
#ifndef pr_warn
#define pr_warn(fmt, ...) XYLog("mt76: " fmt, ##__VA_ARGS__)
#endif

#ifndef __ffs
/* index of the least-significant set bit (word must be non-zero) */
#define __ffs(word) ((int)__builtin_ctzl((unsigned long)(word)))
#endif

#ifndef __ffs64
#define __ffs64(x) __builtin_ctzll(x)
#endif

/* Linux kernel.h: smallest power of two >= n (mt76x02_mmio.c) */
static inline unsigned long roundup_pow_of_two(unsigned long n)
{
    unsigned long p = 1;

    while (p < n)
        p <<= 1;
    return p;
}

#ifndef WARN_ONCE
#define WARN_ONCE(condition, ...) ({ \
    static int __mtk_warned; \
    int __mtk_ret_warn = !!(condition); \
    if (unlikely(__mtk_ret_warn) && !__mtk_warned) { \
        __mtk_warned = 1; \
        XYLog("mt76: " __VA_ARGS__); \
    } \
    (bool)__mtk_ret_warn; \
})
#endif
#ifndef pr_info
#undef pr_info
#define pr_info(fmt, ...) XYLog("mt76: " fmt, ##__VA_ARGS__)
#endif
#ifndef pr_debug
#define pr_debug(fmt, ...) do { } while (0)
#endif
#define pr_err_ratelimited(fmt, ...) XYLog("mt76: " fmt, ##__VA_ARGS__)

/* ------------------------------------------------------------------ */
/* lock group (shared by all shim locks)                               */
/* ------------------------------------------------------------------ */
lck_grp_t *mtk_lck_grp(void);           /* defined in glue/mtk_glue.c */

/* ------------------------------------------------------------------ */
/* spinlocks                                                          */
/* (XNU lock types are opaque, so the shim keeps heap-allocated lock   */
/*  handles; every lock is created via spin_lock_init/mutex_init.)     */
/* ------------------------------------------------------------------ */
typedef struct spinlock {
    lck_spin_t *lck;
} spinlock_t;

static inline void spin_lock_init(spinlock_t *lock)
{
    lock->lck = lck_spin_alloc_init(mtk_lck_grp(), LCK_ATTR_NULL);
}
static inline void spin_lock_destroy(spinlock_t *lock)
{
    if (lock->lck) {
        lck_spin_free(lock->lck, mtk_lck_grp());
        lock->lck = NULL;
    }
}
static inline void spin_lock(spinlock_t *lock)
{
    if (lock->lck)
        lck_spin_lock(lock->lck);
}
static inline void spin_unlock(spinlock_t *lock)
{
    if (lock->lck)
        lck_spin_unlock(lock->lck);
}
static inline int spin_trylock(spinlock_t *lock)
{
    /* no try-lock in the KPI: always take it */
    spin_lock(lock);
    return 1;
}
/* _bh/_irq variants: XNU has no BH layer; a plain spinlock is close
 * enough for this single-threaded driver port.  TODO(M2): refine. */
#define spin_lock_bh(lock)          spin_lock(lock)
#define spin_unlock_bh(lock)        spin_unlock(lock)
#define spin_lock_irq(lock)         spin_lock(lock)
#define spin_unlock_irq(lock)       spin_unlock(lock)
#define spin_lock_irqsave(lock, flags) do { (flags) = 0; spin_lock(lock); } while (0)
#define spin_unlock_irqrestore(lock, flags) do { (void)(flags); spin_unlock(lock); } while (0)
#define read_lock(lock)             spin_lock(lock)
#define read_unlock(lock)           spin_unlock(lock)

typedef struct {
    lck_rw_t *lck;
} rwlock_t;
static inline void rwlock_init(rwlock_t *lock)
{
    lock->lck = lck_rw_alloc_init(mtk_lck_grp(), LCK_ATTR_NULL);
}

/* ------------------------------------------------------------------ */
/* mutexes                                                            */
/* ------------------------------------------------------------------ */
struct mutex {
    lck_mtx_t *lck;
};

static inline void mutex_init(struct mutex *m)
{
    m->lck = lck_mtx_alloc_init(mtk_lck_grp(), LCK_ATTR_NULL);
}
static inline void mutex_destroy(struct mutex *m)
{
    if (m->lck) {
        lck_mtx_free(m->lck, mtk_lck_grp());
        m->lck = NULL;
    }
}
static inline void mutex_lock(struct mutex *m)
{
    if (m->lck)
        lck_mtx_lock(m->lck);
}
static inline void mutex_unlock(struct mutex *m)
{
    if (m->lck)
        lck_mtx_unlock(m->lck);
}
static inline int mutex_trylock(struct mutex *m)
{
    /* no try-lock in the KPI: always take it */
    mutex_lock(m);
    return 1;
}
static inline int mutex_is_locked(struct mutex *m)
{
    (void)m;
    return 0;
}

/* ------------------------------------------------------------------ */
/* wait queues / completions                                          */
/* ------------------------------------------------------------------ */
/*
 * Wait queues are implemented as condition polling (1 ms granularity).
 * wake_up() is a no-op: waiters poll the condition.  This is simple and
 * correct (if slightly higher latency) for the MCU/firmware paths that
 * use wait_event_timeout in the mt76 port.  TODO(M2): real wakeups.
 *
 * Every variant is BOUNDED (mt76wm): the Linux "infinite" forms are
 * capped at MTK_WAIT_MAX_MS and warn on expiry, so no kernel thread can
 * ever be wedged inside the shim (a stuck thread keeps the kext
 * retained and blocks kmutil unload).
 */
#define MTK_WAIT_MAX_MS 30000

typedef struct wait_queue_head {
    lck_mtx_t *lock;
    int       flags;
} wait_queue_head_t;

static inline void init_waitqueue_head(wait_queue_head_t *wq)
{
    wq->lock = lck_mtx_alloc_init(mtk_lck_grp(), LCK_ATTR_NULL);
    wq->flags = 0;
}
static inline void wake_up(wait_queue_head_t *wq) { (void)wq; }
static inline void wake_up_all(wait_queue_head_t *wq) { (void)wq; }
static inline void wake_up_interruptible(wait_queue_head_t *wq) { (void)wq; }
static inline void wake_up_interruptible_all(wait_queue_head_t *wq) { (void)wq; }

#define wait_event(wq, condition) \
    do { \
        long __wq_i = 0; \
        (void)(wq); \
        while (!(condition)) { \
            if (++__wq_i > MTK_WAIT_MAX_MS) { \
                XYLog("mt76: wait_event(%s) timed out after %d ms\n", \
                      #condition, MTK_WAIT_MAX_MS); \
                break; \
            } \
            IOSleep(1); \
        } \
    } while (0)

#define wait_event_timeout(wq, condition, timeout) ({            \
    long __wq_rem = (long)(timeout);                             \
    while (!(condition) && __wq_rem > 0) {                       \
        IOSleep(1);                                              \
        __wq_rem--;                                              \
    }                                                            \
    ((condition) ? __wq_rem : 0);                                \
})

#define wait_event_interruptible(wq, condition) ({               \
    long __wq_i = 0;                                             \
    (void)(wq);                                                  \
    while (!(condition)) {                                       \
        if (++__wq_i > MTK_WAIT_MAX_MS) {                        \
            XYLog("mt76: wait_event_interruptible(%s) timed out\n", \
                  #condition);                                   \
            break;                                               \
        }                                                        \
        IOSleep(1);                                              \
    }                                                            \
    0;                                                           \
})

#define wait_event_interruptible_timeout(wq, condition, timeout) ({ \
    long __wq_rem = (long)(timeout);                             \
    while (!(condition) && __wq_rem > 0) {                       \
        IOSleep(1);                                              \
        __wq_rem--;                                              \
    }                                                            \
    ((condition) ? __wq_rem : 0);                                \
})

struct completion {
    volatile int done;
};

static inline void init_completion(struct completion *c) { c->done = 0; }
static inline void reinit_completion(struct completion *c) { c->done = 0; }
static inline void complete(struct completion *c) { c->done = 1; }
static inline void complete_all(struct completion *c) { c->done = 1; }

static inline void wait_for_completion(struct completion *c)
{
    long __wq_i = 0;

    while (!c->done) {
        if (++__wq_i > MTK_WAIT_MAX_MS) {
            XYLog("mt76: wait_for_completion timed out after %d ms\n",
                  MTK_WAIT_MAX_MS);
            break;
        }
        IOSleep(1);
    }
}

static inline long wait_for_completion_timeout(struct completion *c,
                                               long timeout)
{
    long rem = timeout;

    while (!c->done && rem > 0) {
        IOSleep(1);
        rem--;
    }
    return c->done ? rem : 0;
}

/* ------------------------------------------------------------------ */
/* workqueues (thread_call based; see mtk_glue.c)                     */
/* ------------------------------------------------------------------ */
struct work_struct;
typedef void (*work_func_t)(struct work_struct *work);

struct work_struct {
    work_func_t func;
    thread_call_t call;
    volatile unsigned long state;
    volatile int dead;      /* destroyed/draining: never run or re-queue */
    volatile int tc_refs;   /* queued/running invocations; see mtk_glue.c */
};

struct delayed_work {
    struct work_struct work;
    thread_call_t timer;
    volatile int timer_refs; /* same accounting for the delay timer */
    unsigned long delay;    /* jiffies (== ms) */
};

struct workqueue_struct {
    const char *name;
};

void mtk_init_work(struct work_struct *w, work_func_t f);
void mtk_init_delayed_work(struct delayed_work *w, work_func_t f);
void mtk_destroy_work(struct work_struct *w);
void mtk_destroy_delayed_work(struct delayed_work *w);
bool mtk_queue_work(struct work_struct *w);
bool mtk_queue_delayed_work(struct delayed_work *w, unsigned long delay);
bool mtk_cancel_work(struct work_struct *w);        /* async */
void mtk_flush_work(struct work_struct *w);         /* wait for completion */
void mtk_cancel_delayed_work_sync(struct delayed_work *w);
/* Teardown drain: after this, work/timers/tasklets refuse re-arming so a
 * cancelled callback can never be re-queued behind the unload path. */
void mtk_quiesce_work(void);
/* Re-arm after a detach/attach cycle within the same kext load. */
void mtk_quiesce_end(void);
/* Advance the jiffies counter (HZ == 1000) from real time; called from the
 * work/timer/tasklet trampolines and from periodic maintenance paths. */
void mtk_advance_jiffies(void);

#define INIT_WORK(w, f)             mtk_init_work((w), (f))
#define INIT_DELAYED_WORK(w, f)     mtk_init_delayed_work((w), (f))
#define __INIT_WORK(w, f, onstack)  mtk_init_work((w), (f))
#define destroy_work_on_stack(w)    do { } while (0)

/* All work is executed on shared thread_call threads; the workqueue
 * argument is ignored. */
#define queue_work(wq, w)           mtk_queue_work(w)
#define queue_work_on(cpu, wq, w)   mtk_queue_work(w)
#define schedule_work(w)            mtk_queue_work(w)
#define queue_delayed_work(wq, w, d) mtk_queue_delayed_work((w), (d))
#define schedule_delayed_work(w, d)  mtk_queue_delayed_work((w), (d))
#define queue_delayed_work_on(cpu, wq, w, d) mtk_queue_delayed_work((w), (d))
#define mod_delayed_work(wq, w, d)   mtk_queue_delayed_work((w), (d))
#define cancel_work_sync(w)         mtk_flush_work(w)
#define cancel_work(w)              mtk_cancel_work(w)
#define flush_work(w)               mtk_flush_work(w)
#define cancel_delayed_work(w)      mtk_cancel_work(&(w)->work)
#define cancel_delayed_work_sync(w) mtk_cancel_delayed_work_sync(w)
#define flush_delayed_work(w)       mtk_flush_work(&(w)->work)
#define flush_workqueue(wq)         do { (void)(wq); } while (0)

static inline struct workqueue_struct *mtk_dummy_wq(void)
{
    static struct workqueue_struct wq = { "mtkwm" };
    return &wq;
}
#define alloc_workqueue(fmt, flags, args...) mtk_dummy_wq()
#define alloc_ordered_workqueue(fmt, flags, args...) mtk_dummy_wq()
#define create_singlethread_workqueue(name) mtk_dummy_wq()
#define destroy_workqueue(wq) do { (void)(wq); } while (0)
#define WQ_SYSFS 0
#define WQ_UNBOUND 0
#define WQ_HIGHPRI 0

/* ------------------------------------------------------------------ */
/* timers (thread_call based; see mtk_glue.c)                         */
/* ------------------------------------------------------------------ */
struct timer_list {
    void (*function)(struct timer_list *);
    thread_call_t call;
    unsigned long expires;
    bool pending;
    volatile int dead;      /* shutdown: never run or re-arm */
    volatile int tc_refs;   /* queued/running invocations; see mtk_glue.c */
};

void mtk_timer_setup(struct timer_list *t, void (*fn)(struct timer_list *),
                     unsigned int flags);
bool mtk_mod_timer(struct timer_list *t, unsigned long expires);
int mtk_del_timer_sync(struct timer_list *t);
int mtk_del_timer(struct timer_list *t);
void mtk_timer_shutdown_sync(struct timer_list *t); /* dead: no re-arm */

#define TIMER_IRQSAFE 0
#define timer_setup(t, fn, flags) mtk_timer_setup((t), (fn), (flags))
#define from_timer(var, cb, field) container_of(cb, typeof(*var), field)
#define timer_container_of(var, cb_timer, field) \
    container_of(cb_timer, typeof(*var), field)
#define mod_timer(t, expires)       mtk_mod_timer((t), (expires))
#define timer_reduce(t, expires)    mtk_mod_timer((t), (expires))
#define timer_delete_sync(t)        mtk_del_timer_sync(t)
#define del_timer_sync(t)           mtk_del_timer_sync(t)
#define del_timer(t)                mtk_del_timer(t)
#define timer_shutdown_sync(t)      mtk_timer_shutdown_sync(t)
#define timer_pending(t)            ((t)->pending)

/* ------------------------------------------------------------------ */
/* tasklets (thread_call based)                                       */
/* ------------------------------------------------------------------ */
struct tasklet_struct {
    void (*func)(unsigned long);
    unsigned long data;
    thread_call_t call;
    volatile unsigned long state;
    volatile int dead;      /* destroyed/draining: never run or schedule */
    volatile int tc_refs;   /* queued/running invocations; see mtk_glue.c */
};

void mtk_tasklet_init(struct tasklet_struct *t, void (*fn)(unsigned long),
                      unsigned long data);
void mtk_tasklet_destroy(struct tasklet_struct *t);
void mtk_tasklet_schedule(struct tasklet_struct *t);

#define tasklet_init(t, fn, data)   mtk_tasklet_init((t), (fn), (data))
#define tasklet_setup(t, fn)        mtk_tasklet_init((t), (void (*)(unsigned long))(fn), (unsigned long)(t))
#define tasklet_schedule(t)         mtk_tasklet_schedule(t)
#define tasklet_hi_schedule(t)      mtk_tasklet_schedule(t)
#define tasklet_kill(t)             mtk_tasklet_destroy(t)
#define tasklet_disable(t)          do { (void)(t); } while (0)
#define tasklet_enable(t)           do { (void)(t); } while (0)
#define tasklet_disable_nosync(t)   do { (void)(t); } while (0)

typedef void (*tasklet_fn_t)(struct tasklet_struct *);

/* ------------------------------------------------------------------ */
/* kernel threads (thread_call backed)                                */
/* ------------------------------------------------------------------ */
enum {
    TASK_RUNNING = 0,
    TASK_INTERRUPTIBLE = 1,
    TASK_UNINTERRUPTIBLE = 2,
};

struct task_struct {
    thread_call_t call;
    int (*fn)(void *);
    void *arg;
    volatile int should_stop;
    volatile int should_park;
    volatile int parked;
    volatile int finished;
    volatile int state;
    volatile int tc_refs;   /* queued/running invocations; see mtk_glue.c */
    char name[32];
};

struct task_struct *mtk_kthread_run(int (*fn)(void *), void *data,
                                    const char *name);
int mtk_kthread_stop(struct task_struct *t);
void mtk_kthread_park(struct task_struct *t);
void mtk_kthread_unpark(struct task_struct *t);
int mtk_kthread_should_stop(void);
int mtk_kthread_should_park(void);
void mtk_kthread_parkme(void);
void wake_up_process(struct task_struct *t);
void schedule(void);
#define set_current_state(state) do { mtk_set_task_state(state); } while (0)
void mtk_set_task_state(int state);

#define kthread_run(fn, data, name...) mtk_kthread_run((fn), (data), #name)
#define kthread_should_stop()   mtk_kthread_should_stop()
#define kthread_should_park()   mtk_kthread_should_park()
#define kthread_parkme()        mtk_kthread_parkme()
#define kthread_park(t)         mtk_kthread_park(t)
#define kthread_unpark(t)       mtk_kthread_unpark(t)
#define kthread_stop(t)         mtk_kthread_stop(t)
#define kthread_create(fn, data, name...) mtk_kthread_run((fn), (data), #name)
#define wake_up_var(x) do { } while (0)

/* ------------------------------------------------------------------ */
/* NAPI                                                               */
/* ------------------------------------------------------------------ */
struct net_device;

struct napi_struct {
    struct net_device *dev;
    int (*poll)(struct napi_struct *napi, int budget);
    int weight;
    int budget;
    volatile unsigned long state;
    thread_call_t call;
    struct napi_struct *next;
    bool on_list;
};

#define NAPI_STATE_SCHED    0
#define NAPI_STATE_NPSVC    1
#define NAPI_STATE_DISABLE  2
#define NAPI_STATE_MISSED   3

void netif_napi_add(struct net_device *dev, struct napi_struct *napi,
                    int (*poll)(struct napi_struct *, int));
void netif_napi_add_tx(struct net_device *dev, struct napi_struct *napi,
                       int (*poll)(struct napi_struct *, int));
void netif_napi_del(struct napi_struct *napi);
void napi_enable(struct napi_struct *napi);
void napi_disable(struct napi_struct *napi);
void napi_schedule(struct napi_struct *napi);
void napi_schedule_prep_only(struct napi_struct *napi);
int napi_complete(struct napi_struct *napi);
int napi_complete_done(struct napi_struct *napi, int work_done);
int napi_gro_receive_stub(void);
bool napi_schedule_prep(struct napi_struct *napi);

/* drain all pending NAPI polls; called from the glue bottom half */
void mtk_napi_process(void);

/* ------------------------------------------------------------------ */
/* idr                                                                */
/* ------------------------------------------------------------------ */
struct idr {
    void **slots;
    int max;
};

void idr_init(struct idr *idr);
void idr_destroy(struct idr *idr);
int idr_alloc(struct idr *idr, void *ptr, int start, int end, gfp_t gfp);
mtk_any_ptr idr_remove(struct idr *idr, int id);
mtk_any_ptr idr_find(struct idr *idr, int id);
int idr_is_empty(struct idr *idr);

#define idr_for_each_entry(idr, entry, id)                        \
    for ((id) = 0;                                                \
         ((id) < (idr)->max) &&                                   \
         ((entry) = (idr)->slots ?                                \
            (__typeof__(entry))(idr)->slots[(id)] : NULL, 1);     \
         (id)++)                                                  \
        if (entry)

/* ------------------------------------------------------------------ */
/* RCU (single CPU approximation: direct pointers, immediate calls)   */
/* ------------------------------------------------------------------ */
struct rcu_head {
    struct rcu_head *next;
    void (*func)(struct rcu_head *head);
};

#define rcu_read_lock()         do { } while (0)
#define rcu_read_unlock()       do { } while (0)
#define rcu_read_lock_bh()      do { } while (0)
#define rcu_read_unlock_bh()    do { } while (0)
#define synchronize_rcu()       do { } while (0)
#define synchronize_net()       do { } while (0)
#define rcu_barrier()           do { } while (0)

#define rcu_assign_pointer(p, v) \
    do { barrier(); (p) = (v); barrier(); } while (0)
#define rcu_dereference(p) \
    ({ typeof(p) __rcu_p = (p); barrier(); __rcu_p; })
#define rcu_dereference_protected(p, c) \
    ({ (void)(c); typeof(p) __rcu_p = (p); barrier(); __rcu_p; })
#define rcu_access_pointer(p) \
    ({ typeof(p) __rcu_p = (p); barrier(); __rcu_p; })
#define rcu_replace_pointer(rcu_ptr, ptr, c) \
    ({ typeof(ptr) __rcu_old = (rcu_ptr); (void)(c); rcu_assign_pointer((rcu_ptr), (ptr)); __rcu_old; })

void mtk_call_rcu(struct rcu_head *head, void (*func)(struct rcu_head *));
void mtk_kfree_rcu(void *ptr, struct rcu_head *head);
#define call_rcu(head, func)        mtk_call_rcu((head), (func))
#define kfree_rcu(ptr, head)        mtk_kfree_rcu((ptr), &(ptr)->head)
#define rcu_pointer_handoff(p)      (p)

#define lockdep_is_held(lock)       (1)
#define lockdep_assert_held(lock)   do { } while (0)
#define rcu_dereference_check(p, c) rcu_dereference_protected(p, c)

/* ------------------------------------------------------------------ */
/* scheduling / misc                                                  */
/* ------------------------------------------------------------------ */
#define cond_resched_lock(lock) ({ 0; })
#define might_sleep() do { } while (0)
#define in_atomic()   (0)
#define in_irq()      (0)
#define in_softirq()  (0)

static inline void msleep(unsigned int ms) { IOSleep(ms); }
static inline void ssleep(unsigned int s) { IOSleep(s * 1000); }
static inline void udelay(unsigned int us) { IODelay(us); }
static inline void ndelay(unsigned int ns) { IODelay((ns + 999) / 1000); }
#define mdelay(ms) IODelay((ms) * 1000)

/* ktime (nanoseconds) */
typedef s64 ktime_t;
typedef s64 time64_t;

static inline ktime_t ktime_get_boottime(void)
{
    uint64_t ns;
    clock_get_uptime((uint64_t *)&ns);
    absolutetime_to_nanoseconds(ns, &ns);
    return (ktime_t)ns;
}
static inline ktime_t ktime_get(void) { return ktime_get_boottime(); }
#define ktime_to_us(kt) ((u64)(kt) / 1000)
#define ktime_to_ms(kt) ((u64)(kt) / 1000000)
#define ktime_add_us(kt, us) ((kt) + (ktime_t)(us) * 1000)
#define ktime_add_ms(kt, ms) ((kt) + (ktime_t)(ms) * 1000000)
#define ktime_sub(a, b) ((a) - (b))
#define NSEC_PER_MSEC 1000000ULL
#define NSEC_PER_USEC 1000ULL
#define USEC_PER_MSEC 1000ULL
#define ktime_set(secs, nsecs) ((ktime_t)(secs) * 1000000000LL + (nsecs))
#define ktime_after(a, b) ((a) > (b))

/* irq */
typedef int irqreturn_t;
#define IRQ_NONE        0
#define IRQ_HANDLED     1
#define IRQ_WAKE_THREAD 2
#define IRQF_SHARED     0x00000080
#define IRQF_NO_AUTOEN  0x00020000

/* ------------------------------------------------------------------ */
/* device / net_device                                                */
/* ------------------------------------------------------------------ */

struct device_node;

struct device {
    const char *name;
    void *driver_data;          /* dev_set_drvdata */
    struct device_node *of_node;
    struct device *parent;
    void *driver;
    void *platform_data;
    void *release_fn;
};

struct net_device {
    char name[IFNAMSIZ];
    u8 dev_addr[ETH_ALEN];
    u8 perm_addr[ETH_ALEN];
    unsigned int mtu;
    int threaded;
    void *ml_priv;
};

#define NETDEV_ALIGN 32

static inline void *netdev_priv(const struct net_device *dev)
{
    return (void *)((char *)dev + ALIGN(sizeof(struct net_device), NETDEV_ALIGN));
}

static inline mtk_any_ptr dev_get_drvdata(const struct device *dev)
{
    return mtk_any_ptr(dev ? dev->driver_data : NULL);
}
static inline void dev_set_drvdata(struct device *dev, void *data)
{
    if (dev)
        dev->driver_data = data;
}
static inline const char *dev_name(const struct device *dev)
{
    return (dev && dev->name) ? dev->name : "mtk";
}
static inline void device_init_wakeup(struct device *dev, bool enable)
{
    (void)dev; (void)enable;
}
static inline void device_set_wakeup_enable(struct device *dev, bool enable)
{
    (void)dev; (void)enable;
}
static inline int device_init_wakeup_stub(void) { return 0; }

struct net_device *alloc_netdev_dummy(int sizeof_priv);
void free_netdev(struct net_device *dev);

/* debugfs / seq_file / file stubs (symbols referenced but unused) */
struct dentry;
struct file;
struct seq_file;
struct file_operations;
struct inode;
struct netlink_callback;
struct nlattr;

struct debugfs_blob_wrapper {
    void *data;
    unsigned long size;
};

/* module / kbuild macros (kext has no module system) */
#define EXPORT_SYMBOL(x)
#define EXPORT_SYMBOL_GPL(x)
#define EXPORT_UNUSED_SYMBOL(x)
#define MODULE_LICENSE(x)
#define MODULE_AUTHOR(x)
#define MODULE_DESCRIPTION(x)
#define MODULE_FIRMWARE(x)
#define MODULE_DEVICE_TABLE(t, x)
#define MODULE_PARM_DESC(x, y)
#define MODULE_VERSION(x)
#define module_init(x)
#define module_exit(x)
#define module_param_named(name, value, type, perm)
#define module_param(var, type, perm)
#define module_pci_driver(drv)
#define builtin_firmware(x, y)

#define pm_sleep_ptr(x) (x)
#define SIMPLE_DEV_PM_OPS(name, suspend_fn, resume_fn) \
    static const struct dev_pm_ops name = { }
struct device;
struct dev_pm_ops {
    int unused;
    int  (*prepare)(struct device *dev);
    void (*complete)(struct device *dev);
    int  (*suspend)(struct device *dev);
    int  (*resume)(struct device *dev);
    int  (*freeze)(struct device *dev);
    int  (*thaw)(struct device *dev);
    int  (*poweroff)(struct device *dev);
    int  (*restore)(struct device *dev);
    int  (*suspend_late)(struct device *dev);
    int  (*resume_early)(struct device *dev);
    int  (*freeze_late)(struct device *dev);
    int  (*thaw_early)(struct device *dev);
    int  (*poweroff_late)(struct device *dev);
    int  (*restore_early)(struct device *dev);
    int  (*suspend_noirq)(struct device *dev);
    int  (*resume_noirq)(struct device *dev);
    int  (*freeze_noirq)(struct device *dev);
    int  (*thaw_noirq)(struct device *dev);
    int  (*poweroff_noirq)(struct device *dev);
    int  (*restore_noirq)(struct device *dev);
    int  (*runtime_suspend)(struct device *dev);
    int  (*runtime_resume)(struct device *dev);
    int  (*runtime_idle)(struct device *dev);
};
#define DEFINE_SIMPLE_DEV_PM_OPS(name, suspend_fn, resume_fn) \
    static const struct dev_pm_ops name = { }
#define __maybe_unused  __attribute__((unused))
#ifndef __always_unused
#define __always_unused __attribute__((unused))
#endif

#define to_pci_dev(d) container_of(d, struct pci_dev, dev)
struct pci_dev;                 /* see <linux/pci.h> */

/* IS_ENABLED / IS_REACHABLE: everything not defined is disabled */
#define __ARG_PLACEHOLDER_1 0,
#define __config_enabled(cfg) _config_enabled(cfg)
#define _config_enabled(value) __config_enabled_1(value)
#define __config_enabled_1(value) __config_enabled_2(__ARG_PLACEHOLDER_##value)
#define __config_enabled_2(prio) __config_enabled_3(prio 1, 0)
#define __config_enabled_3(ignore, val, ...) val
#define IS_ENABLED(option) __config_enabled(option)
#define IS_REACHABLE(option) IS_ENABLED(option)

/* kernel.h numeric helpers (mt7915 init.c thermal sysfs store) */
#ifndef DIV_ROUND_CLOSEST
#define DIV_ROUND_CLOSEST(x, divisor)(          \
{                                               \
    typeof(x) __x = (x);                        \
    typeof(divisor) __d = (divisor);            \
    (((typeof(x))-1) > 0 || ((typeof(divisor))-1) > 0 || (__x) > 0) ? \
        (((__x) + ((__d) / 2)) / (__d)) :       \
        (((__x) - ((__d) / 2)) / (__d));        \
}                                               \
)
#endif

static inline int kstrtol(const char *s, unsigned int base, long *res)
{
    /* decimal only in the port paths */
    char *end;
    long v;

    (void)base;
    if (!s || !res)
        return -EINVAL;
    v = strtol(s, &end, 10);
    if (end == s)
        return -EINVAL;
    *res = v;
    return 0;
}

/* kernel.h WARN (mt7915 mac.c) */
#ifndef WARN
#define WARN(cond, fmt, ...) do {                                       \
        if (cond)                                                       \
            XYLog("WARNING: " fmt, ##__VA_ARGS__);                      \
    } while (0)
#endif

/* linux/math.h 64/32 division helper (mt7915 mac.c TSF math) */
static inline u32 div_u64_rem(u64 dividend, u32 divisor, u32 *remainder)
{
    *remainder = (u32)(dividend % divisor);
    return (u32)(dividend / divisor);
}

/* mt7915/mmio.c register remap + mac.c: the port is PCIe-only (no
 * SoC/AXI bus), so every bound device is a PCI device. */
static inline bool dev_is_pci(const struct device *dev)
{
    (void)dev;
    return true;
}

/* linux/list.h: first entry or NULL on empty list (mt7915 mac.c TWT) */
#ifndef list_first_entry_or_null
#define list_first_entry_or_null(ptr, type, member) ({                  \
        struct list_head *head__ = (ptr);                               \
        type *res__ = NULL;                                             \
        if (!list_empty(head__))                                        \
            res__ = list_first_entry(head__, type, member);             \
        res__;                                                          \
    })
#endif

#endif /* MTK_CORE_H */
