/*
 * What the Espressif libraries ask of the system (wifi_osi_funcs_t): the semaphores, the queues, the tasks, the timers, the memory, the
 * interrupts. ESP-IDF fills this table with calls of its FreeRTOS and its heap; here it is dmosi (which uses the ISR variants of FreeRTOS by
 * itself when it is called from an interrupt), dmod's heaps and the interrupt lines of dmod (DMOD_IRQ_HANDLER).
 *
 * A tick of the libraries is a millisecond here: they get it from _task_ms_to_tick and give it back in the blocking calls.
 */
#include "private.h"
#include <string.h>

#define TICKS_FOREVER   OSI_FUNCS_TIME_BLOCKING

static inline int32_t timeout_ms(uint32_t ticks)
{
    return (ticks == TICKS_FOREVER) ? -1 : (int32_t) ticks;
}

/* ---- the CPU: interrupts ---- */

#if defined(__XTENSA__)
static inline uint32_t cpu_irq_disable(void)
{
    uint32_t ps;
    __asm__ volatile("rsil %0, 3" : "=a"(ps)::"memory");    /* up to the level of the exceptions: what portENTER_CRITICAL does of ESP-IDF */
    return ps;
}

static inline void cpu_irq_restore(uint32_t ps)
{
    __asm__ volatile("wsr.ps %0\n rsync" ::"a"(ps) : "memory");
}

static void cpu_ints_on(uint32_t mask)
{
    uint32_t t;
    __asm__ volatile("rsr.intenable %0\n or %0, %0, %1\n wsr.intenable %0\n rsync" : "=&a"(t) : "a"(mask) : "memory");
}

static void cpu_ints_off(uint32_t mask)
{
    uint32_t t;
    __asm__ volatile("rsr.intenable %0\n and %0, %0, %1\n wsr.intenable %0\n rsync" : "=&a"(t) : "a"(~mask) : "memory");
}
#else
static inline uint32_t cpu_irq_disable(void) { return 0; }
static inline void cpu_irq_restore(uint32_t ps) { (void) ps; }
static void cpu_ints_on(uint32_t mask) { (void) mask; }
static void cpu_ints_off(uint32_t mask) { (void) mask; }
#endif

#define INTMATRIX_CORE0     0x600C2000u
#define LINES               32

typedef struct
{
    void (*handler)(void *);
    void *arg;
} isr_t;

static isr_t g_isr[LINES];
static volatile uint32_t g_isr_calls[LINES];     /* for the log: how many times the libraries' handlers ran (the interrupts reach them) */
static volatile int32_t g_isr_registered[LINES];

static void osi_set_intr(int32_t cpu, uint32_t source, uint32_t number, int32_t priority)
{
    (void) priority;
    *(volatile uint32_t *) (INTMATRIX_CORE0 + (cpu == 0 ? 0u : 0x800u) + source * 4u) = number;
}

static void osi_set_isr(int32_t n, void *f, void *arg)
{
    if (n >= 0 && n < LINES)
    {
        g_isr[n].arg = arg;
        g_isr[n].handler = (void (*)(void *)) f;
        g_isr_registered[n] = 1;
    }
}

/* "line:calls" of the lines the libraries took, for the log */
void osi_isr_stats(char *out, size_t size)
{
    size_t at = 0;
    out[0] = '\0';
    for (int i = 0; i < LINES && at + 16 < size; i++)
    {
        if (g_isr_registered[i])
            at += (size_t) Dmod_SnPrintf(out + at, size - at, " %d:%u", i, (unsigned) g_isr_calls[i]);
    }
}

int osi_isr_dispatch(int line)
{
    isr_t *isr = &g_isr[line];
    g_isr_calls[line]++;
    if (isr->handler != NULL)
        isr->handler(isr->arg);
    return 0;
}

/* The lines of the CPU are dmod's: the vector of the port calls Dmod_IrqAll(line), which comes here */
#define IRQ_LINE(n)     DMOD_IRQ_HANDLER(n) { osi_isr_dispatch(n); }
IRQ_LINE(0)  IRQ_LINE(1)  IRQ_LINE(2)  IRQ_LINE(3)  IRQ_LINE(4)  IRQ_LINE(5)  IRQ_LINE(6)  IRQ_LINE(7)
IRQ_LINE(8)  IRQ_LINE(9)  IRQ_LINE(10) IRQ_LINE(11) IRQ_LINE(12) IRQ_LINE(13) IRQ_LINE(14) IRQ_LINE(15)
IRQ_LINE(16) IRQ_LINE(17) IRQ_LINE(18) IRQ_LINE(19) IRQ_LINE(20) IRQ_LINE(21) IRQ_LINE(22) IRQ_LINE(23)
IRQ_LINE(24) IRQ_LINE(25) IRQ_LINE(26) IRQ_LINE(27) IRQ_LINE(28) IRQ_LINE(29) IRQ_LINE(30) IRQ_LINE(31)

