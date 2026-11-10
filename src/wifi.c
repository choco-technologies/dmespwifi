/*
 * The Wi-Fi of the node: the start of the radio (the order of esp_wifi_init() of ESP-IDF), the events of the libraries, the scan, the
 * connection that is kept, the frames.
 */
#include "private.h"
#define BLK6 (*(volatile uint32_t *) 0x600080C8u)
#include "dmheap.h"
#include <errno.h>
#include <string.h>

struct dmdrvi_context *g_ctx;

wifi_osi_funcs_t g_osi;             /* the libraries keep the pointer (and the supplicant of the image copies the table) */
static wifi_init_config_t g_config;

#define RECONNECT_DELAY_MS      2000
#define SCAN_MAX_RECORDS        24

/* WIFI_EVENT_* of esp_wifi_types.h */
enum
{
    EVENT_WIFI_READY = 0,
    EVENT_SCAN_DONE,
    EVENT_STA_START,
    EVENT_STA_STOP,
    EVENT_STA_CONNECTED,
    EVENT_STA_DISCONNECTED,
};

/* ---- the events of the libraries (they come from the task of the Wi-Fi) ---- */

static void state_set(struct dmdrvi_context *c, dmdrvi_wifi_state_t state)
{
    dmosi_mutex_lock(c->lock);
    c->state = state;
    dmosi_mutex_unlock(c->lock);
}

int wifi_event(const char *base, int32_t id, void *data, size_t size)
{
    struct dmdrvi_context *c = g_ctx;
    (void) base;
    (void) size;
    if (c == NULL)
        return 0;
    BLK6 = 13;
    radio_log("wifi: event %d\n", (int) id);
    switch (id)
    {
        case EVENT_SCAN_DONE:
            dmosi_semaphore_post(c->scan_done, 1);
            break;
        case EVENT_STA_START:
            dmosi_semaphore_post(c->wake, 1);
            break;
        case EVENT_STA_CONNECTED:
        {
            const wifi_event_sta_connected_t *e = data;
            dmosi_mutex_lock(c->lock);
            if (e != NULL)
            {
                memcpy(c->bssid, e->bssid, sizeof(c->bssid));
                c->channel = e->channel;
            }
            c->reason = 0;
            c->state = DMDRVI_WIFI_STATE_CONNECTED;
            dmosi_mutex_unlock(c->lock);
            radio_log("wifi: connected, channel %u\n", (unsigned) c->channel);
            break;
        }
        case EVENT_STA_DISCONNECTED:
        {
            const wifi_event_sta_disconnected_t *e = data;
            dmosi_mutex_lock(c->lock);
            c->reason = (e != NULL) ? e->reason : 0;
            c->state = c->want_connected ? DMDRVI_WIFI_STATE_CONNECTING : DMDRVI_WIFI_STATE_IDLE;
            dmosi_mutex_unlock(c->lock);
            radio_log("wifi: disconnected, reason %u\n", (unsigned) c->reason);
            dmosi_semaphore_post(c->wake, 1);       /* the control thread connects again */
            break;
        }
        default:
            break;
    }
    BLK6 = 0;
    return 0;
}

/* ---- frames ---- */

