/*
 * The image of the Espressif Wi-Fi libraries (image/): linked at fixed addresses and put in the flash by dmod-boot at DMESPWIFI_IMAGE_FLASH.
 * It is not a module: its code is executed in place from the flash, its .data and .bss are in the RAM that dmod-boot reserves for it.
 * This loads them and gives the image what it needs of the system.
 */
#include "private.h"
#include <string.h>

const dmwifi_image_api_t *g_api;

static void host_free(void *ptr)
{
    Dmod_Free(ptr);
}

static int host_vprintf(const char *format, va_list args)
{
    char line[160];
    int n = Dmod_VSnPrintf(line, sizeof(line), format, args);
    radio_log("%s", line);
    return n;
}

static const dmwifi_host_t g_host = { .free = host_free, .vprintf = host_vprintf };

bool image_load(void)
{
    const dmwifi_image_header_t *h = (const dmwifi_image_header_t *) DMESPWIFI_IMAGE_DATA_WINDOW;

    if (h->magic != DMWIFI_IMAGE_MAGIC || h->abi != DMWIFI_IMAGE_ABI)
    {
        DMOD_LOG_ERROR("dmespwifi: no Wi-Fi image at 0x%08x (magic 0x%08x, abi %u)\n", (unsigned) DMESPWIFI_IMAGE_DATA_WINDOW, (unsigned) h->magic,
                       (unsigned) h->abi);
        return false;
    }
    if (h->data_addr < DMESPWIFI_IMAGE_RAM_BASE || h->ram_end > DMESPWIFI_IMAGE_RAM_END)
    {
        DMOD_LOG_ERROR("dmespwifi: the image needs the RAM 0x%08x..0x%08x, 0x%08x..0x%08x is reserved\n", (unsigned) h->data_addr, (unsigned) h->ram_end,
                       (unsigned) DMESPWIFI_IMAGE_RAM_BASE, (unsigned) DMESPWIFI_IMAGE_RAM_END);
        return false;
    }
    memcpy((void *) h->data_addr, (const void *) h->data_load, h->data_size);
    memset((void *) h->bss_addr, 0, h->bss_size);
    h->init(&g_host);
    g_api = &h->api;
    radio_log("dmespwifi: the Wi-Fi image of %u bytes, RAM 0x%08x..0x%08x\n", (unsigned) h->image_size, (unsigned) h->data_addr, (unsigned) h->ram_end);
    return true;
}
