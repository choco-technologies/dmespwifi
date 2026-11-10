#!/bin/bash
# build the image: dmespwifi_image.elf / .bin (the libraries of ESP-IDF + image.c + wpa_ie.c, linked by image.ld). IDF is the source of the
# four .a and of the symbols of the ROM only (IDF=/path/to/esp-idf; TOOLCHAIN=/path/to/xtensa-esp-elf/bin)
cd "$(dirname "$0")"
I=${IDF:-/tools/esp-idf}/components; T=${TOOLCHAIN:-/tools/.espressif/tools/xtensa-esp-elf/esp-13.2.0_20230928/xtensa-esp-elf/bin}; CC=$T/xtensa-esp32s3-elf-gcc
ROM=""; for f in rom api libgcc newlib newlib-nano version; do ROM="$ROM -T $I/esp_rom/esp32s3/ld/esp32s3.rom$( [ $f = rom ] || echo .$f ).ld"; done
$CC -Os -ffunction-sections -mlongcalls -I. -c image.c -o image.o || exit 1
$CC -Os -ffunction-sections -mlongcalls -I. -c wpa_ie.c -o wpa_ie.o || exit 1
U=""; for s in esp_wifi_init_internal esp_wifi_deinit_internal esp_wifi_start esp_wifi_stop esp_wifi_set_mode esp_wifi_scan_start esp_wifi_scan_get_ap_num esp_wifi_scan_get_ap_records esp_wifi_set_config esp_wifi_connect esp_wifi_disconnect esp_wifi_internal_tx esp_wifi_internal_reg_rxcb esp_wifi_internal_free_rx_buffer esp_wifi_get_mac register_chipv7_phy get_phy_version_str phy_param_track_tot esp_wifi_internal_set_log_level phy_bbpll_en_usb; do U="$U -Wl,-u,$s"; done
$CC -nostdlib -mlongcalls -Wl,--gc-sections -Wl,-T,image.ld $ROM $U image.o wpa_ie.o $I/esp_wifi/lib/esp32s3/{libnet80211,libpp,libcore}.a $I/esp_phy/lib/esp32s3/libphy.a $(${CC%gcc}gcc -print-libgcc-file-name) -Wl,-Map,dmespwifi_image.map -o dmespwifi_image.elf || exit 1
$T/xtensa-esp32s3-elf-objcopy -O binary dmespwifi_image.elf dmespwifi_image.bin
$T/xtensa-esp32s3-elf-size -A dmespwifi_image.elf | awk 'NF==3 && $2>0 {print}'; ls -la dmespwifi_image.bin | awk '{print $5, $9}'
