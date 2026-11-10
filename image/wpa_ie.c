/*
 * The supplicant side of the Wi-Fi libraries, the part that a scan needs: the parser of the WPA/RSN information elements of an access
 * point (what ciphers and key management it offers), registered for the libraries as `wpa_funcs` (what esp_supplicant_init() of
 * ESP-IDF does with the whole supplicant).
 *
 * The parser is the one of wpa_supplicant/hostap (src/common/wpa_common.c, src/rsn_supp/wpa_ie.c of ESP-IDF 5.2: BSD licence,
 * (c) Jouni Malinen and the contributors) with its suites and constants (src/common/defs.h, wpa_common.h); the log of the hostap is
 * left out. Built as in ESP-IDF: 802.11w, WPA3-SAE, GCMP, GMAC and OWE are on (they only add suites that the parser knows).
 */
#include "dmwifi_image.h"
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

#define BIT(n)                  (1u << (n))

/* the IE headers and the suites (wpa_common.h, ieee802_11_defs.h) */
#define WLAN_EID_RSN                    48
#define WLAN_EID_WAPI                   68
#define WLAN_EID_VENDOR_SPECIFIC        221
#define WLAN_EID_RSNX                   244

#define WPA_SELECTOR_LEN                4
#define RSN_SELECTOR_LEN                4
#define WPA_VERSION                     1
#define RSN_VERSION                     1
#define PMKID_LEN                       16

#define RSN_SELECTOR(a, b, c, d)        ((((u32) (a)) << 24) | (((u32) (b)) << 16) | (((u32) (c)) << 8) | (u32) (d))
#define RSN_SELECTOR_GET(a)             WPA_GET_BE32((const u8 *) (a))
#define WPA_GET_BE32(a)                 ((((u32) (a)[0]) << 24) | (((u32) (a)[1]) << 16) | (((u32) (a)[2]) << 8) | ((u32) (a)[3]))
#define WPA_GET_LE16(a)                 ((u16) (((a)[1] << 8) | (a)[0]))

#define WPA_OUI_TYPE                    RSN_SELECTOR(0x00, 0x50, 0xf2, 1)
#define WPA_AUTH_KEY_MGMT_NONE          RSN_SELECTOR(0x00, 0x50, 0xf2, 0)
#define WPA_AUTH_KEY_MGMT_UNSPEC_802_1X RSN_SELECTOR(0x00, 0x50, 0xf2, 1)
#define WPA_AUTH_KEY_MGMT_PSK_OVER_802_1X RSN_SELECTOR(0x00, 0x50, 0xf2, 2)
#define WPA_CIPHER_SUITE_NONE           RSN_SELECTOR(0x00, 0x50, 0xf2, 0)
#define WPA_CIPHER_SUITE_WEP40          RSN_SELECTOR(0x00, 0x50, 0xf2, 1)
#define WPA_CIPHER_SUITE_TKIP           RSN_SELECTOR(0x00, 0x50, 0xf2, 2)
#define WPA_CIPHER_SUITE_CCMP           RSN_SELECTOR(0x00, 0x50, 0xf2, 4)
#define WPA_CIPHER_SUITE_WEP104         RSN_SELECTOR(0x00, 0x50, 0xf2, 5)

#define RSN_AUTH_KEY_MGMT_UNSPEC_802_1X RSN_SELECTOR(0x00, 0x0f, 0xac, 1)
#define RSN_AUTH_KEY_MGMT_PSK_OVER_802_1X RSN_SELECTOR(0x00, 0x0f, 0xac, 2)
#define RSN_AUTH_KEY_MGMT_802_1X_SHA256 RSN_SELECTOR(0x00, 0x0f, 0xac, 5)
#define RSN_AUTH_KEY_MGMT_PSK_SHA256    RSN_SELECTOR(0x00, 0x0f, 0xac, 6)
#define RSN_AUTH_KEY_MGMT_SAE           RSN_SELECTOR(0x00, 0x0f, 0xac, 8)
#define RSN_AUTH_KEY_MGMT_OWE           RSN_SELECTOR(0x00, 0x0f, 0xac, 18)
#define RSN_AUTH_KEY_MGMT_SAE_EXT_KEY   RSN_SELECTOR(0x00, 0x0f, 0xac, 24)
#define RSN_CIPHER_SUITE_NONE           RSN_SELECTOR(0x00, 0x0f, 0xac, 0)
#define RSN_CIPHER_SUITE_WEP40          RSN_SELECTOR(0x00, 0x0f, 0xac, 1)
#define RSN_CIPHER_SUITE_TKIP           RSN_SELECTOR(0x00, 0x0f, 0xac, 2)
#define RSN_CIPHER_SUITE_CCMP           RSN_SELECTOR(0x00, 0x0f, 0xac, 4)
#define RSN_CIPHER_SUITE_WEP104         RSN_SELECTOR(0x00, 0x0f, 0xac, 5)
#define RSN_CIPHER_SUITE_AES_128_CMAC   RSN_SELECTOR(0x00, 0x0f, 0xac, 6)
#define RSN_CIPHER_SUITE_GCMP           RSN_SELECTOR(0x00, 0x0f, 0xac, 8)
#define RSN_CIPHER_SUITE_GCMP_256       RSN_SELECTOR(0x00, 0x0f, 0xac, 9)
#define RSN_CIPHER_SUITE_BIP_GMAC_128   RSN_SELECTOR(0x00, 0x0f, 0xac, 11)
#define RSN_CIPHER_SUITE_BIP_GMAC_256   RSN_SELECTOR(0x00, 0x0f, 0xac, 12)

