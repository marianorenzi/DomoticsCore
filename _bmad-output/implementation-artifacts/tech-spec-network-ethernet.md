# Technical Specification — DomoticsCore Network and Ethernet

## 1. Purpose

This document defines the requirements, architecture, scope, and delivery stages for DomoticsCore Network. It is the primary context for incremental implementation.

Implementation MUST:

- Follow the [DomoticsCore Constitution](../../.specify/memory/constitution.md).
- Preserve existing WiFi behavior and public APIs.
- Avoid speculative functionality.
- Keep platform-specific code inside HAL files.
- Use English for code, types, events, properties, and documentation.
- Ask before making an incompatible architectural change.

## 2. Background and goals

DomoticsCore currently treats WiFi as the network transport in several components. The project will add Ethernet support and make network-dependent services transport-neutral.

Goals:

- Use Arduino-ESP32 `NetworkClient`, `NetworkClientSecure`, `NetworkServer`, and `NetworkUDP` where appropriate instead of WiFi-specific equivalents.
- Coordinate multiple network providers using an ordered preference list.
- Preserve `WifiComponent`, including its AP connectivity semantics.
- Support both ESP32 EMAC PHYs and W5500 SPI Ethernet.
- Use a pinned pioarduino release with Arduino-ESP32 3.x for ESP32 builds.

## 3. Components

### 3.1 DomoticsCore-Network

New component containing:

- `INetworkProvider`.
- `NetworkComponent`, which registers providers, evaluates their ordered priority, and publishes aggregate network events.
- `NetworkWebUI`, which displays provider state and edits their order.
- The transport-neutral socket HAL currently misplaced in DomoticsCore-Wifi:
  `Network_HAL.h`, `Network_ESP32.h`, `Network_ESP8266.h`, and `Network_Stub.h`.

`NetworkComponent` does not own providers. `Core` retains component ownership and lifecycle control.

The Network HAL exposes `HAL::NetworkClient`, `HAL::SecureNetworkClient`,
`HAL::NetworkServer`, `HAL::NetworkUDP`, and `HAL::IPAddress` where shared code or native tests
need platform-independent types. These are embedded-platform aliases and native test doubles; they
are not owned or configured by `NetworkComponent`.

### 3.2 DomoticsCore-Wifi

Existing WiFi peripheral manager and an `INetworkProvider` implementation.

Its AP/STA behavior, configuration, events, and `isConnected()` semantics remain unchanged. Only changes required to adopt the relocated interface and publish generic provider events are allowed in the initial implementation.

`Wifi_ESP32.h`, `Wifi_ESP8266.h`, and `Wifi_Stub.h` remain responsible for WiFi peripheral
operations. Their generic client aliases move to the Network HAL. The existing
`WiFiServer_HAL.h` and `WiFiServer_*.h` names do not describe their contents: they contain generic
TCP aliases and the native TCP test double, not WiFi management. Their contents move to the
corresponding `Network_*.h` files. `IPAddress_Stub.h`, currently used by `WiFiServer_Stub.h`, moves
with them to DomoticsCore-Network because IP addresses are also transport-neutral. Compatibility
routing headers and deprecated aliases MAY remain temporarily to preserve existing includes and
`HAL::WiFiClient`/`HAL::WiFiServer` users.

### 3.3 DomoticsCore-Ethernet

New Ethernet peripheral manager and an `INetworkProvider` implementation. Its first supported targets are:

- Olimex ESP32-POE using the ESP32 EMAC and LAN87xx PHY through Arduino-ESP32 `ETH`.
- CDEBYTE ECM50-A08LA (4G) using its W5500 SPI Ethernet interface through Arduino-ESP32 `ETH`.

Hardware configuration and IP configuration MUST be separate.

## 4. Functional requirements

### 4.1 Provider registration