/* the first frames of the link, for the log: the type, the length, the UDP ports (no payload) */
static void trace_frame(const char *dir, const uint8_t *f, size_t len, int32_t result)
{
    static uint32_t count;
    if (count >= 60u || len < 14u)
        return;
    count++;
    unsigned type = ((unsigned) f[12] << 8) | f[13];
    unsigned sport = 0, dport = 0;
    if (type == 0x0800u && len >= 38u && f[23] == 17u)
    {
        size_t ihl = (size_t) (f[14] & 0x0Fu) * 4u;
        if (len >= 14u + ihl + 4u)
        {
            sport = ((unsigned) f[14u + ihl] << 8) | f[15u + ihl];
            dport = ((unsigned) f[16u + ihl] << 8) | f[17u + ihl];
        }
    }
    if ((sport == 67u || dport == 67u) && len >= 62u)
        radio_log("net: dhcp dst mac %02x:%02x:%02x:%02x:%02x:%02x src mac %02x:%02x:%02x:%02x:%02x:%02x ip %u.%u.%u.%u>%u.%u.%u.%u flags %02x%02x op %u\n",
                  f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9], f[10], f[11], f[26], f[27], f[28], f[29], f[30], f[31], f[32], f[33], f[52], f[53], f[42]);
    if (dport == 67u && len > 282u)
    {
        char opts[120];
        size_t n = 0;
        for (size_t i = 282u; i < len && n + 3u < sizeof(opts); i++)
            n += (size_t) Dmod_SnPrintf(opts + n, sizeof(opts) - n, "%02x", f[i]);
        radio_log("net: dhcp xid %02x%02x%02x%02x options %s\n", f[46], f[47], f[48], f[49], opts);
    }
    if (sport == 67u && len >= 62u)
        radio_log("net: dhcp from %u.%u.%u.%u offers %u.%u.%u.%u\n", f[26], f[27], f[28], f[29], f[58], f[59], f[60], f[61]);
    radio_log("net: %s type 0x%04x len %u udp %u>%u result %d\n", dir, type, (unsigned) len, sport, dport, (int) result);
}


int wifi_rx_frame(void *buffer, uint16_t length, void *eb)
{
    BLK6 = 12;
    struct dmdrvi_context *c = g_ctx;
    if (buffer != NULL)
        trace_frame(c != NULL && c->running ? "rx" : "rx (not running)", buffer, length, 0);
    if (c != NULL && c->running && buffer != NULL && length > 0 && length <= DMESPWIFI_FRAME_MAX)
    {
        dmosi_mutex_lock(c->lock);
        uint32_t next = (c->rx_head + 1u) % DMESPWIFI_RX_FRAMES;
        if (next != c->rx_tail)
        {
            frame_t *f = &c->rx_frames[c->rx_head];
            f->length = length;
            memcpy(f->data, buffer, length);
            c->rx_head = next;
            c->rx_count++;
            dmosi_mutex_unlock(c->lock);
            dmosi_semaphore_post(c->rx_ready, 1);
        }
        else
        {
            c->rx_dropped++;
            dmosi_mutex_unlock(c->lock);
        }
    }
    if (eb != NULL)
        g_api->esp_wifi_internal_free_rx_buffer(eb);
    BLK6 = 0;
    return 0;
}

int wifi_send_frame(struct dmdrvi_context *c, const void *frame, size_t size)
{
    if (size == 0 || size > DMESPWIFI_FRAME_MAX)
        return -EINVAL;
    dmosi_mutex_lock(c->lock);
    bool can = c->running && c->state == DMDRVI_WIFI_STATE_CONNECTED;
    dmosi_mutex_unlock(c->lock);
    {
        const uint8_t *fr = frame;
        if (size >= 32u && fr[12] == 0x08 && fr[13] == 0x06 && fr[28] == 0 && fr[29] == 0 && fr[30] == 0 && fr[31] == 0)
        {
            radio_log("net: ARP probe dropped (experiment)\n");
            return 0;
        }
    }
    int32_t ret = can ? g_api->esp_wifi_internal_tx(WIFI_IF_STA, (void *) frame, (uint16_t) size) : -1;
    trace_frame(can ? "tx" : "tx (link down)", frame, size, ret);
    dmosi_mutex_lock(c->lock);
    if (ret == 0)
        c->tx_count++;
    else
        c->tx_dropped++;
    dmosi_mutex_unlock(c->lock);
    return ret == 0 ? 0 : -EIO;
}

/* ---- the start (esp_wifi_init() and esp_wifi_start() of ESP-IDF, without what a FreeRTOS system with a lot of RAM adds) ---- */