static bool osi_is_from_isr(void)
{
    return Dmod_IsInsideInterrupt();
}

static void *osi_spin_lock_create(void)
{
    return Dmod_Malloc(4);
}

static void osi_spin_lock_delete(void *lock)
{
    Dmod_Free(lock);
}

#define FROM_ISR_FLAG   0x80000000u     /* PS keeps no bit there: the state is the level of the interrupts */

static uint32_t osi_int_disable(void *mux)
{
    (void) mux;
    return cpu_irq_disable();
}

static void osi_int_restore(void *mux, uint32_t state)
{
    (void) mux;
    cpu_irq_restore(state);
}

static void osi_yield_from_isr(void)
{
    /* dmosi yields by itself when a call from an interrupt wakes something */
}

/* where the threads block (a table at the end of the pool - JTAG reads it when something hangs): thread, what, on what, count */
typedef struct { uint32_t thread, op, obj, count; } blk_t;
#define BLK_TABLE   ((volatile blk_t *) (0x3fce0000u - 64u))
#define BLK_SLOTS   4
static volatile dmosi_thread_t g_wifi_task_thread;
static volatile blk_t *blk_slot(void)
{
    uint32_t me = (uint32_t) dmosi_thread_current();
    for (int i = 0; i < BLK_SLOTS; i++)
    {
        if (BLK_TABLE[i].thread == me)
            return &BLK_TABLE[i];
        if (BLK_TABLE[i].thread == 0)
        {
            BLK_TABLE[i].thread = me;
            return &BLK_TABLE[i];
        }
    }
    return &BLK_TABLE[BLK_SLOTS - 1];
}
#define BLK_ENTER(id, ob) volatile blk_t *blk_me = blk_slot(); blk_me->op = (id); blk_me->obj = (uint32_t) (ob); blk_me->count++;
#define BLK_LEAVE()       blk_me->op = 0;

/* ---- semaphores, mutexes ---- */

static void *osi_semphr_create(uint32_t max, uint32_t init)
{
    return dmosi_semaphore_create(init, max);
}

static void osi_semphr_delete(void *s)
{
    dmosi_semaphore_destroy(s);
}

static int32_t osi_semphr_take(void *s, uint32_t ticks)
{
    BLK_ENTER(2, s);
    int r = dmosi_semaphore_wait(s, 1, timeout_ms(ticks));
    BLK_LEAVE();
    return r == 0 ? 1 : 0;
}

volatile uint32_t g_osi_fail_sem_give, g_osi_fail_queue_send, g_osi_queue_sent, g_osi_sem_gave;

static int32_t osi_semphr_give(void *s)
{
    int r = dmosi_semaphore_post(s, 1);
    g_osi_sem_gave++;
    if (r != 0)
        g_osi_fail_sem_give++;
    return r == 0 ? 1 : 0;
}

/* a semaphore of every thread that asks (a few threads only) */
#define THREAD_SEMS     8
static struct { dmosi_thread_t thread; dmosi_semaphore_t sem; } g_thread_sems[THREAD_SEMS];

static void *osi_thread_semphr_get(void)
{
    dmosi_thread_t me = dmosi_thread_current();
    uint32_t ps = cpu_irq_disable();
    int free_slot = -1;
    for (int i = 0; i < THREAD_SEMS; i++)
    {
        if (g_thread_sems[i].thread == me)
        {
            void *s = g_thread_sems[i].sem;
            cpu_irq_restore(ps);
            return s;
        }
        if (g_thread_sems[i].thread == NULL && free_slot < 0)
            free_slot = i;
    }
    cpu_irq_restore(ps);
    dmosi_semaphore_t s = dmosi_semaphore_create(0, 1);
    if (s == NULL || free_slot < 0)
        return s;
    ps = cpu_irq_disable();
    g_thread_sems[free_slot].thread = me;
    g_thread_sems[free_slot].sem = s;
    cpu_irq_restore(ps);
    return s;
}

static void *osi_mutex_create(void)
{
    return dmosi_mutex_create(true);
}

static void *osi_recursive_mutex_create(void)
{
    return dmosi_mutex_create(true);
}

static void osi_mutex_delete(void *m)
{
    dmosi_mutex_destroy(m);
}

static int32_t osi_mutex_lock(void *m)
{
    BLK_ENTER(3, m);
    int r = dmosi_mutex_lock(m);
    BLK_LEAVE();
    return r == 0 ? 1 : 0;
}

static int32_t osi_mutex_unlock(void *m)
{
    return dmosi_mutex_unlock(m) == 0 ? 1 : 0;
}

/* ---- queues (with the count of the messages: dmosi has no call for it) ---- */

