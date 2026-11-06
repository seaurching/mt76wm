/* SPDX-License-Identifier: GPL-2.0 */

#ifndef kernel_h
#define kernel_h

#include <linux/types.h>
#include <linux/mtk_alloc.h>

#include <sys/errno.h>
#include <libkern/OSTypes.h>
#include <IOKit/IOLib.h>


/*
 * mtk_any_ptr: a void* wrapper with a templated conversion operator so
 * that allocation results (which are void* in Linux) can be assigned to
 * typed pointers even though the whole tree is compiled as C++.
 */
struct mtk_any_ptr {
    void *p;
    mtk_any_ptr() : p(0) {}
    mtk_any_ptr(void *q) : p(q) {}
    template <typename T> operator T() const { return (T)p; }
    void *get() const { return p; }
};

#define ERFKILL        132    /* Operation not possible due to RF-kill */

#define EHWPOISON    133    /* Memory page has hardware error */


/* Defined for the NFSv3 protocol */
#define EBADHANDLE    521    /* Illegal NFS file handle */
#define ENOTSYNC    522    /* Update synchronization mismatch */
#define EBADCOOKIE    523    /* Cookie is stale */
#define ENOTSUPP    524    /* Operation is not supported */
#define ETOOSMALL    525    /* Buffer or request is too small */
#define ESERVERFAULT    526    /* An untranslatable error occurred */
#define EBADTYPE    527    /* Type not supported by server */
#define EJUKEBOX    528    /* Request initiated, but will not complete before timeout */
#define EIOCBQUEUED    529    /* iocb queued, will get completion event */
#define ERECALLCONFLICT    530    /* conflict with recalled state */


#define min_t(type,x,y) \
({ type __x = (x); type __y = (y); __x < __y ? __x: __y; })

#define max_t(type, x, y) \
({ type __x = (x); type __y = (y); __x > __y ? __x: __y; })



#define OS_EXPECT(x, v) __builtin_expect((x), (v))

#define likely(x)       OS_EXPECT(!!(x), 1)
#define unlikely(x)     ({ \
                            OS_EXPECT(!!(x), 0); \
                            !!(x); \
                        })

# define do_div(n,base) ({                    \
uint32_t __base = (base);                \
uint32_t __rem;                        \
__rem = ((uint64_t)(n)) % __base;            \
(n) = ((uint64_t)(n)) / __base;                \
__rem;                            \
})

#define MAX_ERRNO 4095

//判断x是不是在（0xfffff000，0xffffffff)之间，注意这里用unlikely()的用意
#define IS_ERR_VALUE(x) unlikely((x) >= (unsigned long)-MAX_ERRNO)
  
//由错误码求指针，-1 -> 0xFFFFFFFF
static inline void *ERR_PTR(long error)
{
    return (void *) error;
}

//由指针求错误码，0xFFFFFFFF -> -1 ,依次类推
static inline long PTR_ERR(const void *ptr)
{
    return (long) ptr;
}
  
//判断x是不是在（0xfffff000，0xffffffff)之间，x是不是一个有效的指针
 static inline long IS_ERR(const void *ptr)
{
    return IS_ERR_VALUE((unsigned long)ptr);
}

static inline int fls64(UInt64 x)
{
    int bitpos = -1;
    /*
     * AMD64 says BSRQ won't clobber the dest reg if x==0; Intel64 says the
     * dest reg is undefined if x==0, but their CPU architect says its
     * value is written to set it to the same as before.
     */
    asm("bsrq %1,%q0"
        : "+r" (bitpos)
        : "rm" (x));
    return bitpos + 1;
}

static inline const char *reverse_strchr(const char *s, int c)
{
    char *find = NULL;
    const char *ss = s;
    
    while (*ss != '\0' && (find = strchr(ss, c))) {
        ss = ++find;
    }
    return ss;
}

#ifndef strrchr
#define strrchr(x, y) reverse_strchr((x), (y))
#endif

/*
 * Runtime evaluation of get_order()
 */
static inline
int __get_order(unsigned long size)
{
    int order;
    
    size--;
    size >>= PAGE_SHIFT;
#if BITS_PER_LONG == 32
    order = linux_fls(size);
#else
    order = fls64(size);
#endif
    return order;
}

