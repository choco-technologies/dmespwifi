/*
 * The radio of the ESP32-S3 as the libraries find it: the power domain and the clocks of the Wi-Fi, the PHY (its calibration and the
 * parameters), the clock and the random numbers, the MAC address. What ESP-IDF does in esp_phy (phy_init.c), esp_hw_support (periph_ctrl.c,
 * the startup of esp_system: esp_perip_clk_init) and the adapter (esp_adapter.c) - read as documentation, written here for the registers.
 */
#include "private.h"
#include <string.h>

#define REG(a)              (*(volatile uint32_t *) (a))

/* SYSCON (SYSTEM_WIFI_CLK_EN_REG and SYSTEM_WIFI_RST_EN_REG of IDF) */
#define REG_WIFI_CLK_EN     0x60026014u
#define REG_WIFI_RST_EN     0x60026018u
#define WIFI_CLK_ALL        0x00FB9FCFu     /* SYSTEM_WIFI_CLK_EN: esp_perip_clk_init() switches all of these on at the start (bit 6 is the one the MAC needs) */
#define WIFI_BT_COMMON_CLK  0x0078078Fu     /* SYSTEM_WIFI_CLK_WIFI_BT_COMMON_M: the ones that are switched with the PHY */
#define MODEM_RESET_PU      0x00002A1Fu     /* WIFIBB, FE, WIFIMAC, BTBB, BTMAC, RW_BTMAC, RW_BTMAC_REG, BTBB_REG */

/* RTC_CNTL */
#define REG_RTC_DIG_PWC     0x60008090u
#define RTC_WIFI_FORCE_PD   (1u << 17)
#define REG_RTC_DIG_ISO     0x60008094u
#define RTC_WIFI_FORCE_ISO  (1u << 28)
#define REG_RTC_STORE4      0x600080C0u     /* IDF keeps the frequency of the XTAL here (MHz, twice) */
#define REG_RTC_STORE5      0x600080C4u     /* ours: where the log ring is */

#define REG_RND             0x6003507Cu     /* WDEV_RND_REG */
#define REG_EFUSE_MAC0      0x60007044u
#define REG_EFUSE_MAC1      0x60007048u
#define REG_SYSTIMER_OP     0x60023004u
#define REG_SYSTIMER_HI     0x60023040u
#define REG_SYSTIMER_LO     0x60023044u

#define PHY_MODEM_WIFI      1u
#define PHY_RF_CAL_FULL     2

/* The default PHY parameters of ESP-IDF 5.2 for the ESP32-S3 (esp_phy/esp32s3/include/phy_init_data.h, maximum TX power 20 dBm) */
static const uint8_t g_phy_init_data[128] =
{
#include "phy_init_data.inc"
};

/* ---- the log: dmlog, and a ring of our own for JTAG (no secret is ever logged) ---- */

static char *g_log;                         /* in the internal RAM: JTAG does not read the PSRAM, where the data of a module is */
static volatile uint32_t g_log_len;

void radio_log_init(void)
{
    if (g_log == NULL)
        g_log = osi_pool_alloc(DMESPWIFI_LOG_SIZE);
    if (g_log == NULL)
        return;
    g_log[0] = '\0';
    g_log_len = 0;
    REG(REG_RTC_STORE5) = (uint32_t) g_log;
}

void radio_log(const char *format, ...)
{
    char line[200];
    va_list args;
    va_start(args, format);
    int n = Dmod_VSnPrintf(line, sizeof(line), format, args);
    va_end(args);
    if (n < 0)
        return;
    if ((size_t) n >= sizeof(line))
        n = (int) sizeof(line) - 1;
    DMOD_LOG_INFO("%s", line);
    if (g_log == NULL)
        return;
    uint32_t len = g_log_len;
    if (len + (uint32_t) n >= DMESPWIFI_LOG_SIZE)
        len = 0;                                /* the oldest goes: it starts again */
    memcpy(&g_log[len], line, (size_t) n);
    g_log_len = len + (uint32_t) n;
    g_log[g_log_len] = '\0';
}

