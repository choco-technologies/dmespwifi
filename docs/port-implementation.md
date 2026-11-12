# Adding a New MCU Port to dmespwifi

`dmespwifi_port` currently supports `esp32s3` (the radio of the chip, run by the libraries of Espressif: `src/port/esp32s3/radio/`) and
`x86_64` (no radio: the tests on the host, never released). The port is split from the core `dmespwifi` module (the node, the
configuration, the interface of dmnetif) so another radio can be added without touching it. The API is `include/dmespwifi_port.h`:
the radio (start, stop, ready, MAC), the Wi-Fi (scan, connect, disconnect, status, info) and the frames.

## Steps to add another architecture

1. Create `src/port/<family>/config.cmake`, setting `DMOD_TOOLS_NAME` for the
   target architecture (see `src/port/esp32s3/config.cmake` for the pattern -
   it must match a directory under `dmod/configs/arch/...`).
2. Create `src/port/<family>/port.c` implementing the
   `dmod_dmespwifi_port_api_declaration(...)` functions declared in
   `include/dmespwifi_port.h`, plus `dmod_init`/`dmod_deinit` and any
   `DMOD_IRQ_HANDLER(...)` needed.
3. If the underlying peripheral IP is identical across families, factor the
   shared logic into `src/port/<family>_common/` and keep each
   `src/port/<family>/port.c` a thin wrapper (lifecycle + IRQ only) - see
   `dmfmc/src/port/stm32_common/` for a real example of this split.
4. Build by selecting the new family:
   `cmake .. -DDMOD_CPU_FAMILY=<family>`.
5. Do not introduce a module-specific variable (e.g. `<MODULE>_MCU_SERIES`) for
   this - `DMOD_CPU_FAMILY` is the ecosystem-wide convention, already wired
   into `dmf-get` package resolution.
