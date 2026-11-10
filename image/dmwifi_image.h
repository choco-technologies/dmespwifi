/* The image of the Espressif Wi-Fi libraries (libnet80211, libpp, libcore, libphy): what the code around it sees of it.
 * The image is linked at fixed addresses (image.ld) and put in the flash; its first bytes are this header. The code around it
 * loads .data/.bss, calls init() with the services it gives the libraries and then calls the libraries through the table. */
#ifndef DMWIFI_IMAGE_H
#define DMWIFI_IMAGE_H

#include "dmwifi_abi.h"

#define DMWIFI_IMAGE_MAGIC      0x49464D44u     /* "DMFI" */
#define DMWIFI_IMAGE_ABI        1u

/* what the libraries call that is not in the ROM (the glue of the image forwards to it) */
typedef struct
{
    void (*free)(void *ptr);
    const wifi_osi_funcs_t *osi;                            /* the table the libraries get (filled before supplicant_init is called) */
    int  (*vprintf)(const char *format, va_list args);      /* the log of the libraries */
} dmwifi_host_t;

/* the functions of the libraries the driver calls (esp_err_t is int32_t; ESP_OK is 0) */
typedef struct
{
    int32_t (*esp_wifi_init_internal)(const wifi_init_config_t *config);
    int32_t (*esp_wifi_deinit_internal)(void);
    int32_t (*esp_wifi_start)(void);
    int32_t (*esp_wifi_stop)(void);
    int32_t (*esp_wifi_set_mode)(wifi_mode_t mode);
    int32_t (*esp_wifi_scan_start)(const void *config, bool block);
    int32_t (*esp_wifi_scan_get_ap_num)(uint16_t *number);
    int32_t (*esp_wifi_scan_get_ap_records)(uint16_t *number, wifi_ap_record_t *records);
    int32_t (*esp_wifi_connect)(void);
    int32_t (*esp_wifi_disconnect)(void);
    int32_t (*esp_wifi_set_config)(int interface, void *config);
    int32_t (*esp_wifi_get_mac)(int interface, uint8_t mac[6]);
    int32_t (*esp_wifi_internal_tx)(int interface, void *buffer, uint16_t length);
    int32_t (*esp_wifi_internal_reg_rxcb)(int interface, void *callback);
    void    (*esp_wifi_internal_free_rx_buffer)(void *buffer);
    int32_t (*register_chipv7_phy)(const void *init_data, void *cal_data, int cal_mode);
    char *  (*get_phy_version_str)(void);
    void    (*phy_param_track_tot)(bool wifi, bool ble_154);
    int32_t (*esp_wifi_internal_set_log_level)(int level);
    int32_t (*supplicant_init)(void);                   /* registers the supplicant (the WPA/RSN parser: a scan needs it) after esp_wifi_init_internal */
    void    (*phy_bbpll_en_usb)(bool enable);       /* PHY init disables the USB PHY (the USB-Serial-JTAG) unless this is called with true first */
} dmwifi_image_api_t;

typedef struct
{
    uint32_t    magic;
    uint32_t    abi;                /* DMWIFI_IMAGE_ABI */
    uint32_t    image_size;         /* bytes, the header included */
    uint32_t    data_load;          /* where the initial .data is (data bus address) */
    uint32_t    data_addr;          /* ... and where it goes (RAM) */
    uint32_t    data_size;
    uint32_t    bss_addr;
    uint32_t    bss_size;
    uint32_t    ram_end;            /* the end of what the image needs of the RAM (checked against the space reserved for it) */
    void      (*init)(const dmwifi_host_t *host);
    dmwifi_image_api_t api;
} dmwifi_image_header_t;

#endif /* DMWIFI_IMAGE_H */
