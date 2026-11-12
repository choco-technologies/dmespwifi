/* The ABI of the Espressif Wi-Fi libraries (libnet80211, libpp, libphy): the structures that cross the line between them and the
 * code around them, copied from ESP-IDF 5.2 (components/esp_wifi/include, Apache-2.0) for ESP32-S3. Nothing else of ESP-IDF is needed
 * to call the libraries: the code around fills these tables. */
#ifndef DMWIFI_ABI_H
#define DMWIFI_ABI_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>


#define ESP_WIFI_OS_ADAPTER_VERSION  0x00000008
#define ESP_WIFI_OS_ADAPTER_MAGIC    0xDEADBEAF
#define OSI_FUNCS_TIME_BLOCKING      0xffffffff
#define WIFI_INIT_CONFIG_MAGIC       0x1F2F3F4F


typedef struct {
    int32_t _version;
    bool (* _env_is_chip)(void);
    void (*_set_intr)(int32_t cpu_no, uint32_t intr_source, uint32_t intr_num, int32_t intr_prio);
    void (*_clear_intr)(uint32_t intr_source, uint32_t intr_num);
    void (*_set_isr)(int32_t n, void *f, void *arg);
    void (*_ints_on)(uint32_t mask);
    void (*_ints_off)(uint32_t mask);
    bool (* _is_from_isr)(void);
    void *(* _spin_lock_create)(void);
    void (* _spin_lock_delete)(void *lock);
    uint32_t (*_wifi_int_disable)(void *wifi_int_mux);
    void (*_wifi_int_restore)(void *wifi_int_mux, uint32_t tmp);
    void (*_task_yield_from_isr)(void);
    void *(*_semphr_create)(uint32_t max, uint32_t init);
    void (*_semphr_delete)(void *semphr);
    int32_t (*_semphr_take)(void *semphr, uint32_t block_time_tick);
    int32_t (*_semphr_give)(void *semphr);
    void *(*_wifi_thread_semphr_get)(void);
    void *(*_mutex_create)(void);
    void *(*_recursive_mutex_create)(void);
    void (*_mutex_delete)(void *mutex);
    int32_t (*_mutex_lock)(void *mutex);
    int32_t (*_mutex_unlock)(void *mutex);
    void *(* _queue_create)(uint32_t queue_len, uint32_t item_size);
    void (* _queue_delete)(void *queue);
    int32_t (* _queue_send)(void *queue, void *item, uint32_t block_time_tick);
    int32_t (* _queue_send_from_isr)(void *queue, void *item, void *hptw);
    int32_t (* _queue_send_to_back)(void *queue, void *item, uint32_t block_time_tick);
    int32_t (* _queue_send_to_front)(void *queue, void *item, uint32_t block_time_tick);
    int32_t (* _queue_recv)(void *queue, void *item, uint32_t block_time_tick);
    uint32_t (* _queue_msg_waiting)(void *queue);
    void *(* _event_group_create)(void);
    void (* _event_group_delete)(void *event);
    uint32_t (* _event_group_set_bits)(void *event, uint32_t bits);
    uint32_t (* _event_group_clear_bits)(void *event, uint32_t bits);
    uint32_t (* _event_group_wait_bits)(void *event, uint32_t bits_to_wait_for, int clear_on_exit, int wait_for_all_bits, uint32_t block_time_tick);
    int32_t (* _task_create_pinned_to_core)(void *task_func, const char *name, uint32_t stack_depth, void *param, uint32_t prio, void *task_handle, uint32_t core_id);
    int32_t (* _task_create)(void *task_func, const char *name, uint32_t stack_depth, void *param, uint32_t prio, void *task_handle);
    void (* _task_delete)(void *task_handle);
    void (* _task_delay)(uint32_t tick);
    int32_t (* _task_ms_to_tick)(uint32_t ms);
    void *(* _task_get_current_task)(void);
    int32_t (* _task_get_max_priority)(void);
    void *(* _malloc)(size_t size);
    void (* _free)(void *p);
    int32_t (* _event_post)(const char* event_base, int32_t event_id, void* event_data, size_t event_data_size, uint32_t ticks_to_wait);
    uint32_t (* _get_free_heap_size)(void);
    uint32_t (* _rand)(void);
    void (* _dport_access_stall_other_cpu_start_wrap)(void);
    void (* _dport_access_stall_other_cpu_end_wrap)(void);
    void (* _wifi_apb80m_request)(void);
    void (* _wifi_apb80m_release)(void);
    void (* _phy_disable)(void);
    void (* _phy_enable)(void);
    int (* _phy_update_country_info)(const char* country);
    int (* _read_mac)(uint8_t* mac, unsigned int type);
    void (* _timer_arm)(void *timer, uint32_t tmout, bool repeat);
    void (* _timer_disarm)(void *timer);
    void (* _timer_done)(void *ptimer);
    void (* _timer_setfn)(void *ptimer, void *pfunction, void *parg);
    void (* _timer_arm_us)(void *ptimer, uint32_t us, bool repeat);
    void (* _wifi_reset_mac)(void);
    void (* _wifi_clock_enable)(void);
    void (* _wifi_clock_disable)(void);
    void (* _wifi_rtc_enable_iso)(void);
    void (* _wifi_rtc_disable_iso)(void);
    int64_t (* _esp_timer_get_time)(void);
    int (* _nvs_set_i8)(uint32_t handle, const char* key, int8_t value);
    int (* _nvs_get_i8)(uint32_t handle, const char* key, int8_t* out_value);
    int (* _nvs_set_u8)(uint32_t handle, const char* key, uint8_t value);
    int (* _nvs_get_u8)(uint32_t handle, const char* key, uint8_t* out_value);
    int (* _nvs_set_u16)(uint32_t handle, const char* key, uint16_t value);
    int (* _nvs_get_u16)(uint32_t handle, const char* key, uint16_t* out_value);
    int (* _nvs_open)(const char* name, unsigned int open_mode, uint32_t *out_handle);
    void (* _nvs_close)(uint32_t handle);
    int (* _nvs_commit)(uint32_t handle);
    int (* _nvs_set_blob)(uint32_t handle, const char* key, const void* value, size_t length);
    int (* _nvs_get_blob)(uint32_t handle, const char* key, void* out_value, size_t* length);
    int (* _nvs_erase_key)(uint32_t handle, const char* key);
    int (* _get_random)(uint8_t *buf, size_t len);
    int (* _get_time)(void *t);
    unsigned long (* _random)(void);
    uint32_t (* _slowclk_cal_get)(void);
    void (* _log_write)(unsigned int level, const char* tag, const char* format, ...);
    void (* _log_writev)(unsigned int level, const char* tag, const char* format, va_list args);
    uint32_t (* _log_timestamp)(void);
    void * (* _malloc_internal)(size_t size);
    void * (* _realloc_internal)(void *ptr, size_t size);
    void * (* _calloc_internal)(size_t n, size_t size);
    void * (* _zalloc_internal)(size_t size);
    void * (* _wifi_malloc)(size_t size);
    void * (* _wifi_realloc)(void *ptr, size_t size);
    void * (* _wifi_calloc)(size_t n, size_t size);
    void * (* _wifi_zalloc)(size_t size);
    void * (* _wifi_create_queue)(int queue_len, int item_size);
    void (* _wifi_delete_queue)(void * queue);
    int (* _coex_init)(void);
    void (* _coex_deinit)(void);
    int (* _coex_enable)(void);
    void (* _coex_disable)(void);
    uint32_t (* _coex_status_get)(void);
    void (* _coex_condition_set)(uint32_t type, bool dissatisfy);
    int (* _coex_wifi_request)(uint32_t event, uint32_t latency, uint32_t duration);
    int (* _coex_wifi_release)(uint32_t event);
    int (* _coex_wifi_channel_set)(uint8_t primary, uint8_t secondary);
    int (* _coex_event_duration_get)(uint32_t event, uint32_t *duration);
    int (* _coex_pti_get)(uint32_t event, uint8_t *pti);
    void (* _coex_schm_status_bit_clear)(uint32_t type, uint32_t status);
    void (* _coex_schm_status_bit_set)(uint32_t type, uint32_t status);
    int (* _coex_schm_interval_set)(uint32_t interval);
    uint32_t (* _coex_schm_interval_get)(void);
    uint8_t (* _coex_schm_curr_period_get)(void);
    void * (* _coex_schm_curr_phase_get)(void);
    int (* _coex_schm_process_restart)(void);
    int (* _coex_schm_register_cb)(int, int (* cb)(int));
    int (* _coex_register_start_cb)(int (* cb)(void));
    int32_t _magic;
} wifi_osi_funcs_t;


