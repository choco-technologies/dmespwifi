#ifndef DMESPWIFI_H
#define DMESPWIFI_H

/*
 * dmespwifi - the Wi-Fi of the ESP32-S3 itself, as a network node.
 *
 * The node is a network device like dmeth's and dmesphosted's: read() / write() are 802.3 frames, DMDRVI_IOCTL_NET_* work as for any network
 * driver, the Wi-Fi is dmdrvi's DMDRVI_IOCTL_WIFI_* (scan, connect, status, info) - the `wifi` tool of dmnet works with it - and the driver
 * registers the interface with dmnetif itself.
 *
 * The radio is run by the libraries of Espressif, which live in an image of their own in the flash (image/): this module only gives
 * them the system they run on.
 */
#include "dmespwifi_defs.h"

/** @brief The chip the radio is on. */
dmod_dmespwifi_api(1.0, const char *, _chip_name, ( void ));

#endif /* DMESPWIFI_H */
