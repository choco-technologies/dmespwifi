#define DMOD_ENABLE_REGISTRATION    ON
#include "dmod.h"
#include "dmespwifi.h"
#include "private.h"
#include "dmini.h"
#include "dmnetif.h"
#include <errno.h>
#include <string.h>

/*
 * dmespwifi: a network node on the Wi-Fi of the chip itself (ESP32-S3). It is the same node as dmesphosted's: read() and write() are
 * 802.3 frames, DMDRVI_IOCTL_NET_* work as for any network driver, the Wi-Fi is DMDRVI_IOCTL_WIFI_* (the `wifi` tool of dmnet works with
 * it) and the driver registers the interface with dmnetif itself.
 */

static bool is_valid_context(struct dmdrvi_context *c)
{
    return c != NULL && c->magic == DMESPWIFI_CONTEXT_MAGIC;
}

/* ---- the interface of dmnetif: when the radio answers and the path of the node is known ---- */

static void iface_ensure(struct dmdrvi_context *c)
{
    dmosi_mutex_lock(c->lock);
    bool ready = c->started && c->node_path != NULL && c->iface == NULL;
    char *path = ready ? Dmod_StrDup(c->node_path) : NULL;
    dmosi_mutex_unlock(c->lock);
    if (path == NULL)
        return;
    /* The name of the interface: the friendly name of the configuration and a number. The number is the one of the node (/dev/dmespwifi<major>);
     * when another driver already has that name (e.g. dmesphosted's wlan0), the next free one */
    char name[DMNETIF_NAME_MAX_LEN + 1];
    void *iface = NULL;
    for (unsigned number = c->major; number < c->major + DMESPWIFI_IFACE_NUMBERS; number++)
    {
        if (Dmod_SnPrintf(name, sizeof(name), "%s%u", c->iface_name, number) >= (int) sizeof(name))
            break;
        if (dmnetif_find_by_name(name) != NULL)
            continue;
        iface = dmnetif_register(name, path);
        break;
    }
    if (iface == NULL)
    {
        DMOD_LOG_ERROR("dmespwifi: failed to register %s<n> (%s) with dmnetif\n", c->iface_name, path);
    }
    else
    {
        DMOD_LOG_INFO("dmespwifi: registered %s -> %s with dmnetif\n", name, path);
        dmosi_mutex_lock(c->lock);
        c->iface = iface;
        dmosi_mutex_unlock(c->lock);
    }
    Dmod_Free(path);
}

void dmespwifi_radio_ready(struct dmdrvi_context *c)
{
    iface_ensure(c);
}

/* ---- configuration ---- */

static int read_config(struct dmdrvi_context *c, dmini_context_t ini)
{
    const char *name = dmini_get_string(ini, NULL, "interface", DMESPWIFI_DEFAULT_IFACE);
    c->iface_name = Dmod_StrDup((name != NULL && name[0] != '\0') ? name : DMESPWIFI_DEFAULT_IFACE);
    c->priority = dmini_get_int(ini, NULL, "thread_priority", DMESPWIFI_DEFAULT_PRIORITY);
    c->log_level = dmini_get_int(ini, NULL, "log_level", 1);        /* of the libraries: 0 error, 1 warning, 2 info, 3 debug, 4 verbose */
    if (c->iface_name == NULL)
        return -ENOMEM;
    if (c->log_level < 0 || c->log_level > 4)
    {
        DMOD_LOG_ERROR("dmespwifi: invalid log_level %d (expected 0..4)\n", c->log_level);
        return -EINVAL;
    }
    if (c->priority < 1 || c->priority > 4)
    {
        DMOD_LOG_ERROR("dmespwifi: invalid thread_priority %d (expected 1..4)\n", c->priority);
        return -EINVAL;
    }
    return 0;
}

/* The node is named after the interface (dmdevfs has no other way to name it than the driver name and the major, or this name): the friendly
 * name of the configuration and the number - there is one radio, so 0: /dev/wlan0, where the tools (wifi) look for it by default */
static void fill_dev_num(const struct dmdrvi_context *c, dmdrvi_dev_num_t *dev_num)
{
    memset(dev_num, 0, sizeof(*dev_num));
    dev_num->major = 0;
    if (Dmod_SnPrintf(dev_num->alt_name, sizeof(dev_num->alt_name), "%s%u", c->iface_name, (unsigned) dev_num->major) < (int) sizeof(dev_num->alt_name))
        dev_num->flags = DMDRVI_NUM_MAJOR | DMDRVI_NUM_ALT_NAME;
    else
    {
        memset(dev_num->alt_name, 0, sizeof(dev_num->alt_name));
        dev_num->flags = DMDRVI_NUM_MAJOR;      /* too long a name: /dev/dmespwifi0 */
    }
}