typedef struct
{
    dmosi_queue_t   queue;
    volatile uint32_t waiting;
} osi_queue_t;

static void *osi_queue_create(uint32_t length, uint32_t item_size)
{
    osi_queue_t *q = Dmod_Malloc(sizeof(*q));
    if (q == NULL)
        return NULL;
    q->waiting = 0;
    q->queue = dmosi_queue_create(item_size, length);
    if (q->queue == NULL)
    {
        Dmod_Free(q);
        return NULL;
    }
    return q;
}

static void osi_queue_delete(void *queue)
{
    osi_queue_t *q = queue;
    if (q != NULL)
    {
        dmosi_queue_destroy(q->queue);
        Dmod_Free(q);
    }
}

static int32_t osi_queue_send(void *queue, void *item, uint32_t ticks)
{
    osi_queue_t *q = queue;
    g_osi_queue_sent++;
    if (dmosi_queue_send(q->queue, item, timeout_ms(ticks)) != 0)
    {
        g_osi_fail_queue_send++;
        return 0;
    }
    uint32_t ps = cpu_irq_disable();
    q->waiting++;
    cpu_irq_restore(ps);
    return 1;
}

static int32_t osi_queue_send_from_isr(void *queue, void *item, void *woken)
{
    if (woken != NULL)
        *(int32_t *) woken = 0;
    return osi_queue_send(queue, item, 0);
}

static int32_t osi_queue_recv(void *queue, void *item, uint32_t ticks)
{
    osi_queue_t *q = queue;
    BLK_ENTER(1, q);
    int r = dmosi_queue_receive(q->queue, item, timeout_ms(ticks));
    BLK_LEAVE();
    if (r != 0)
        return 0;
    uint32_t ps = cpu_irq_disable();
    if (q->waiting > 0)
        q->waiting--;
    cpu_irq_restore(ps);
    return 1;
}

static uint32_t osi_queue_msg_waiting(void *queue)
{
    return ((osi_queue_t *) queue)->waiting;
}

/* the queue of wifi_create_queue(): the libraries read ->handle themselves (see esp_adapter.c of ESP-IDF) */
typedef struct
{
    void *handle;
    void *storage;
} wifi_static_queue_t;

static void *osi_wifi_create_queue(int length, int item_size)
{
    wifi_static_queue_t *q = Dmod_Malloc(sizeof(*q));
    if (q == NULL)
        return NULL;
    q->storage = NULL;
    q->handle = osi_queue_create((uint32_t) length, (uint32_t) item_size);
    if (q->handle == NULL)
    {
        Dmod_Free(q);
        return NULL;
    }
    return q;
}

static void osi_wifi_delete_queue(void *queue)
{
    wifi_static_queue_t *q = queue;
    if (q != NULL)
    {
        osi_queue_delete(q->handle);
        Dmod_Free(q);
    }
}

/* ---- event groups: few and slow - a mutex, the bits and a polling wait ---- */

typedef struct
{
    dmosi_mutex_t   lock;
    uint32_t        bits;
} osi_event_t;

static void *osi_event_group_create(void)
{
    osi_event_t *e = Dmod_Malloc(sizeof(*e));
    if (e == NULL)
        return NULL;
    e->bits = 0;
    e->lock = dmosi_mutex_create(false);
    if (e->lock == NULL)
    {
        Dmod_Free(e);
        return NULL;
    }
    return e;
}

static void osi_event_group_delete(void *event)
{
    osi_event_t *e = event;
    if (e != NULL)
    {
        dmosi_mutex_destroy(e->lock);
        Dmod_Free(e);
    }
}

static uint32_t osi_event_group_set(void *event, uint32_t bits)
{
    osi_event_t *e = event;
    dmosi_mutex_lock(e->lock);
    e->bits |= bits;
    uint32_t now = e->bits;
    dmosi_mutex_unlock(e->lock);
    return now;
}

static uint32_t osi_event_group_clear(void *event, uint32_t bits)
{
    osi_event_t *e = event;
    dmosi_mutex_lock(e->lock);
    uint32_t before = e->bits;
    e->bits &= ~bits;
    dmosi_mutex_unlock(e->lock);
    return before;
}

static uint32_t osi_event_group_wait(void *event, uint32_t bits, int clear, int all, uint32_t ticks)
{
    osi_event_t *e = event;
    uint32_t waited = 0;
    BLK_ENTER(5, e);
    for (;;)
    {
        dmosi_mutex_lock(e->lock);
        uint32_t now = e->bits;
        bool met = all ? ((now & bits) == bits) : ((now & bits) != 0);
        if (met && clear)
            e->bits &= ~bits;
        dmosi_mutex_unlock(e->lock);
        if (met || (ticks != TICKS_FOREVER && waited >= ticks))
        {
            BLK_LEAVE();
            return now;
        }
        dmosi_thread_sleep(5);
        waited += 5;
    }
}