- Network providers are regular `IComponent` instances owned by `Core`.
- External providers such as Ethernet are created in the application, added through `System::getCore().addComponent()`, and initialized by the normal Core lifecycle.
- Providers announce themselves through EventBus from `afterAllComponentsReady()`; direct calls to `NetworkComponent::registerProvider()` are forbidden.
- `INetworkProvider` is query-only. Each implementing component emits its own registration and state-change events.
- Providers added before `System::begin()` may announce themselves at any point after their
  initialization; the provider list and WebUI MUST update without schema invalidation.
- Runtime component removal MUST unregister the provider before destroying it. Adding and
  initializing new Core components after `Core::begin()` is outside the initial scope because
  the current Core lifecycle does not initialize components registered at runtime.
- Provider identifiers MUST be stable, unique, lowercase strings such as `wifi` and `ethernet`.
- Provider references held by Network MUST be non-owning and removed on component removal.
- Provider registration MUST be idempotent. Component shutdown marks it disconnected but does not remove its configured priority; only component removal unregisters it.
- Inter-component state changes MUST be published through EventBus.

### 4.2 Priority configuration

- Runtime and persisted priority are owned by `NetworkComponent`. `SystemConfig` supplies an optional default through `networkPriorities`; an empty vector means no default order.
- Priority precedence is: persisted order, `SystemConfig.networkPriorities`, then provider registration order.
- An empty default means registration order; a non-empty order seeds the first boot only when no persisted order exists.
- Registered providers missing from the configured order are appended. Configured but absent providers retain their position.
- Priorities do not select an active provider and do not modify lwIP routing in the initial implementation.
- All registered providers may operate simultaneously. No singular active-provider concept is introduced.
- Routing and failover policy are deferred to the dedicated Failover stage.

### 4.3 Network WebUI

`NetworkWebUI` MUST:

- Show every registered provider, connection status, and local IP.
- Use `WebUIFieldType::DynamicOrderedList` to edit priority.
- Accept providers registered after WebUI initialization.
- Persist order through the existing Storage integration.
- Notify `NetworkComponent` through EventBus when configuration changes.

`DynamicOrderedList` is generic. Its schema is static and its runtime data is an ordered array of
`{value, label}` items, matching the value/label terminology used by `Select` options. It does not
contain an enabled flag or provider status. Disabling Ethernet or another provider is done by
disabling its component. Runtime item changes MUST NOT invalidate the cached WebUI schema.

### 4.4 Generic events

Event names and payloads MUST be centralized in `NetworkEvents.h`.

Required events:

- `network/provider/registered`
- `network/provider/unregistered`
- `network/provider/state-changed`
- `network/provider/address-changed`
- `network/config/changed`
- `network/ready`

`network/ready` MUST be sticky and carry the current aggregate Boolean state: `true` when at least
one provider is connected and `false` otherwise. A separate sticky `network/not-ready` event MUST
NOT be introduced because two retained topics could expose contradictory historical states.
Registration and provider-state events are queued, non-sticky events. Payloads MUST be trivially
copyable, fixed-size types. Only registration carries a provider pointer; removal and state payloads
identify the provider by a copied, bounded ID so they remain safe if the component is destroyed
before queued dispatch.

Provider-address events carry the provider ID and a bounded copied textual address. An empty
address means that provider no longer has a usable local address. The aggregate `network/ready`
event MUST NOT carry an address because multiple providers may be connected simultaneously.

Providers emit registration and state events. `NetworkComponent` emits the aggregate availability
event only when the value changes, according to whether any registered provider is connected.

Existing `wifi/...` events remain unchanged. Compatibility analysis found no subscriber to the
current `WifiEvents::EVENT_NETWORK_READY`; only Wifi publishes its `String` IP payload. Stage 2
therefore removes that WiFi-owned constant and publication points, and transfers ownership of
`network/ready` to `NetworkEvents` with the Boolean aggregate payload.

### 4.5 System integration

