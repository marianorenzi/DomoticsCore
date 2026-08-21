#pragma once

#include <cstddef>
#include <cstring>
#include <type_traits>

namespace DomoticsCore {
namespace Components { class INetworkProvider; }
namespace NetworkEvents {

static constexpr const char* EVENT_PROVIDER_REGISTERED = "network/provider/registered";
static constexpr const char* EVENT_PROVIDER_UNREGISTERED = "network/provider/unregistered";
static constexpr const char* EVENT_PROVIDER_STATE_CHANGED = "network/provider/state-changed";
static constexpr const char* EVENT_PROVIDER_ADDRESS_CHANGED = "network/provider/address-changed";
static constexpr const char* EVENT_READY = "network/ready";

static constexpr size_t PROVIDER_ID_CAPACITY = 24;
static constexpr size_t ADDRESS_CAPACITY = 48;
static constexpr size_t MAX_PRIORITY_ITEMS = 8;

struct NetworkProviderRegisteredEvent {
    Components::INetworkProvider* provider;
    char providerId[PROVIDER_ID_CAPACITY];
};

struct NetworkProviderIdEvent { char providerId[PROVIDER_ID_CAPACITY]; };

struct NetworkProviderStateEvent {
    char providerId[PROVIDER_ID_CAPACITY];
    bool connected;
};

struct NetworkAvailabilityEvent { bool available; };

struct NetworkProviderAddressEvent {
    char providerId[PROVIDER_ID_CAPACITY];
    char address[ADDRESS_CAPACITY];
};

inline void copyProviderId(char (&destination)[PROVIDER_ID_CAPACITY], const char* source) {
    std::memset(destination, 0, sizeof(destination));
    if (source) std::strncpy(destination, source, sizeof(destination) - 1);
}

inline void copyAddress(char (&destination)[ADDRESS_CAPACITY], const char* source) {
    std::memset(destination, 0, sizeof(destination));
    if (source) std::strncpy(destination, source, sizeof(destination) - 1);
}

static_assert(std::is_trivially_copyable<NetworkProviderRegisteredEvent>::value, "event payload must be trivial");
static_assert(std::is_trivially_copyable<NetworkProviderIdEvent>::value, "event payload must be trivial");
static_assert(std::is_trivially_copyable<NetworkProviderStateEvent>::value, "event payload must be trivial");
static_assert(std::is_trivially_copyable<NetworkAvailabilityEvent>::value, "event payload must be trivial");
static_assert(std::is_trivially_copyable<NetworkProviderAddressEvent>::value, "event payload must be trivial");

} // namespace NetworkEvents
} // namespace DomoticsCore
