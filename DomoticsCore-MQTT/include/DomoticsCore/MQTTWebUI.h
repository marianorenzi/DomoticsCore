#pragma once

#include "DomoticsCore/MQTT.h"
#include "DomoticsCore/IWebUIProvider.h"
#include "DomoticsCore/WebUI.h"
#include "DomoticsCore/BaseWebUIComponents.h"
#include <ArduinoJson.h>

namespace DomoticsCore {
namespace Components {
namespace WebUI {

/**
 * @brief WebUI provider for MQTT component
 * 
 * Provides web interface for MQTT configuration, monitoring, and testing.
 * Uses composition pattern - holds non-owning pointer to MQTT component.
 * 
 * UI Contexts:
 * - mqtt_status: Header badge showing connection status
 * - mqtt_settings: Settings card for configuration
 * - mqtt_detail: Component detail with statistics and test interface
 * 
 * @example Usage
 * ```cpp
 * auto* webui = core.getComponent<WebUIComponent>("WebUI");
 * auto* mqtt = core.getComponent<MQTTComponent>("MQTT");
 * if (webui && mqtt) {
 *     webui->registerProviderWithComponent(new MQTTWebUI(mqtt), mqtt);
 * }
 * ```
 */
class MQTTWebUI : public CachingWebUIProvider {
public:
    /**
     * @brief Construct WebUI provider
     * @param component Non-owning pointer to MQTT component
     */
    explicit MQTTWebUI(MQTTComponent* component)
        : mqtt(component) {}

    /**
     * @brief Set callback for MQTT configuration persistence (optional)
     */
    void setConfigSaveCallback(std::function<void(const MQTTConfig&)> callback) {
        onConfigSaved = callback;
    }

    // ========== IWebUIProvider Interface ==========

    String getWebUIName() const override {
        return mqtt ? mqtt->metadata.name : String("MQTT");
    }

    String getWebUIVersion() const override {
        return mqtt ? mqtt->metadata.version : String("1.4.0");
    }

protected:
    void buildContexts(std::vector<WebUIContext>& contexts) override {
        if (!mqtt) return;

        // Header status badge - placeholder values, real values from getWebUIData()
        contexts.push_back(WebUIContext::statusBadge("mqtt_status", "MQTT", "dc-mqtt")
            .withField(WebUIField("state", "State", WebUIFieldType::Status, "OFF"))
            .withRealTime(2000)
            .withAPI("/api/mqtt/status"));

        // Settings card - placeholder values
        WebUIContext settings = WebUIContext::settings("mqtt_settings", "MQTT Configuration");
        settings.withField(WebUIField("enabled", "MQTT Enabled", WebUIFieldType::Boolean, "false"))
                .withField(WebUIField("broker", "Broker Address", WebUIFieldType::Text, ""))
                .withField(WebUIField("port", "Port", WebUIFieldType::Number, "1883"))
                .withField(WebUIField("username", "Username", WebUIFieldType::Text, ""))
                .withField(WebUIField("password", "Password", WebUIFieldType::Text, ""))
                .withField(WebUIField("client_id", "Client ID", WebUIFieldType::Text, ""))
                .withField(WebUIField("use_tls", "Use TLS/SSL", WebUIFieldType::Boolean, "false"))
                .withField(WebUIField("lwt_enabled", "Last Will Enabled", WebUIFieldType::Boolean, "false"))
                .withField(WebUIField("lwt_topic", "LWT Topic", WebUIFieldType::Text, ""))
                .withField(WebUIField("lwt_message", "LWT Message", WebUIFieldType::Text, ""))
                .withAPI("/api/mqtt/settings");

        contexts.push_back(settings);

        // Component detail with statistics - placeholder values
        WebUIContext detail = WebUIContext("mqtt_detail", "MQTT Client", "dc-mqtt",
                                          WebUILocation::ComponentDetail, WebUIPresentation::Card);

        detail.withField(WebUIField("broker_addr", "Broker", WebUIFieldType::Text, ""))
              .withField(WebUIField("state", "Connection State", WebUIFieldType::Status, "Disconnected"))
              .withField(WebUIField("uptime", "Uptime", WebUIFieldType::Text, "0s"))
              .withField(WebUIField("client_id", "Client ID", WebUIFieldType::Text, ""))
              .withField(WebUIField("publish_count", "Messages Published", WebUIFieldType::Number, "0"))
              .withField(WebUIField("receive_count", "Messages Received", WebUIFieldType::Number, "0"))
              .withField(WebUIField("subscription_count", "Active Subscriptions", WebUIFieldType::Number, "0"))
              .withField(WebUIField("queue_size", "Queued Messages", WebUIFieldType::Number, "0"))
              .withField(WebUIField("reconnect_count", "Reconnections", WebUIFieldType::Number, "0"))
              .withField(WebUIField("error_count", "Publish Errors", WebUIFieldType::Number, "0"))
              .withRealTime(1000)
              .withAPI("/api/mqtt/detail");

        contexts.push_back(detail);
    }

public:
    
