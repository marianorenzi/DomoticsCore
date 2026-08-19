#pragma once

#include <DomoticsCore/IComponent.h>
#include <DomoticsCore/INetworkProvider.h>
#include <DomoticsCore/NetworkEvents.h>
#include <DomoticsCore/Network_HAL.h>
#include <algorithm>
#include <vector>

#if __has_include(<DomoticsCore/Storage.h>)
#include <DomoticsCore/Storage.h>
#define DOMOTICS_NETWORK_HAS_STORAGE 1
#else
#define DOMOTICS_NETWORK_HAS_STORAGE 0
#endif

namespace DomoticsCore { 
namespace Components {

class NetworkComponent : public IComponent {
    struct ProviderSnapshot {
        String id;
        INetworkProvider* provider;
        bool connected;
    };
    std::vector<ProviderSnapshot> providers_;
    std::vector<String> priorities_;
    bool ready_ = false;

public:
    explicit NetworkComponent(const std::vector<String>& priorities = {})
        : priorities_(sanitize(priorities)) { initializeMetadata(); }

    ComponentStatus begin() override {
        on<NetworkEvents::NetworkProviderRegisteredEvent>(NetworkEvents::EVENT_PROVIDER_REGISTERED,
            [this](const NetworkEvents::NetworkProviderRegisteredEvent& event) { registerProvider(event); });
        on<NetworkEvents::NetworkProviderIdEvent>(NetworkEvents::EVENT_PROVIDER_UNREGISTERED,
            [this](const NetworkEvents::NetworkProviderIdEvent& event) { unregisterProvider(event.providerId); });
        on<NetworkEvents::NetworkProviderStateEvent>(NetworkEvents::EVENT_PROVIDER_STATE_CHANGED,
            [this](const NetworkEvents::NetworkProviderStateEvent& event) { updateProviderState(event); });
        setStatus(ComponentStatus::Success);
        return ComponentStatus::Success;
    }

    void afterAllComponentsReady() override {
        loadPriorities();
        publishReady();
    }
    void loop() override {}
    ComponentStatus shutdown() override {
        providers_.clear();
        if (ready_) { ready_ = false; publishReady(); }
        setStatus(ComponentStatus::Success);
        return ComponentStatus::Success;
    }
    std::vector<Dependency> getDependencies() const override { return {{"Storage", false}}; }

    size_t getProviderCount() const { return providers_.size(); }
    bool isReady() const { return ready_; }
    const std::vector<String>& getPriorities() const { return priorities_; }
    INetworkProvider* getProvider(const String& id) const {
        auto it = std::find_if(providers_.begin(), providers_.end(), [&](const ProviderSnapshot& p) { return p.id == id; });
        return it == providers_.end() ? nullptr : it->provider;
    }