/* WIFI_INIT_CONFIG_DEFAULT of ESP-IDF 5.2 for the ESP32-S3: no NVS (nothing is saved in the flash), the crypto table is set by the image */
static void config_fill(wifi_init_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->osi_funcs = &g_osi;
    memcpy(&cfg->wpa_crypto_funcs, g_api->crypto_funcs, sizeof(cfg->wpa_crypto_funcs));   /* what the image links: size, version, the functions */
    cfg->static_rx_buf_num = 10;
    cfg->dynamic_rx_buf_num = 32;
    cfg->tx_buf_type = 1;
    cfg->dynamic_tx_buf_num = 32;
    cfg->rx_mgmt_buf_num = 5;
    cfg->ampdu_rx_enable = 1;
    cfg->ampdu_tx_enable = 1;
    cfg->rx_ba_win = 6;
    cfg->beacon_max_len = 752;
    cfg->mgmt_sbuf_num = 32;
    cfg->espnow_max_encrypt_num = 7;
    cfg->magic = WIFI_INIT_CONFIG_MAGIC;
}

/* a look at the air (the headers of the data frames; no payload): what the AP sends us, also what the MAC then drops */
static void sniff(void *buffer, int type)
{
    static uint32_t count;
    const uint8_t *b = buffer;
    if (type != 2 || count >= 40u)
        return;
    const uint8_t *p = b + 48;
    if (!(p[10] == 0x62 && p[11] == 0x6f && p[12] == 0x81) && memcmp(p + 4, g_ctx->mac, 6) != 0)
        return;                                 /* only the phone's frames and the ones to us */
    count++;
    radio_log("air: state %u siglen %u fc %02x%02x a1 %02x:%02x:%02x:%02x:%02x:%02x a2 %02x:%02x:%02x:%02x:%02x:%02x\n", (unsigned) b[47], (unsigned) ((b[44] | (b[45] << 8)) & 0xFFFu), p[0], p[1], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11],
              p[12], p[13], p[14], p[15]);
}

static int radio_up(struct dmdrvi_context *c)
{
    if (!image_load())
        return -ENODEV;
    osi_fill(&g_osi);
    config_fill(&g_config);
    radio_base_mac(c->mac);
    radio_power_domain_on();
    g_api->esp_wifi_internal_set_log_level(c->log_level);
    int32_t ret = g_api->esp_wifi_init_internal(&g_config);
    if (ret != 0)
    {
        radio_log("wifi: esp_wifi_init_internal: 0x%x\n", (unsigned) ret);
        return -EIO;
    }
    ret = g_api->supplicant_init();
    if (ret == 0)
        ret = g_api->esp_wifi_internal_reg_rxcb(WIFI_IF_STA, (void *) wifi_rx_frame);
    if (ret == 0)
        ret = g_api->esp_wifi_set_mode(1 /* WIFI_MODE_STA */);
    if (ret == 0)
    {
        /* Europe: the channels 1-13 (the libraries start with 1-11 and would not see an access point on 12 or 13) */
        static const wifi_country_t country = { .cc = "PL", .schan = 1, .nchan = 13, .max_tx_power = 20, .policy = WIFI_COUNTRY_POLICY_MANUAL };
        int32_t cr = g_api->esp_wifi_set_country(&country);
        radio_log("wifi: country: %d\n", (int) cr);
        ret = g_api->esp_wifi_start();
        {
            static const uint32_t filter = 1u << 2;     /* WIFI_PROMIS_FILTER_MASK_DATA */
            g_api->esp_wifi_set_promiscuous_filter(&filter);
            g_api->esp_wifi_set_promiscuous_rx_cb(sniff);
            g_api->esp_wifi_set_promiscuous(true);
        }
        if (ret == 0)
            radio_log("wifi: power save off: %d\n", (int) g_api->esp_wifi_set_ps(0 /* WIFI_PS_NONE */));
    }
    if (ret != 0)
    {
        radio_log("wifi: the station did not start: 0x%x\n", (unsigned) ret);
        return -EIO;
    }
    return 0;
}

