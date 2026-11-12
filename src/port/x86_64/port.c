/*
 * The port of dmespwifi for the host (x86_64): there is no radio. The driver builds, loads and answers its node; the radio never
 * comes up, so everything that needs it fails (-ENODEV). For the tests only - never released.
 */
#define DMOD_ENABLE_REGISTRATION    ON
#include "dmod.h"
#include "dmosi.h"
#include "dmespwifi_port.h"
#include <errno.h>
#include <string.h>

int dmod_init(const Dmod_Config_t *Config)
{
    (void) Config;
    return 0;
}

int dmod_deinit(void)
{
    return 0;
}

dmod_dmespwifi_port_api_declaration(1.0, const char *, _chip_name, ( void ))
{
    return "none (x86_64)";
}

dmod_dmespwifi_port_api_declaration(1.0, int, _radio_start, ( const dmespwifi_port_config_t *config ))
{
    return (config != NULL) ? 0 : -EINVAL;      /* it starts, and never answers */
}

dmod_dmespwifi_port_api_declaration(1.0, void, _radio_stop, ( void ))
{
}

dmod_dmespwifi_port_api_declaration(1.0, bool, _radio_ready, ( void ))
{
    return false;
}

dmod_dmespwifi_port_api_declaration(1.0, int, _get_mac, ( uint8_t mac[DMDRVI_WIFI_MAC_LEN] ))
{
    (void) mac;
    return -ENODEV;
}

dmod_dmespwifi_port_api_declaration(1.0, int, _scan, ( dmdrvi_wifi_scan_t *scan ))
{
    (void) scan;
    return -ENODEV;
}

dmod_dmespwifi_port_api_declaration(1.0, int, _connect, ( const dmdrvi_wifi_connect_t *request ))
{
    (void) request;
    return -ENODEV;
}

dmod_dmespwifi_port_api_declaration(1.0, int, _disconnect, ( void ))
{
    return -ENODEV;
}

dmod_dmespwifi_port_api_declaration(1.0, void, _get_status, ( dmdrvi_wifi_status_t *status ))
{
    if (status != NULL)
    {
        memset(status, 0, sizeof(*status));
        status->state = DMDRVI_WIFI_STATE_OFF;
    }
}

dmod_dmespwifi_port_api_declaration(1.0, void, _get_info, ( dmdrvi_wifi_info_t *info ))
{
    if (info != NULL)
        memset(info, 0, sizeof(*info));
}

dmod_dmespwifi_port_api_declaration(1.0, void, _set_running, ( bool running ))
{
    (void) running;
}

dmod_dmespwifi_port_api_declaration(1.0, int, _transmit_frame, ( const void *frame, size_t size ))
{
    (void) frame;
    (void) size;
    return -EIO;
}

dmod_dmespwifi_port_api_declaration(1.0, int, _receive_frame, ( void *buffer, size_t size, size_t *received, uint32_t timeout_ms ))
{
    (void) buffer;
    (void) size;
    if (received != NULL)
        *received = 0;
    dmosi_thread_sleep(timeout_ms);
    return 0;
}
