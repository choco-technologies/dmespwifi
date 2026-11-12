#define DMOD_ENABLE_REGISTRATION    ON
#include "dmod_test.h"
#include "dmespwifi.h"
#include <string.h>

DMOD_TEST_STEP(the_chip_is_named)
{
    const char *chip = dmespwifi_chip_name();             /* the port's: "ESP32-S3", or the host port's for the tests */
    DMOD_TEST_EXPECT_TRUE(chip != NULL && chip[0] != '\0');
}