- `System` keeps its `WifiComponent*` reference and `getWiFi()` helper.
- `System` creates `NetworkComponent` before registering `WifiComponent` and exposes `getNetwork()`.
- WiFi remains the only built-in provider; `System` does not create or configure Ethernet.
- External providers are added to Core before `System::begin()` following the existing RF/LoRa composition pattern.
- `SystemConfig` contains `networkPriorities` defaults but no Ethernet configuration. System passes this vector to `NetworkComponent`; empty and unspecified have the same meaning.
- Replacing the built-in `NetworkComponent` or `WifiComponent` with application-created instances is outside the initial API.
- `System` MUST stop using the existence of WiFi as a prerequisite for generic services such as MQTT or NTP.
- WiFi-specific persistence, commands, provisioning, and WebUI remain WiFi-specific.

### 4.6 Network consumers

- ESP32 MQTT uses Arduino `NetworkClient`/`NetworkClientSecure` directly and checks generic network readiness instead of `WiFiHAL`.
- MQTT connects when `network/ready` is `true` and suspends retries when it is `false`, subscribing with sticky replay.
- RemoteConsole uses `HAL::NetworkServer`/`HAL::NetworkClient` because its shared implementation
  requires native test doubles, plus generic connection information.
- NTP startup depends on generic network readiness.
- WebUI continues using its async TCP server; interface-change behavior must not depend only on WiFi AP events.
- Platform-specific consumers instantiate standard network clients directly; shared code may use
  the Network HAL for native-test substitution. `NetworkComponent::getClient()` MUST NOT be
  introduced because routing is handled by lwIP and the selected default interface.
- `NetworkComponent::Client` and equivalent nested aliases MUST NOT be introduced. Socket types
  belong to the Network HAL or, in platform-specific ESP32 files, directly to Arduino Network.
- On Arduino-ESP32 3.x, `WiFiClient`, `WiFiServer`, and `WiFiUDP` are compatibility typedefs for
  `NetworkClient`, `NetworkServer`, and `NetworkUDP`. Renaming them does not change routing; it
  removes nominal WiFi coupling and allows consumers to stop depending on DomoticsCore-Wifi.

## 5. `INetworkProvider`

The interface remains small and reports provider state. It MUST support the current WiFi implementation and Ethernet without introducing Internet-specific concepts.

Required operations:

```cpp
class INetworkProvider {
public:
    virtual ~INetworkProvider() = default;
    virtual const char* getProviderId() const = 0;
    virtual bool isConnected() const = 0;
    virtual String getLocalIP() const = 0;
    virtual String getNetworkType() const = 0;
    virtual String getConnectionStatus() const = 0;
    virtual String getNetworkInfo() const = 0;
    virtual int32_t getSignalStrength() const { return 0; }
    virtual String getMacAddress() const { return ""; }
};
```

Future route selection remains a `NetworkComponent`/Network HAL responsibility and MUST NOT expand `INetworkProvider` with Arduino-ESP32-specific concepts.

`INetworkProvider` MUST NOT publish events or depend on `IComponent`. Components implementing it are responsible for publishing registration and state-change events through their existing `IComponent` EventBus access.

The unused connection callback should be removed only if compatibility analysis confirms it has no external consumers; otherwise it remains deprecated.

## 6. Ethernet requirements

Initial Ethernet support MUST include:

- DHCP.
- A hardware `BeginCallback` supplied by the application and invoked by `EthernetComponent`; users MUST NOT call `ETH.begin()` before component initialization.
- `HAL::EthernetBegin` convenience factories that return, but do not immediately execute, a
  `BeginCallback`:
  - `emac(...)` for an external PHY connected to the ESP32 integrated EMAC.
  - `spi(..., SPIClass&, ...)` for an existing SPI bus object.
  - `spi(..., spi_host_device_t, sck, miso, mosi, ...)` for an explicit SPI host and pins.
- The two SPI forms MUST be C++ overloads named `spi`; a separate `spiRef` name MUST NOT be
  introduced. `emac` is preferred over `mac` to avoid confusion with a MAC address.