/* a callback of the crypto table: only its size matters here */
typedef void (*dmwifi_fn_t)(void);

typedef struct {
    uint32_t size;                                   /**< The crypto callback function structure size */
    uint32_t version;                                /**< The crypto callback function structure version */
    dmwifi_fn_t aes_wrap;                         /**< The AES wrap callback function used by esp_wifi */
    dmwifi_fn_t aes_unwrap;                     /**< The AES unwrap callback function used by esp_wifi */
    dmwifi_fn_t hmac_sha256_vector;     /**< The SHA256 callback function used by esp_wifi */
    dmwifi_fn_t sha256_prf;                     /**< The SHA256 PRF callback function used by esp_wifi */
    dmwifi_fn_t hmac_md5;                         /**< HMAC-MD5 callback function over data buffer (RFC 2104) */
    dmwifi_fn_t hamc_md5_vector;           /**< HMAC-MD5 callback function over data vector (RFC 2104) */
    dmwifi_fn_t hmac_sha1;                       /**< HMAC-SHA1 callback function over data buffer (RFC 2104) */
    dmwifi_fn_t hmac_sha1_vector;         /**< HMAC-SHA1 callback function over data vector (RFC 2104) */
    dmwifi_fn_t sha1_prf;                         /**< SHA1-based Pseudo-Random Function (PRF) (IEEE 802.11i, 8.5.1.1) callback function */
    dmwifi_fn_t sha1_vector;                   /**< SHA-1 hash callback function for data vector */
    dmwifi_fn_t pbkdf2_sha1;                   /**< SHA1-based key derivation function (PBKDF2) callback function for IEEE 802.11i */
    dmwifi_fn_t rc4_skip;                         /**< XOR RC4 stream callback function to given data with skip-stream-start */
    dmwifi_fn_t md5_vector;                     /**< MD5 hash callback function for data vector */
    dmwifi_fn_t aes_encrypt;                   /**< Encrypt one AES block callback function */
    dmwifi_fn_t aes_encrypt_init;         /**< Initialize AES callback function for encryption */
    dmwifi_fn_t aes_encrypt_deinit;     /**< Deinitialize AES encryption callback function */
    dmwifi_fn_t aes_decrypt;                   /**< Decrypt one AES block callback function */
    dmwifi_fn_t aes_decrypt_init;         /**< Initialize AES callback function for decryption */
    dmwifi_fn_t aes_decrypt_deinit;     /**< Deinitialize AES decryption callback function */
    dmwifi_fn_t aes_128_encrypt;           /**< The AES 128 encrypt callback function used by esp_wifi */
    dmwifi_fn_t aes_128_decrypt;           /**< The AES 128 decrypt callback function used by esp_wifi */
    dmwifi_fn_t omac1_aes_128;               /**< One-Key CBC MAC (OMAC1) hash with AES-128 callback function for MIC computation */
    dmwifi_fn_t ccmp_decrypt;                 /**< Decrypt data callback function using CCMP */
    dmwifi_fn_t ccmp_encrypt;                 /**< Encrypt data callback function using CCMP */
    dmwifi_fn_t aes_gmac;                         /**< One-Key GMAC hash callback function with AES for MIC computation */
    dmwifi_fn_t sha256_vector;               /**< SHA256 hash callback function for data vector */
    dmwifi_fn_t crc32;                            /**< CRC32 value callback function in little endian */
}wpa_crypto_funcs_t;


