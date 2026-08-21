# Network with WebUI

Minimal ESP32 example for testing `NetworkComponent` priorities through WebUI.
It registers WiFi as a network provider, persists the ordered provider list in
Storage, and exposes it under **Settings > Network priority**. Two inert mock
providers (`ethernet` and `cellular`) make reordering testable without additional
hardware; their assigned route priorities are printed to the serial monitor.

With empty credentials the example creates the open access point
`DomoticsCore-Network`. Define `WIFI_SSID` and `WIFI_PASSWORD` as build flags to
use STA mode instead.

Build and upload to an ESP32-C3 SuperMini:

```sh
pio run -d DomoticsCore-Network/examples/NetworkWithWebUI \
  -e nologo_esp32c3_super_mini -t upload
```

Then open the IP printed on the serial monitor. In AP mode the default address
is normally `http://192.168.4.1`.