/* ---- tasks ---- */

typedef struct
{
    void (*entry)(void *);
    void *param;
    dmosi_thread_t thread;
} osi_task_t;

#define TASKS_KEPT      8
static osi_task_t *g_tasks[TASKS_KEPT];

/* "tasks, KB of their stacks, KB of the deepest use" for the debugging line of `wifi info` */
void osi_task_stats(uint32_t *count, uint32_t *total_kb, uint32_t *peak_kb)
{
    *count = *total_kb = *peak_kb = 0;
    for (int i = 0; i < TASKS_KEPT; i++)
    {
        dmosi_thread_info_t info;
        if (g_tasks[i] != NULL && dmosi_thread_get_info(g_tasks[i]->thread, &info) == 0)
        {
            (*count)++;
            *total_kb += (uint32_t) (info.stack_total >> 10);
            if ((uint32_t) (info.stack_peak >> 10) > *peak_kb)
                *peak_kb = (uint32_t) (info.stack_peak >> 10);
        }
    }
}

static void osi_task_main(void *arg)
{
    osi_task_t *task = arg;
    task->entry(task->param);
}

static int32_t osi_task_create(void *entry, const char *name, uint32_t stack_bytes, void *param, uint32_t priority, void *handle)
{
    osi_task_t *task = Dmod_Malloc(sizeof(*task));
    if (task == NULL)
        return 0;
    /* the libraries ask for the stacks of IDF (bytes) and the priorities of IDF; these tasks print and run on a heavier system */
    uint32_t bytes = (stack_bytes * 2u > 8192u) ? stack_bytes * 2u : 8192u;
    int prio = (g_ctx != NULL) ? g_ctx->priority : DMESPWIFI_DEFAULT_PRIORITY;
    if ((int) priority < prio)
        prio = (int) priority;
    task->entry = (void (*)(void *)) entry;
    task->param = param;
    task->thread = dmosi_thread_create(osi_task_main, task, prio, bytes, name, (g_ctx != NULL) ? g_ctx->process : NULL);

    if (task->thread == NULL)
    {
        Dmod_Free(task);
        return 0;
    }
    for (int i = 0; i < TASKS_KEPT; i++)
    {
        if (g_tasks[i] == NULL)
        {
            g_tasks[i] = task;
            break;
        }
    }
    if (handle != NULL)
        *(void **) handle = task;
    return 1;
}

static int32_t osi_task_create_pinned(void *entry, const char *name, uint32_t stack, void *param, uint32_t priority, void *handle,
                                      uint32_t core)
{
    (void) core;
    return osi_task_create(entry, name, stack, param, priority, handle);
}

static void osi_task_delete(void *handle)
{
    osi_task_t *task = handle;
    dmosi_thread_t thread = (task != NULL) ? task->thread : dmosi_thread_current();
    dmosi_thread_kill(thread, 0);
}

static void osi_task_delay(uint32_t ticks)
{
    BLK_ENTER(4, ticks);
    dmosi_thread_sleep(ticks);
    BLK_LEAVE();
}

static int32_t osi_task_ms_to_tick(uint32_t ms)
{
    return (int32_t) ms;
}

/* the handle of a task is what task_create gave back (the libraries compare the two to see that they run in the task of the Wi-Fi) */
static void *osi_task_current(void)
{
    dmosi_thread_t current = dmosi_thread_current();
    for (int i = 0; i < TASKS_KEPT; i++)
    {
        if (g_tasks[i] != NULL && g_tasks[i]->thread == current)
            return g_tasks[i];
    }
    return current;
}

static int32_t osi_task_max_priority(void)
{
    return (g_ctx != NULL) ? g_ctx->priority + 2 : DMESPWIFI_DEFAULT_PRIORITY + 2;   /* the libraries take a place below the top of it */
}

/* ---- timers: the ETSTimer of the libraries, kept in a list of our own and run by a thread of our own ----
 *
 * Not the timers of dmosi: those are commands in the queue of the timer task of FreeRTOS, and the libraries arm timers from their timer
 * callbacks (several at once) and with their locks held - the queue fills up, the timer task waits for itself and everything that arms a
 * timer after it (the task of the Wi-Fi) waits for ever. Here arming is a change of a list, like in the esp_timer of ESP-IDF. */

typedef struct ets_timer
{
    struct ets_timer *next;         /* in g_timers (sorted by `expire`), NULL when it is not armed or the last one */
    uint32_t expire;                /* ms of radio_time_us(), wraps */
    uint32_t period;                /* ms, 0: once */
    void (*func)(void *);
    void *arg;
} ets_timer_t;