    String getWebUIData(const String& contextId) override {
        if (!mqtt) return "{}";
        
        JsonDocument doc;
        
        if (contextId == "mqtt_status") {
            const String label = mqtt->getStateString();
            // Primary state used by app.js to toggle 'active' class (true/false)
            doc["state"] = mqtt->isConnected() ? "true" : "false";
            // Provide normalized code + human label additionally
            String code = "unknown";
            if (label == "Connected") code = "connected";
            else if (label == "Connecting") code = "connecting";
            else if (label == "Disconnected") code = "disconnected";
            else if (label == "Error") code = "error";
            doc["state_label"] = label;
            doc["connected"] = mqtt->isConnected();
            doc["state_code"] = code;
            
        } else if (contextId == "mqtt_settings") {
            const MQTTConfig& cfg = mqtt->getConfig();
            doc["enabled"] = cfg.enabled;
            doc["broker"] = cfg.broker;
            doc["port"] = cfg.port;
            doc["username"] = cfg.username;
            doc["client_id"] = cfg.clientId;
            doc["use_tls"] = cfg.useTLS;
            doc["lwt_enabled"] = cfg.enableLWT;
            doc["lwt_topic"] = cfg.lwtTopic;
            doc["lwt_message"] = cfg.lwtMessage;
            doc["connected"] = mqtt->isConnected();
            
        } else if (contextId == "mqtt_detail") {
            const MQTTConfig& cfg = mqtt->getConfig();
            const auto& stats = mqtt->getStatistics();
            
            doc["broker_addr"] = cfg.broker + ":" + String(cfg.port);
            doc["state"] = mqtt->getStateString();
            doc["uptime"] = stats.uptime;
            doc["client_id"] = cfg.clientId;
            doc["publish_count"] = stats.publishCount;
            doc["receive_count"] = stats.receiveCount;
            doc["subscription_count"] = stats.subscriptionCount;
            doc["queue_size"] = mqtt->getQueuedMessageCount();
            doc["reconnect_count"] = stats.reconnectCount;
            doc["error_count"] = stats.publishErrors;
            
            // Add subscription list
            JsonArray subs = doc["subscriptions"].to<JsonArray>();
            for (const auto& topic : mqtt->getActiveSubscriptions()) {
                subs.add(topic);
            }
        }

        String json;
        if (serializeJson(doc, json) == 0) {
            return "{}";
        }
        return json;
    }
    
    bool hasDataChanged(const String& contextId) override {
        if (!mqtt) return false;
        if (contextId == "mqtt_status") {
            // Only push status updates when the connection state string changes
            return statusState.hasChanged(mqtt->getStateString());
        }
        return true; // default behavior for other contexts
    }
    
    String handleWebUIRequest(const String& contextId, const String& /*endpoint*/, 
                              const String& method, const std::map<String, String>& params) override {
        if (!mqtt) {
            return "{\"success\":false,\"error\":\"Component not available\"}";
        }
        
        if (method == "GET") {
            return "{\"success\":true}";
        }
        
        if (method != "POST") {
            return "{\"success\":false,\"error\":\"Method not allowed\"}";
        }
        
        // Handle settings updates
        if (contextId == "mqtt_settings") {
            auto fieldIt = params.find("field");
            auto valueIt = params.find("value");
            
            MQTTConfig cfg = mqtt->getConfig();
            
            // Handle field updates
            if (fieldIt != params.end() && valueIt != params.end()) {
                const String& field = fieldIt->second;
                const String& value = valueIt->second;
                
                if (field == "enabled") {
                    bool newEnabled = (value == "true" || value == "1");
                    bool wasEnabled = cfg.enabled;
                    cfg.enabled = newEnabled;
                    
                    // Handle state change
                    mqtt->setConfig(cfg);
                    
                    // Persist the change
                    if (onConfigSaved) {
                        onConfigSaved(cfg);
                    }
                    
                    if (!wasEnabled && newEnabled) {
                        // Enabling: connect
                        mqtt->connect();
                    } else if (wasEnabled && !newEnabled) {
                        // Disabling: disconnect
                        mqtt->disconnect();
                    }
                    return "{\"success\":true}";
                } else if (field == "broker") {
                    cfg.broker = value;
                } else if (field == "port") {
                    cfg.port = value.toInt();
                } else if (field == "username") {
                    cfg.username = value;
                } else if (field == "password") {
                    if (!value.isEmpty()) {  // Only update if not empty
                        cfg.password = value;
                    }
                } else if (field == "client_id") {
                    cfg.clientId = value;
                } else if (field == "use_tls") {
                    cfg.useTLS = (value == "true" || value == "1");
                } else if (field == "lwt_enabled") {
                    cfg.enableLWT = (value == "true" || value == "1");
                } else if (field == "lwt_topic") {
                    cfg.lwtTopic = value;
                } else if (field == "lwt_message") {
                    cfg.lwtMessage = value;
                }
                
                mqtt->setConfig(cfg);
                
                // Invoke persistence callback if set
                if (onConfigSaved) {
                    onConfigSaved(cfg);
                }
                
                return "{\"success\":true}";
            }
        }
        
        return "{\"success\":false,\"error\":\"Unknown request\"}";
    }

private:
    MQTTComponent* mqtt;  // Non-owning pointer
    std::function<void(const MQTTConfig&)> onConfigSaved;  // Callback for persistence
    LazyState<String> statusState;
};

} // namespace WebUI
} // namespace Components
} // namespace DomoticsCore