/* The thread of the radio: it starts it and keeps the connection (the libraries only tell that it was lost) */
static void control_main(void *arg)
{
    struct dmdrvi_context *c = arg;

    state_set(c, DMDRVI_WIFI_STATE_STARTING);
    int ret = radio_up(c);
    dmosi_mutex_lock(c->lock);
    c->started = (ret == 0);
    c->state = (ret == 0) ? DMDRVI_WIFI_STATE_IDLE : DMDRVI_WIFI_STATE_OFF;
    c->resets++;
    dmosi_mutex_unlock(c->lock);
    if (ret == 0)
        dmespwifi_radio_ready(c);

    while (ret == 0 && !c->stop)
    {
        dmosi_semaphore_wait(c->wake, 1, 1000);
        if (c->stop)
            break;
        dmosi_mutex_lock(c->lock);
        bool again = c->want_connected && c->state == DMDRVI_WIFI_STATE_CONNECTING;
        dmosi_mutex_unlock(c->lock);
        if (again)
        {
            dmosi_thread_sleep(RECONNECT_DELAY_MS);
            if (!c->stop && c->want_connected)
                g_api->esp_wifi_connect();
        }
    }
}

int wifi_start(struct dmdrvi_context *c)
{
    g_ctx = c;
    radio_log_init();
    c->control_thread = dmosi_thread_create(control_main, c, c->priority, 8192, "dmespwifi", c->process);
    return c->control_thread != NULL ? 0 : -ENOMEM;
}

void wifi_stop(struct dmdrvi_context *c)
{
    c->stop = true;
    if (c->wake != NULL)
        dmosi_semaphore_post(c->wake, 1);
    if (c->control_thread != NULL)
    {
        dmosi_thread_join(c->control_thread);
        dmosi_thread_destroy(c->control_thread);
        c->control_thread = NULL;
    }
    if (c->started)
    {
        g_api->esp_wifi_stop();
        c->started = false;
    }
    g_ctx = NULL;
}

/* ---- the scan ---- */

static uint8_t auth_of(wifi_auth_mode_t mode)
{
    switch (mode)
    {
        case 0:  return DMDRVI_WIFI_AUTH_OPEN;
        case 1:  return DMDRVI_WIFI_AUTH_WEP;
        case 2:  return DMDRVI_WIFI_AUTH_WPA;
        case 3:  return DMDRVI_WIFI_AUTH_WPA2;
        case 4:  return DMDRVI_WIFI_AUTH_WPA_WPA2;
        case 5:  return DMDRVI_WIFI_AUTH_WPA2_ENTERPRISE;
        case 6:  return DMDRVI_WIFI_AUTH_WPA3;
        case 7:  return DMDRVI_WIFI_AUTH_WPA2_WPA3;
        default: return DMDRVI_WIFI_AUTH_OTHER;
    }
}