volatile uint32_t g_osi_timer_armed, g_osi_timer_fired, g_osi_timer_created, g_osi_timer_create_failed;

static ets_timer_t *g_timers;
static dmosi_semaphore_t g_timer_wake;
static dmosi_thread_t g_timer_thread;

static inline uint32_t timer_now(void)
{
    return (uint32_t) (radio_time_us() / 1000u);
}

/* (interrupts off) */
static void timer_unlink(ets_timer_t *timer)
{
    for (ets_timer_t **at = &g_timers; *at != NULL; at = &(*at)->next)
    {
        if (*at == timer)
        {
            *at = timer->next;
            break;
        }
    }
    timer->next = NULL;
}

/* (interrupts off) true: it is the first to expire now */
static bool timer_link(ets_timer_t *timer)
{
    ets_timer_t **at = &g_timers;
    while (*at != NULL && (int32_t) ((*at)->expire - timer->expire) <= 0)
        at = &(*at)->next;
    timer->next = *at;
    *at = timer;
    return at == &g_timers;
}

static void timer_main(void *arg)
{
    (void) arg;
    for (;;)
    {
        int32_t wait = 1000;
        uint32_t ps = cpu_irq_disable();
        for (;;)
        {
            ets_timer_t *timer = g_timers;
            if (timer == NULL)
                break;
            int32_t left = (int32_t) (timer->expire - timer_now());
            if (left > 0)
            {
                wait = left < wait ? left : wait;
                break;
            }
            g_timers = timer->next;
            timer->next = NULL;
            if (timer->period != 0)
            {
                timer->expire += timer->period;
                if ((int32_t) (timer->expire - timer_now()) <= 0)
                    timer->expire = timer_now() + timer->period;
                timer_link(timer);
            }
            void (*func)(void *) = timer->func;
            void *func_arg = timer->arg;
            cpu_irq_restore(ps);
            g_osi_timer_fired++;
            if (func != NULL)
                func(func_arg);
            ps = cpu_irq_disable();
        }
        cpu_irq_restore(ps);
        dmosi_semaphore_wait(g_timer_wake, 1, wait);
    }
}

static bool timer_init(void)
{
    if (g_timer_thread != NULL)
        return true;
    if (g_timer_wake == NULL)
        g_timer_wake = dmosi_semaphore_create(0, 1);
    if (g_timer_wake == NULL)
        return false;
    g_timer_thread = dmosi_thread_create(timer_main, NULL, (g_ctx != NULL) ? g_ctx->priority : DMESPWIFI_DEFAULT_PRIORITY, 8192,
                                         "wifi-timer", (g_ctx != NULL) ? g_ctx->process : NULL);
    return g_timer_thread != NULL;
}

static void osi_timer_setfn(void *ptimer, void *func, void *arg)
{
    ets_timer_t *timer = ptimer;
    uint32_t ps = cpu_irq_disable();
    timer_unlink(timer);
    timer->func = (void (*)(void *)) func;
    timer->arg = arg;
    cpu_irq_restore(ps);
}

static void osi_timer_arm_ms(void *ptimer, uint32_t ms, bool repeat)
{
    ets_timer_t *timer = ptimer;
    if (ms == 0)
        ms = 1;
    g_osi_timer_armed++;
    uint32_t ps = cpu_irq_disable();
    timer_unlink(timer);
    timer->expire = timer_now() + ms;
    timer->period = repeat ? ms : 0;
    bool first = timer_link(timer);
    cpu_irq_restore(ps);
    if (first && g_timer_wake != NULL)
        dmosi_semaphore_post(g_timer_wake, 1);
}

static void osi_timer_arm(void *ptimer, uint32_t ms, bool repeat)
{
    osi_timer_arm_ms(ptimer, ms, repeat);
}

static void osi_timer_arm_us(void *ptimer, uint32_t us, bool repeat)
{
    osi_timer_arm_ms(ptimer, (us + 999u) / 1000u, repeat);
}

static void osi_timer_disarm(void *ptimer)
{
    uint32_t ps = cpu_irq_disable();
    timer_unlink(ptimer);
    cpu_irq_restore(ps);
}

static void osi_timer_done(void *ptimer)
{
    osi_timer_disarm(ptimer);
}

/* ---- the pool: internal RAM only for the radio (the MAC reaches its buffers by DMA; the heaps of the firmware are used up by then) ---- */

#define POOL_BASE       0x3fcd0000u         /* behind the DMA heap of dmod-boot; 0x3fcd0000..0x3fce0000 is nobody else's */
#define POOL_SIZE       0xFFC0u
#define POOL_ALIGN      16u

typedef struct pool_block { uint32_t size; uint32_t used; } pool_block_t;   /* 8 bytes + 8 of padding: the data is 16-aligned */

static bool g_pool_ready;
static uint32_t g_pool_free_min = POOL_SIZE;