typedef struct {
    wifi_osi_funcs_t*      osi_funcs;              /**< WiFi OS functions */
    wpa_crypto_funcs_t     wpa_crypto_funcs;       /**< WiFi station crypto functions when connect */
    int                    static_rx_buf_num;      /**< WiFi static RX buffer number */
    int                    dynamic_rx_buf_num;     /**< WiFi dynamic RX buffer number */
    int                    tx_buf_type;            /**< WiFi TX buffer type */
    int                    static_tx_buf_num;      /**< WiFi static TX buffer number */
    int                    dynamic_tx_buf_num;     /**< WiFi dynamic TX buffer number */
    int                    rx_mgmt_buf_type;       /**< WiFi RX MGMT buffer type */
    int                    rx_mgmt_buf_num;        /**< WiFi RX MGMT buffer number */
    int                    cache_tx_buf_num;       /**< WiFi TX cache buffer number */
    int                    csi_enable;             /**< WiFi channel state information enable flag */
    int                    ampdu_rx_enable;        /**< WiFi AMPDU RX feature enable flag */
    int                    ampdu_tx_enable;        /**< WiFi AMPDU TX feature enable flag */
    int                    amsdu_tx_enable;        /**< WiFi AMSDU TX feature enable flag */
    int                    nvs_enable;             /**< WiFi NVS flash enable flag */
    int                    nano_enable;            /**< Nano option for printf/scan family enable flag */
    int                    rx_ba_win;              /**< WiFi Block Ack RX window size */
    int                    wifi_task_core_id;      /**< WiFi Task Core ID */
    int                    beacon_max_len;         /**< WiFi softAP maximum length of the beacon */
    int                    mgmt_sbuf_num;          /**< WiFi management short buffer number, the minimum value is 6, the maximum value is 32 */
    uint64_t               feature_caps;           /**< Enables additional WiFi features and capabilities */
    bool                   sta_disconnected_pm;    /**< WiFi Power Management for station at disconnected status */
    int                    espnow_max_encrypt_num; /**< Maximum encrypt number of peers supported by espnow */
    int                    magic;                  /**< WiFi init magic number, it should be the last field */
} wifi_init_config_t;