/* ---- time, numbers, MAC ---- */

uint64_t radio_time_us(void)
{
    REG(REG_SYSTIMER_OP) = 1u << 30;            /* update the value of the unit 0 */
    while ((REG(REG_SYSTIMER_OP) & (1u << 29)) == 0)
    { }
    uint64_t hi = REG(REG_SYSTIMER_HI) & 0xFFFFFu, lo = REG(REG_SYSTIMER_LO);
    return ((hi << 32) | lo) / 16u;             /* 16 MHz */
}

uint32_t radio_random(void)
{
    return REG(REG_RND);
}

void radio_base_mac(uint8_t mac[6])
{
    uint32_t w0 = REG(REG_EFUSE_MAC0), w1 = REG(REG_EFUSE_MAC1);
    mac[0] = (uint8_t) (w1 >> 8);  mac[1] = (uint8_t) w1;
    mac[2] = (uint8_t) (w0 >> 24); mac[3] = (uint8_t) (w0 >> 16);
    mac[4] = (uint8_t) (w0 >> 8);  mac[5] = (uint8_t) w0;
}

/* ---- the power domain and the clocks ---- */

void radio_clock_enable(void)
{
    REG(REG_WIFI_CLK_EN) |= WIFI_BT_COMMON_CLK;
}

void radio_clock_disable(void)
{
    REG(REG_WIFI_CLK_EN) &= ~WIFI_BT_COMMON_CLK;
}

/* esp_perip_clk_init() and esp_wifi_bt_power_domain_on() of ESP-IDF (the ESP32-S3 has no PMU): after a reset the domain of the Wi-Fi is
 * powered down and isolated - its registers read 0 and ignore the writes until this is done */
void radio_power_domain_on(void)
{
    REG(REG_WIFI_CLK_EN) |= WIFI_CLK_ALL;
    REG(REG_RTC_STORE4) = (40u << 16) | 40u;    /* rtc_clk_init() of IDF: the PHY and the MAC read the frequency of the XTAL from here */
    REG(REG_RTC_DIG_PWC) &= ~RTC_WIFI_FORCE_PD;
    uint64_t until = radio_time_us() + 10u;
    while (radio_time_us() < until)
    { }                                         /* (10 us) */
    radio_clock_enable();
    REG(REG_WIFI_RST_EN) |= MODEM_RESET_PU;
    REG(REG_WIFI_RST_EN) &= ~MODEM_RESET_PU;
    REG(REG_RTC_DIG_ISO) &= ~RTC_WIFI_FORCE_ISO;
    radio_clock_disable();
}

/* ---- the PHY ---- */

static bool g_phy_calibrated;
static uint32_t g_phy_modem_flag;

void radio_phy_enable(void)
{
    if (g_phy_modem_flag == 0u)
    {
        radio_clock_enable();
        if (!g_phy_calibrated)
        {
            uint8_t *cal = Dmod_Malloc(1904);   /* esp_phy_calibration_data_t: version[4], mac[6], opaque[1894] */
            if (cal == NULL)
            {
                radio_log("wifi: no memory for the calibration data\n");
                return;
            }
            memset(cal, 0, 1904);
            radio_base_mac(cal + 4);
            g_api->phy_bbpll_en_usb(true);      /* (without it the PHY init switches the USB PHY - the JTAG and the console - off) */
            int32_t ret = g_api->register_chipv7_phy(g_phy_init_data, cal, PHY_RF_CAL_FULL);
            radio_log("wifi: PHY %s, calibration: %d\n", g_api->get_phy_version_str(), (int) ret);
            Dmod_Free(cal);
            g_phy_calibrated = true;
        }
        g_api->phy_param_track_tot(true, false);
    }
    g_phy_modem_flag |= PHY_MODEM_WIFI;
}

void radio_phy_disable(void)
{
    g_phy_modem_flag &= ~PHY_MODEM_WIFI;
    if (g_phy_modem_flag == 0u)
        radio_clock_disable();
}
