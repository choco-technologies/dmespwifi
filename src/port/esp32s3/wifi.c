/*
 * The Wi-Fi of the node: the start of the radio (the order of esp_wifi_init() of ESP-IDF), the events of the libraries, the scan, the
 * connection that is kept, the frames.
 */
#include "private.h"
#include <errno.h>
#include <string.h>

radio_t *g_radio;

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

static void state_set(radio_t *c, dmdrvi_wifi_state_t state)
{
    dmosi_mutex_lock(c->lock);
    c->state = state;
    dmosi_mutex_unlock(c->lock);
}

int wifi_event(const char *base, int32_t id, void *data, size_t size)
{
    radio_t *c = g_radio;
    (void) base;
    (void) size;
    if (c == NULL)
        return 0;
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
            /* (a radio that was switched off tells it too: it stays off) */
            c->state = !c->started ? DMDRVI_WIFI_STATE_OFF : c->want_connected ? DMDRVI_WIFI_STATE_CONNECTING : DMDRVI_WIFI_STATE_IDLE;
            dmosi_mutex_unlock(c->lock);
            radio_log("wifi: disconnected, reason %u\n", (unsigned) c->reason);
            dmosi_semaphore_post(c->wake, 1);       /* the control thread connects again */
            break;
        }
        default:
            break;
    }
    return 0;
}

/* ---- frames ---- */

int wifi_rx_frame(void *buffer, uint16_t length, void *eb)
{
    radio_t *c = g_radio;
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
        dmespradio_esp_wifi_internal_free_rx_buffer(eb);
    return 0;
}

int wifi_receive_frame(radio_t *c, void *buffer, size_t size, size_t *received, uint32_t timeout_ms)
{
    *received = 0;
    dmosi_mutex_lock(c->lock);
    bool running = c->running;
    dmosi_mutex_unlock(c->lock);
    if (!running || dmosi_semaphore_wait(c->rx_ready, 1, (int32_t) timeout_ms) != 0)
        return 0;
    dmosi_mutex_lock(c->lock);
    if (c->rx_tail != c->rx_head)
    {
        frame_t *f = &c->rx_frames[c->rx_tail];
        size_t n = (f->length < size) ? f->length : size;
        memcpy(buffer, f->data, n);
        c->rx_tail = (c->rx_tail + 1u) % DMESPWIFI_RX_FRAMES;
        *received = n;
    }
    dmosi_mutex_unlock(c->lock);
    return 0;
}

void wifi_set_running(radio_t *c, bool running)
{
    dmosi_mutex_lock(c->lock);
    c->running = running;
    if (!running)
        c->rx_head = c->rx_tail = 0;            /* (what came before the start is not the interface's) */
    dmosi_mutex_unlock(c->lock);
}

int wifi_send_frame(radio_t *c, const void *frame, size_t size)
{
    if (size == 0 || size > DMESPWIFI_FRAME_MAX)
        return -EINVAL;
    dmosi_mutex_lock(c->lock);
    bool can = c->running && c->state == DMDRVI_WIFI_STATE_CONNECTED;
    dmosi_mutex_unlock(c->lock);
    int32_t ret = can ? dmespradio_esp_wifi_internal_tx(WIFI_IF_STA, (void *) frame, (uint16_t) size) : -1;
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
    memcpy(&cfg->wpa_crypto_funcs, dmespradio_crypto_funcs(), sizeof(cfg->wpa_crypto_funcs));   /* what the image links: size, version, the functions */
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

static int radio_up(radio_t *c)
{
    if (!glue_host_start())
        return -ENODEV;
    osi_fill(&g_osi);
    config_fill(&g_config);
    radio_base_mac(c->mac);
    radio_power_domain_on();
    dmespradio_esp_wifi_internal_set_log_level(c->config.log_level);
    int32_t ret = dmespradio_esp_wifi_init_internal(&g_config);
    if (ret != 0)
    {
        radio_log("wifi: esp_wifi_init_internal: 0x%x\n", (unsigned) ret);
        return -EIO;
    }
    ret = dmespradio_supplicant_init();
    if (ret == 0)
        ret = dmespradio_esp_wifi_internal_reg_rxcb(WIFI_IF_STA, (void *) wifi_rx_frame);
    if (ret == 0)
        ret = dmespradio_esp_wifi_set_mode(1 /* WIFI_MODE_STA */);
    if (ret == 0)
    {
        /* Europe: the channels 1-13 (the libraries start with 1-11 and would not see an access point on 12 or 13) */
        static const wifi_country_t country = { .cc = "PL", .schan = 1, .nchan = 13, .max_tx_power = 20, .policy = WIFI_COUNTRY_POLICY_MANUAL };
        ret = dmespradio_esp_wifi_set_country(&country);
        if (ret == 0)
            ret = dmespradio_esp_wifi_start();
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
    radio_t *c = arg;

    state_set(c, DMDRVI_WIFI_STATE_STARTING);
    int ret = radio_up(c);
    dmosi_mutex_lock(c->lock);
    c->started = (ret == 0);
    c->up = (ret == 0);
    c->booting = false;
    c->state = (ret == 0) ? DMDRVI_WIFI_STATE_IDLE : DMDRVI_WIFI_STATE_OFF;
    c->resets++;
    dmosi_mutex_unlock(c->lock);
    if (ret == 0 && c->config.ready != NULL)
        c->config.ready(c->config.arg);             /* (the driver registers the interface with dmnetif) */

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
                dmespradio_esp_wifi_connect();
        }
    }
}

