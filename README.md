# dmespwifi

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![CI](https://github.com/choco-technologies/dmespwifi/actions/workflows/ci.yml/badge.svg)](https://github.com/choco-technologies/dmespwifi/actions/workflows/ci.yml)

The Wi-Fi of the **ESP32-S3** itself (the radio of the chip, not a second chip), as a network node of dmod: a station that scans, connects
(open, WPA2-PSK) and carries 802.3 frames.

The node is a network device like dmeth's and dmesphosted's: `read()` / `write()` are frames, `DMDRVI_IOCTL_NET_*` work as for any network
driver and the Wi-Fi is dmdrvi's `DMDRVI_IOCTL_WIFI_*` (scan, connect, status, info), so the `wifi` tool, `wifid`, `networkd` and `dhcpc`
of dmnet work with it as they are. The driver registers the interface with dmnetif itself.

```
radio --> libraries of Espressif (in the firmware) --> dmespwifi_port (the radio) --> dmespwifi (/dev/wlan0) --> dmnetif --> networkd, dhcpc, wifi, ...
```

## How it is built

Two modules, like the other drivers of hardware:

- **`dmespwifi`** (`src/dmespwifi.c`) is the driver: the node (`/dev/wlan0`), its configuration, the interface of dmnetif, `read()` /
  `write()` and the ioctls. It knows nothing of the chip: it calls the port.
- **`dmespwifi_port`** (`src/port/<family>/`, `include/dmespwifi_port.h`) is the radio of a CPU family: it starts the radio, keeps the
  connection, scans and carries the frames. One per family - `esp32s3`, and `x86_64` with no radio for the tests on the host.

The radio of the ESP32-S3 is run by the libraries of Espressif (net80211, pp, core, phy: binaries of ESP-IDF) and the supplicant of
ESP-IDF. dmod-boot stays bare metal - no ESP-IDF, no FreeRTOS of Espressif in the firmware - so the libraries come as **a library of the
system**:

- **`src/port/esp32s3/radio/`** links the libraries, the supplicant (the station only, software crypto) and a little glue into one object,
  makes every symbol local but the Built-in API of the radio (`dmespradio.h`: the functions of the libraries the port calls, as
  `dmespradio_<name>`) and packs it as `libdmespwifi_port.a`. The package of the port carries it (the `lib` resource) and requires it:
  `dmespwifi_port@<version> @builtin` in the `.dmd` of the port (`dmod_require_builtin`), so whatever carries the module links it - dmod-boot
  finds it in the dependencies of its modules (`dmod_link_builtin(... DEPENDENCIES ...)`) - and its linker script places the sections of
  the libraries (what ESP-IDF puts in the IRAM is executed from the flash here).
- **`src/port/esp32s3/`** gives the libraries the system they expect (`osi.c`: tasks, queues, semaphores and mutexes on dmosi; timers on a
  list and a thread of their own; the interrupt of the MAC - line 0 only; memory), the clocks and the PHY calibration (`radio.c`), and
  starts the radio and turns its events and frames into the API of the port (`wifi.c`, `port.c`).

The memory of the radio (the buffers the MAC reaches by DMA, and what the libraries allocate) is not taken from the heap of the
firmware: it is the 61 kB of internal RAM that the selector of dmod-boot needs only until it starts the bank (`0x3fcda000..0x3fce9700`).

| What                          | Where                            | Size   |
|-------------------------------|----------------------------------|--------|
| the libraries (code, rodata)  | the firmware (flash, in place)   | 290 kB |
| `.data` / `.bss` of them      | the RAM of the firmware          | 14 kB  |
| memory of the radio           | RAM `0x3fcda000..0x3fce9700`     | 61 kB  |

## Building

The module is configured like every dmod module: with the installed `dmod-dev` when there is one, otherwise a checkout of dmod
(`-DDMOD_DIR=...`) or dmod from GitHub (`-DDMOD_USE_INSTALLED_SDK=OFF` fetches it even when `dmod-dev` is installed). The library is
built after the configure step - it needs the headers of dmod of that build, which the configure step lists in
`build/dmespradio-includes.txt` - and before the build, which packs it:

The modules are built for a CPU family, not an architecture - the radio is the one of the chip: `DMOD_CPU_FAMILY=esp32s3` (the default;
`src/port/esp32s3/config.cmake` selects the tools, `arch/xtensa/esp32s3`). `x86_64` builds the port without a radio, for the tests on
the host; it is never released. The release has the packages of one family each (`dmespwifi-v<version>-esp32s3.zip`,
`dmespwifi_port-v<version>-esp32s3.zip`; `<cpu_family>` in [manifest.dmm](manifest.dmm)).

```bash
cmake -B build                                    # DMOD_CPU_FAMILY=esp32s3
DMOD_BUILD=$PWD/build src/port/esp32s3/radio/build.sh   # fetches ESP-IDF v5.2.2 (shallow, to radio/esp-idf), builds radio/libdmespwifi_port.a
#   IDF=/path/to/esp-idf DMOD_BUILD=... ...       # ... or with an ESP-IDF that is already there
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
- One radio: the libraries have no context, so there is one node; the API of the port has no instance.
- Tested on the Waveshare ESP32-S3-LCD-2.8 (ESP32-S3FN16R8) and the LILYGO T-Deck Pro (ESP32-S3).

## License

MIT, see [LICENSE](LICENSE). The libraries in `libdmespwifi_port.a` are Espressif's (ESP-IDF, Apache 2.0).
