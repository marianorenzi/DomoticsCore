#ifndef DOMOTICS_REMOTE_CONSOLE_WEBUI_H
#define DOMOTICS_REMOTE_CONSOLE_WEBUI_H

#include <DomoticsCore/IWebUIProvider.h>
#include <DomoticsCore/RemoteConsole.h>
#include <DomoticsCore/WebUI.h>
#include <ArduinoJson.h>

namespace DomoticsCore {
namespace Components {
namespace WebUI {

/**
 * @brief Minimal WebUI provider for RemoteConsole component
 *
 * Shows RemoteConsole in the component list. No configuration needed.
 */
class RemoteConsoleWebUI : public CachingWebUIProvider {
private:
    RemoteConsoleComponent* console;

    struct ConsoleUIState {
        bool active;
        uint16_t port;
        uint8_t logLevel;
        std::map<String, String> addresses;

        bool operator==(const ConsoleUIState& other) const {
            return active == other.active && port == other.port && logLevel == other.logLevel && addresses == other.addresses;
        }
        bool operator!=(const ConsoleUIState& other) const { return !(*this == other); }
    };
    LazyState<ConsoleUIState> uiState;

public:
    explicit RemoteConsoleWebUI(RemoteConsoleComponent* c) : console(c) {}

    void init(WebUIComponent* webui) {
        if (!webui) return;

        webui->registerApiRoute("/api/console/loglevels", HTTP_GET, [](AsyncWebServerRequest* request) {
            static const char* const VALUES[] = {"0", "1", "2", "3", "4", "5"};
            static const char* const LABELS[] = {"NONE", "ERROR", "WARN", "INFO", "DEBUG", "VERBOSE"};
            static constexpr size_t COUNT = 6;

            AsyncResponseStream* response = request->beginResponseStream("application/json");
            response->print("[");
            for (size_t i = 0; i < COUNT; ++i) {
                if (i > 0) response->print(",");
                response->print("{\"value\":\"");
                response->print(VALUES[i]);
                response->print("\",\"label\":\"");
                response->print(LABELS[i]);
                response->print("\"}");
            }
            response->print("]");
            request->send(response);
        });
    }

    String getWebUIName() const override {
        return console ? console->metadata.name : String("RemoteConsole");
    }

    String getWebUIVersion() const override {
        return console ? console->metadata.version : String("1.4.0");
    }

protected:
    void buildContexts(std::vector<WebUIContext>& ctxs) override {
        if (!console) return;

        WebUIField logLevelField("log_level", "Log level", WebUIFieldType::Select, "3");
        logLevelField.endpoint = "/api/console/loglevels";

        ctxs.push_back(WebUIContext::settings("console_settings", "Remote Console")
            .withField(WebUIField("status", "Status", WebUIFieldType::Display, "--", "", true))
            .withField(WebUIField("connect", "Connect", WebUIFieldType::Display, "--", "", true))
            .withField(WebUIField("port", "Port", WebUIFieldType::Number, "23"))
            .withField(logLevelField)
            .withRealTime(5000));
    }

public:
    
    String getWebUIData(const String& contextId) override {
        if (!console) return "{}";
        
        JsonDocument doc;

        if (contextId == "console_settings") {
            const uint16_t port = console->getPort();
            String connect;
            for (const auto& entry : console->getNetworkAddresses()) {
                if (entry.second.isEmpty()) continue;
                if (!connect.isEmpty()) connect += "\n";
                connect += entry.first;
                connect += ": telnet ";
                connect += entry.second;
                connect += " ";
                connect += String(port);
            }
            if (connect.isEmpty()) connect = "No network address";

            doc["status"] = console->isActive() ? "Active" : "Inactive";
            doc["connect"] = connect;
            doc["port"] = String(port);
            doc["log_level"] = String((int)console->getLogLevel());
        }
        
        String json;
        serializeJson(doc, json);
        return json;
    }
    
    String handleWebUIRequest(const String& contextId, const String& /*endpoint*/, 
                             const String& method, const std::map<String, String>& params) override {
        if (!console) return "{\"success\":false}";
        if (contextId != "console_settings" || method != "POST") return "{\"success\":false}";

        auto fieldIt = params.find("field");
        auto valueIt = params.find("value");
        if (fieldIt == params.end() || valueIt == params.end()) return "{\"success\":false}";

        const String& field = fieldIt->second;
        const String& value = valueIt->second;

        if (field == "port") {
            long p = value.toInt();
            if (p <= 0 || p > 65535) {
                DLOG_W(LOG_CONSOLE, "WebUI: invalid port '%s'", value.c_str());
                uiState.reset();
                return "{\"success\":false}";
            }
            if (!console->setPort((uint16_t)p)) {
                uiState.reset();
                return "{\"success\":false}";
            }
            uiState.reset();
            return "{\"success\":true}";
        }

        if (field == "log_level") {
            long lvl = value.toInt();
            if (lvl < 0 || lvl > 5) {
                DLOG_W(LOG_CONSOLE, "WebUI: invalid log level '%s'", value.c_str());
                uiState.reset();
                return "{\"success\":false}";
            }
            console->setLogLevel((LogLevel)lvl);
            uiState.reset();
            return "{\"success\":true}";
        }

        return "{\"success\":false}";
    }
    
    bool hasDataChanged(const String& contextId) override {
        if (!console) return false;
        if (contextId != "console_settings") return true;

        ConsoleUIState current{
            console->isActive(),
            console->getPort(),
            (uint8_t)console->getLogLevel(),
            console->getNetworkAddresses()
        };

        return uiState.hasChanged(current);
    }
};

} // namespace WebUI
} // namespace Components
} // namespace DomoticsCore

#endif // DOMOTICS_REMOTE_CONSOLE_WEBUI_H