/* the bit fields of the parser (defs.h) */
#define WPA_CIPHER_NONE                 BIT(0)
#define WPA_CIPHER_WEP40                BIT(7)
#define WPA_CIPHER_WEP104               BIT(8)
#define WPA_CIPHER_TKIP                 BIT(1)
#define WPA_CIPHER_CCMP                 BIT(3)
#define WPA_CIPHER_AES_128_CMAC         BIT(5)
#define WPA_CIPHER_SMS4                 BIT(10)
#define WPA_CIPHER_GCMP                 BIT(11)
#define WPA_CIPHER_GCMP_256             BIT(12)
#define WPA_CIPHER_BIP_GMAC_128         BIT(13)
#define WPA_CIPHER_BIP_GMAC_256         BIT(14)

#define WPA_KEY_MGMT_IEEE8021X          BIT(0)
#define WPA_KEY_MGMT_PSK                BIT(1)
#define WPA_KEY_MGMT_WPA_NONE           BIT(4)
#define WPA_KEY_MGMT_IEEE8021X_SHA256   BIT(7)
#define WPA_KEY_MGMT_PSK_SHA256         BIT(8)
#define WPA_KEY_MGMT_SAE                BIT(10)
#define WPA_KEY_MGMT_OWE                BIT(22)
#define WPA_KEY_MGMT_SAE_EXT_KEY        BIT(26)

#define WPA_PROTO_WPA                   BIT(0)
#define WPA_PROTO_RSN                   BIT(1)

struct rsn_ie_hdr
{
    u8 elem_id;
    u8 len;
    u8 version[2];
} __attribute__((packed));

struct wpa_ie_hdr
{
    u8 elem_id;
    u8 len;
    u8 oui[4];
    u8 version[2];
} __attribute__((packed));

struct wpa_ie_data
{
    int proto;
    int pairwise_cipher;
    int group_cipher;
    int key_mgmt;
    int capabilities;
    size_t num_pmkid;
    const u8 *pmkid;
    int mgmt_group_cipher;
    uint8_t rsnxe_capa;
};

/* ---- the suites ---- */

static int rsn_selector_to_bitfield(const u8 *s)
{
    u32 v = RSN_SELECTOR_GET(s);
    if (v == RSN_CIPHER_SUITE_NONE)          return WPA_CIPHER_NONE;
    if (v == RSN_CIPHER_SUITE_WEP40)         return WPA_CIPHER_WEP40;
    if (v == RSN_CIPHER_SUITE_TKIP)          return WPA_CIPHER_TKIP;
    if (v == RSN_CIPHER_SUITE_CCMP)          return WPA_CIPHER_CCMP;
    if (v == RSN_CIPHER_SUITE_WEP104)        return WPA_CIPHER_WEP104;
    if (v == RSN_CIPHER_SUITE_GCMP)          return WPA_CIPHER_GCMP;
    if (v == RSN_CIPHER_SUITE_GCMP_256)      return WPA_CIPHER_GCMP_256;
    if (v == RSN_CIPHER_SUITE_AES_128_CMAC)  return WPA_CIPHER_AES_128_CMAC;
    if (v == RSN_CIPHER_SUITE_BIP_GMAC_128)  return WPA_CIPHER_BIP_GMAC_128;
    if (v == RSN_CIPHER_SUITE_BIP_GMAC_256)  return WPA_CIPHER_BIP_GMAC_256;
    return 0;
}