typedef enum {
    WIFI_SECOND_CHAN_NONE = 0,  /**< the channel width is HT20 */
    WIFI_SECOND_CHAN_ABOVE,     /**< the channel width is HT40 and the secondary channel is above the primary channel */
    WIFI_SECOND_CHAN_BELOW,     /**< the channel width is HT40 and the secondary channel is below the primary channel */
} wifi_second_chan_t;

typedef enum {
    WIFI_AUTH_OPEN = 0,         /**< authenticate mode : open */
    WIFI_AUTH_WEP,              /**< authenticate mode : WEP */
    WIFI_AUTH_WPA_PSK,          /**< authenticate mode : WPA_PSK */
    WIFI_AUTH_WPA2_PSK,         /**< authenticate mode : WPA2_PSK */
    WIFI_AUTH_WPA_WPA2_PSK,     /**< authenticate mode : WPA_WPA2_PSK */
    WIFI_AUTH_ENTERPRISE,       /**< authenticate mode : WiFi EAP security */
    WIFI_AUTH_WPA2_ENTERPRISE = WIFI_AUTH_ENTERPRISE,  /**< authenticate mode : WiFi EAP security */
    WIFI_AUTH_WPA3_PSK,         /**< authenticate mode : WPA3_PSK */
    WIFI_AUTH_WPA2_WPA3_PSK,    /**< authenticate mode : WPA2_WPA3_PSK */
    WIFI_AUTH_WAPI_PSK,         /**< authenticate mode : WAPI_PSK */
    WIFI_AUTH_OWE,              /**< authenticate mode : OWE */
    WIFI_AUTH_WPA3_ENT_192,     /**< authenticate mode : WPA3_ENT_SUITE_B_192_BIT */
    WIFI_AUTH_WPA3_EXT_PSK,     /**< this authentication mode will yield same result as WIFI_AUTH_WPA3_PSK and not recommended to be used. It will be deprecated in future, please use WIFI_AUTH_WPA3_PSK instead. */
    WIFI_AUTH_WPA3_EXT_PSK_MIXED_MODE, /**< this authentication mode will yield same result as WIFI_AUTH_WPA3_PSK and not recommended to be used. It will be deprecated in future, please use WIFI_AUTH_WPA3_PSK instead.*/
    WIFI_AUTH_MAX
} wifi_auth_mode_t;