static inline bool pool_owns(const void *p)
{
    return (uintptr_t) p >= POOL_BASE && (uintptr_t) p < POOL_BASE + POOL_SIZE;
}

static void pool_init(void)
{
    pool_block_t *b = (pool_block_t *) POOL_BASE;
    b->size = POOL_SIZE - 16u;
    b->used = 0;
    g_pool_ready = true;
}

static void *pool_alloc(size_t size)
{
    if (!g_pool_ready)
        pool_init();
    size = (size + POOL_ALIGN - 1u) & ~(POOL_ALIGN - 1u);
    uint32_t pos = 0;
    while (pos < POOL_SIZE)
    {
        pool_block_t *b = (pool_block_t *) (POOL_BASE + pos);
        if (!b->used && b->size >= size)
        {
            if (b->size >= size + 32u)
            {
                pool_block_t *n = (pool_block_t *) (POOL_BASE + pos + 16u + size);
                n->size = b->size - size - 16u;
                n->used = 0;
                b->size = (uint32_t) size;
            }
            b->used = 1;
            return (void *) (POOL_BASE + pos + 16u);
        }
        pos += 16u + b->size;
    }
    return NULL;
}

static void pool_free(void *p)
{
    pool_block_t *b = (pool_block_t *) ((uintptr_t) p - 16u);
    b->used = 0;
    uint32_t pos = 0;                       /* join the neighbours that are free */
    while (pos < POOL_SIZE)
    {
        pool_block_t *c = (pool_block_t *) (POOL_BASE + pos);
        uint32_t next = pos + 16u + c->size;
        if (!c->used && next < POOL_SIZE)
        {
            pool_block_t *d = (pool_block_t *) (POOL_BASE + next);
            if (!d->used)
            {
                c->size += 16u + d->size;
                continue;
            }
        }
        pos = next;
    }
}

void *osi_pool_alloc(size_t size)
{
    return pool_alloc(size);
}

uint32_t osi_pool_stats(uint32_t *largest)
{
    uint32_t total = 0, big = 0, pos = 0;
    if (!g_pool_ready)
        pool_init();
    while (pos < POOL_SIZE)
    {
        pool_block_t *c = (pool_block_t *) (POOL_BASE + pos);
        if (!c->used)
        {
            total += c->size;
            if (c->size > big)
                big = c->size;
        }
        pos += 16u + c->size;
    }
    *largest = big;
    (void) g_pool_free_min;
    return total;
}

/* ---- memory ---- */

static void *osi_malloc(size_t size)
{
    return Dmod_Malloc(size);
}

static void osi_free(void *p)
{
    if (pool_owns(p))
        pool_free(p);
    else
        Dmod_Free(p);
}

static void *osi_realloc(void *p, size_t size)
{
    if (p != NULL && pool_owns(p))
    {
        pool_block_t *b = (pool_block_t *) ((uintptr_t) p - 16u);
        void *n = pool_alloc(size);
        if (n != NULL)
        {
            memcpy(n, p, b->size < size ? b->size : size);
            pool_free(p);
        }
        return n;
    }
    return Dmod_Realloc(p, size);
}

static void *osi_calloc(size_t n, size_t size)
{
    void *p = Dmod_Malloc(n * size);
    if (p != NULL)
        memset(p, 0, n * size);
    return p;
}

static void *osi_zalloc(size_t size)
{
    return osi_calloc(1, size);
}

/* what the MAC reaches by DMA (the descriptors, the buffers of the frames) is in the internal RAM, not in the PSRAM: the DMA heap first */
static volatile uint32_t g_internal_ok, g_internal_bad, g_internal_bad_bytes;

static void *osi_internal_malloc(size_t size)
{
    void *p = pool_alloc(size);
    if (p != NULL)
        g_internal_ok++;
    else
    {
        g_internal_bad++;
        g_internal_bad_bytes += (uint32_t) size;
    }
    return p;
}

void osi_alloc_stats(uint32_t *ok, uint32_t *bad, uint32_t *bad_bytes)
{
    *ok = g_internal_ok;
    *bad = g_internal_bad;
    *bad_bytes = g_internal_bad_bytes;
}

static void *osi_internal_calloc(size_t n, size_t size)
{
    void *p = osi_internal_malloc(n * size);
    if (p != NULL)
        memset(p, 0, n * size);
    return p;
}

static void *osi_internal_zalloc(size_t size)
{
    return osi_internal_calloc(1, size);
}

static uint32_t osi_free_heap(void)
{
    return 64u * 1024u;
}

/* ---- the rest ---- */

static void osi_empty(void) { }
static bool osi_env_is_chip(void) { return true; }
static void osi_clear_intr(uint32_t source, uint32_t number) { (void) source; (void) number; }
static int osi_zero(void) { return 0; }
static void osi_nothing(void) { }
static int osi_nvs_fail(void) { return -1; }

