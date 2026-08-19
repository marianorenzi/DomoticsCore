# WiFi-only Network validation

This example is the Stage 2.5 hardware gate for `NetworkComponent`, `WifiComponent`, provider
registration, sticky `network/ready`, and the moved Network socket HAL. Stage 2 only guarantees
that the example exists and compiles as part of the pinned pioarduino toolchain; upload and serial
validation remain explicitly pending.

The serial monitor reports provider registration, aggregate `network/ready` transitions, and the
provider-specific `network/provider/state-changed` and `network/provider/address-changed` events.
An empty provider address is displayed as `<none>`.

Port 2323 is a bidirectional Network HAL probe. Connect using `telnet <device-ip> 2323` or
`nc <device-ip> 2323`. The example logs each received byte sequence and echoes it to the client,
validating `HAL::NetworkServer` plus `HAL::NetworkClient` accept, read, and write operations.

The default target is `nologo_esp32c3_super_mini`. For clones that do not match that board
definition, select `esp32-c3-devkitc-02`; both environments enable USB CDC on boot.

For AP mode, build without credentials. For STA mode, provide them as build flags, for example:

```ini
build_flags =
    ${env.build_flags}
    -DWIFI_SSID=\"your-ssid\"
    -DWIFI_PASSWORD=\"your-password\"
```

The platform is pinned to pioarduino `55.03.32`; mutable `stable` URLs are intentionally avoided.
