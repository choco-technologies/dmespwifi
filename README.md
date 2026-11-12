# dmespwifi

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![CI](https://github.com/choco-technologies/dmespwifi/actions/workflows/ci.yml/badge.svg)](https://github.com/choco-technologies/dmespwifi/actions/workflows/ci.yml)

The Wi-Fi of the **ESP32-S3** itself (the radio of the chip, not a second chip), as a network node of dmod: a station that scans, connects
(open, WPA2-PSK) and carries 802.3 frames.

The node is a network device like dmeth's and dmesphosted's: `read()` / `write()` are frames, `DMDRVI_IOCTL_NET_*` work as for any network
driver and the Wi-Fi is dmdrvi's `DMDRVI_IOCTL_WIFI_*` (scan, connect, status, info), so the `wifi` tool, `wifid`, `networkd` and `dhcpc`
of dmnet work with it as they are. The driver registers the interface with dmnetif itself.

```
radio --> libraries of Espressif (libdmespwifi.a, in the firmware) --> dmespwifi (/dev/wlan0) --> dmnetif --> networkd, dhcpc, wifi, ...
```

## How it is built

The radio is run by the libraries of Espressif (net80211, pp, core, phy: binaries of ESP-IDF) and the supplicant of ESP-IDF. dmod-boot
stays bare metal - no ESP-IDF, no FreeRTOS of Espressif in the firmware - so the libraries come as **a library of the system**:

- **`image/`** links the libraries, the supplicant (the station only, software crypto) and a little glue into one object, makes every
  symbol local but the Built-in API of the radio (`image/dmespradio.h`: the functions of the libraries the driver calls, as
  `dmespradio_<name>`) and packs it as `libdmespwifi.a`. The package carries it (the `lib` resource); dmod-boot links it into the firmware
  (`dmod_link_builtin`, `DMBOOT_BUILTIN_MODULES` of the board) and its linker script places the sections of the libraries (what ESP-IDF
  puts in the IRAM is executed from the flash here).
- **`src/`** is the module. It gives the libraries the system they expect (`src/osi.c`: tasks, queues, semaphores and mutexes on dmosi;
  timers on a list and a thread of its own; the interrupt of the MAC; memory), the clocks and the PHY calibration (`src/radio.c`), and
  turns their events and frames into the node (`src/wifi.c`, `src/dmespwifi.c`).

The memory of the radio (the buffers the MAC reaches by DMA, and what the libraries allocate) is not taken from the heap of the
firmware: it is the 61 kB of internal RAM that the selector of dmod-boot needs only until it starts the bank (`0x3fcda000..0x3fce9700`).

| What                          | Where                            | Size   |
|-------------------------------|----------------------------------|--------|
| the libraries (code, rodata)  | the firmware (flash, in place)   | 290 kB |
| `.data` / `.bss` of them      | the RAM of the firmware          | 14 kB  |
| memory of the radio           | RAM `0x3fcda000..0x3fce9700`     | 61 kB  |

## Building

The library is built first - the package carries it - with the headers of dmod (`dmod_defs.h` and the `dmod-config.h` of a build of dmod,
e.g. the one the configure step of the module fetches):

```bash
cmake -B build -DDMOD_TOOLS_NAME=arch/xtensa/esp32s3          # configure: fetches dmod
DMOD_INC="-I$PWD/build/_deps/dmod-src/inc -I$PWD/build/_deps/dmod-build" \
    image/build.sh                  # fetches ESP-IDF v5.2.2 (shallow, to image/esp-idf) and builds image/libdmespwifi.a
#   IDF=/path/to/esp-idf ...        # ... or with an ESP-IDF that is already there
cmake --build build
```

`xtensa-esp32s3-elf-gcc` is taken from the `PATH` (or `TOOLCHAIN=<dir of the binaries>`). The library does not depend on where ESP-IDF is.

## Configuration

```ini
[wifi]
driver_name=dmespwifi
interface=wlan         ; the friendly name of the interface (the default)
log_level=1            ; of the libraries: 0 errors, 1 warnings, 2 info, 3 debug, 4 verbose
;thread_priority=3     ; of the threads of the driver and of the libraries
```

The node is named after the interface: the friendly name of the configuration and the number of the node (0, one radio) - `/dev/wlan0`,
where the `wifi` tool looks by default. The interface is the same, or the next free number when another driver (dmesphosted) already
has that name.

The configurations of the boards: [Waveshare ESP32-S3-LCD-2.8](configs/board/esp32s3-lcd28/wifi.ini),
[LILYGO T-Deck Pro](configs/board/t-deck-pro/wifi.ini).

## Use

```
wifi scan
wifi connect SSID PASSWORD      # or: ... connect SSID -s  (asks for the password, saves the network for wifid)
wifi status
wifi info
```

## The log

The libraries log to dmlog and to a ring of 4 kB in the RAM, whose address is in `RTC_CNTL_STORE5` (`0x600080C4`) - a debugger finds it
when the console is gone. No password and no key is ever written to it.

## Limits

- A station only (no access point), 2.4 GHz, open and WPA2-PSK networks; the cryptography of the supplicant is software.
- One radio: the libraries have no context, so there is one node.
- Tested on the Waveshare ESP32-S3-LCD-2.8 (ESP32-S3FN16R8) and the LILYGO T-Deck Pro (ESP32-S3).

## License

MIT, see [LICENSE](LICENSE). The libraries in `libdmespwifi.a` are Espressif's (ESP-IDF, Apache 2.0).