/**
 * get_order - Determine the allocation order of a memory size
 * @size: The size for which to get the order
 *
 * Determine the allocation order of a particular sized block of memory.  This
 * is on a logarithmic scale, where:
 *
 *    0 -> 2^0 * PAGE_SIZE and below
 *    1 -> 2^1 * PAGE_SIZE to 2^0 * PAGE_SIZE + 1
 *    2 -> 2^2 * PAGE_SIZE to 2^1 * PAGE_SIZE + 1
 *    3 -> 2^3 * PAGE_SIZE to 2^2 * PAGE_SIZE + 1
 *    4 -> 2^4 * PAGE_SIZE to 2^3 * PAGE_SIZE + 1
 *    ...
 *
 * The order returned is used to find the smallest allocation granule required
 * to hold an object of the specified size.
 *
 * The result is undefined if the size is 0.
 *
 * This function may be used to initialise variables with compile time
 * evaluations of constants.
 */
#define get_order(n)                        \
(                                \
__builtin_constant_p(n) ? (                \
((n) == 0UL) ? BITS_PER_LONG - PAGE_SHIFT :    \
(((n) < (1UL << PAGE_SHIFT)) ? 0 :        \
ilog2((n) - 1) - PAGE_SHIFT + 1)        \
) :                            \
__get_order(n)                        \
)

/**
 * container_of - cast a member of a structure out to the containing structure
 * @ptr:    the pointer to the member.
 * @type:    the type of the container struct this is embedded in.
 * @member:    the name of the member within the struct.
 *
 */
#define container_of(ptr, type, member) ({                \
pointer_t __mptr = (pointer_t)(ptr);                    \
((type *)(__mptr - offsetof(type, member))); })


/*
 * This looks more complex than it should be. But we need to
 * get the type for the ~ right in round_down (it needs to be
 * as wide as the result!), and we want to evaluate the macro
 * arguments just once each.
 */
#define __round_mask(x, y) ((__typeof__(x))((y)-1))
#define round_up(x, y) ((((x)-1) | __round_mask(x, y))+1)
#define round_down(x, y) ((x) & ~__round_mask(x, y))

/*
 * jiffies: milliseconds since boot.  Implemented as a plain variable
 * (renamed via macro) so that ported sources may use "jiffies" as a
 * struct member name as well; it is advanced by the glue sleep/timer
 * paths.  TODO(M2): real tick source.
 */
extern volatile unsigned long jiffies_global;
#define jiffies jiffies_global

#ifndef FIELD_GET
#define FIELD_GET(_mask, _val) ({ \
    (typeof(_mask))(((_val) & (_mask)) >> __bf_shf(_mask)); \
})
#endif



#define time_after(a,b)    \
((long)(b) - (long)(a) < 0)
#define time_before(a, b)   time_after(b, a)
#define time_after32(a, b)  ((s32)((u32)(b) - (u32)(a)) < 0)
#define time_before32(a, b) time_after32(b, a)
#define time_is_after_jiffies(a) time_after(jiffies, (a))

#define EPROBE_DEFER    517

#define fallthrough     do { } while (0)
#define time_is_before_jiffies(a) time_after(ticks, a)

#define DMA_BIT_MASK(n)    (((n) == 64) ? ~0ULL : ((1ULL<<(n))-1))

#define struct_size(p, member, count) \
    (sizeof(*p) + sizeof(*(p)->member) * (size_t)(count))

// log2.h
/*
 *  Determine whether some value is a power of two, where zero is
 * *not* considered a power of two.
 */

static inline __attribute__((const))
bool is_power_of_2(unsigned long n)
{
    return (n != 0 && ((n & (n - 1)) == 0));
}

/**
 * kmemdup - duplicate region of memory
 *
 * @src: memory region to duplicate
 * @len: memory region length
 * @gfp: GFP mask to use
 */
static inline mtk_any_ptr kmemdup(const void *src, size_t len, int gfp = 0) {
    void *p;
    (void)gfp;
    p = mtk_heap_alloc(len);
    if (p)
        memcpy(p, src, len);
    return p;
}