static int rsn_key_mgmt_to_bitfield(const u8 *s)
{
    u32 v = RSN_SELECTOR_GET(s);
    if (v == RSN_AUTH_KEY_MGMT_UNSPEC_802_1X)     return WPA_KEY_MGMT_IEEE8021X;
    if (v == RSN_AUTH_KEY_MGMT_PSK_OVER_802_1X)   return WPA_KEY_MGMT_PSK;
    if (v == RSN_AUTH_KEY_MGMT_SAE)               return WPA_KEY_MGMT_SAE;
    if (v == RSN_AUTH_KEY_MGMT_802_1X_SHA256)     return WPA_KEY_MGMT_IEEE8021X_SHA256;
    if (v == RSN_AUTH_KEY_MGMT_PSK_SHA256)        return WPA_KEY_MGMT_PSK_SHA256;
    if (v == RSN_AUTH_KEY_MGMT_SAE_EXT_KEY)       return WPA_KEY_MGMT_SAE_EXT_KEY;
    if (v == RSN_AUTH_KEY_MGMT_OWE)               return WPA_KEY_MGMT_OWE;
    return 0;
}

static int wpa_selector_to_bitfield(const u8 *s)
{
    u32 v = RSN_SELECTOR_GET(s);
    if (v == WPA_CIPHER_SUITE_NONE)   return WPA_CIPHER_NONE;
    if (v == WPA_CIPHER_SUITE_WEP40)  return WPA_CIPHER_WEP40;
    if (v == WPA_CIPHER_SUITE_TKIP)   return WPA_CIPHER_TKIP;
    if (v == WPA_CIPHER_SUITE_CCMP)   return WPA_CIPHER_CCMP;
    if (v == WPA_CIPHER_SUITE_WEP104) return WPA_CIPHER_WEP104;
    return 0;
}

static int wpa_key_mgmt_to_bitfield(const u8 *s)
{
    u32 v = RSN_SELECTOR_GET(s);
    if (v == WPA_AUTH_KEY_MGMT_UNSPEC_802_1X)   return WPA_KEY_MGMT_IEEE8021X;
    if (v == WPA_AUTH_KEY_MGMT_PSK_OVER_802_1X) return WPA_KEY_MGMT_PSK;
    if (v == WPA_AUTH_KEY_MGMT_NONE)            return WPA_KEY_MGMT_WPA_NONE;
    return 0;
}

static int wpa_cipher_valid_mgmt_group(int cipher)
{
    return cipher == WPA_CIPHER_AES_128_CMAC || cipher == WPA_CIPHER_BIP_GMAC_128 || cipher == WPA_CIPHER_BIP_GMAC_256;
}

/* ---- the parsers ---- */

