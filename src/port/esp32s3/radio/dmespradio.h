/* The radio of the ESP32-S3: the libraries of Espressif (libnet80211, libpp, libcore, libphy) and the supplicant, as a library of the system
 * (libdmespwifi_port.a, radio/build.sh). dmod-boot links it (dmod_link_builtin) and the driver calls it through the Built-in API below: the functions
 * of the libraries under their own names (esp_err_t is int32_t; ESP_OK is 0). */
#ifndef DMESPRADIO_H
#define DMESPRADIO_H

#include "dmod_defs.h"
#include "dmod_types.h"
#include "dmwifi_abi.h"

#ifndef dmod_dmespradio_api
#   define dmod_dmespradio_api(VERSION, RET, NAME, PARAMS)     DMOD_BUILTIN_API(dmespradio, VERSION, RET, NAME, PARAMS)
#endif

/* what the libraries call that is not in the ROM (the glue of the library forwards to it) */
typedef struct
{
    void (*free)(void *ptr);
    const wifi_osi_funcs_t *osi;                            /* the table the libraries get (filled before supplicant_init is called) */
    int  (*vprintf)(const char *format, va_list args);      /* the log of the libraries */
} dmespradio_host_t;

/* X(return type, function of the libraries, parameters, arguments) */
#define DMESPRADIO_FUNCTIONS(X) \
    X(int32_t, esp_wifi_init_internal,           (const wifi_init_config_t *config),                   (config)) \
    X(int32_t, esp_wifi_deinit_internal,         (void),                                               ()) \
    X(int32_t, esp_wifi_start,                   (void),                                               ()) \
    X(int32_t, esp_wifi_stop,                    (void),                                               ()) \
    X(int32_t, esp_wifi_set_mode,                (wifi_mode_t mode),                                   (mode)) \
    X(int32_t, esp_wifi_scan_start,              (const void *config, bool block),                     (config, block)) \
    X(int32_t, esp_wifi_scan_get_ap_num,         (uint16_t *number),                                   (number)) \
    X(int32_t, esp_wifi_scan_get_ap_records,     (uint16_t *number, wifi_ap_record_t *records),        (number, records)) \
    X(int32_t, esp_wifi_connect,                 (void),                                               ()) \
    X(int32_t, esp_wifi_disconnect,              (void),                                               ()) \
    X(int32_t, esp_wifi_set_config,              (int interface, void *config),                        (interface, config)) \
    X(int32_t, esp_wifi_get_mac,                 (int interface, uint8_t *mac),                        (interface, mac)) \
    X(int32_t, esp_wifi_internal_tx,             (int interface, void *buffer, uint16_t length),       (interface, buffer, length)) \
    X(int32_t, esp_wifi_internal_reg_rxcb,       (int interface, void *callback),                      (interface, callback)) \
    X(void,    esp_wifi_internal_free_rx_buffer, (void *buffer),                                       (buffer)) \
    X(int32_t, register_chipv7_phy,              (const void *init_data, void *cal_data, int cal_mode), (init_data, cal_data, cal_mode)) \
    X(char *,  get_phy_version_str,              (void),                                               ()) \
    X(void,    phy_param_track_tot,              (bool wifi, bool ble_154),                            (wifi, ble_154)) \
    X(int32_t, esp_wifi_internal_set_log_level,  (int level),                                          (level)) \
    X(void,    phy_bbpll_en_usb,                 (bool enable),                                        (enable)) /* PHY init disables the USB PHY unless called with true first */ \
    X(int32_t, esp_wifi_set_country,             (const wifi_country_t *country),                      (country)) \
    X(int32_t, esp_wifi_set_ps,                  (int type),                                           (type)) \
    X(int32_t, esp_wifi_sta_get_ap_info,         (wifi_ap_record_t *record),                           (record))

#define DMESPRADIO_API(RET, FUNCTION, PARAMS, ARGS)     dmod_dmespradio_api(1.0, RET, _##FUNCTION, PARAMS);
DMESPRADIO_FUNCTIONS(DMESPRADIO_API)

/* What the libraries need of the system; before anything else */
dmod_dmespradio_api(1.0, void, _init, (const dmespradio_host_t *host));

/* Registers the supplicant (the WPA/RSN parser: a scan needs it), after esp_wifi_init_internal */
dmod_dmespradio_api(1.0, int32_t, _supplicant_init, (void));

/* The table of the cryptography for the init config (wpa_crypto_funcs_t) */
dmod_dmespradio_api(1.0, const void *, _crypto_funcs, (void));

#endif /* DMESPRADIO_H */