static inline mtk_any_ptr kcalloc(size_t n, size_t size, int gfp = 0)
{
    (void)gfp;
    if (size != 0 && n > SIZE_MAX / size) {
        return NULL;
    }
    void *ret = mtk_heap_alloc(n * size);
    if (ret) {
        bzero(ret, n * size);
    }
    return ret;
}

static inline mtk_any_ptr kzalloc(size_t size, int gfp = 0)
{
    (void)gfp;
    void *ret = mtk_heap_alloc(size);
    if (ret) {
        bzero(ret, size);
    }
    return ret;
}



static inline int atomic_dec_and_test(volatile SInt32 * addr)
{
    return ((OSDecrementAtomic(addr) == 1) ? 1 : 0);
}

static inline int atomic_inc_and_test(volatile SInt32 * addr)
{
    return ((OSIncrementAtomic(addr) == -1) ? 1 : 0);
}

#define atomic_inc(v) ((void)OSIncrementAtomic(v))
#define atomic_dec(v) ((void)OSDecrementAtomic(v))
#define atomic_read(v) (*(volatile SInt32 *)(v))
#define atomic_set(v, i) (*(volatile SInt32 *)(v) = (SInt32)(i))
#define atomic_add(i, v) ((void)OSAddAtomic((SInt32)(i), (volatile SInt32 *)(v)))
#define atomic_sub(i, v) ((void)OSAddAtomic(-(SInt32)(i), (volatile SInt32 *)(v)))
#define atomic_inc_return(v) (OSIncrementAtomic(v) + 1)
#define atomic_dec_return(v) (OSDecrementAtomic(v) - 1)
#define atomic_add_return(i, v) (OSAddAtomic((SInt32)(i), (volatile SInt32 *)(v)) + (i))

static inline SInt32 atomic_cmpxchg(volatile SInt32 *v, SInt32 old, SInt32 newv)
{
    if (OSCompareAndSwap((UInt32)old, (UInt32)newv, (volatile UInt32 *)v))
        return old;
    return *v;
}

static inline int atomic_xchg(volatile SInt32 *v, SInt32 newv)
{
    SInt32 old;
    do {
        old = *v;
    } while (!OSCompareAndSwap((UInt32)old, (UInt32)newv, (volatile UInt32 *)v));
    return old;
}

#if __has_builtin(__builtin_abs)
#define abs(N) __builtin_abs((N))
#else
#define abs(N) (((N)<0)?-(N):(N))
#endif /* __has_builtin(__builtin_abs) */

/* ---- additions for the mt76 (mt76wm) port ---- */



#ifndef HZ
#define HZ 1000
#endif

#define MAX_SCHEDULE_TIMEOUT   LONG_MAX

#define msecs_to_jiffies(m) ((unsigned long)(m))   /* HZ == 1000 */
#define jiffies_to_msecs(j) ((unsigned int)(j))
#define usecs_to_jiffies(u) ((unsigned long)((u) / 1000))
#define time_is_after_jiffies(a) time_after(jiffies, (a))

#ifndef min
#define min(x, y) ({                                \
    typeof(x) _min1 = (x);                          \
    typeof(y) _min2 = (y);                          \
    (void) (&_min1 == &_min2);                      \
    _min1 < _min2 ? _min1 : _min2; })
#endif
#ifndef max
#define max(x, y) ({                                \
    typeof(x) _max1 = (x);                          \
    typeof(y) _max2 = (y);                          \
    (void) (&_max1 == &_max2);                      \
    _max1 > _max2 ? _max1 : _max2; })
#endif
#define min3(x, y, z) min((typeof(x))min(x, y), z)
#define max3(x, y, z) max((typeof(x))max(x, y), z)

template <typename T, typename U>
static inline void swap(T &a, U &b)
{
    T tmp = a;
    a = (T)b;
    b = (U)tmp;
}

#define clamp(val, lo, hi) min((typeof(val))max(val, lo), hi)
#define clamp_t(type, val, lo, hi) min((type)max(val, lo), (type)hi)
#define clamp_val(val, lo, hi) clamp_t(typeof(val), val, lo, hi)

