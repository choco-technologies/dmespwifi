# dmespwifi API Reference

The module is a driver: it is used through its node (`/dev/wlan0`), not through functions.

## The node

| Call | What it does |
|------|--------------|
| `read()` / `write()` | one 802.3 frame |
| `DMDRVI_IOCTL_NET_*` | as for every network driver of dmdrvi (the MAC address, the link, start / stop) |
| `DMDRVI_IOCTL_WIFI_SCAN` | the networks around (`dmdrvi_wifi_scan_t`) |
| `DMDRVI_IOCTL_WIFI_CONNECT` | connects to a network (`dmdrvi_wifi_connect_t`) and connects again whenever the link is lost, until `DISCONNECT` |
| `DMDRVI_IOCTL_WIFI_DISCONNECT` | leaves the network |
| `DMDRVI_IOCTL_WIFI_GET_STATUS` | the state, the network, the channel, the signal, the reason of the last disconnection |
| `DMDRVI_IOCTL_WIFI_GET_INFO` | the driver, the chip, the version of the PHY, the counters of the frames |

## Functions

```c
const char *dmespwifi_chip_name(void);   /* the chip the radio is on: "ESP32-S3" (the port's) */
```

The API of the port (the radio of a CPU family) is in `include/dmespwifi_port.h`; see [port-implementation.md](port-implementation.md).