- A constructor-provided default `EthernetConfig`, with enabled Ethernet, DHCP, optional hostname, and auto-negotiation as permissive defaults.
- Optional Storage dependency. Persisted valid fields override constructor defaults; absence of Storage or saved values retains the defaults.
- Deferred hardware start: `begin()` registers Arduino network handlers, while `afterAllComponentsReady()` loads and validates configuration, announces the provider, invokes the callback, applies IP configuration, and applies hostname.
- Link and IP tracking through `Network.onEvent()` for `ARDUINO_EVENT_ETH_*`. The callback handle MUST be removed on shutdown.
- Arduino callbacks run on a separate task and only set pending state; DomoticsCore events are emitted from `loop()`.
- Hostname, local IP, MAC address, and JSON status reporting.
- Clean shutdown and event-handler cleanup.
- Native-test stub implementation.
- Olimex ESP32-POE profile: LAN8720-compatible PHY, address `0`, MDC `23`, MDIO `18`, power `12`, and `ETH_CLOCK_GPIO17_OUT`.
- W5500 SPI support through the application-provided `BeginCallback`, allowing arbitrary SPI bus, pins, interrupt, reset, PHY address, and frequency.
- CDEBYTE ECM50-A08LA hardware profile and example.

Static IP is required in a later milestone. Additional PHY profiles are added only when a concrete target requires them.

Representative ESP32 signatures are:

```cpp
namespace DomoticsCore::HAL::EthernetBegin {

BeginCallback emac(
    eth_phy_type_t phy, int32_t phyAddress,
    int mdc, int mdio, int power, eth_clock_mode_t clockMode);

BeginCallback spi(
    eth_phy_type_t phy, int32_t phyAddress,
    int cs, int irq, int reset, SPIClass& spi,
    uint8_t frequencyMHz = ETH_PHY_SPI_FREQ_MHZ);

BeginCallback spi(
    eth_phy_type_t phy, int32_t phyAddress,
    int cs, int irq, int reset, spi_host_device_t host,
    int sck, int miso, int mosi,
    uint8_t frequencyMHz = ETH_PHY_SPI_FREQ_MHZ);

} // namespace DomoticsCore::HAL::EthernetBegin
```

These declarations and definitions belong to the Ethernet ESP32 HAL and MUST be conditionally
available only when the corresponding Arduino-ESP32 `CONFIG_ETH_*` and
`ETH_SPI_SUPPORTS_CUSTOM` capabilities exist. `EthernetComponent` continues accepting an arbitrary
`BeginCallback` as the extension path for unsupported hardware or framework overloads. A captured
`SPIClass&` MUST outlive deferred callback execution; global `SPI` satisfies that requirement.

Example:

```cpp
auto ethernet = std::make_unique<EthernetComponent>(
    HAL::EthernetBegin::spi(
        ETH_PHY_W5500, 1,
        W5500_CS, W5500_IRQ, W5500_RESET,
        SPI),
    ethernetConfig);
```

## 7. Toolchain

- ESP32 validation uses a pinned pioarduino release containing Arduino-ESP32 3.x.
- The exact release is recorded in example and CI configuration; mutable `stable` URLs are not allowed there.
- Native tests continue using PlatformIO native environments.
- The inspected VSCode installation uses PlatformIO IDE 3.3.4 and PlatformIO Core 6.1.18.
  Its default installed `espressif32` 6.11.0 platform resolves Arduino-ESP32 2.0.17, while an
  installed but unselected pioarduino platform 55.03.32 contains Arduino-ESP32 3.3.2. The
  repository and GitHub workflow currently select unpinned `platform = espressif32`; Stage 2
  MUST pin and select pioarduino before relying on Arduino-ESP32 3.x Network APIs.

## 8. Out of scope

- Internet reachability monitoring or captive-portal detection before the Failover stage.
- Changing WiFi AP/STA behavior or `isConnected()` semantics.
- Automatically disabling inactive providers.
- Load balancing or per-connection interface selection.
- A client factory owned by `NetworkComponent`.
- Direct control or monitoring of the ECM50-A08LA cellular modem; its W5500 Ethernet interface is in scope.
- ESP8266 support and validation.
- Full support for every Ethernet controller in the first milestone.

## 9. Delivery stages

Each stage follows TDD and must pass its tests before the next stage begins.

