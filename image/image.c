/* The glue of the image: the 8 symbols the libraries need that are neither theirs nor in the ROM, and the header. */
#include "dmwifi_image.h"
#include <string.h>

extern int32_t esp_wifi_init_internal(const wifi_init_config_t *);
extern int32_t esp_wifi_deinit_internal(void);
extern int32_t esp_wifi_start(void);
extern int32_t esp_wifi_stop(void);
extern int32_t esp_wifi_set_mode(wifi_mode_t);
extern int32_t esp_wifi_scan_start(const void *, bool);
extern int32_t esp_wifi_scan_get_ap_num(uint16_t *);
extern int32_t esp_wifi_scan_get_ap_records(uint16_t *, wifi_ap_record_t *);
extern int32_t esp_wifi_connect(void);
extern int32_t esp_wifi_disconnect(void);
extern int32_t esp_wifi_set_config(int, void *);
extern int32_t esp_wifi_get_mac(int, uint8_t *);
extern int32_t esp_wifi_internal_tx(int, void *, uint16_t);
extern int32_t esp_wifi_internal_reg_rxcb(int, void *);
extern void    esp_wifi_internal_free_rx_buffer(void *);
extern int32_t register_chipv7_phy(const void *, void *, int);
extern char *  get_phy_version_str(void);
extern void    phy_param_track_tot(bool, bool);
extern int32_t esp_wifi_internal_set_log_level(int);
extern void    phy_bbpll_en_usb(bool);
extern int32_t esp_supplicant_init(void);
extern wifi_osi_funcs_t g_wifi_osi_funcs;

extern uint32_t _data_load_d, _data_start, _data_size, _bss_start, _bss_size, _ram_end, _image_size;   /* (image.ld) */

static const dmwifi_host_t *g_host;

const char *const WIFI_EVENT = "WIFI_EVENT";

void free(void *ptr)
{
    if (g_host != NULL && ptr != NULL)
        g_host->free(ptr);
}

/* the log of the libraries: to the host (nothing when there is none) */
static int log_to_host(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int n = (g_host != NULL && g_host->vprintf != NULL) ? g_host->vprintf(format, args) : 0;
    va_end(args);
    return n;
}

int phy_printf(const char *format, ...)     { va_list a; va_start(a, format); int n = (g_host && g_host->vprintf) ? g_host->vprintf(format, a) : 0; va_end(a); return n; }
int net80211_printf(const char *format, ...){ va_list a; va_start(a, format); int n = (g_host && g_host->vprintf) ? g_host->vprintf(format, a) : 0; va_end(a); return n; }
int pp_printf(const char *format, ...)      { va_list a; va_start(a, format); int n = (g_host && g_host->vprintf) ? g_host->vprintf(format, a) : 0; va_end(a); return n; }

int puts(const char *text)
{
    return log_to_host("%s\n", text);
}

/* sprintf of the libraries (nothing in the ROM does it): %s %c %d %i %u %x %X %p %%, flag 0, a width, a length l */
static char *put_number(char *out, char *end, uint32_t value, uint32_t base, bool upper, bool negative, int width, bool zero)
{
    char digits[12];
    int n = 0;
    do
    {
        uint32_t d = value % base;
        digits[n++] = (char)(d < 10 ? '0' + d : (upper ? 'A' : 'a') + d - 10);
        value /= base;
    } while (value != 0U);
    int total = n + (negative ? 1 : 0);
    if (negative && zero && out < end)
        *out++ = '-';
    for (int pad = total; pad < width && out < end; pad++)
        *out++ = zero ? '0' : ' ';
    if (negative && !zero && out < end)
        *out++ = '-';
    while (n > 0 && out < end)
        *out++ = digits[--n];
    return out;
}

int sprintf(char *buffer, const char *format, ...)
{
    va_list args;
    char *out = buffer, *end = buffer + 0x7fffffff;
    va_start(args, format);
    for (; *format != '\0'; format++)
    {
        if (*format != '%')
        {
            *out++ = *format;
            continue;
        }
        bool zero = false;
        int width = 0;
        format++;
        if (*format == '0')
            zero = true, format++;
        while (*format >= '0' && *format <= '9')
            width = width * 10 + (*format++ - '0');
        if (*format == 'l')
            format++;
        switch (*format)
        {
            case 's': { const char *t = va_arg(args, const char *); if (t == NULL) t = "(null)"; while (*t != '\0') *out++ = *t++; break; }
            case 'c': *out++ = (char)va_arg(args, int); break;
            case 'd': case 'i': { int v = va_arg(args, int); out = put_number(out, end, v < 0 ? 0U - (uint32_t)v : (uint32_t)v, 10, false, v < 0, width, zero); break; }
            case 'u': out = put_number(out, end, va_arg(args, uint32_t), 10, false, false, width, zero); break;
            case 'x': out = put_number(out, end, va_arg(args, uint32_t), 16, false, false, width, zero); break;
            case 'X': out = put_number(out, end, va_arg(args, uint32_t), 16, true, false, width, zero); break;
            case 'p': *out++ = '0'; *out++ = 'x'; out = put_number(out, end, (uint32_t)va_arg(args, void *), 16, false, false, 8, true); break;
            case '%': *out++ = '%'; break;
            default: *out++ = '%'; if (*format == '\0') format--; else *out++ = *format; break;
        }
    }
    va_end(args);
    *out = '\0';
    return (int)(out - buffer);
}

/* the supplicant of hostap (supp/): the libraries call it through the table that esp_supplicant_init() registers */
static int32_t supplicant_start(void)
{
    g_wifi_osi_funcs = *g_host->osi;
    return esp_supplicant_init();
}

static void image_init(const dmwifi_host_t *host)
{
    g_host = host;
}

__attribute__((section(".header"), used))
const dmwifi_image_header_t dmwifi_image_header =
{
    .magic = DMWIFI_IMAGE_MAGIC,
    .abi = DMWIFI_IMAGE_ABI,
    .image_size = (uint32_t)&_image_size,
    .data_load = (uint32_t)&_data_load_d,
    .data_addr = (uint32_t)&_data_start,
    .data_size = (uint32_t)&_data_size,
    .bss_addr = (uint32_t)&_bss_start,
    .bss_size = (uint32_t)&_bss_size,
    .ram_end = (uint32_t)&_ram_end,
    .init = image_init,
    .api =
    {
        esp_wifi_init_internal, esp_wifi_deinit_internal, esp_wifi_start, esp_wifi_stop, esp_wifi_set_mode, esp_wifi_scan_start,
        esp_wifi_scan_get_ap_num, esp_wifi_scan_get_ap_records, esp_wifi_connect, esp_wifi_disconnect, esp_wifi_set_config,
        esp_wifi_get_mac, esp_wifi_internal_tx, esp_wifi_internal_reg_rxcb, esp_wifi_internal_free_rx_buffer, register_chipv7_phy,
        get_phy_version_str, phy_param_track_tot, esp_wifi_internal_set_log_level, supplicant_start, phy_bbpll_en_usb,
    },
};