static int osi_nvs_open(const char *name, unsigned int mode, uint32_t *handle)
{
    (void) name; (void) mode; (void) handle;
    return -1;                                          /* (the libraries are told not to use it: nvs_enable = 0) */
}

static void osi_nvs_close(uint32_t handle)
{
    (void) handle;
}

static int osi_phy_update_country(const char *country) { (void) country; return 0; }

static int osi_read_mac(uint8_t *mac, unsigned int type)
{
    (void) type;
    radio_base_mac(mac);
    return 0;
}

static int32_t osi_event_post(const char *base, int32_t id, void *data, size_t size, uint32_t ticks)
{
    (void) ticks;
    return wifi_event(base, id, data, size);
}

static uint32_t osi_rand(void) { return radio_random(); }
static unsigned long osi_random(void) { return radio_random(); }

static int osi_get_random(uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++)
        buf[i] = (uint8_t) radio_random();
    return 0;
}

static int osi_get_time(void *t) { (void) t; return -1; }
static uint32_t osi_slowclk_cal(void) { return 0; }
static int64_t osi_time_us(void) { return (int64_t) radio_time_us(); }
static uint32_t osi_log_timestamp(void) { return (uint32_t) (radio_time_us() / 1000u); }

static void osi_log_writev(unsigned int level, const char *tag, const char *format, va_list args)
{
    (void) level;
    char line[160];
    Dmod_VSnPrintf(line, sizeof(line), format, args);
    radio_log("%s: %s\n", tag, line);
}

static void osi_log_write(unsigned int level, const char *tag, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    osi_log_writev(level, tag, format, args);
    va_end(args);
}