static void radio_free(radio_t *c)
{
    if (c->process != NULL)
        dmosi_process_destroy(c->process);
    if (c->rx_ready != NULL)
        dmosi_semaphore_destroy(c->rx_ready);
    if (c->scan_done != NULL)
        dmosi_semaphore_destroy(c->scan_done);
    if (c->wake != NULL)
        dmosi_semaphore_destroy(c->wake);
    if (c->scan_lock != NULL)
        dmosi_mutex_destroy(c->scan_lock);
    if (c->power_lock != NULL)
        dmosi_mutex_destroy(c->power_lock);
    if (c->lock != NULL)
        dmosi_mutex_destroy(c->lock);
    Dmod_Free(c->rx_frames);
    Dmod_Free(c);
}

int wifi_start(const dmespwifi_port_config_t *config)
{
    if (g_radio != NULL)
        return -EBUSY;
    radio_t *c = Dmod_Malloc(sizeof(*c));
    if (c == NULL)
        return -ENOMEM;
    memset(c, 0, sizeof(*c));
    c->config = *config;
    c->lock = dmosi_mutex_create(false);
    c->scan_lock = dmosi_mutex_create(false);
    c->power_lock = dmosi_mutex_create(false);
    c->booting = true;
    c->rx_ready = dmosi_semaphore_create(0, DMESPWIFI_RX_FRAMES);
    c->scan_done = dmosi_semaphore_create(0, 1);
    c->wake = dmosi_semaphore_create(0, 1);
    c->rx_frames = Dmod_Malloc(DMESPWIFI_RX_FRAMES * sizeof(frame_t));
    c->process = dmosi_process_create("dmespwifi", Dmod_GetCurrentModuleName(), NULL);
    if (c->lock == NULL || c->scan_lock == NULL || c->power_lock == NULL || c->rx_ready == NULL || c->scan_done == NULL || c->wake == NULL || c->rx_frames == NULL)
    {
        radio_free(c);
        return -ENOMEM;
    }
    g_radio = c;
    radio_log_init();
    c->control_thread = dmosi_thread_create(control_main, c, c->config.priority, 8192, "dmespwifi", c->process);
    if (c->control_thread == NULL)
    {
        g_radio = NULL;
        radio_free(c);
        return -ENOMEM;
    }
    return 0;
}

