# DomoticsCore-Network

Transport-neutral network coordination for DomoticsCore. `NetworkComponent` receives provider
registration and state events, retains a bounded priority order, and publishes aggregate
availability as the sticky Boolean event `network/ready`.

```cpp
auto network = std::make_unique<DomoticsCore::Components::NetworkComponent>();
core.addComponent(std::move(network)); // register before WiFi/Ethernet providers
```

Providers implement the query-only `INetworkProvider` interface and publish the fixed-size events
declared in `NetworkEvents.h` from their normal component lifecycle. Network stores provider
pointers without taking ownership. Priorities use persisted Storage data when available, then a
constructor/SystemConfig default, then registration order. Priorities do not select an active
route in this stage.

`network/ready` intentionally has no IP address because availability may come from multiple
providers. Each provider reports address changes independently through
`network/provider/address-changed`, carrying its provider ID and a bounded copied address.

Generic socket types are exposed by `Network_HAL.h` as `HAL::NetworkClient`,
`HAL::NetworkServer`, `HAL::NetworkUDP`, and, when supplied by the platform,
`HAL::SecureNetworkClient`. The former WiFi-named headers remain compatibility facades.

See `examples/WifiOnly` for the pinned pioarduino ESP32-C3 validation project. Hardware validation
is the separate Stage 2.5 gate.
