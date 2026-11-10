#define DMOD_ENABLE_REGISTRATION    ON
#include "dmod_test.h"
#include "dmespwifi.h"
#include <string.h>

DMOD_TEST_STEP(the_chip_is_named)
{
    DMOD_TEST_EXPECT_TRUE(strcmp(dmespwifi_chip_name(), "ESP32-S3") == 0);
}