void wifi_stop(void)
{
    radio_t *c = g_radio;
    if (c == NULL)
        return;
    c->stop = true;
    dmosi_semaphore_post(c->wake, 1);
    if (c->control_thread != NULL)
    {
        dmosi_thread_join(c->control_thread);
        dmosi_thread_destroy(c->control_thread);
        c->control_thread = NULL;
    }
    if (c->started)
    {
        dmespradio_esp_wifi_stop();
        c->started = false;
    }
    g_radio = NULL;
    radio_free(c);
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

int wifi_scan(radio_t *c, dmdrvi_wifi_scan_t *scan)
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
    dmosi_mutex_lock(c->lock);
    started = c->started;                       /* (the radio may have been switched off while this waited for the scan before) */
    dmosi_mutex_unlock(c->lock);
    if (!started)
    {
        dmosi_mutex_unlock(c->scan_lock);
        return -ENODEV;
    }

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
    int32_t scan_ret = dmespradio_esp_wifi_scan_start(&config, true);
    if (scan_ret != 0)
        ret = -EIO;
    uint16_t found = 0;
    if (ret == 0)
        dmespradio_esp_wifi_scan_get_ap_num(&found);
    uint16_t room = (found > SCAN_MAX_RECORDS) ? SCAN_MAX_RECORDS : found;
    wifi_ap_record_t *records = (ret == 0 && room > 0) ? Dmod_Malloc(room * sizeof(*records)) : NULL;
    if (ret == 0 && room > 0)
    {
        if (records == NULL)
            ret = -ENOMEM;
        else if (dmespradio_esp_wifi_scan_get_ap_records(&room, records) != 0)
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

int wifi_connect(radio_t *c, const dmdrvi_wifi_connect_t *request)
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

    wifi_config_t *cfg = &c->sta_config;
    memset(cfg, 0, sizeof(*cfg));
    memcpy(cfg->sta.ssid, request->ssid, ssid_len);
    memcpy(cfg->sta.password, request->password, pass_len);
    cfg->sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    cfg->sta.threshold.authmode = (pass_len == 0) ? 0 /* open */ : 3 /* WPA2-PSK: the weakest that is taken */;
    cfg->sta.pmf_cfg.capable = true;
    cfg->sta.pmf_cfg.required = false;

    /* A connection that is there, or being made, is dropped first: the libraries refuse to connect over it ("sta is connecting, return
     * error") and tell nothing, so the state would stay "connecting" for good. Dropped, they tell that the link is lost and the control
     * thread connects again - with the new configuration */
    dmosi_mutex_lock(c->lock);
    bool busy = c->want_connected && (c->state == DMDRVI_WIFI_STATE_CONNECTED || c->state == DMDRVI_WIFI_STATE_CONNECTING);
    memcpy(c->ssid, request->ssid, ssid_len + 1u);
    c->want_connected = true;
    c->state = DMDRVI_WIFI_STATE_CONNECTING;
    c->reason = 0;
    dmosi_mutex_unlock(c->lock);

    if (busy)
        dmespradio_esp_wifi_disconnect();
    if (dmespradio_esp_wifi_set_config(WIFI_IF_STA, cfg) != 0)
        return -EINVAL;
    if (!busy)
        dmespradio_esp_wifi_connect();              /* (a refused try is tried again by the control thread: it tells when the link is lost) */
    /* (busy: the control thread connects - woken by the lost link, or by itself within a second when nothing was lost, between two tries) */
    return 0;
}

int wifi_disconnect(radio_t *c)
{
    dmosi_mutex_lock(c->lock);
    c->want_connected = false;
    memset(&c->sta_config, 0, sizeof(c->sta_config));   /* (the passphrase is not kept) */
    c->ssid[0] = '\0';
    bool started = c->started;
    c->state = started ? DMDRVI_WIFI_STATE_IDLE : DMDRVI_WIFI_STATE_OFF;
    dmosi_mutex_unlock(c->lock);
    if (started)
        dmespradio_esp_wifi_disconnect();
    return 0;
}

/* The switch of the radio: esp_wifi_stop() and esp_wifi_start() - the libraries stay initialized (the memory of the radio is not given back
 * and taken again), the station goes away and comes back. Off drops the connection and the network like wifi_disconnect() */
int wifi_set_enabled(radio_t *c, bool enabled)
{
    if (dmosi_mutex_lock(c->power_lock) != 0)
        return -EBUSY;
    dmosi_mutex_lock(c->lock);
    bool up = c->up, booting = c->booting, started = c->started;
    dmosi_mutex_unlock(c->lock);
    int ret = 0;
    if (!up)
        ret = booting ? -EBUSY : -ENODEV;
    else if (enabled && !started)
    {
        if (dmespradio_esp_wifi_start() != 0)
        {
            radio_log("wifi: the station did not start again\n");
            ret = -EIO;
        }
        else
        {
            dmosi_mutex_lock(c->lock);
            c->started = true;
            c->state = DMDRVI_WIFI_STATE_IDLE;
            c->reason = 0;
            dmosi_mutex_unlock(c->lock);
        }
    }
    else if (!enabled && started)
    {
        dmosi_mutex_lock(c->lock);
        c->started = false;                     /* (first: a scan or a connect that comes now is refused, the events of the stop do not set a state) */
        c->want_connected = false;
        memset(&c->sta_config, 0, sizeof(c->sta_config));   /* (the passphrase is not kept) */
        c->ssid[0] = '\0';
        c->state = DMDRVI_WIFI_STATE_OFF;
        c->reason = 0;
        c->rx_head = c->rx_tail = 0;
        dmosi_mutex_unlock(c->lock);
        dmespradio_esp_wifi_stop();             /* (ends a scan that runs, and the link) */
    }
    dmosi_mutex_unlock(c->power_lock);
    return ret;
}

void wifi_get_status(radio_t *c, dmdrvi_wifi_status_t *status)
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
    bool connected = (c->state == DMDRVI_WIFI_STATE_CONNECTED);
    dmosi_mutex_unlock(c->lock);
    if (connected)
    {
        wifi_ap_record_t ap;                        /* the signal: of the access point, as the radio hears it now */
        memset(&ap, 0, sizeof(ap));
        if (dmespradio_esp_wifi_sta_get_ap_info(&ap) == 0)
            status->rssi = ap.rssi;
    }
}

void wifi_get_info(radio_t *c, dmdrvi_wifi_info_t *info)
{
    memset(info, 0, sizeof(*info));
    memcpy(info->driver, "dmespwifi", sizeof("dmespwifi"));
    const char *phy = dmespradio_get_phy_version_str();
    if (phy != NULL)
        Dmod_SnPrintf(info->firmware, sizeof(info->firmware), "%s", phy);
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