static void destroy_context(struct dmdrvi_context *c)
{
    wifi_stop(c);
    if (c->iface != NULL)
        dmnetif_unregister(c->iface);
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
    if (c->lock != NULL)
        dmosi_mutex_destroy(c->lock);
    Dmod_Free(c->rx_frames);
    Dmod_Free(c->node_path);
    Dmod_Free(c->iface_name);
    c->magic = 0;
    Dmod_Free(c);
}

/* ---- DMOD lifecycle ---- */

int dmod_init(const Dmod_Config_t *Config)
{
    (void) Config;
    DMOD_LOG_INFO("dmespwifi Wi-Fi driver module initialized\n");
    return 0;
}

int dmod_deinit(void)
{
    DMOD_LOG_INFO("dmespwifi Wi-Fi driver module deinitialized\n");
    return 0;
}

dmod_dmespwifi_api_declaration(1.0, const char *, _chip_name, ( void ))
{
    return "ESP32-S3";
}

/* ---- DMDRVI ---- */

dmod_dmdrvi_dif_api_declaration(2.0, dmespwifi, dmdrvi_context_t, _create, ( dmini_context_t config, dmdrvi_dev_num_t *dev_num ))
{
    if (config == NULL || dev_num == NULL)
        return NULL;
    struct dmdrvi_context *c = Dmod_Malloc(sizeof(*c));
    if (c == NULL)
        return NULL;
    memset(c, 0, sizeof(*c));
    c->magic = DMESPWIFI_CONTEXT_MAGIC;
    c->lock = dmosi_mutex_create(false);
    c->scan_lock = dmosi_mutex_create(false);
    c->rx_ready = dmosi_semaphore_create(0, DMESPWIFI_RX_FRAMES);
    c->scan_done = dmosi_semaphore_create(0, 1);
    c->wake = dmosi_semaphore_create(0, 1);
    c->rx_frames = Dmod_Malloc(DMESPWIFI_RX_FRAMES * sizeof(frame_t));
    c->process = dmosi_process_create("dmespwifi", Dmod_GetCurrentModuleName(), NULL);
    if (c->lock == NULL || c->scan_lock == NULL || c->rx_ready == NULL || c->scan_done == NULL || c->wake == NULL || c->rx_frames == NULL ||
        read_config(c, config) != 0 || wifi_start(c) != 0)
    {
        DMOD_LOG_ERROR("dmespwifi: failed to create the context\n");
        destroy_context(c);
        return NULL;
    }
    fill_dev_num(c, dev_num);
    return c;
}

dmod_dmdrvi_dif_api_declaration(2.0, dmespwifi, void, _free, ( dmdrvi_context_t context ))
{
    if (is_valid_context(context))
        destroy_context(context);
}

dmod_dmdrvi_dif_api_declaration(2.0, dmespwifi, void, _path_ready, ( dmdrvi_context_t context, const dmdrvi_dev_num_t *dev_num, const char *path ))
{
    if (!is_valid_context(context) || path == NULL)
        return;
    char *copy = Dmod_StrDup(path);
    if (copy == NULL)
        return;
    dmosi_mutex_lock(context->lock);
    Dmod_Free(context->node_path);
    context->node_path = copy;
    context->major = (dev_num != NULL) ? dev_num->major : 0;
    dmosi_mutex_unlock(context->lock);
    iface_ensure(context);
}

dmod_dmdrvi_dif_api_declaration(2.0, dmespwifi, void *, _open, ( dmdrvi_context_t context, int flags, const dmdrvi_dev_num_t *dev_num ))
{
    (void) flags;
    (void) dev_num;
    return is_valid_context(context) ? (void *) context : NULL;
}

dmod_dmdrvi_dif_api_declaration(2.0, dmespwifi, void, _close, ( dmdrvi_context_t context, void *handle ))
{
    (void) context;
    (void) handle;
}

/* One received 802.3 frame; 0 when none came within the time (or the interface is not started) */
dmod_dmdrvi_dif_api_declaration(2.0, dmespwifi, dmdrvi_ssize_t, _read, ( dmdrvi_context_t context, void *handle, void *buffer, size_t size, dmdrvi_offset_t offset ))
{
    (void) handle;
    if (offset < 0)
        return -EINVAL;
    if (!is_valid_context(context) || buffer == NULL || size == 0)
        return 0;
    dmosi_mutex_lock(context->lock);
    bool running = context->running;
    dmosi_mutex_unlock(context->lock);
    if (!running || dmosi_semaphore_wait(context->rx_ready, 1, DMESPWIFI_READ_TIMEOUT_MS) != 0)
        return 0;
    dmosi_mutex_lock(context->lock);
    if (context->rx_tail == context->rx_head)
    {
        dmosi_mutex_unlock(context->lock);
        return 0;
    }
    frame_t *f = &context->rx_frames[context->rx_tail];
    size_t n = (f->length < size) ? f->length : size;
    memcpy(buffer, f->data, n);
    context->rx_tail = (context->rx_tail + 1u) % DMESPWIFI_RX_FRAMES;
    dmosi_mutex_unlock(context->lock);
    return (dmdrvi_ssize_t) n;
}