typedef enum {
    WIFI_CIPHER_TYPE_NONE = 0,   /**< the cipher type is none */
    WIFI_CIPHER_TYPE_WEP40,      /**< the cipher type is WEP40 */
    WIFI_CIPHER_TYPE_WEP104,     /**< the cipher type is WEP104 */
    WIFI_CIPHER_TYPE_TKIP,       /**< the cipher type is TKIP */
    WIFI_CIPHER_TYPE_CCMP,       /**< the cipher type is CCMP */
    WIFI_CIPHER_TYPE_TKIP_CCMP,  /**< the cipher type is TKIP and CCMP */
    WIFI_CIPHER_TYPE_AES_CMAC128,/**< the cipher type is AES-CMAC-128 */
    WIFI_CIPHER_TYPE_SMS4,       /**< the cipher type is SMS4 */
    WIFI_CIPHER_TYPE_GCMP,       /**< the cipher type is GCMP */
    WIFI_CIPHER_TYPE_GCMP256,    /**< the cipher type is GCMP-256 */
    WIFI_CIPHER_TYPE_AES_GMAC128,/**< the cipher type is AES-GMAC-128 */
    WIFI_CIPHER_TYPE_AES_GMAC256,/**< the cipher type is AES-GMAC-256 */
    WIFI_CIPHER_TYPE_UNKNOWN,    /**< the cipher type is unknown */
} wifi_cipher_type_t;

typedef enum {
    WIFI_ANT_ANT0,          /**< WiFi antenna 0 */
    WIFI_ANT_ANT1,          /**< WiFi antenna 1 */
    WIFI_ANT_MAX,           /**< Invalid WiFi antenna */
} wifi_ant_t;

typedef enum {
    WIFI_COUNTRY_POLICY_AUTO,   /**< Country policy is auto, use the country info of AP to which the station is connected */
    WIFI_COUNTRY_POLICY_MANUAL, /**< Country policy is manual, always use the configured country info */
} wifi_country_policy_t;

typedef enum {
    WIFI_MODE_NULL = 0,  /**< null mode */
    WIFI_MODE_STA,       /**< WiFi station mode */
    WIFI_MODE_AP,        /**< WiFi soft-AP mode */
    WIFI_MODE_APSTA,     /**< WiFi station + soft-AP mode */
    WIFI_MODE_NAN,       /**< WiFi NAN mode */
    WIFI_MODE_MAX
} wifi_mode_t;

typedef enum {
    WIFI_BW_HT20 = 1, /* Bandwidth is HT20 */
    WIFI_BW_HT40,     /* Bandwidth is HT40 */
} wifi_bandwidth_t;

typedef struct {
    char                  cc[3];   /**< country code string */
    uint8_t               schan;   /**< start channel */
    uint8_t               nchan;   /**< total channel number */
    int8_t                max_tx_power;   /**< This field is used for getting WiFi maximum transmitting power, call esp_wifi_set_max_tx_power to set the maximum transmitting power. */
    wifi_country_policy_t policy;  /**< country policy */
} wifi_country_t;

typedef struct {
    uint8_t bss_color:6;                  /**< an unsigned integer whose value is the BSS Color of the BSS corresponding to the AP */
    uint8_t partial_bss_color:1;          /**< indicate if an AID assignment rule based on the BSS color */
    uint8_t bss_color_disabled:1;         /**< indicate if the use of BSS color is disabled */
    uint8_t bssid_index;                  /**< in M-BSSID set, identifies the nontransmitted BSSID */
} wifi_he_ap_info_t;

