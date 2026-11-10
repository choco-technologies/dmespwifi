#ifndef DMESPWIFI_PRIVATE_H
#define DMESPWIFI_PRIVATE_H

#include "dmod.h"
#include "dmosi.h"
#include "dmdrvi.h"
#include "dmwifi_image.h"

/* ---- Where things are ---- */

/* The image of the Espressif libraries (image/): the flash from this offset, seen by the data bus (the header and the read-only data) and by
 * the instruction bus (the code) at the identity mapping of the flash (0x3C000000 / 0x42000000 + offset); its .data/.bss at the RAM that
 * dmod-boot reserves for it (the addresses are linked in) */
#define DMESPWIFI_IMAGE_FLASH           0x00020000u     /* where dmod-boot writes the image: the free 896 kB after the store of the selector */
#define DMESPWIFI_IMAGE_DATA_WINDOW     0x3C020000u
#define DMESPWIFI_IMAGE_RAM_BASE        0x3fcc4000u
#define DMESPWIFI_IMAGE_RAM_END         0x3fcc8000u

/* The memory of the radio (the buffers of the libraries: the MAC reaches them by DMA, so the internal RAM, and the heap of the firmware
 * there is used up by the modules): the RAM that the selector of dmod-boot needs only for its start (selector.ld: BOOT), 61 kB. What is below
 * it (0x3fcd1700..0x3fcda000) is the code and the table of the selector, which the firmware calls: not to be touched */
#define DMESPWIFI_POOL_BASE             0x3fcda000u
#define DMESPWIFI_POOL_END              0x3fce9700u

#define DMESPWIFI_CONTEXT_MAGIC         0x46575345u     /* "ESWF" */
#define DMESPWIFI_FRAME_MAX             1600u           /* an 802.3 frame (1500 of data) and what the radio adds */
#define DMESPWIFI_RX_FRAMES             16u
#define DMESPWIFI_READ_TIMEOUT_MS       50
#define DMESPWIFI_DEFAULT_IFACE         "wlan0"
#define DMESPWIFI_DEFAULT_PRIORITY      3
#define DMESPWIFI_LOG_SIZE              4096u

typedef struct
{
    uint16_t    length;
    uint8_t     data[DMESPWIFI_FRAME_MAX];
} frame_t;

struct dmdrvi_context
{
    uint32_t                magic;
    dmosi_mutex_t           lock;               /* the fields below */
    char *                  iface_name;
    char *                  node_path;
    void *                  iface;              /* the dmnetif interface */
    int                     priority;           /* of the threads of the driver */
    int                     log_level;          /* of the libraries (WIFI_LOG_*) */

    /* the radio */
    dmosi_process_t         process;
    dmosi_thread_t          control_thread;
    bool                    stop;
    bool                    started;            /* esp_wifi_start() done: the station exists */
    uint8_t                 mac[DMDRVI_WIFI_MAC_LEN];

    /* the network */
    dmdrvi_wifi_state_t     state;
    bool                    want_connected;     /* DMDRVI_IOCTL_WIFI_CONNECT, until DISCONNECT: connect again whenever the link is lost */
    wifi_config_t           config;
    char                    ssid[DMDRVI_WIFI_SSID_MAX + 1];
    uint8_t                 bssid[DMDRVI_WIFI_MAC_LEN];
    uint8_t                 channel;
    int8_t                  rssi;
    uint16_t                reason;
    bool                    running;            /* DMDRVI_IOCTL_NET_START */

    /* the frames */
    frame_t *               rx_frames;          /* a ring */
    uint32_t                rx_head, rx_tail;
    dmosi_semaphore_t       rx_ready;
    dmosi_semaphore_t       scan_done;
    dmosi_semaphore_t       wake;               /* the control thread */
    dmosi_mutex_t           scan_lock;
    uint32_t                rx_count, tx_count, rx_dropped, tx_dropped, errors, resets;
};

extern struct dmdrvi_context *  g_ctx;          /* one radio: the libraries' callbacks have no context */
extern const dmwifi_image_api_t * g_api;
extern wifi_osi_funcs_t g_osi;

/* ---- log: dmlog and a ring of our own (its address in RTC_CNTL_STORE5: JTAG finds it, no secret goes in) ---- */
void  radio_log(const char *format, ...);
void  radio_log_init(void);

/* ---- image.c ---- */
bool  image_load(void);

/* ---- osi.c ---- */
void  osi_fill(wifi_osi_funcs_t *osi);
int   osi_isr_dispatch(int line);
void *osi_pool_alloc(size_t size);

/* ---- radio.c ---- */
uint64_t radio_time_us(void);
uint32_t radio_random(void);
void  radio_base_mac(uint8_t mac[6]);
void  radio_power_domain_on(void);
void  radio_clock_enable(void);
void  radio_clock_disable(void);
void  radio_phy_enable(void);
void  radio_phy_disable(void);

/* ---- dmespwifi.c ---- */
void  dmespwifi_radio_ready(struct dmdrvi_context *context);   /* the radio answers: the interface goes to dmnetif */

/* ---- wifi.c ---- */
int   wifi_event(const char *base, int32_t id, void *data, size_t size);
int   wifi_start(struct dmdrvi_context *context);
void  wifi_stop(struct dmdrvi_context *context);
int   wifi_scan(struct dmdrvi_context *context, dmdrvi_wifi_scan_t *scan);
int   wifi_connect(struct dmdrvi_context *context, const dmdrvi_wifi_connect_t *request);
int   wifi_disconnect(struct dmdrvi_context *context);
void  wifi_get_status(struct dmdrvi_context *context, dmdrvi_wifi_status_t *status);
void  wifi_get_info(struct dmdrvi_context *context, dmdrvi_wifi_info_t *info);
int   wifi_send_frame(struct dmdrvi_context *context, const void *frame, size_t size);
int   wifi_rx_frame(void *buffer, uint16_t length, void *eb);

#endif /* DMESPWIFI_PRIVATE_H */