### Stage 1 — Analysis and contracts

- [x] Inventory WiFi-specific client/server/status usage.
- [x] Confirm pioarduino version and Arduino-ESP32 APIs used by VSCode/Github Workflow CI.
- [x] Define provider registration, event payloads, aggregate readiness, and compatibility tests.
- [x] Validate late WebUI provider registration with the existing component lifecycle.

Results and implementation constraints are recorded in the Stage 1 evidence appendix of this
document.

### Stage 2 — DomoticsCore-Network

Status: implemented. Native contracts and the ESP32 System integration build pass.

- [x] Move `INetworkProvider` with compatibility handling.
- [x] Move the generic contents of `WiFiServer_HAL.h` and every `WiFiServer_*.h` variant to
  `Network_HAL.h`/`Network_*.h`, rename their public types to Network, and move the generic client
  aliases out of `Wifi_*.h`. Move `IPAddress_Stub.h` to DomoticsCore-Network and update the native
  Network stub to include it there. Preserve transitional compatibility includes and aliases.
- [x] Implement event-driven provider registration/removal and state changes directly in each provider component.
- [x] Implement persisted/SystemConfig/registration-order precedence, aggregate events, and native tests.
- [x] Implement the `DomoticsCore-Network/examples/WifiOnly` hardware-validation example required by
  Stage 2.5. Do not migrate all existing examples to pioarduino until that validation passes.

### Stage 2.5 — Pioarduino WiFi-only validation

Status: validated on the ESP32-C3 SuperMini. The pinned build, upload, serial monitoring, AP and
STA connectivity, provider events, disconnect/reconnect transitions, and bidirectional
`NetworkServer`/`NetworkClient` TCP probe passed. The resolved toolchain is pioarduino `55.3.32`,
Arduino-ESP32 `3.3.2`, and ESP-IDF `5.5.1`.

- Build and run `DomoticsCore-Network/examples/WifiOnly` on the available ESP32-C3 SuperMini before
  migrating network consumers.
- Pin the example to the selected pioarduino platform release by immutable URL. For the currently
  verified toolchain, use:

  ```ini
  platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.32/platform-espressif32.zip
  ```

- Provide a `nologo_esp32c3_super_mini` environment and document
  `esp32-c3-devkitc-02` plus the USB CDC flags as the fallback for incompatible SuperMini clones.
- Exercise Core, `NetworkComponent`, `WifiComponent`, provider registration, aggregate
  `network/ready`, AP mode, STA mode, disconnect/reconnect, and the moved Network HAL types without
  requiring Ethernet hardware, MQTT, or external peripherals.
- Record the resolved pioarduino, Arduino-ESP32, and ESP-IDF versions from the build output or
  `idedata.json`.
- The stage passes only after compile, upload, serial monitoring, AP/STA connectivity, readiness
  transitions, and reconnect behavior are verified on hardware.
- After this gate passes, Stage 3 may replace the official `espressif32` platform in all ESP32 and
  ESP32-C3 examples with the same pinned pioarduino URL. Native environments remain unchanged.

### Stage 3 — Network consumers

- Migrate all existing ESP32 and ESP32-C3 example environments to the pioarduino release validated
  in Stage 2.5; do not use the mutable `stable` URL.
- Migrate MQTT to Arduino Network types and RemoteConsole to the Network HAL; remove their
  DomoticsCore-Wifi dependency where it is no longer otherwise required. Migrate NTP, WebUI
  orchestration, and System incrementally.
- Preserve existing WiFi APIs. Initial implementation and validation target ESP32 only.

### Stage 4 — Network priority configuration

- Implement and test `WebUIFieldType::DynamicOrderedList` with runtime `{value, label}` items.
- Implement `NetworkWebUI`, persistence, reordering, and late provider updates without schema invalidation.

### Stage 5 — DomoticsCore-Ethernet

- Implement deferred `BeginCallback`, `HAL::EthernetBegin::emac`/`spi` factories, constructor
  defaults, optional Storage loading, Arduino event handling, ESP32 HAL, stub, and tests.
