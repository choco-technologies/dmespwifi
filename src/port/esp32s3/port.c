/*
 * The port of dmespwifi for the ESP32-S3: the radio of the chip. The libraries of Espressif run it (radio/: linked into the firmware as
 * libdmespwifi_port.a, called through the Built-in API dmespradio); osi.c gives them the system, radio.c the clocks and the PHY, wifi.c
 * starts the radio, keeps the connection and carries the frames. This file is the API of the port (include/dmespwifi_port.h).
 */
#define DMOD_ENABLE_REGISTRATION    ON
#include "private.h"
#include <errno.h>
#include <string.h>

/* ---- DMOD lifecycle ---- */

int dmod_init(const Dmod_Config_t *Config)
{
    (void) Config;
    return 0;
}

int dmod_deinit(void)
{
    wifi_stop();
    return 0;
}

/* ---- the radio ---- */

dmod_dmespwifi_port_api_declaration(1.0, const char *, _chip_name, ( void ))
{
    return "ESP32-S3";
}

dmod_dmespwifi_port_api_declaration(1.0, int, _radio_start, ( const dmespwifi_port_config_t *config ))
{
    if (config == NULL || config->priority < 1 || config->priority > 4 || config->log_level < 0 || config->log_level > 4)
        return -EINVAL;
    return wifi_start(config);
}

dmod_dmespwifi_port_api_declaration(1.0, void, _radio_stop, ( void ))
{
    wifi_stop();
}

dmod_dmespwifi_port_api_declaration(1.0, bool, _radio_ready, ( void ))
{
    radio_t *c = g_radio;
    if (c == NULL)
        return false;
    dmosi_mutex_lock(c->lock);
    bool started = c->started;
    dmosi_mutex_unlock(c->lock);
    return started;
}

dmod_dmespwifi_port_api_declaration(1.0, int, _get_mac, ( uint8_t mac[DMDRVI_WIFI_MAC_LEN] ))
{
    radio_t *c = g_radio;
    if (c == NULL || mac == NULL)
        return -ENODEV;
    dmosi_mutex_lock(c->lock);
    bool started = c->started;
    memcpy(mac, c->mac, DMDRVI_WIFI_MAC_LEN);
    dmosi_mutex_unlock(c->lock);
    return started ? 0 : -ENODEV;
}

/* ---- the Wi-Fi ---- */

dmod_dmespwifi_port_api_declaration(1.0, int, _scan, ( dmdrvi_wifi_scan_t *scan ))
{
    return (g_radio != NULL) ? wifi_scan(g_radio, scan) : -ENODEV;
}

dmod_dmespwifi_port_api_declaration(1.0, int, _connect, ( const dmdrvi_wifi_connect_t *request ))
{
    if (request == NULL)
        return -EINVAL;
    return (g_radio != NULL) ? wifi_connect(g_radio, request) : -ENODEV;
}

dmod_dmespwifi_port_api_declaration(1.0, int, _disconnect, ( void ))
{
    return (g_radio != NULL) ? wifi_disconnect(g_radio) : -ENODEV;
}

dmod_dmespwifi_port_api_declaration(1.0, void, _get_status, ( dmdrvi_wifi_status_t *status ))
{
    if (status == NULL)
        return;
    if (g_radio != NULL)
        wifi_get_status(g_radio, status);
    else
    {
        memset(status, 0, sizeof(*status));
        status->state = DMDRVI_WIFI_STATE_OFF;
    }
}

dmod_dmespwifi_port_api_declaration(1.0, void, _get_info, ( dmdrvi_wifi_info_t *info ))
{
    if (info == NULL)
        return;
    if (g_radio != NULL)
        wifi_get_info(g_radio, info);
    else
        memset(info, 0, sizeof(*info));
}

/* ---- the frames ---- */

dmod_dmespwifi_port_api_declaration(1.0, void, _set_running, ( bool running ))
{
    if (g_radio != NULL)
        wifi_set_running(g_radio, running);
}

dmod_dmespwifi_port_api_declaration(1.0, int, _transmit_frame, ( const void *frame, size_t size ))
{
    if (frame == NULL)
        return -EINVAL;
    return (g_radio != NULL) ? wifi_send_frame(g_radio, frame, size) : -EIO;
}

dmod_dmespwifi_port_api_declaration(1.0, int, _receive_frame, ( void *buffer, size_t size, size_t *received, uint32_t timeout_ms ))
{
    if (buffer == NULL || received == NULL)
        return -EINVAL;
    *received = 0;
    if (g_radio == NULL)
    {
        dmosi_thread_sleep(timeout_ms);             /* (no radio: nothing comes, the reader waits as it would) */
        return 0;
    }
    return wifi_receive_frame(g_radio, buffer, size, received, timeout_ms);
}