typedef struct {
    uint8_t bssid[6];                     /**< MAC address of AP */
    uint8_t ssid[33];                     /**< SSID of AP */
    uint8_t primary;                      /**< channel of AP */
    wifi_second_chan_t second;            /**< secondary channel of AP */
    int8_t  rssi;                         /**< signal strength of AP. Note that in some rare cases where signal strength is very strong, rssi values can be slightly positive */
    wifi_auth_mode_t authmode;            /**< authmode of AP */
    wifi_cipher_type_t pairwise_cipher;   /**< pairwise cipher of AP */
    wifi_cipher_type_t group_cipher;      /**< group cipher of AP */
    wifi_ant_t ant;                       /**< antenna used to receive beacon from AP */
    uint32_t phy_11b:1;                   /**< bit: 0 flag to identify if 11b mode is enabled or not */
    uint32_t phy_11g:1;                   /**< bit: 1 flag to identify if 11g mode is enabled or not */
    uint32_t phy_11n:1;                   /**< bit: 2 flag to identify if 11n mode is enabled or not */
    uint32_t phy_lr:1;                    /**< bit: 3 flag to identify if low rate is enabled or not */
    uint32_t phy_11ax:1;                  /**< bit: 4 flag to identify if 11ax mode is enabled or not */
    uint32_t wps:1;                       /**< bit: 5 flag to identify if WPS is supported or not */
    uint32_t ftm_responder:1;             /**< bit: 6 flag to identify if FTM is supported in responder mode */
    uint32_t ftm_initiator:1;             /**< bit: 7 flag to identify if FTM is supported in initiator mode */
    uint32_t reserved:24;                 /**< bit: 8..31 reserved */
    wifi_country_t country;               /**< country information of AP */
    wifi_he_ap_info_t he_ap;              /**< HE AP info */
} wifi_ap_record_t;


/* ---- the station: configuration and events (esp_wifi_types.h) ---- */

#define SAE_H2E_IDENTIFIER_LEN  32

typedef enum {
    WIFI_FAST_SCAN = 0,
    WIFI_ALL_CHANNEL_SCAN,
}wifi_scan_method_t;

typedef enum {
    WIFI_CONNECT_AP_BY_SIGNAL = 0,
    WIFI_CONNECT_AP_BY_SECURITY,
}wifi_sort_method_t;

typedef struct {
    int8_t              rssi;
    wifi_auth_mode_t    authmode;
}wifi_scan_threshold_t;

typedef struct {
    bool capable;
    bool required;
} wifi_pmf_config_t;

typedef enum {
    WPA3_SAE_PWE_UNSPECIFIED,
    WPA3_SAE_PWE_HUNT_AND_PECK,
    WPA3_SAE_PWE_HASH_TO_ELEMENT,
    WPA3_SAE_PWE_BOTH,
} wifi_sae_pwe_method_t;

typedef enum {
    WPA3_SAE_PK_MODE_AUTOMATIC = 0,
    WPA3_SAE_PK_MODE_ONLY = 1,
    WPA3_SAE_PK_MODE_DISABLED = 2,
} wifi_sae_pk_mode_t;

typedef struct {
    uint8_t ssid[32];
    uint8_t password[64];
    wifi_scan_method_t scan_method;
    bool bssid_set;
    uint8_t bssid[6];
    uint8_t channel;
    uint16_t listen_interval;
    wifi_sort_method_t sort_method;
    wifi_scan_threshold_t  threshold;
    wifi_pmf_config_t pmf_cfg;
    uint32_t rm_enabled:1;
    uint32_t btm_enabled:1;
    uint32_t mbo_enabled:1;
    uint32_t ft_enabled:1;
    uint32_t owe_enabled:1;
    uint32_t transition_disable:1;
    uint32_t reserved:26;
    wifi_sae_pwe_method_t sae_pwe_h2e;
    wifi_sae_pk_mode_t sae_pk_mode;
    uint8_t failure_retry_cnt;
    uint32_t he_dcm_set:1;
    uint32_t he_dcm_max_constellation_tx:2;
    uint32_t he_dcm_max_constellation_rx:2;
    uint32_t he_mcs9_enabled:1;
    uint32_t he_su_beamformee_disabled:1;
    uint32_t he_trig_su_bmforming_feedback_disabled:1;
    uint32_t he_trig_mu_bmforming_partial_feedback_disabled:1;
    uint32_t he_trig_cqi_feedback_disabled:1;
    uint32_t he_reserved:22;
    uint8_t sae_h2e_identifier[SAE_H2E_IDENTIFIER_LEN];
} wifi_sta_config_t;