- Add Olimex ESP32-POE and W5500/ECM50-A08LA examples using pinned pioarduino.

### Stage 6 — Static IP

- Add generic IPv4 configuration for DHCP or static address, subnet, gateway, and DNS servers.
- Persist and expose the configuration through Ethernet WebUI.
- Validate static IP independently on Olimex ESP32-POE and W5500.

### Stage 7 — Failover

- Define and implement failover after the previous stages are complete.
- Selection criteria, Internet reachability checks, route priorities, default-interface changes, and persistent-connection behavior remain pending definition.

### Stage 8 — Hardware validation

- Validate both EMAC and W5500 with and without cable, DHCP, reconnect, Ethernet-to-WiFi and WiFi-to-Ethernet fallback, WebUI, MQTT, NTP, RemoteConsole, and OTA.
- Run heap stability, stress, and one-hour soak tests.
- Record binary size and runtime memory impact.

### Stage 9 — Documentation and examples

- Update root and component READMEs, architecture documents, technical references, and generated documentation for Network and Ethernet.
- Update existing examples to use generic network clients and events where applicable.
- Add complete examples for WiFi-only, Olimex ESP32-POE, W5500/ECM50-A08LA, priorities, DHCP, and static IP.
- Document migration from WiFi-specific clients and readiness checks, configuration precedence, provider lifecycle, current limitations, and the deferred failover policy.
- Verify that all documented APIs and examples compile with the pinned toolchains.

## 10. Stage 1 analysis evidence

This section records the evidence used to establish the preceding requirements. It is descriptive
where it inventories the pre-migration code and normative where it states an explicit contract.
If historical observations conflict with another section, the current staged requirements above
take precedence.

### 10.1 WiFi coupling inventory

| Area | Coupling found before Stage 2 | Required migration |
|---|---|---|
| MQTT transport | `MQTT_ESP32.h` owned `WiFiClient`/`WiFiClientSecure`; `MQTT_impl.h` checked `WiFiHAL::isConnected()` | Use Arduino Network clients and aggregate readiness |
| System orchestration | MQTT and NTP startup depended on a `WifiComponent*` and WiFi events | Subscribe generic consumers to Network readiness while retaining WiFi-specific helpers |
| RemoteConsole | Used `WiFiServer`, `WiFiClient`, and `WiFiHAL` connection data | Use the Network HAL and generic provider information |
| WebUI | Async server was transport-neutral, but connection cleanup listened only to WiFi AP events | Retain the server and add generic interface-change handling |
| NTP | ESP32 implementation was not client-type-specific | Change only startup orchestration |
| OTA | HTTP transport was supplied by WebUI with no direct WiFi readiness check | No client migration required; validate interface changes |
| WiFi | Contained WiFi operations plus misplaced generic socket aliases and stubs | Keep peripheral behavior; move only generic abstractions and emit provider events |
| Examples and manifests | Several examples and native manifests depended nominally on WiFi | Migrate incrementally in Stages 3 and 9 |

ESP8266 aliases were identified during the inventory. They require mechanical compatibility but
remain outside the initial implementation and validation scope.

### 10.2 Socket abstraction findings

Before Stage 2, DomoticsCore-Wifi contained a transport abstraction under misleading names:

- `Wifi_ESP32.h` and `Wifi_ESP8266.h` exposed generic client aliases.
- `WiFiServer_ESP32.h` and `WiFiServer_ESP8266.h` contained only client, server, and IP aliases.
- `WiFiServer_Stub.h` implemented the shared-state TCP doubles used by native RemoteConsole tests.
- `IPAddress_Stub.h` implemented the transport-neutral native IP address.
- `WiFiServer_HAL.h` only selected the platform variant.

These implementations belong to DomoticsCore-Network as `Network_HAL.h`, `Network_ESP32.h`,
`Network_ESP8266.h`, `Network_Stub.h`, and `IPAddress_Stub.h`. WiFi-named headers and aliases may
remain only as compatibility facades. The move changes ownership and naming; it does not select a
route or enable Ethernet by itself.

