#!/bin/bash
# build the image: dmespwifi_image.elf / .bin - the libraries of ESP-IDF (the Wi-Fi: net80211, pp, core, phy), the supplicant of hostap from the
# sources of ESP-IDF (the station only: cfg/sdkconfig.h has the rest switched off, software crypto) and the glue (image.c, supp.c), linked by
# image.ld. IDF is the source of the libraries, the sources and the symbols of the ROM (IDF=/path/to/esp-idf; TOOLCHAIN=.../xtensa-esp-elf/bin)
cd "$(dirname "$0")"; H=$PWD
# Without IDF= the sources are fetched (the tag below, shallow, with the two submodules of the libraries) to image/esp-idf; without TOOLCHAIN= the
# compiler is the one of the PATH
IDF_TAG=v5.2.2
if [ -z "$IDF" ]; then
  IDF=$H/esp-idf
  if [ ! -d "$IDF/components/esp_wifi/lib/esp32s3" ]; then
    rm -rf "$IDF"
    git clone -q --depth 1 -b $IDF_TAG https://github.com/espressif/esp-idf.git "$IDF" || exit 1
    git -C "$IDF" submodule update -q --init --depth 1 components/esp_wifi/lib components/esp_phy/lib || exit 1
  fi
fi
I=$IDF/components; W=$I/wpa_supplicant
T=${TOOLCHAIN:-$(dirname "$(command -v xtensa-esp32s3-elf-gcc)")}; CC=$T/xtensa-esp32s3-elf-gcc
[ -x "$CC" ] || { echo "xtensa-esp32s3-elf-gcc not found (PATH or TOOLCHAIN=)"; exit 1; }
ROM=""; for f in rom api libgcc newlib newlib-nano version; do ROM="$ROM -T $I/esp_rom/esp32s3/ld/esp32s3.rom$( [ $f = rom ] || echo .$f ).ld"; done
OUT=${OUT:-$H/obj}; mkdir -p "$OUT"; rm -f "$OUT"/*.o
INC=""; for d in include port/include esp_supplicant/include src src/utils esp_supplicant/src src/crypto; do INC="$INC -I$W/$d"; done
for d in newlib/platform_include freertos/config/include freertos/config/include/freertos freertos/config/xtensa/include freertos/FreeRTOS-Kernel/include freertos/FreeRTOS-Kernel/portable/xtensa/include freertos/FreeRTOS-Kernel/portable/xtensa/include/freertos freertos/esp_additions/include esp_hw_support/include esp_hw_support/include/soc esp_hw_support/include/soc/esp32s3 esp_hw_support/port/esp32s3 heap/include log/include soc/include soc/esp32s3 soc/esp32s3/include hal/platform_port/include hal/esp32s3/include hal/include esp_rom/include esp_rom/include/esp32s3 esp_rom/esp32s3 esp_common/include esp_system/include esp_system/port/soc esp_system/port/include/private xtensa/esp32s3/include xtensa/include xtensa/deprecated_include lwip/include lwip/lwip/src/include lwip/port/include lwip/port/freertos/include lwip/port/esp32xx/include lwip/port/esp32xx/include/arch lwip/port/esp32xx/include/sys esp_timer/include esp_wifi/include esp_event/include esp_phy/include esp_phy/esp32s3/include esp_netif/include; do INC="$INC -I$I/$d"; done
DEFS="-DCONFIG_IEEE80211W -DCONFIG_NO_RADIUS -DCONFIG_SHA256 -DESPRESSIF_USE -DESP_PLATFORM -DESP_SUPPLICANT -DIEEE8021X_EAPOL -DUSE_WPA2_TASK -D_GNU_SOURCE -D__ets__"
CFLAGS="-fmacro-prefix-map=$IDF=esp-idf -Os -mlongcalls -ffunction-sections -fdata-sections -fno-jump-tables -fno-tree-switch-conversion -std=gnu17 -w -I$H/cfg -I$H $DEFS $INC"
for f in src/rsn_supp/wpa src/rsn_supp/wpa_ie src/rsn_supp/pmksa_cache src/common/wpa_common src/common/ieee802_11_common src/utils/common src/utils/wpabuf src/utils/wpa_debug esp_supplicant/src/esp_wpa_main esp_supplicant/src/esp_common esp_supplicant/src/esp_wpas_glue src/crypto/sha1 src/crypto/sha256 src/crypto/sha256-internal src/crypto/md5 src/crypto/md5-internal src/crypto/aes-internal src/crypto/aes-internal-enc src/crypto/aes-internal-dec src/crypto/aes-cbc src/crypto/aes-omac1 src/crypto/aes-ccm src/crypto/ccmp src/crypto/sha1-pbkdf2 src/crypto/sha256-prf src/crypto/sha1-prf src/crypto/aes-wrap src/crypto/aes-unwrap src/crypto/rc4 src/crypto/crypto_ops src/crypto/sha256-kdf; do
  $CC $CFLAGS -o "$OUT/$(basename $f).o" -c $W/$f.c || exit 1
done
$CC $CFLAGS -DCONFIG_CRYPTO_INTERNAL -o "$OUT/sha1-internal.o" -c $W/src/crypto/sha1-internal.c || exit 1
for f in image supp; do $CC $CFLAGS -o "$OUT/glue_$f.o" -c $f.c || exit 1; done
U=""; for s in esp_wifi_init_internal esp_wifi_deinit_internal esp_wifi_start esp_wifi_stop esp_wifi_set_mode esp_wifi_scan_start esp_wifi_scan_get_ap_num esp_wifi_scan_get_ap_records esp_wifi_set_config esp_wifi_connect esp_wifi_disconnect esp_wifi_internal_tx esp_wifi_internal_reg_rxcb esp_wifi_internal_free_rx_buffer esp_wifi_get_mac register_chipv7_phy get_phy_version_str phy_param_track_tot esp_wifi_internal_set_log_level phy_bbpll_en_usb esp_wifi_set_country esp_wifi_set_ps esp_supplicant_init g_wifi_default_wpa_crypto_funcs; do U="$U -Wl,-u,$s"; done
$CC -nostdlib -mlongcalls -Wl,--gc-sections -Wl,-T,image.ld $ROM $U "$OUT"/*.o $I/esp_wifi/lib/esp32s3/{libnet80211,libpp,libcore}.a $I/esp_phy/lib/esp32s3/libphy.a $($CC -print-libgcc-file-name) -Wl,-Map,dmespwifi_image.map -o dmespwifi_image.elf || exit 1
$T/xtensa-esp32s3-elf-objcopy -O binary dmespwifi_image.elf dmespwifi_image.bin
$T/xtensa-esp32s3-elf-size -A dmespwifi_image.elf | awk 'NF==3 && $2>0 && $1!~/debug/ {print}'; ls -la dmespwifi_image.bin | awk '{print $5, $9}'