typedef enum {
    WIFI_EVENT_WIFI_READY = 0,
    WIFI_EVENT_SCAN_DONE,
    WIFI_EVENT_STA_START,
    WIFI_EVENT_STA_STOP,
    WIFI_EVENT_STA_CONNECTED,
    WIFI_EVENT_STA_DISCONNECTED,
    WIFI_EVENT_STA_AUTHMODE_CHANGE,

    WIFI_EVENT_STA_WPS_ER_SUCCESS,
    WIFI_EVENT_STA_WPS_ER_FAILED,
    WIFI_EVENT_STA_WPS_ER_TIMEOUT,
    WIFI_EVENT_STA_WPS_ER_PIN,
    WIFI_EVENT_STA_WPS_ER_PBC_OVERLAP,

    WIFI_EVENT_AP_START,
    WIFI_EVENT_AP_STOP,
    WIFI_EVENT_AP_STACONNECTED,
    WIFI_EVENT_AP_STADISCONNECTED,
    WIFI_EVENT_AP_PROBEREQRECVED,

    WIFI_EVENT_FTM_REPORT,

    /* Add next events after this only */
    WIFI_EVENT_STA_BSS_RSSI_LOW,
    WIFI_EVENT_ACTION_TX_STATUS,
    WIFI_EVENT_ROC_DONE,

    WIFI_EVENT_STA_BEACON_TIMEOUT,

    WIFI_EVENT_CONNECTIONLESS_MODULE_WAKE_INTERVAL_START,

    WIFI_EVENT_AP_WPS_RG_SUCCESS,
    WIFI_EVENT_AP_WPS_RG_FAILED,
    WIFI_EVENT_AP_WPS_RG_TIMEOUT,
    WIFI_EVENT_AP_WPS_RG_PIN,
    WIFI_EVENT_AP_WPS_RG_PBC_OVERLAP,

    WIFI_EVENT_ITWT_SETUP,
    WIFI_EVENT_ITWT_TEARDOWN,
    WIFI_EVENT_ITWT_PROBE,
    WIFI_EVENT_ITWT_SUSPEND,
    WIFI_EVENT_TWT_WAKEUP,

    WIFI_EVENT_NAN_STARTED,
    WIFI_EVENT_NAN_STOPPED,
    WIFI_EVENT_NAN_SVC_MATCH,
    WIFI_EVENT_NAN_REPLIED,
    WIFI_EVENT_NAN_RECEIVE,
    WIFI_EVENT_NDP_INDICATION,
    WIFI_EVENT_NDP_CONFIRM,
    WIFI_EVENT_NDP_TERMINATED,
    WIFI_EVENT_HOME_CHANNEL_CHANGE,

    WIFI_EVENT_MAX,
} wifi_event_t;

typedef struct {
    uint32_t status;
    uint8_t  number;
    uint8_t  scan_id;
} wifi_event_sta_scan_done_t;

typedef struct {
    uint8_t ssid[32];
    uint8_t ssid_len;
    uint8_t bssid[6];
    uint8_t channel;
    wifi_auth_mode_t authmode;
    uint16_t aid;
} wifi_event_sta_connected_t;

typedef struct {
    uint8_t ssid[32];
    uint8_t ssid_len;
    uint8_t bssid[6];
    uint8_t reason;
    int8_t  rssi;
} wifi_event_sta_disconnected_t;

/* wifi_config_t is a union of the configurations of the station, of the soft AP and of NAN; the station's is the largest */
typedef union
{
    wifi_sta_config_t   sta;
    uint8_t             room[256];
} wifi_config_t;

#define WIFI_IF_STA     0

#endif /* DMWIFI_ABI_H */