#define ALIGN(x, a)         __ALIGN_KERNEL((x), (a))
#define __ALIGN_KERNEL(x, a)        __ALIGN_KERNEL_MASK(x, (typeof(x))(a) - 1)
#define __ALIGN_KERNEL_MASK(x, mask)    (((x) + (mask)) & ~(mask))
#define PTR_ALIGN(p, a)     ((typeof(p))ALIGN((unsigned long)(p), (a)))
#define IS_ALIGNED(x, a)    (((x) & ((typeof(x))(a) - 1)) == 0)

#define L1_CACHE_ALIGN(x) ALIGN(x, 64)

#ifndef offsetofend
#define offsetofend(TYPE, MEMBER) \
    (offsetof(TYPE, MEMBER) + sizeof(((TYPE *)0)->MEMBER))
#endif

#define PTR_ERR_OR_ZERO(ptr) (IS_ERR(ptr) ? PTR_ERR(ptr) : 0)
#define ERR_CAST(ptr) ((typeof(ptr))ERR_PTR(PTR_ERR(ptr)))
#define ZERO_SIZE_PTR ((void *)16)
#define ZERO_OR_NULL_PTR(x) ((unsigned long)(x) <= (unsigned long)ZERO_SIZE_PTR)

#define struct_group(NAME, MEMBERS...) \
    union { \
        struct { MEMBERS }; \
        struct NAME { MEMBERS } NAME; \
    }
#define struct_group_tagged(TAG, NAME, MEMBERS...) \
    union { \
        struct { MEMBERS }; \
        struct TAG { MEMBERS } NAME; \
    }
#define DECLARE_FLEX_ARRAY(TYPE, NAME) \
    struct { \
        struct { } __empty_ ## NAME; \
        TYPE NAME[0]; \
    }

#ifndef container_of
#define container_of(ptr, type, member) ({                \
    pointer_t __mptr = (pointer_t)(ptr);                    \
    ((type *)(__mptr - offsetof(type, member))); })
#endif

/* ---- bitops not covered by <linux/bitfield.h> ---- */
/* ffs/fls are provided by libkern (libkern.h). */

static inline int ilog2(unsigned long n)
{
    return (int)(sizeof(unsigned long) * 8 - 1 - __builtin_clzl(n));
}

/* ---- compiler memory barriers / READ_ONCE ---- */
#define barrier()       __asm__ __volatile__("": : :"memory")
#define mb()            __asm__ __volatile__("mfence": : :"memory")
#define rmb()           __asm__ __volatile__("lfence": : :"memory")
#define wmb()           __asm__ __volatile__("sfence": : :"memory")
#define smp_mb()        barrier()
#define smp_rmb()       barrier()
#define smp_wmb()       barrier()
#define smp_mb__before_atomic() barrier()
#define smp_mb__after_atomic()  barrier()

#define READ_ONCE(x) ({ typeof(x) __x = (x); barrier(); __x; })
#define WRITE_ONCE(x, val) do { barrier(); (x) = (val); barrier(); } while (0)

#define memzero_explicit(p, s) do { memset((p), 0, (s)); barrier(); } while (0)

static inline char *mtk_strscpy(char *dest, const char *src, size_t count)
{
    size_t i;

    if (count == 0)
        return dest;
    for (i = 0; i + 1 < count && src[i]; i++)
        dest[i] = src[i];
    dest[i] = '\0';
    return dest;
}
/* Linux >= 6.8: strscpy(dst, src) infers the size of array destinations */
#define strscpy(dst, src, ...) mtk_strscpy((dst), (src), sizeof(dst))

#define cond_resched() do { } while (0)
#define need_resched() (0)

/* ---- Linux-style doubly linked list ---- */
struct list_head {
    struct list_head *next, *prev;
};

#define LIST_HEAD_INIT(name) { &(name), &(name) }
/*
 * LIST_HEAD is variadic so that both the Linux form LIST_HEAD(name) and
 * the BSD sys/queue.h form LIST_HEAD(name, type) work regardless of
 * include order.
 */
#define MTK_LIST_HEAD_1(name) struct list_head name = LIST_HEAD_INIT(name)
#define MTK_LIST_HEAD_2(bname, btype) struct bname { struct btype *lh_first; }
#define MTK_LIST_HEAD_PICK(_1, _2, NAME, ...) NAME
#define LIST_HEAD(...) \
    MTK_LIST_HEAD_PICK(__VA_ARGS__, MTK_LIST_HEAD_2, MTK_LIST_HEAD_1)(__VA_ARGS__)

