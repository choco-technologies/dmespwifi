/* The port of the ESP32-S3: the radio of the chip, run by the libraries of Espressif (radio/: libdmespwifi_port.a, the Built-in API
 * dmespradio). */
#ifndef DMESPWIFI_PORT_PRIVATE_H
#define DMESPWIFI_PORT_PRIVATE_H

#include "dmod.h"
#include "dmosi.h"
#include "dmespwifi_port.h"
#include "dmespradio.h"

/* ---- Where things are ---- */

/* The memory of the radio (the buffers of the libraries: the MAC reaches them by DMA, so the internal RAM, and the heap of the firmware
 * there is used up by the modules): the RAM that the selector of dmod-boot needs only for its start (selector.ld: BOOT), 61 kB. What is below
 * it (0x3fcd1700..0x3fcda000) is the code and the table of the selector, which the firmware calls: not to be touched */
#define DMESPWIFI_POOL_BASE             0x3fcda000u
#define DMESPWIFI_POOL_END              0x3fce9700u

#define DMESPWIFI_FRAME_MAX             1600u           /* an 802.3 frame (1500 of data) and what the radio adds */
#define DMESPWIFI_RX_FRAMES             16u
#define DMESPWIFI_LOG_SIZE              4096u
#define DMESPWIFI_DEFAULT_PRIORITY      3

typedef struct
{
    uint16_t    length;
    uint8_t     data[DMESPWIFI_FRAME_MAX];
} frame_t;

/* The radio (one: the callbacks of the libraries have no context) */
typedef struct
{
    dmosi_mutex_t           lock;               /* the fields below */
    dmespwifi_port_config_t config;             /* of the driver: the priority, the log level, ready() */

    /* the radio */
    dmosi_process_t         process;
    dmosi_thread_t          control_thread;
    bool                    stop;
    bool                    started;            /* esp_wifi_start() done and not stopped since: the station exists (the radio is on) */
    bool                    booting;            /* the control thread is still starting the radio */
    bool                    up;                 /* the libraries are initialized: the station can be stopped and started */
    dmosi_mutex_t           power_lock;         /* one switch of the radio at a time (wifi_set_enabled) */
    uint8_t                 mac[DMDRVI_WIFI_MAC_LEN];

    /* the network */
    dmdrvi_wifi_state_t     state;
    bool                    want_connected;     /* DMDRVI_IOCTL_WIFI_CONNECT, until DISCONNECT: connect again whenever the link is lost */
    wifi_config_t           sta_config;
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
} radio_t;

extern radio_t *                g_radio;        /* NULL while the radio is stopped */
extern wifi_osi_funcs_t g_osi;

/* ---- log: dmlog and a ring of our own (its address in RTC_CNTL_STORE5: JTAG finds it, no secret goes in) ---- */
void  radio_log(const char *format, ...);
void  radio_log_init(void);

/* ---- glue_host.c ---- */
bool  glue_host_start(void);        /* what the libraries need of the system (dmespradio_init) */

/* ---- osi.c ---- */
void  osi_fill(wifi_osi_funcs_t *osi);
void *osi_pool_alloc(size_t size);
void  osi_release(void *p);            /* what the pool or the heap gave */

/* ---- radio.c ---- */
uint64_t radio_time_us(void);
uint32_t radio_random(void);
void  radio_base_mac(uint8_t mac[6]);
void  radio_power_domain_on(void);
void  radio_clock_enable(void);
void  radio_clock_disable(void);
void  radio_phy_enable(void);
void  radio_phy_disable(void);

/* ---- wifi.c ---- */
int   wifi_event(const char *base, int32_t id, void *data, size_t size);
int   wifi_start(const dmespwifi_port_config_t *config);
void  wifi_stop(void);
int   wifi_scan(radio_t *radio, dmdrvi_wifi_scan_t *scan);
int   wifi_connect(radio_t *radio, const dmdrvi_wifi_connect_t *request);
int   wifi_disconnect(radio_t *radio);
int   wifi_set_enabled(radio_t *radio, bool enabled);
void  wifi_get_status(radio_t *radio, dmdrvi_wifi_status_t *status);
void  wifi_get_info(radio_t *radio, dmdrvi_wifi_info_t *info);
int   wifi_send_frame(radio_t *radio, const void *frame, size_t size);
int   wifi_receive_frame(radio_t *radio, void *buffer, size_t size, size_t *received, uint32_t timeout_ms);
void  wifi_set_running(radio_t *radio, bool running);
int   wifi_rx_frame(void *buffer, uint16_t length, void *eb);

#endif /* DMESPWIFI_PORT_PRIVATE_H */