/* One 802.3 frame to send; 0 when it cannot go (not connected) */
dmod_dmdrvi_dif_api_declaration(2.0, dmespwifi, dmdrvi_ssize_t, _write, ( dmdrvi_context_t context, void *handle, const void *buffer, size_t size, dmdrvi_offset_t offset ))
{
    (void) handle;
    if (offset < 0)
        return -EINVAL;
    if (!is_valid_context(context) || buffer == NULL || size == 0)
        return 0;
    return wifi_send_frame(context, buffer, size) == 0 ? (dmdrvi_ssize_t) size : 0;
}

static bool ioctl_needs_arg(int command)
{
    switch (command)
    {
        case DMDRVI_IOCTL_NET_SET_MAC_ADDR:
        case DMDRVI_IOCTL_NET_GET_MAC_ADDR:
        case DMDRVI_IOCTL_NET_GET_LINK_STATUS:
        case DMDRVI_IOCTL_WIFI_GET_INFO:
        case DMDRVI_IOCTL_WIFI_GET_STATUS:
        case DMDRVI_IOCTL_WIFI_CONNECT:
        case DMDRVI_IOCTL_WIFI_SCAN:
            return true;
        default:
            return false;
    }
}

dmod_dmdrvi_dif_api_declaration(2.0, dmespwifi, int, _ioctl, ( dmdrvi_context_t context, void *handle, int command, void *arg ))
{
    (void) handle;
    if (!is_valid_context(context))
        return -EINVAL;
    if (arg == NULL && ioctl_needs_arg(command))
        return -EINVAL;
    switch (command)
    {
        case DMDRVI_IOCTL_NET_GET_MAC_ADDR:
        {
            static const uint8_t none[DMDRVI_WIFI_MAC_LEN] = { 0 };
            dmosi_mutex_lock(context->lock);
            bool known = context->started && memcmp(context->mac, none, sizeof(none)) != 0;
            memcpy(((dmdrvi_net_mac_addr_t *) arg)->addr, context->mac, DMDRVI_WIFI_MAC_LEN);
            dmosi_mutex_unlock(context->lock);
            return known ? 0 : -ENODEV;
        }
        case DMDRVI_IOCTL_NET_SET_MAC_ADDR:
            return -ENOTSUP;                    /* the chip's own */
        case DMDRVI_IOCTL_NET_GET_LINK_STATUS:
            dmosi_mutex_lock(context->lock);
            *(dmdrvi_net_link_status_t *) arg = (context->state == DMDRVI_WIFI_STATE_CONNECTED) ? DMDRVI_NET_LINK_UP : DMDRVI_NET_LINK_DOWN;
            dmosi_mutex_unlock(context->lock);
            return 0;
        case DMDRVI_IOCTL_NET_START:
        case DMDRVI_IOCTL_NET_STOP:
            dmosi_mutex_lock(context->lock);
            context->running = (command == DMDRVI_IOCTL_NET_START);
            if (command == DMDRVI_IOCTL_NET_STOP)
                context->rx_head = context->rx_tail = 0;
            dmosi_mutex_unlock(context->lock);
            return 0;
        case DMDRVI_IOCTL_WIFI_GET_INFO:
            wifi_get_info(context, (dmdrvi_wifi_info_t *) arg);
            return 0;
        case DMDRVI_IOCTL_WIFI_GET_STATUS:
            wifi_get_status(context, (dmdrvi_wifi_status_t *) arg);
            return 0;
        case DMDRVI_IOCTL_WIFI_CONNECT:
            return wifi_connect(context, (const dmdrvi_wifi_connect_t *) arg);
        case DMDRVI_IOCTL_WIFI_DISCONNECT:
            return wifi_disconnect(context);
        case DMDRVI_IOCTL_WIFI_SCAN:
            return wifi_scan(context, (dmdrvi_wifi_scan_t *) arg);
        default:
            return -ENOTTY;                     /* including the commands dmdevfs probes every node with */
    }
}

dmod_dmdrvi_dif_api_declaration(2.0, dmespwifi, int, _flush, ( dmdrvi_context_t context, void *handle ))
{
    (void) handle;
    return is_valid_context(context) ? 0 : -EINVAL;
}

dmod_dmdrvi_dif_api_declaration(2.0, dmespwifi, int, _stat, ( dmdrvi_context_t context, const char *path, dmdrvi_stat_t *stat ))
{
    (void) path;
    if (!is_valid_context(context) || stat == NULL)
        return -EINVAL;
    stat->size = (dmdrvi_size_t) 0;            /* frame by frame, not a byte store */
    stat->mode = 0666;
    return 0;
}