int wifi_scan(struct dmdrvi_context *c, dmdrvi_wifi_scan_t *scan)
{
    if (scan == NULL || (scan->aps == NULL && scan->max > 0))
        return -EINVAL;
    dmosi_mutex_lock(c->lock);
    bool started = c->started;
    dmosi_mutex_unlock(c->lock);
    if (!started)
        return -ENODEV;
    if (dmosi_mutex_lock(c->scan_lock) != 0)
        return -EBUSY;

    scan->count = 0;
    int ret = 0;
    while (dmosi_semaphore_wait(c->scan_done, 1, 0) == 0)
    { }                                         /* (an event of an earlier scan) */
    /* wifi_scan_config_t of ESP-IDF 5.2: every channel the country allows, passively too (a beacon is enough), hidden networks shown */
    struct
    {
        uint8_t *ssid, *bssid;
        uint8_t channel;
        bool show_hidden;
        uint32_t scan_type;
        struct { uint32_t active_min, active_max, passive; } time;
        uint8_t home_chan_dwell_time;
    } config = { .show_hidden = true, .scan_type = 0 /* active */, .time = { 120, 300, 300 } };
    int32_t scan_ret = g_api->esp_wifi_scan_start(&config, true);
    if (scan_ret != 0)
        ret = -EIO;
    uint16_t found = 0;
    if (ret == 0)
        g_api->esp_wifi_scan_get_ap_num(&found);
    char isr[96];
    osi_isr_stats(isr, sizeof(isr));
    uint32_t mem_ok, mem_bad, mem_bad_bytes;
    osi_alloc_stats(&mem_ok, &mem_bad, &mem_bad_bytes);
    radio_log("wifi: scan 0x%x, %u found, interrupts (line:calls)%s; internal memory: %u ok, %u not internal (%u bytes)\n", (unsigned) scan_ret,
              (unsigned) found, isr, (unsigned) mem_ok, (unsigned) mem_bad, (unsigned) mem_bad_bytes);
    uint16_t room = (found > SCAN_MAX_RECORDS) ? SCAN_MAX_RECORDS : found;
    wifi_ap_record_t *records = (ret == 0 && room > 0) ? Dmod_Malloc(room * sizeof(*records)) : NULL;
    if (ret == 0 && room > 0)
    {
        if (records == NULL)
            ret = -ENOMEM;
        else if (g_api->esp_wifi_scan_get_ap_records(&room, records) != 0)
            ret = -EIO;
    }
    for (uint16_t i = 0; ret == 0 && records != NULL && i < room && scan->count < scan->max; i++)
    {
        dmdrvi_wifi_ap_t *ap = &scan->aps[scan->count++];
        memset(ap, 0, sizeof(*ap));
        memcpy(ap->ssid, records[i].ssid, sizeof(records[i].ssid) < sizeof(ap->ssid) ? sizeof(records[i].ssid) : sizeof(ap->ssid) - 1u);
        ap->ssid[DMDRVI_WIFI_SSID_MAX] = '\0';
        memcpy(ap->bssid, records[i].bssid, DMDRVI_WIFI_MAC_LEN);
        ap->channel = records[i].primary;
        ap->rssi = records[i].rssi;
        ap->auth = auth_of(records[i].authmode);
    }
    Dmod_Free(records);
    dmosi_mutex_unlock(c->scan_lock);
    return ret;
}

/* ---- the connection ---- */

/* (strlen that stops at the end of the buffer: a text that is not finished is not a text) */
static size_t bounded_len(const char *text, size_t size)
{
    size_t n = 0;
    while (n < size && text[n] != '\0')
        n++;
    return n;
}

int wifi_connect(struct dmdrvi_context *c, const dmdrvi_wifi_connect_t *request)
{
    size_t ssid_len = bounded_len(request->ssid, sizeof(request->ssid));
    size_t pass_len = bounded_len(request->password, sizeof(request->password));
    if (ssid_len == 0 || ssid_len > DMDRVI_WIFI_SSID_MAX || pass_len > DMDRVI_WIFI_PASSWORD_MAX || (pass_len > 0 && pass_len < 8))
        return -EINVAL;
    dmosi_mutex_lock(c->lock);
    bool started = c->started;
    dmosi_mutex_unlock(c->lock);
    if (!started)
        return -ENODEV;

    wifi_config_t *cfg = &c->config;
    memset(cfg, 0, sizeof(*cfg));
    memcpy(cfg->sta.ssid, request->ssid, ssid_len);
    memcpy(cfg->sta.password, request->password, pass_len);
    cfg->sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    cfg->sta.threshold.authmode = (pass_len == 0) ? 0 /* open */ : 3 /* WPA2-PSK: the weakest that is taken */;
    cfg->sta.pmf_cfg.capable = true;
    cfg->sta.pmf_cfg.required = false;

    dmosi_mutex_lock(c->lock);
    memcpy(c->ssid, request->ssid, ssid_len + 1u);
    c->want_connected = true;
    c->state = DMDRVI_WIFI_STATE_CONNECTING;
    c->reason = 0;
    dmosi_mutex_unlock(c->lock);

    if (g_api->esp_wifi_set_config(WIFI_IF_STA, cfg) != 0)
        return -EINVAL;
    g_api->esp_wifi_connect();                  /* (a refused try is tried again by the control thread: it tells when the link is lost) */
    return 0;
}