void osi_fill(wifi_osi_funcs_t *o)
{
    memset(o, 0, sizeof(*o));
    timer_init();
    o->_version = ESP_WIFI_OS_ADAPTER_VERSION;
    o->_env_is_chip = osi_env_is_chip;
    o->_set_intr = osi_set_intr;
    o->_clear_intr = osi_clear_intr;
    o->_set_isr = osi_set_isr;
    o->_ints_on = cpu_ints_on;
    o->_ints_off = cpu_ints_off;
    o->_is_from_isr = osi_is_from_isr;
    o->_spin_lock_create = osi_spin_lock_create;
    o->_spin_lock_delete = osi_spin_lock_delete;
    o->_wifi_int_disable = osi_int_disable;
    o->_wifi_int_restore = osi_int_restore;
    o->_task_yield_from_isr = osi_yield_from_isr;
    o->_semphr_create = osi_semphr_create;
    o->_semphr_delete = osi_semphr_delete;
    o->_semphr_take = osi_semphr_take;
    o->_semphr_give = osi_semphr_give;
    o->_wifi_thread_semphr_get = osi_thread_semphr_get;
    o->_mutex_create = osi_mutex_create;
    o->_recursive_mutex_create = osi_recursive_mutex_create;
    o->_mutex_delete = osi_mutex_delete;
    o->_mutex_lock = osi_mutex_lock;
    o->_mutex_unlock = osi_mutex_unlock;
    o->_queue_create = osi_queue_create;
    o->_queue_delete = osi_queue_delete;
    o->_queue_send = osi_queue_send;
    o->_queue_send_from_isr = osi_queue_send_from_isr;
    o->_queue_send_to_back = osi_queue_send;
    o->_queue_send_to_front = osi_queue_send;
    o->_queue_recv = osi_queue_recv;
    o->_queue_msg_waiting = osi_queue_msg_waiting;
    o->_event_group_create = osi_event_group_create;
    o->_event_group_delete = osi_event_group_delete;
    o->_event_group_set_bits = osi_event_group_set;
    o->_event_group_clear_bits = osi_event_group_clear;
    o->_event_group_wait_bits = osi_event_group_wait;
    o->_task_create_pinned_to_core = osi_task_create_pinned;
    o->_task_create = osi_task_create;
    o->_task_delete = osi_task_delete;
    o->_task_delay = osi_task_delay;
    o->_task_ms_to_tick = osi_task_ms_to_tick;
    o->_task_get_current_task = osi_task_current;
    o->_task_get_max_priority = osi_task_max_priority;
    o->_malloc = osi_malloc;
    o->_free = osi_free;
    o->_event_post = osi_event_post;
    o->_get_free_heap_size = osi_free_heap;
    o->_rand = osi_rand;
    o->_dport_access_stall_other_cpu_start_wrap = osi_empty;
    o->_dport_access_stall_other_cpu_end_wrap = osi_empty;
    o->_wifi_apb80m_request = osi_empty;
    o->_wifi_apb80m_release = osi_empty;
    o->_phy_disable = radio_phy_disable;
    o->_phy_enable = radio_phy_enable;
    o->_phy_update_country_info = osi_phy_update_country;
    o->_read_mac = osi_read_mac;
    o->_timer_arm = osi_timer_arm;
    o->_timer_disarm = osi_timer_disarm;
    o->_timer_done = osi_timer_done;
    o->_timer_setfn = osi_timer_setfn;
    o->_timer_arm_us = osi_timer_arm_us;
    o->_wifi_reset_mac = osi_empty;
    o->_wifi_clock_enable = osi_empty;
    o->_wifi_clock_disable = osi_empty;
    o->_wifi_rtc_enable_iso = osi_empty;
    o->_wifi_rtc_disable_iso = osi_empty;
    o->_esp_timer_get_time = osi_time_us;
    o->_nvs_set_i8 = (int (*)(uint32_t, const char *, int8_t)) osi_nvs_fail;
    o->_nvs_get_i8 = (int (*)(uint32_t, const char *, int8_t *)) osi_nvs_fail;
    o->_nvs_set_u8 = (int (*)(uint32_t, const char *, uint8_t)) osi_nvs_fail;
    o->_nvs_get_u8 = (int (*)(uint32_t, const char *, uint8_t *)) osi_nvs_fail;
    o->_nvs_set_u16 = (int (*)(uint32_t, const char *, uint16_t)) osi_nvs_fail;
    o->_nvs_get_u16 = (int (*)(uint32_t, const char *, uint16_t *)) osi_nvs_fail;
    o->_nvs_open = osi_nvs_open;
    o->_nvs_close = osi_nvs_close;
    o->_nvs_commit = (int (*)(uint32_t)) osi_nvs_fail;
    o->_nvs_set_blob = (int (*)(uint32_t, const char *, const void *, size_t)) osi_nvs_fail;
    o->_nvs_get_blob = (int (*)(uint32_t, const char *, void *, size_t *)) osi_nvs_fail;
    o->_nvs_erase_key = (int (*)(uint32_t, const char *)) osi_nvs_fail;
    o->_get_random = osi_get_random;
    o->_get_time = osi_get_time;
    o->_random = osi_random;
    o->_slowclk_cal_get = osi_slowclk_cal;
    o->_log_write = osi_log_write;
    o->_log_writev = osi_log_writev;
    o->_log_timestamp = osi_log_timestamp;
    o->_malloc_internal = osi_internal_malloc;
    o->_realloc_internal = osi_realloc;
    o->_calloc_internal = osi_internal_calloc;
    o->_zalloc_internal = osi_internal_zalloc;
    o->_wifi_malloc = osi_internal_malloc;
    o->_wifi_realloc = osi_realloc;
    o->_wifi_calloc = osi_internal_calloc;
    o->_wifi_zalloc = osi_internal_zalloc;
    o->_wifi_create_queue = osi_wifi_create_queue;
    o->_wifi_delete_queue = osi_wifi_delete_queue;
    /* no coexistence (no Bluetooth): the calls agree */
    o->_coex_init = osi_zero;
    o->_coex_deinit = osi_nothing;
    o->_coex_enable = osi_zero;
    o->_coex_disable = osi_nothing;
    o->_coex_status_get = (uint32_t (*)(void)) osi_zero;
    o->_coex_condition_set = (void (*)(uint32_t, bool)) osi_nothing;
    o->_coex_wifi_request = (int (*)(uint32_t, uint32_t, uint32_t)) osi_zero;
    o->_coex_wifi_release = (int (*)(uint32_t)) osi_zero;
    o->_coex_wifi_channel_set = (int (*)(uint8_t, uint8_t)) osi_zero;
    o->_coex_event_duration_get = (int (*)(uint32_t, uint32_t *)) osi_zero;
    o->_coex_pti_get = (int (*)(uint32_t, uint8_t *)) osi_zero;
    o->_coex_schm_status_bit_clear = (void (*)(uint32_t, uint32_t)) osi_nothing;
    o->_coex_schm_status_bit_set = (void (*)(uint32_t, uint32_t)) osi_nothing;
    o->_coex_schm_interval_set = (int (*)(uint32_t)) osi_zero;
    o->_coex_schm_interval_get = (uint32_t (*)(void)) osi_zero;
    o->_coex_schm_curr_period_get = (uint8_t (*)(void)) osi_zero;
    o->_coex_schm_curr_phase_get = (void *(*)(void)) osi_zero;
    o->_coex_schm_process_restart = osi_zero;
    o->_coex_schm_register_cb = (int (*)(int, int (*)(int))) osi_zero;
    o->_coex_register_start_cb = (int (*)(int (*)(void))) osi_zero;
    o->_magic = (int32_t) ESP_WIFI_OS_ADAPTER_MAGIC;
}