static int wpa_parse_wpa_ie_rsn(const u8 *rsn_ie, size_t rsn_ie_len, struct wpa_ie_data *data)
{
    const struct rsn_ie_hdr *hdr;
    const u8 *pos;
    int left;
    int i, count;

    memset(data, 0, sizeof(*data));
    data->proto = WPA_PROTO_RSN;
    data->pairwise_cipher = WPA_CIPHER_CCMP;
    data->group_cipher = WPA_CIPHER_CCMP;
    data->key_mgmt = WPA_KEY_MGMT_IEEE8021X;
    data->mgmt_group_cipher = WPA_CIPHER_AES_128_CMAC;

    if (rsn_ie_len == 0 || rsn_ie_len < sizeof(struct rsn_ie_hdr))
        return -1;

    hdr = (const struct rsn_ie_hdr *) rsn_ie;
    if (hdr->elem_id != WLAN_EID_RSN || hdr->len != rsn_ie_len - 2 || WPA_GET_LE16(hdr->version) != RSN_VERSION)
        return -2;

    pos = (const u8 *) (hdr + 1);
    left = (int) (rsn_ie_len - sizeof(*hdr));

    if (left >= RSN_SELECTOR_LEN)
    {
        data->group_cipher = rsn_selector_to_bitfield(pos);
        pos += RSN_SELECTOR_LEN;
        left -= RSN_SELECTOR_LEN;
    }
    else if (left > 0)
        return -3;

    if (left >= 2)
    {
        data->pairwise_cipher = 0;
        count = WPA_GET_LE16(pos);
        pos += 2;
        left -= 2;
        if (count == 0 || left < count * RSN_SELECTOR_LEN)
            return -4;
        for (i = 0; i < count; i++)
        {
            data->pairwise_cipher |= rsn_selector_to_bitfield(pos);
            pos += RSN_SELECTOR_LEN;
            left -= RSN_SELECTOR_LEN;
        }
    }
    else if (left == 1)
        return -5;

    if (left >= 2)
    {
        data->key_mgmt = 0;
        count = WPA_GET_LE16(pos);
        pos += 2;
        left -= 2;
        if (count == 0 || left < count * RSN_SELECTOR_LEN)
            return -6;
        for (i = 0; i < count; i++)
        {
            data->key_mgmt |= rsn_key_mgmt_to_bitfield(pos);
            pos += RSN_SELECTOR_LEN;
            left -= RSN_SELECTOR_LEN;
        }
    }
    else if (left == 1)
        return -7;

    if (left >= 2)
    {
        data->capabilities = WPA_GET_LE16(pos);
        pos += 2;
        left -= 2;
    }

    if (left >= 2)
    {
        u16 num_pmkid = WPA_GET_LE16(pos);
        pos += 2;
        left -= 2;
        if (num_pmkid > (unsigned int) left / PMKID_LEN)
        {
            data->num_pmkid = 0;
            return -9;
        }
        data->num_pmkid = num_pmkid;
        data->pmkid = pos;
        pos += data->num_pmkid * PMKID_LEN;
        left -= (int) (data->num_pmkid * PMKID_LEN);
    }

    if (left >= 4)
    {
        data->mgmt_group_cipher = rsn_selector_to_bitfield(pos);
        if (!wpa_cipher_valid_mgmt_group(data->mgmt_group_cipher))
            return -10;
        pos += RSN_SELECTOR_LEN;
        left -= RSN_SELECTOR_LEN;
    }
    return 0;           /* (trailing bytes are ignored) */
}

static int wpa_parse_wpa_ie_wpa(const u8 *wpa_ie, size_t wpa_ie_len, struct wpa_ie_data *data)
{
    const struct wpa_ie_hdr *hdr;
    const u8 *pos;
    int left;
    int i, count;

    memset(data, 0, sizeof(*data));
    data->proto = WPA_PROTO_WPA;
    data->pairwise_cipher = WPA_CIPHER_TKIP;
    data->group_cipher = WPA_CIPHER_TKIP;
    data->key_mgmt = WPA_KEY_MGMT_IEEE8021X;

    if (wpa_ie_len == 0 || wpa_ie_len < sizeof(struct wpa_ie_hdr))
        return -1;

    hdr = (const struct wpa_ie_hdr *) wpa_ie;
    if (hdr->elem_id != WLAN_EID_VENDOR_SPECIFIC || hdr->len != wpa_ie_len - 2 || RSN_SELECTOR_GET(hdr->oui) != WPA_OUI_TYPE ||
        WPA_GET_LE16(hdr->version) != WPA_VERSION)
        return -2;

    pos = (const u8 *) (hdr + 1);
    left = (int) (wpa_ie_len - sizeof(*hdr));

    if (left >= WPA_SELECTOR_LEN)
    {
        data->group_cipher = wpa_selector_to_bitfield(pos);
        pos += WPA_SELECTOR_LEN;
        left -= WPA_SELECTOR_LEN;
    }
    else if (left > 0)
        return -3;

    if (left >= 2)
    {
        data->pairwise_cipher = 0;
        count = WPA_GET_LE16(pos);
        pos += 2;
        left -= 2;
        if (count == 0 || left < count * WPA_SELECTOR_LEN)
            return -4;
        for (i = 0; i < count; i++)
        {
            data->pairwise_cipher |= wpa_selector_to_bitfield(pos);
            pos += WPA_SELECTOR_LEN;
            left -= WPA_SELECTOR_LEN;
        }
    }
    else if (left == 1)
        return -5;

    if (left >= 2)
    {
        data->key_mgmt = 0;
        count = WPA_GET_LE16(pos);
        pos += 2;
        left -= 2;
        if (count == 0 || left < count * WPA_SELECTOR_LEN)
            return -6;
        for (i = 0; i < count; i++)
        {
            data->key_mgmt |= wpa_key_mgmt_to_bitfield(pos);
            pos += WPA_SELECTOR_LEN;
            left -= WPA_SELECTOR_LEN;
        }
    }
    else if (left == 1)
        return -7;

    if (left >= 2)
        data->capabilities = WPA_GET_LE16(pos);
    return 0;
}