int wifi_disconnect(struct dmdrvi_context *c)
{
    dmosi_mutex_lock(c->lock);
    c->want_connected = false;
    memset(&c->config, 0, sizeof(c->config));   /* (the passphrase is not kept) */
    c->ssid[0] = '\0';
    bool started = c->started;
    c->state = started ? DMDRVI_WIFI_STATE_IDLE : DMDRVI_WIFI_STATE_OFF;
    dmosi_mutex_unlock(c->lock);
    if (started)
        g_api->esp_wifi_disconnect();
    return 0;
}

void wifi_get_status(struct dmdrvi_context *c, dmdrvi_wifi_status_t *status)
{
    memset(status, 0, sizeof(*status));
    dmosi_mutex_lock(c->lock);
    status->state = c->state;
    memcpy(status->ssid, c->ssid, sizeof(status->ssid));
    if (c->state == DMDRVI_WIFI_STATE_CONNECTED)
    {
        memcpy(status->bssid, c->bssid, sizeof(status->bssid));
        status->channel = c->channel;
        status->rssi = c->rssi;
    }
    status->reason = c->reason;
    dmosi_mutex_unlock(c->lock);
}

void wifi_get_info(struct dmdrvi_context *c, dmdrvi_wifi_info_t *info)
{
    {
        char isr[96];
        osi_isr_stats(isr, sizeof(isr));
        radio_log("info: timers armed %u fired %u created %u failed %u\n", (unsigned) g_osi_timer_armed, (unsigned) g_osi_timer_fired,
                  (unsigned) g_osi_timer_created, (unsigned) g_osi_timer_create_failed);
        radio_log("info: interrupts (line:calls)%s; sem give %u (failed %u), queue send %u (failed %u)\n", isr, (unsigned) g_osi_sem_gave, (unsigned) g_osi_fail_sem_give,
                  (unsigned) g_osi_queue_sent, (unsigned) g_osi_fail_queue_send);
    }
    memset(info, 0, sizeof(*info));
    memcpy(info->driver, "dmespwifi", sizeof("dmespwifi"));
    /* (debugging: the internal heaps, KB free/largest of "main" and "dma", and how many allocations of the radio missed the internal RAM) */
    {
        dmheap_stats_t m = { 0 }, d = { 0 };
        dmheap_context_t *hm = dmheap_get_context_by_name("main"), *hd = dmheap_get_context_by_name("dma");
        uint32_t ok, bad, bad_bytes;
        osi_alloc_stats(&ok, &bad, &bad_bytes);
        if (hm != NULL) dmheap_get_stats(hm, &m);
        if (hd != NULL) dmheap_get_stats(hd, &d);
        uint32_t pl, pf = osi_pool_stats(&pl);
        uint32_t tasks, total_kb, peak_kb;
        osi_task_stats(&tasks, &total_kb, &peak_kb);
        Dmod_SnPrintf(info->firmware, sizeof(info->firmware), "m%u/%u p%u/%u b%u/%u t%u/%u/%u", (unsigned) (m.free_bytes >> 10),
                      (unsigned) (m.largest_free_block >> 10), (unsigned) (pf >> 10), (unsigned) (pl >> 10),
                      (unsigned) bad, (unsigned) (ok + bad), (unsigned) tasks, (unsigned) total_kb, (unsigned) peak_kb);
    }
    memcpy(info->chip, "ESP32-S3", sizeof("ESP32-S3"));
    dmosi_mutex_lock(c->lock);
    if (c->started)
        memcpy(info->mac, c->mac, DMDRVI_WIFI_MAC_LEN);
    info->rx_frames = c->rx_count;
    info->tx_frames = c->tx_count;
    info->rx_dropped = c->rx_dropped;
    info->tx_dropped = c->tx_dropped;
    info->errors = c->errors;
    info->resets = c->resets;
    dmosi_mutex_unlock(c->lock);
}
