/* What the supplicant of hostap (esp_supplicant/, rsn_supp/, crypto/ of ESP-IDF, the station only) needs of its surroundings: the memory, the
 * time, the random numbers and the timers of the event loop - all of them from the table of the host. */
#include "dmwifi_image.h"
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

/* ---- a trace of the supplicant (the linker wraps these: the names and the results, never a key or a passphrase) ---- */

extern int dmwifi_log(const char *format, ...);

#define TRACE_WRAP(ret, name, params, args, fmt, ...) \
    extern ret __real_##name params; \
    ret __wrap_##name params { dmwifi_log("supp: " #name "...\n"); ret r = __real_##name args; dmwifi_log("supp: " #name " -> " fmt "\n", (int) r); return r; }

TRACE_WRAP(int, wpa_set_profile, (uint32_t wpa_proto, uint8_t auth_mode), (wpa_proto, auth_mode), "%d")
TRACE_WRAP(int, esp_wifi_sta_connect_internal, (const uint8_t *bssid), (bssid), "%d")
TRACE_WRAP(int, wpa_sm_rx_eapol, (uint8_t *src, uint8_t *buf, uint32_t len), (src, buf, len), "%d")
TRACE_WRAP(int, wpa_set_bss, (char *macddr, char *bssid, uint8_t pairwise_cipher, uint8_t group_cipher, char *passphrase, uint8_t *ssid, size_t ssid_len), (macddr, bssid, pairwise_cipher, group_cipher, passphrase, ssid, ssid_len), "%d")

void __real_wpa_neg_complete(void);
void __wrap_wpa_neg_complete(void) { dmwifi_log("supp: 4-way handshake complete\n"); __real_wpa_neg_complete(); }
void __real_wpa_deauthenticate(uint8_t reason);
void __wrap_wpa_deauthenticate(uint8_t reason) { dmwifi_log("supp: deauthenticate, reason %d\n", (int) reason); __real_wpa_deauthenticate(reason); }
void __real_wpa_install_key(int alg, uint8_t *addr, int key_idx, int set_tx, uint8_t *seq, size_t seq_len, uint8_t *key, size_t key_len, int key_flag);
void __wrap_wpa_install_key(int alg, uint8_t *addr, int key_idx, int set_tx, uint8_t *seq, size_t seq_len, uint8_t *key, size_t key_len, int key_flag)
{
    dmwifi_log("supp: install key alg %d idx %d tx %d len %d\n", alg, key_idx, set_tx, (int) key_len);
    __real_wpa_install_key(alg, addr, key_idx, set_tx, seq, seq_len, key, key_len, key_flag);
}

#define TRACE_GENERIC(name) \
    extern uintptr_t __real_##name(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t); \
    uintptr_t __wrap_##name(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e, uintptr_t f) \
    { dmwifi_log("supp: > " #name "\n"); uintptr_t r = __real_##name(a, b, c, d, e, f); dmwifi_log("supp: < " #name " %d\n", (int) r); return r; }

TRACE_GENERIC(esp_wifi_skip_supp_pmkcaching)
TRACE_GENERIC(esp_wifi_get_config)
TRACE_GENERIC(esp_wifi_sta_pmf_enabled)
TRACE_GENERIC(esp_wifi_sta_get_mgmt_group_cipher)
TRACE_GENERIC(esp_wifi_get_config_sae_pwe_h2e_internal)
TRACE_GENERIC(esp_wifi_sta_is_ap_notify_completed_rsne_internal)
TRACE_GENERIC(pmksa_cache_clear_current)
TRACE_GENERIC(pmksa_cache_get)
TRACE_GENERIC(wpa_gen_wpa_ie)
TRACE_GENERIC(wpa_config_assoc_ie)
TRACE_GENERIC(esp_wifi_set_appie_internal)