    bool setPriorities(const std::vector<String>& priorities, bool persist = true) {
        auto clean = sanitize(priorities);
        if (clean.size() != priorities.size()) return false;
        priorities_ = clean;
        for (const auto& provider : providers_) appendPriority(provider.id);
        if (persist) savePriorities();
        publishPriorities();
        return true;
    }

private:
    void initializeMetadata() {
        metadata.name = "Network";
        metadata.version = "1.0.0";
        metadata.author = "DomoticsCore";
        metadata.description = "Network provider registry and aggregate availability";
        metadata.category = "Communication";
    }
    static bool validId(const char* id) {
        if (!id || !id[0]) return false;
        size_t length = std::strlen(id);
        if (length >= NetworkEvents::PROVIDER_ID_CAPACITY) return false;
        for (size_t i = 0; i < length; ++i) {
            char c = id[i];
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
        }
        return true;
    }
    static std::vector<String> sanitize(const std::vector<String>& values) {
        std::vector<String> result;
        for (const auto& value : values) {
            if (!validId(value.c_str()) || result.size() >= NetworkEvents::MAX_PRIORITY_ITEMS ||
                std::find(result.begin(), result.end(), value) != result.end()) continue;
            result.push_back(value);
        }
        return result;
    }
    void registerProvider(const NetworkEvents::NetworkProviderRegisteredEvent& event) {
        if (!event.provider || !validId(event.providerId) || !event.provider->getProviderId() ||
            std::strcmp(event.provider->getProviderId(), event.providerId) != 0) return;
        for (const auto& existing : providers_) {
            if (existing.id == event.providerId) return;
            if (existing.provider == event.provider) return;
        }
        if (providers_.size() >= NetworkEvents::MAX_PRIORITY_ITEMS) return;
        providers_.push_back({String(event.providerId), event.provider, event.provider->isConnected()});
        if (appendPriority(event.providerId)) {
            savePriorities();
            publishPriorities();
        }
        evaluateReady();
    }
    void unregisterProvider(const char* id) {
        providers_.erase(std::remove_if(providers_.begin(), providers_.end(),
            [&](const ProviderSnapshot& provider) { return provider.id == id; }), providers_.end());
        evaluateReady();
    }
    void updateProviderState(const NetworkEvents::NetworkProviderStateEvent& event) {
        auto it = std::find_if(providers_.begin(), providers_.end(), [&](const ProviderSnapshot& p) { return p.id == event.providerId; });
        if (it == providers_.end() || it->connected == event.connected) return;
        it->connected = event.connected;
        evaluateReady();
    }
    bool appendPriority(const String& id) {
        if (priorities_.size() < NetworkEvents::MAX_PRIORITY_ITEMS &&
            std::find(priorities_.begin(), priorities_.end(), id) == priorities_.end()) {
            priorities_.push_back(id);
            return true;
        }
        return false;
    }
    void evaluateReady() {
        bool available = std::any_of(providers_.begin(), providers_.end(), [](const ProviderSnapshot& p) { return p.connected; });
        if (available == ready_) return;
        ready_ = available;
        publishReady();
    }
    void publishReady() {
        NetworkEvents::NetworkAvailabilityEvent event{ready_};
        emit(NetworkEvents::EVENT_READY, event, true);
    }
    void publishPriorities() {
        NetworkEvents::NetworkPriorityChangedEvent event{};
        event.count = uint8_t(priorities_.size());
        for (size_t i = 0; i < priorities_.size(); ++i)
            NetworkEvents::copyProviderId(event.providerIds[i], priorities_[i].c_str());
        emit(NetworkEvents::EVENT_CONFIG_CHANGED, event);
    }
    String serializePriorities() const {
        String value;
        for (size_t i = 0; i < priorities_.size(); ++i) { if (i) value += ','; value += priorities_[i]; }
        return value;
    }
    bool parsePriorities(const String& value) {
        if (value.isEmpty()) return false;
        std::vector<String> parsed;
        size_t start = 0;
        while (start <= value.length()) {
            int separator = value.indexOf(',', start);
            size_t end = separator < 0 ? value.length() : size_t(separator);
            parsed.push_back(value.substring(start, end));
            if (separator < 0) break;
            start = end + 1;
        }
        auto clean = sanitize(parsed);
        if (clean.size() != parsed.size()) return false;
        priorities_ = clean;
        return true;
    }
    bool loadPriorities() {
#if DOMOTICS_NETWORK_HAS_STORAGE
        auto* core = getCore();
        auto* storage = core ? core->getComponent<StorageComponent>("Storage") : nullptr;
        if (!storage) return false;
        storage->registerKeys("Network", {{"network_order", 's', "Ordered network provider identifiers"}});
        return parsePriorities(storage->getString("network_order", ""));
#else
        return false;
#endif
    }
    void savePriorities() {
#if DOMOTICS_NETWORK_HAS_STORAGE
        auto* core = getCore();
        auto* storage = core ? core->getComponent<StorageComponent>("Storage") : nullptr;
        if (storage) storage->putString("network_order", serializePriorities());
#endif
    }
};

} // namespace Components
} // namespace DomoticsCore

#undef DOMOTICS_NETWORK_HAS_STORAGE
