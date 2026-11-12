/*
 * The radio (the libraries of Espressif and the supplicant) is a library of the system that dmod-boot links (radio/: libdmespwifi_port.a, dmod_link_builtin): the
 * driver calls it through the Built-in API (dmespradio.h). This gives it what it needs of the system.
 */
#include "private.h"
#include <string.h>

/* (malloc() of the libraries is the pool of the radio: what they free is the pool's - the heap refuses it and the pool runs out) */
static void host_free(void *ptr)
{
    osi_release(ptr);
}

static int host_vprintf(const char *format, va_list args)
{
    char line[160];
    int n = Dmod_VSnPrintf(line, sizeof(line), format, args);
    radio_log("%s", line);
    return n;
}

static const dmespradio_host_t g_host = { .free = host_free, .osi = &g_osi, .vprintf = host_vprintf };

bool glue_host_start(void)
{
    dmespradio_init(&g_host);
    return true;
}