/* RSNXE: the capabilities byte (ESP-IDF also refuses an AP without SAE H2E when the station requires it: nothing requires it here) */
static int wpa_parse_wpa_ie_rsnxe(const u8 *rsnxe_ie, size_t rsnxe_ie_len, struct wpa_ie_data *data)
{
    memset(data, 0, sizeof(*data));
    if (rsnxe_ie_len < 3)
        return -1;
    data->rsnxe_capa = rsnxe_ie[2];
    return 0;
}

static int wpa_parse_wpa_ie(const u8 *wpa_ie, size_t wpa_ie_len, struct wpa_ie_data *data)
{
    if (wpa_ie_len >= 1 && wpa_ie[0] == WLAN_EID_RSN)
        return wpa_parse_wpa_ie_rsn(wpa_ie, wpa_ie_len, data);
    if (wpa_ie_len >= 1 && wpa_ie[0] == WLAN_EID_RSNX)
        return wpa_parse_wpa_ie_rsnxe(wpa_ie, wpa_ie_len, data);
    if (wpa_ie_len >= 1 && wpa_ie[0] == WLAN_EID_WAPI)
        return 0;
    return wpa_parse_wpa_ie_wpa(wpa_ie, wpa_ie_len, data);
}

/* ---- what the libraries get ---- */

/* the cipher as the libraries name it (wifi_cipher_type_t): cipher_type_map_supp_to_public of ESP-IDF */
static wifi_cipher_type_t cipher_to_public(unsigned cipher)
{
    switch (cipher)
    {
        case WPA_CIPHER_NONE:                       return WIFI_CIPHER_TYPE_NONE;
        case WPA_CIPHER_WEP40:                      return WIFI_CIPHER_TYPE_WEP40;
        case WPA_CIPHER_WEP104:                     return WIFI_CIPHER_TYPE_WEP104;
        case WPA_CIPHER_TKIP:                       return WIFI_CIPHER_TYPE_TKIP;
        case WPA_CIPHER_CCMP:                       return WIFI_CIPHER_TYPE_CCMP;
        case WPA_CIPHER_CCMP | WPA_CIPHER_TKIP:     return WIFI_CIPHER_TYPE_TKIP_CCMP;
        case WPA_CIPHER_AES_128_CMAC:               return WIFI_CIPHER_TYPE_AES_CMAC128;
        case WPA_CIPHER_SMS4:                       return WIFI_CIPHER_TYPE_SMS4;
        case WPA_CIPHER_GCMP:                       return WIFI_CIPHER_TYPE_GCMP;
        case WPA_CIPHER_GCMP_256:                   return WIFI_CIPHER_TYPE_GCMP256;
        default:                                    return WIFI_CIPHER_TYPE_UNKNOWN;
    }
}

/* the result of the parser in the type the libraries read (wifi_wpa_ie_t of esp_wifi_driver.h) */
typedef struct
{
    int proto;
    int pairwise_cipher;
    int group_cipher;
    int key_mgmt;
    int capabilities;
    size_t num_pmkid;
    const u8 *pmkid;
    int mgmt_group_cipher;
    uint8_t rsnxe_capa;
} wifi_wpa_ie_t;

static int wpa_parse_wpa_ie_wrapper(const u8 *wpa_ie, size_t wpa_ie_len, wifi_wpa_ie_t *data)
{
    struct wpa_ie_data ie;
    int ret = wpa_parse_wpa_ie(wpa_ie, wpa_ie_len, &ie);

    data->proto = ie.proto;
    data->pairwise_cipher = cipher_to_public((unsigned) ie.pairwise_cipher);
    data->group_cipher = cipher_to_public((unsigned) ie.group_cipher);
    data->key_mgmt = ie.key_mgmt;
    data->capabilities = ie.capabilities;
    data->num_pmkid = ie.num_pmkid;
    data->pmkid = ie.pmkid;
    data->mgmt_group_cipher = cipher_to_public((unsigned) ie.mgmt_group_cipher);
    data->rsnxe_capa = ie.rsnxe_capa;
    return ret;
}

