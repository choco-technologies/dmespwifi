#ifndef DMESPWIFI_PORT_H
#define DMESPWIFI_PORT_H

/*
 * The port of dmespwifi: the radio of a CPU family (src/port/<family>/). The driver (dmespwifi) is the node, its configuration and the
 * interface of dmnetif; the port is the radio itself - it starts it, keeps the connection, scans and carries the frames.
 *
 * There is one radio: the functions have no instance. The Wi-Fi types are dmdrvi's (DMDRVI_IOCTL_WIFI_*): the driver hands them through.
 */
#include "dmod_types.h"
#include "dmdrvi_ioctl.h"
#include "dmespwifi_port_defs.h"

/** @brief What the driver gives the port when it starts the radio. */
typedef struct
{
    int     priority;                   /**< of the threads of the radio (1..4) */
    int     log_level;                  /**< of the radio: 0 errors, 1 warnings, 2 info, 3 debug, 4 verbose */
    void  (*ready)(void *arg);          /**< called (once, from the thread of the radio) when the radio answers; may be NULL */
    void   *arg;                        /**< for ready */
} dmespwifi_port_config_t;

/* --- the radio --- */

/** @brief The chip the radio is on (e.g. "ESP32-S3"). */
dmod_dmespwifi_port_api(1.0, const char *, _chip_name, ( void ));

/** @brief Starts the radio; it comes up in the background (config->ready). 0, or a negative errno. */
dmod_dmespwifi_port_api(1.0, int, _radio_start, ( const dmespwifi_port_config_t *config ));

/** @brief Stops the radio. */
dmod_dmespwifi_port_api(1.0, void, _radio_stop, ( void ));

/** @brief True once the radio answers (the station exists). */
dmod_dmespwifi_port_api(1.0, bool, _radio_ready, ( void ));

/** @brief The MAC address of the radio. 0, -ENODEV before the radio answers. */
dmod_dmespwifi_port_api(1.0, int, _get_mac, ( uint8_t mac[DMDRVI_WIFI_MAC_LEN] ));

/* --- the Wi-Fi --- */

/** @brief The networks around (DMDRVI_IOCTL_WIFI_SCAN). 0, or a negative errno. */
dmod_dmespwifi_port_api(1.0, int, _scan, ( dmdrvi_wifi_scan_t *scan ));

/** @brief Connects to a network and connects again whenever the link is lost, until _disconnect (DMDRVI_IOCTL_WIFI_CONNECT). */
dmod_dmespwifi_port_api(1.0, int, _connect, ( const dmdrvi_wifi_connect_t *request ));

/** @brief Leaves the network (DMDRVI_IOCTL_WIFI_DISCONNECT). */
dmod_dmespwifi_port_api(1.0, int, _disconnect, ( void ));

/** @brief The state of the connection (DMDRVI_IOCTL_WIFI_GET_STATUS). */
dmod_dmespwifi_port_api(1.0, void, _get_status, ( dmdrvi_wifi_status_t *status ));

/** @brief The driver, the chip, the firmware of the radio and the counters (DMDRVI_IOCTL_WIFI_GET_INFO). */
dmod_dmespwifi_port_api(1.0, void, _get_info, ( dmdrvi_wifi_info_t *info ));

/* --- the frames (802.3) --- */

/** @brief Frames are carried only between DMDRVI_IOCTL_NET_START and _STOP: what comes before is dropped. */
dmod_dmespwifi_port_api(1.0, void, _set_running, ( bool running ));

/** @brief Sends one frame. 0, -EIO when it cannot go (not connected, not running), -EINVAL for a bad size. */
dmod_dmespwifi_port_api(1.0, int, _transmit_frame, ( const void *frame, size_t size ));

/** @brief One received frame within timeout_ms: 0 and *received (0: none came). */
dmod_dmespwifi_port_api(1.0, int, _receive_frame, ( void *buffer, size_t size, size_t *received, uint32_t timeout_ms ));

#endif /* DMESPWIFI_PORT_H */