Platform-specific MQTT code should use Arduino Network types directly. Shared code such as
RemoteConsole should use the Network HAL for native substitution. Socket factories and nested
`NetworkComponent::Client` aliases are explicitly excluded.

### 10.3 Toolchain evidence

The Stage 1 workstation inspection found:

- PlatformIO IDE `3.3.4` and PlatformIO Core `6.1.18`.
- Official `espressif32` `6.11.0`, resolving Arduino-ESP32 `2.0.17`.
- pioarduino `55.03.32`, resolving Arduino-ESP32 `3.3.2`.
- Repository examples and CI using unpinned `platform = espressif32`.

Consequently the migration uses the pinned WiFi-only Stage 2.5 hardware gate before changing all
consumer examples. Native environments remain on PlatformIO's native platform.

### 10.4 Provider event contract

Provider IDs are stable lowercase strings. `NetworkComponent` stores non-owning provider pointers,
deduplicates by ID and pointer, and keeps copied state needed after queued dispatch. Stage 2 selected
these bounded payload capacities:

```cpp
constexpr size_t NETWORK_PROVIDER_ID_CAPACITY = 24;
constexpr size_t NETWORK_ADDRESS_CAPACITY = 48;

struct NetworkProviderRegisteredEvent {
    INetworkProvider* provider;
    char providerId[NETWORK_PROVIDER_ID_CAPACITY];
};

struct NetworkProviderIdEvent {
    char providerId[NETWORK_PROVIDER_ID_CAPACITY];
};

struct NetworkProviderStateEvent {
    char providerId[NETWORK_PROVIDER_ID_CAPACITY];
    bool connected;
};

struct NetworkProviderAddressEvent {
    char providerId[NETWORK_PROVIDER_ID_CAPACITY];
    char address[NETWORK_ADDRESS_CAPACITY];
};

struct NetworkAvailabilityEvent {
    bool available;
};
```

Only registration carries a pointer. Unregistration, state, and address payloads retain copied data
because the provider may be destroyed before EventBus dispatch. A provider emits unregistration
during `shutdown()` before Core destroys it; Network may safely process that queued event later.

There is no `network/not-ready` topic. The single sticky Boolean `network/ready` represents both
aggregate states and avoids contradictory retained events. Provider addresses are reported
separately because multiple providers may be connected simultaneously.

### 10.5 Lifecycle findings

The startup lifecycle supports event-driven registration:

1. System registers Network before WiFi; applications add external providers before `begin()`.
2. `NetworkComponent::begin()` installs subscriptions.
3. Providers announce from `afterAllComponentsReady()`.
4. `Core::loop()` dispatches queued registration and aggregate events.

WebUI discovers existing providers and component lifecycle listeners during initialization. A
future `NetworkWebUI` can therefore keep a static schema while reading a dynamic provider list.
`DynamicOrderedList` runtime entries use `{value, label}`.

General hot-add remains outside scope: current `Core::addComponent()` after initialization does not
run `begin()`, dependency resolution, or readiness hooks. Runtime removal is supported because the
provider emits copied unregistration data before destruction.

### 10.6 Verification contract derived in Stage 1

The staged implementation tests cover:

- WiFi registration without changing AP/STA behavior or existing `wifi/...` events.
- Idempotent registration, duplicate-ID rejection, pointer lifetime, shutdown, and removal.
- Aggregate readiness transitions and sticky replay using the established EventBus behavior.
- Persisted/default/registration-order precedence, absent configured providers, invalid input, and bounds.
- Late provider visibility without WebUI schema regeneration.
- Native repeated registration, state, address, and configuration cycles under the memory gate.
- Network HAL client/server shared state, accept, input, output, disconnect, and IP address behavior.
- Compatibility compilation through transitional WiFi-named headers and aliases.
- A pinned Arduino-ESP32 3.x build before migrating consumers.

Stage 1 itself introduced no executable code or tests; its evidence came from source inventory,
lifecycle tracing, installed-package manifests, and CI workflow inspection.
