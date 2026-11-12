/* The glue of the library: the 8 symbols the libraries need that are neither theirs nor in the ROM, and the Built-in API of the radio. */
#define DMOD_ENABLE_REGISTRATION
#define dmod_dmespradio_api(VERSION, RET, NAME, PARAMS)     DMOD_BUILTIN_INPUT_API(dmespradio, VERSION, RET, NAME, PARAMS)
#include "dmespradio.h"
#include <string.h>
#include <stdarg.h>

#define DECLARE(RET, FUNCTION, PARAMS, ARGS)    extern RET FUNCTION PARAMS;
DMESPRADIO_FUNCTIONS(DECLARE)
extern int32_t esp_supplicant_init(void);
extern const char g_wifi_default_wpa_crypto_funcs[];
extern wifi_osi_funcs_t g_wifi_osi_funcs;

static const dmespradio_host_t *g_host;

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

/* ---- the Built-in API ---- */

#define DEFINE(RET, FUNCTION, PARAMS, ARGS)     DMOD_INPUT_API_DECLARATION(dmespradio, 1.0, RET, _##FUNCTION, PARAMS) { return FUNCTION ARGS; }
DMESPRADIO_FUNCTIONS(DEFINE)

DMOD_INPUT_API_DECLARATION(dmespradio, 1.0, void, _init, (const dmespradio_host_t *host))
{
    g_host = host;
}

/* the supplicant of hostap (supp/): the libraries call it through the table that esp_supplicant_init() registers */
DMOD_INPUT_API_DECLARATION(dmespradio, 1.0, int32_t, _supplicant_init, (void))
{
    g_wifi_osi_funcs = *g_host->osi;
    return esp_supplicant_init();
}

DMOD_INPUT_API_DECLARATION(dmespradio, 1.0, const void *, _crypto_funcs, (void))
{
    return g_wifi_default_wpa_crypto_funcs;
}