/* wpa_funcs of esp_wifi_driver.h: what the libraries call of the supplicant; only the parser is there (a scan needs no more) */
struct wpa_funcs
{
    bool (*wpa_sta_init)(void);
    bool (*wpa_sta_deinit)(void);
    int (*wpa_sta_connect)(uint8_t *bssid);
    void (*wpa_sta_connected_cb)(uint8_t *bssid);
    void (*wpa_sta_disconnected_cb)(uint8_t reason_code);
    int (*wpa_sta_rx_eapol)(u8 *src_addr, u8 *buf, u32 len);
    bool (*wpa_sta_in_4way_handshake)(void);
    void *(*wpa_ap_init)(void);
    bool (*wpa_ap_deinit)(void *data);
    bool (*wpa_ap_join)(void **sm, u8 *bssid, u8 *wpa_ie, u8 wpa_ie_len, u8 *rsnxe, u8 rsnxe_len, bool *pmf_enable, int subtype,
                        uint8_t *pairwise_cipher);
    bool (*wpa_ap_remove)(u8 *bssid);
    uint8_t *(*wpa_ap_get_wpa_ie)(uint8_t *len);
    bool (*wpa_ap_rx_eapol)(void *hapd_data, void *sm, u8 *data, size_t data_len);
    void (*wpa_ap_get_peer_spp_msg)(void *sm, bool *spp_cap, bool *spp_req);
    char *(*wpa_config_parse_string)(const char *value, size_t *len);
    int (*wpa_parse_wpa_ie)(const u8 *wpa_ie, size_t wpa_ie_len, wifi_wpa_ie_t *data);
    int (*wpa_config_bss)(u8 *bssid);
    int (*wpa_michael_mic_failure)(u16 is_unicast);
    uint8_t *(*wpa3_build_sae_msg)(uint8_t *bssid, uint32_t type, size_t *len);
    int (*wpa3_parse_sae_msg)(uint8_t *buf, size_t len, uint32_t type, uint16_t status);
    int (*wpa3_hostap_handle_auth)(uint8_t *buf, size_t len, uint32_t type, uint16_t status, uint8_t *bssid);
    int (*wpa_sta_rx_mgmt)(u8 type, u8 *frame, size_t len, u8 *sender, int8_t rssi, u8 channel, uint64_t current_tsf);
    void (*wpa_config_done)(void);
    uint8_t *(*owe_build_dhie)(uint16_t group);
    int (*owe_process_assoc_resp)(const u8 *rsn_ie, size_t rsn_len, const uint8_t *dh_ie, size_t dh_len);
    int (*wpa_sta_set_ap_rsnxe)(const u8 *rsnxe, size_t rsnxe_ie_len);
};

extern int32_t esp_wifi_register_wpa_cb_internal(struct wpa_funcs *cb);

/* what the station calls when it starts and stops, and the events of a connection: there is no connection yet (a scan), so these only agree
 * (ESP-IDF's versions set up the state machine of the 4-way handshake, the PMKSA cache, SAE): to be replaced by the WPA2 of the supplicant */
static bool sta_init_stub(void) { return true; }
static bool sta_deinit_stub(void) { return true; }
static void sta_connected_stub(uint8_t *bssid) { (void) bssid; }
static void sta_disconnected_stub(uint8_t reason_code) { (void) reason_code; }
static bool sta_in_4way_stub(void) { return false; }
static int sta_rx_eapol_stub(u8 *src_addr, u8 *buf, u32 len) { (void) src_addr; (void) buf; (void) len; return 0; }
static void config_done_stub(void) { }
static int michael_mic_failure_stub(u16 is_unicast) { (void) is_unicast; return 0; }

static struct wpa_funcs g_wpa_funcs;

/* esp_supplicant_init() of the image: registers what there is of the supplicant */
int32_t dmwifi_supplicant_init(void)
{
    memset(&g_wpa_funcs, 0, sizeof(g_wpa_funcs));
    g_wpa_funcs.wpa_parse_wpa_ie = wpa_parse_wpa_ie_wrapper;
    g_wpa_funcs.wpa_sta_init = sta_init_stub;
    g_wpa_funcs.wpa_sta_deinit = sta_deinit_stub;
    g_wpa_funcs.wpa_sta_connected_cb = sta_connected_stub;
    g_wpa_funcs.wpa_sta_disconnected_cb = sta_disconnected_stub;
    g_wpa_funcs.wpa_sta_in_4way_handshake = sta_in_4way_stub;
    g_wpa_funcs.wpa_sta_rx_eapol = sta_rx_eapol_stub;
    g_wpa_funcs.wpa_config_done = config_done_stub;
    g_wpa_funcs.wpa_michael_mic_failure = michael_mic_failure_stub;
    return esp_wifi_register_wpa_cb_internal(&g_wpa_funcs);
}
