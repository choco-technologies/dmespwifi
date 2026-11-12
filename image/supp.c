/* What the supplicant of hostap (esp_supplicant/, rsn_supp/, crypto/ of ESP-IDF, the station only) needs of its surroundings: the memory, the
 * time, the random numbers and the timers of the event loop - all of them from the table of the host. */
#include "dmespradio.h"
#include <string.h>

wifi_osi_funcs_t g_wifi_osi_funcs;

typedef struct { long sec; long usec; } os_time_t_pair;

void *malloc(size_t size)                   { return g_wifi_osi_funcs._wifi_malloc(size); }
void *calloc(size_t n, size_t size)         { return g_wifi_osi_funcs._wifi_calloc(n, size); }
void *realloc(void *p, size_t size)         { return g_wifi_osi_funcs._wifi_realloc(p, size); }

void __assert_func(const char *file, int line, const char *func, const char *expr)
{
    (void) file; (void) line; (void) func; (void) expr;
    for (;;)
    { }
}

int os_get_time(os_time_t_pair *t)
{
    int64_t us = g_wifi_osi_funcs._esp_timer_get_time();
    t->sec = (long) (us / 1000000);
    t->usec = (long) (us % 1000000);
    return 0;
}

int os_get_random(unsigned char *buf, size_t len)
{
    return g_wifi_osi_funcs._get_random(buf, len);
}

void os_sleep(long sec, long usec)
{
    g_wifi_osi_funcs._task_delay((uint32_t) (sec * 1000 + usec / 1000));
}

/* ---- the event loop: only the timeouts ---- */

typedef void (*eloop_timeout_handler)(void *eloop_ctx, void *user_ctx);

typedef struct timeout
{
    struct timeout *next;
    eloop_timeout_handler handler;
    void *eloop_data, *user_data;
    uint8_t timer[64];                      /* ets_timer_t of the host */
} timeout_t;

static timeout_t *g_timeouts;

int eloop_init(void)
{
    g_timeouts = NULL;
    return 0;
}

static void timeout_fire(void *arg)
{
    timeout_t *t = arg, **p = &g_timeouts;
    while (*p != NULL && *p != t)
        p = &(*p)->next;
    if (*p == NULL)
        return;
    *p = t->next;
    eloop_timeout_handler handler = t->handler;
    void *a = t->eloop_data, *b = t->user_data;
    g_wifi_osi_funcs._timer_disarm(t->timer);
    g_wifi_osi_funcs._timer_done(t->timer);
    g_wifi_osi_funcs._free(t);
    handler(a, b);
}

int eloop_register_timeout(unsigned int secs, unsigned int usecs, eloop_timeout_handler handler, void *eloop_data, void *user_data)
{
    timeout_t *t = g_wifi_osi_funcs._wifi_zalloc(sizeof(*t));
    if (t == NULL)
        return -1;
    t->handler = handler;
    t->eloop_data = eloop_data;
    t->user_data = user_data;
    t->next = g_timeouts;
    g_timeouts = t;
    g_wifi_osi_funcs._timer_setfn(t->timer, timeout_fire, t);
    g_wifi_osi_funcs._timer_arm(t->timer, secs * 1000u + usecs / 1000u + 1u, false);
    return 0;
}

#define ELOOP_ALL_CTX   ((void *) -1)

int eloop_cancel_timeout(eloop_timeout_handler handler, void *eloop_data, void *user_data)
{
    int removed = 0;
    timeout_t **p = &g_timeouts;
    while (*p != NULL)
    {
        timeout_t *t = *p;
        if (t->handler == handler && (t->eloop_data == eloop_data || eloop_data == ELOOP_ALL_CTX) &&
            (t->user_data == user_data || user_data == ELOOP_ALL_CTX))
        {
            *p = t->next;
            g_wifi_osi_funcs._timer_disarm(t->timer);
            g_wifi_osi_funcs._timer_done(t->timer);
            g_wifi_osi_funcs._free(t);
            removed++;
        }
        else
            p = &t->next;
    }
    return removed;
}

void eloop_destroy(void)
{
    while (g_timeouts != NULL)
        eloop_cancel_timeout(g_timeouts->handler, ELOOP_ALL_CTX, ELOOP_ALL_CTX);
}
