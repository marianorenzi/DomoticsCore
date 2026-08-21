#pragma once

#include <DomoticsCore/Core.h>
#include <DomoticsCore/IWebUIProvider.h>
#include <DomoticsCore/Network.h>
#include <ArduinoJson.h>
#include <cstdio>
#include <map>
#include <vector>

namespace DomoticsCore {
namespace Components {
namespace WebUI {

class NetworkWebUI : public CachingWebUIProvider {
    NetworkComponent* network_ = nullptr; // non-owning
    Utils::EventBus* eventBus_ = nullptr; // non-owning
    bool prioritiesDirty_ = true;

public:
    explicit NetworkWebUI(NetworkComponent* network) : network_(network) {
        Core* core = network_ ? network_->getCore() : nullptr;
        if (!core) return;
        eventBus_ = &core->getEventBus();
        subscribeToProviderEvent(NetworkEvents::EVENT_PROVIDER_REGISTERED);
        subscribeToProviderEvent(NetworkEvents::EVENT_PROVIDER_UNREGISTERED);
    }

    ~NetworkWebUI() override {
        if (eventBus_) eventBus_->unsubscribeOwner(this);
    }

    String getWebUIName() const override { return "Network"; }
    String getWebUIVersion() const override {
        return network_ ? network_->metadata.version : String("1.0.0");
    }

protected:
    void buildContexts(std::vector<WebUIContext>& contexts) override {
        WebUIField priorities("priorities", "Provider priority", WebUIFieldType::OrderedList);
        if (network_) {
            for (const auto& id : network_->getPriorities()) {
                priorities.addOption(id, id, true);
            }
        }

        contexts.push_back(WebUIContext(
                "network_component", "Network", "dc-components", 
                WebUILocation::ComponentDetail, WebUIPresentation::Card
            )
            .withField(WebUIField("providers", "Providers", WebUIFieldType::Display, "", "", true))
            .withField(WebUIField("ready", "Ready", WebUIFieldType::Display, "false", "", true))
            .withCustomCss(R"(
                [data-context-id="network_component"]
                [data-field-name="providers"] {
                    white-space: pre-line;
                }
            )")
            .withRealTime(2000));

        contexts.push_back(WebUIContext::settings("network_settings", "Network priority")
            .withField(priorities)
            .withAPI("/api/network")
            .withRealTime(2000));
    }

public:
    String handleWebUIRequest(const String& contextId, const String&, const String& method,
                              const std::map<String, String>& params) override {

        if (!network_ || contextId != "network_settings" || method != "POST")
            return "{\"success\":false}";

        auto field = params.find("field");
        auto value = params.find("value");
        if (field == params.end() || value == params.end() || field->second != "priorities")
            return "{\"success\":false}";

        JsonDocument document;
        if (deserializeJson(document, value->second) || !document.is<JsonArray>())
            return "{\"success\":false}";

        std::vector<String> priorities;
        for (JsonVariant item : document.as<JsonArray>()) {
            if (!item.is<const char*>()) return "{\"success\":false}";
            priorities.push_back(String(item.as<const char*>()));
        }

        return network_->setPriorities(priorities)
            ? "{\"success\":true}"
            : "{\"success\":false}";
    }

    String getWebUIData(const String& contextId) override {
        if (!network_) return "{}";

        JsonDocument document;
        if (contextId == "network_settings") {
            JsonArray items = document["priorities"].to<JsonArray>();
            for (const auto& id : network_->getPriorities()) items.add(id);
        } else if (contextId == "network_component") {
            document["providers"] = providerSummary();
            document["ready"] = network_->isReady() ? "true" : "false";
        } else {
            return "{}";
        }
        
        String result;
        serializeJson(document, result);
        return result;
    }

    bool hasDataChanged(const String& contextId) override {
        if (contextId != "network_settings") return true;
        bool changed = prioritiesDirty_;
        prioritiesDirty_ = false;
        return changed;
    }

private:
    void subscribeToProviderEvent(const char* topic) {
        eventBus_->subscribe(topic, [this](const void*) { prioritiesDirty_ = true; }, this);
    }

    String providerSummary() const {
        char result[768]{};
        size_t used = 0;
        if (!network_) return String(result);
        for (const auto& id : network_->getPriorities()) {
            INetworkProvider* provider = network_->getProvider(id);
            if (!provider) continue;
            String status = provider->getConnectionStatus();
            String address = provider->getLocalIP();
            int written = std::snprintf(result + used, sizeof(result) - used, "%s%s: %s%s%s%s",
                used ? "\n" : "", id.c_str(), status.c_str(),
                address.isEmpty() ? "" : " (", address.c_str(), address.isEmpty() ? "" : ")");
            if (written < 0 || size_t(written) >= sizeof(result) - used) break;
            used += size_t(written);
        }
        return String(result);
    }
};

} // namespace WebUI
} // namespace Components
} // namespace DomoticsCore