static inline void INIT_LIST_HEAD(struct list_head *list)
{
    list->next = list;
    list->prev = list;
}

static inline void __list_add(struct list_head *n, struct list_head *prev,
                              struct list_head *next)
{
    next->prev = n;
    n->next = next;
    n->prev = prev;
    prev->next = n;
}

static inline void list_add(struct list_head *n, struct list_head *head)
{
    __list_add(n, head, head->next);
}

static inline void list_add_tail(struct list_head *n, struct list_head *head)
{
    __list_add(n, head->prev, head);
}

static inline void __list_del(struct list_head *prev, struct list_head *next)
{
    next->prev = prev;
    prev->next = next;
}

static inline void __list_del_entry(struct list_head *entry)
{
    __list_del(entry->prev, entry->next);
}

static inline void list_del(struct list_head *entry)
{
    __list_del_entry(entry);
    entry->next = (struct list_head *)0xdead000000000100UL;
    entry->prev = (struct list_head *)0xdead000000000122UL;
}

static inline void list_del_init(struct list_head *entry)
{
    __list_del_entry(entry);
    INIT_LIST_HEAD(entry);
}

static inline void list_move(struct list_head *list, struct list_head *head)
{
    __list_del_entry(list);
    list_add(list, head);
}

static inline void list_move_tail(struct list_head *list,
                                  struct list_head *head)
{
    __list_del_entry(list);
    list_add_tail(list, head);
}

static inline int list_empty(const struct list_head *head)
{
    return head->next == head;
}

static inline int list_is_last(const struct list_head *list,
                               const struct list_head *head)
{
    return list->next == head;
}

static inline void __list_splice(const struct list_head *list,
                                 struct list_head *prev,
                                 struct list_head *next)
{
    struct list_head *first = list->next;
    struct list_head *last = list->prev;

    first->prev = prev;
    prev->next = first;

    last->next = next;
    next->prev = last;
}

static inline void list_splice_init(struct list_head *list,
                                    struct list_head *head)
{
    if (!list_empty(list)) {
        __list_splice(list, head, head->next);
        INIT_LIST_HEAD(list);
    }
}

#define list_entry(ptr, type, member) container_of(ptr, type, member)
#define list_first_entry(ptr, type, member) \
    list_entry((ptr)->next, type, member)
#define list_last_entry(ptr, type, member) \
    list_entry((ptr)->prev, type, member)
#define list_next_entry(pos, member) \
    list_entry((pos)->member.next, typeof(*(pos)), member)
#define list_prev_entry(pos, member) \
    list_entry((pos)->member.prev, typeof(*(pos)), member)

#define list_for_each_entry(pos, head, member)                \
    for (pos = list_first_entry(head, typeof(*pos), member);    \
         !list_entry_is_head(pos, head, member);            \
         pos = list_next_entry(pos, member))

#define list_for_each_entry_reverse(pos, head, member)            \
    for (pos = list_last_entry(head, typeof(*pos), member);        \
         !list_entry_is_head(pos, head, member);            \
         pos = list_prev_entry(pos, member))

#define list_for_each_entry_safe(pos, n, head, member)            \
    for (pos = list_first_entry(head, typeof(*pos), member),    \
         n = list_next_entry(pos, member);            \
         !list_entry_is_head(pos, head, member);            \
         pos = n, n = list_next_entry(n, member))

#define list_entry_is_head(pos, head, member)                \
    (&pos->member == (head))

#define list_for_each(pos, head) \
    for (pos = (head)->next; pos != (head); pos = pos->next)

#define list_for_each_safe(pos, n, head) \
    for (pos = (head)->next, n = pos->next; pos != (head); \
         pos = n, n = pos->next)

/* ---- ctype bits ---- */
static inline int isalpha(int c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}
static inline int isdigit(int c)
{
    return c >= '0' && c <= '9';
}
static inline int isspace(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}
static inline int isalnum(int c)
{
    return isalpha(c) || isdigit(c);
}
static inline int isupper(int c)
{
    return c >= 'A' && c <= 'Z';
}
static inline int tolower(int c)
{
    return isupper(c) ? c - 'A' + 'a' : c;
}

#endif /* kernel_h */
