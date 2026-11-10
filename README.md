# dmespwifi

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![CI](https://github.com/choco-technologies/dmespwifi/actions/workflows/ci.yml/badge.svg)](https://github.com/choco-technologies/dmespwifi/actions/workflows/ci.yml)

The Wi-Fi of the **ESP32-S3** itself (the radio of the chip, not a second chip), as a network node of dmod: a station that scans, connects
(open, WPA2-PSK) and carries 802.3 frames.

The node is a network device like dmeth's and dmesphosted's: `read()` / `write()` are frames, `DMDRVI_IOCTL_NET_*` work as for any network
driver and the Wi-Fi is dmdrvi's `DMDRVI_IOCTL_WIFI_*` (scan, connect, status, info), so the `wifi` tool, `wifid`, `networkd` and `dhcpc`
of dmnet work with it as they are. The driver registers the interface (`wlan0`) with dmnetif itself.

```
radio --> libraries of Espressif (the image) --> dmespwifi (/dev/wlan0) --> dmnetif --> networkd, dhcpc, wifi, ...
```

## How it is built

The radio is run by the libraries of Espressif (net80211, pp, core, phy: binaries of ESP-IDF) and the supplicant of ESP-IDF. dmod-boot
stays bare metal - no ESP-IDF, no FreeRTOS of Espressif in the firmware - so the libraries are **an image of their own**:

- **`image/`** links the libraries, the supplicant (the station only, software crypto) and a little glue into `dmespwifi_image.bin`, for
  fixed addresses: the code is executed in place from the flash (offset `0x020000`), its `.data`/`.bss` are 16 kB of RAM that dmod-boot
  leaves alone (`DMBOOT_RESERVED_RAM`). The image starts with a header: a magic, the version of the ABI and a table of the functions
  the driver calls (`image/dmwifi_image.h`).
- **`src/`** is the module. It finds the image, copies its data, and gives the libraries the system they expect (`src/osi.c`: tasks,
  queues, semaphores and mutexes on dmosi; timers on a list and a thread of its own; the interrupt of the MAC; memory), the clocks and the
  PHY calibration (`src/radio.c`), and turns their events and frames into the node (`src/wifi.c`, `src/dmespwifi.c`).

The memory of the radio (the buffers the MAC reaches by DMA) is not taken from the heap of the firmware: it is the 61 kB of internal RAM
that the selector of dmod-boot needs only until it starts the bank (`0x3fcda000..0x3fce9700`).

| What                     | Where                         | Size   |
|--------------------------|-------------------------------|--------|
| the image (code, rodata) | flash `0x020000` (in place)   | 260 kB |
| `.data` / `.bss` of it   | RAM `0x3fcc4000..0x3fcc8000`  | 16 kB  |
| buffers of the radio     | RAM `0x3fcda000..0x3fce9700`  | 61 kB  |

## Building

The module is built like every dmod module (`cmake -B build -DDMOD_TOOLS_NAME=arch/xtensa/esp32s3 && cmake --build build`). The package
carries the image, so the image is built first:

```bash
image/build.sh                      # fetches ESP-IDF v5.2.2 (shallow, to image/esp-idf) and builds image/dmespwifi_image.bin
IDF=/path/to/esp-idf image/build.sh # ... or with an ESP-IDF that is already there
```

`xtensa-esp32s3-elf-gcc` is taken from the `PATH` (or `TOOLCHAIN=<dir of the binaries>`). The image does not depend on where ESP-IDF is: two
builds give the same bytes.

dmod-boot writes the image with the firmware (`install-firmware`; `DMBOOT_ESP_WIFI_IMAGE_OFFSET`, `DMBOOT_ESP_WIFI_IMAGE` of the board).

## Configuration

```ini
[wlan0]
driver_name=dmespwifi
interface=wlan0        ; the name of the interface (and of the node: /dev/wlan0)
log_level=1            ; of the libraries: 0 errors, 1 warnings, 2 info, 3 debug, 4 verbose
;thread_priority=3     ; of the threads of the driver and of the libraries
```

The configuration of the Waveshare ESP32-S3-LCD-2.8 is [configs/board/esp32s3-lcd28/wifi.ini](configs/board/esp32s3-lcd28/wifi.ini).

## Use

```
wifi scan
wifi connect SSID PASSWORD      # or: wifi connect SSID -s  (asks for the password, saves the network for wifid)
wifi status
wifi info
```

## The log

The libraries log to dmlog and to a ring of 4 kB in the RAM, whose address is in `RTC_CNTL_STORE5` (`0x600080C4`) - a debugger finds it
when the console is gone. No password and no key is ever written to it.

## Limits

- A station only (no access point), 2.4 GHz, open and WPA2-PSK networks; the cryptography of the supplicant is software.
- One radio: the libraries have no context, so there is one node.
- Tested on the ESP32-S3FN16R8 (Waveshare ESP32-S3-LCD-2.8).

## License

MIT, see [LICENSE](LICENSE). The libraries in the image are Espressif's (ESP-IDF, Apache 2.0).
