#pragma once

/**
 * @file WebUI.h
 * @brief Declares the DomoticsCore WebUI component and supporting types for dashboard integration.
 */

#include "DomoticsCore/IWebUIProvider.h"
#include "DomoticsCore/ComponentRegistry.h"
#include "DomoticsCore/BaseWebUIComponents.h"
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "DomoticsCore/Filesystem_HAL.h"
#include <vector>
#include <map>
#include <memory>
#include <algorithm>
#include <pgmspace.h>
#include <functional>

#include "DomoticsCore/IComponent.h"
#include "DomoticsCore/Logger.h"
#include "DomoticsCore/Platform_HAL.h"  // For HAL::getFreeHeap()
#include "DomoticsCore/MemoryManager.h" // For adaptive WS limits
#include "DomoticsCore/NetworkEvents.h"
#include "DomoticsCore/WebUI_HAL.h"     // For WebUI buffer sizes
#include "DomoticsCore/Generated/WebUIAssets.h"

// New modular headers
#include "DomoticsCore/WebUI/WebUIConfig.h"
#include "DomoticsCore/WebUI/ProviderRegistry.h"
#include "DomoticsCore/WebUI/WebServerManager.h"
#include "DomoticsCore/WebUI/WebSocketHandler.h"

namespace DomoticsCore {
namespace Components {

// Re-export WebUIConfig in the main namespace for backward compatibility
using WebUIConfig = WebUI::WebUIConfig;

/**
 * @class DomoticsCore::Components::WebUIComponent
 * @brief Async web server + WebSocket frontend that aggregates `IWebUIProvider` contexts.
 *
 * Serves embedded HTML/CSS/JS assets, registers component providers, and pushes real-time updates
 * to connected clients. Acts as both a component and a provider to expose global WebUI settings.
 */
class WebUIComponent : public IComponent, public CachingWebUIProvider, public Components::ComponentRegistry::IComponentLifecycleListener {
private:
    WebUIConfig config;
    
    // Sub-managers
    std::unique_ptr<WebUI::WebServerManager> webServer;
    std::unique_ptr<WebUI::WebSocketHandler> webSocket;
    std::unique_ptr<WebUI::ProviderRegistry> registry;

    // State
    bool forceNextUpdate = false; // force full contexts send on next tick (e.g., after WS reconnect)

    // Shared WS send buffer (single-threaded, safe to share between sendWebSocketUpdate/sendWebSocketUpdates)
    static char wsBuffer_[WEBUI_WS_BUFFER_SIZE];

    struct SchemaMemProbe {
        bool active = false;
        uint32_t seq = 0;
        unsigned long t0 = 0;
        uint32_t heapBefore = 0;
        uint32_t maxBefore = 0;
        uint32_t heapAfterSend = 0;
        uint32_t maxAfterSend = 0;
        uint8_t stage = 0;
    };

    static constexpr uint8_t SCHEMA_PROBE_SLOTS = 6;
    SchemaMemProbe schemaMemProbes[SCHEMA_PROBE_SLOTS];
    uint32_t schemaProbeSeq = 0;
    uint8_t schemaProbeNext = 0;
    
    // Config persistence callback
    std::function<void(const WebUIConfig&)> onConfigChanged;

public:
    /**
     * @brief Construct a WebUI component with the provided configuration.
     */
    WebUIComponent(const WebUIConfig& cfg = WebUIConfig()) 
        : config(cfg) {
        // Initialize component metadata immediately for dependency resolution
        metadata.name = "WebUI";
        metadata.version = "1.5.1";
        metadata.author = "DomoticsCore";
        metadata.description = "Web dashboard and API component";

        registry = std::unique_ptr<WebUI::ProviderRegistry>(new WebUI::ProviderRegistry());
        webServer = std::unique_ptr<WebUI::WebServerManager>(new WebUI::WebServerManager(config));
        webSocket = std::unique_ptr<WebUI::WebSocketHandler>(new WebUI::WebSocketHandler(config));
    }

    /**
     * @brief Release the instances.
     */
    ~WebUIComponent() {
        // unique_ptrs clean themselves up
    }

    // IComponent interface
    /**
     * @brief Initialize the AsyncWebServer, register websocket handler, and configure routes.
     */
    ComponentStatus begin() override {
        // Adapt WS limits based on runtime memory profile
        auto& memMgr = MemoryManager::instance();
        int adaptiveMaxClients = memMgr.getMaxWsClients();
        if (adaptiveMaxClients < config.maxWebSocketClients) {
            DLOG_I(LOG_WEB, "Memory profile %s: WS clients %d -> %d",
                   memMgr.getProfileName(), config.maxWebSocketClients, adaptiveMaxClients);
            config.maxWebSocketClients = adaptiveMaxClients;
            // Recreate WebSocketHandler with updated config
            webSocket = std::unique_ptr<WebUI::WebSocketHandler>(new WebUI::WebSocketHandler(config));
        }

        webServer->begin();
        webServer->setAuthHandler([this](AsyncWebServerRequest* request) {
            return authenticate(request);
        });
        
        if (config.enableWebSocket) {
            webSocket->begin(webServer->getServer());
            
            webSocket->setForceUpdateCallback([this]() {
                forceNextUpdate = true;
            });
            webSocket->setUIActionCallback([this](const String& ctx, const String& field, const String& value) {
                handleUIAction(ctx, field, value);
            });
        }
        
        setupApiRoutes();

        webServer->start();
        
        return ComponentStatus::Success;
    }

    /**
     * @brief Pump periodic websocket updates and clean up disconnected clients.
     */
    void loop() override {
        static unsigned long lastLoopLog = 0;
        unsigned long now = HAL::Platform::getMillis();
        if (now - lastLoopLog >= 10000) {
            DLOG_D(LOG_WEB, "WebUI loop alive, heap=%u, wsClients=%d, sse=%s(%d)",
                   (unsigned)HAL::Platform::getFreeHeap(), webSocket->getClientCount(),
                   webSocket->isSSEEnabled() ? "on" : "off", webSocket->getSSEClientCount());
            lastLoopLog = now;
        }

        webSocket->loop();

        for (uint8_t i = 0; i < SCHEMA_PROBE_SLOTS; i++) {
            SchemaMemProbe& p = schemaMemProbes[i];
            if (!p.active) continue;

            const unsigned long now = HAL::Platform::getMillis();
            const unsigned long dt = now - p.t0;

            if (p.stage == 0 && dt >= 500) {
                const uint32_t h = HAL::Platform::getFreeHeap();
                const uint32_t m = HAL::Platform::getMaxAllocHeap();
                DLOG_D(LOG_WEB, "Schema mem #%u +500ms: heap=%u (delta=%d), max=%u (delta=%d)",
                       (unsigned)p.seq,
                       (unsigned)h, (int)h - (int)p.heapBefore,
                       (unsigned)m, (int)m - (int)p.maxBefore);
                p.stage = 1;
            } else if (p.stage == 1 && dt >= 2000) {
                const uint32_t h = HAL::Platform::getFreeHeap();
                const uint32_t m = HAL::Platform::getMaxAllocHeap();
                DLOG_D(LOG_WEB, "Schema mem #%u +2s: heap=%u (delta=%d), max=%u (delta=%d)",
                       (unsigned)p.seq,
                       (unsigned)h, (int)h - (int)p.heapBefore,
                       (unsigned)m, (int)m - (int)p.maxBefore);
                p.stage = 2;
            } else if (p.stage == 2 && dt >= 10000) {
                const uint32_t h = HAL::Platform::getFreeHeap();
                const uint32_t m = HAL::Platform::getMaxAllocHeap();
                DLOG_D(LOG_WEB, "Schema mem #%u +10s: heap=%u (delta=%d), max=%u (delta=%d)",
                       (unsigned)p.seq,
                       (unsigned)h, (int)h - (int)p.heapBefore,
                       (unsigned)m, (int)m - (int)p.maxBefore);
                p.active = false;
            }
        }

        if (webSocket->shouldSendUpdates()) {
            sendWebSocketUpdates();
        }
    }

    /**
     * @brief Stop the web server but keep configuration for potential restart.
     */
    ComponentStatus shutdown() override {
        webServer->stop();
        return ComponentStatus::Success;
    }

    // Provider management facade
    void registerProvider(IWebUIProvider* provider) {
        registry->registerProvider(provider);
    }

    void registerProviderWithComponent(IWebUIProvider* provider, IComponent* component) {
        registry->registerProviderWithComponent(provider, component);
    }

    void unregisterProvider(IWebUIProvider* provider) {
        registry->unregisterProvider(provider);
    }

    int getWebSocketClients() const {
        return webSocket->getClientCount();
    }

    uint16_t getPort() const { 
        return config.port; 
    }

    void notifyWiFiNetworkChanged() {
        webSocket->notifyWiFiNetworkChanged();
    }
    
    void closeAllWebSocketConnections() {
        webSocket->closeAllConnections();
    }

    void setConfigCallback(std::function<void(const WebUIConfig&)> callback) {
        onConfigChanged = callback;
    }

    const WebUIConfig& getConfig() const {
        return config;
    }
    
    void setConfig(const WebUIConfig& cfg) {
        config = cfg;
        DLOG_I(LOG_WEB, "Config updated: theme=%s, deviceName=%s", 
               config.theme, config.deviceName);
    }

    void registerProviderFactory(const String& typeKey, std::function<IWebUIProvider*(IComponent*)> factory) {
        registry->registerProviderFactory(typeKey, factory);
    }

    void registerApiRoute(const String& uri, WebRequestMethod method, ArRequestHandlerFunction handler) {
        webServer->registerRoute(uri, method, handler);
    }

    void registerApiUploadRoute(const String& uri, ArRequestHandlerFunction handler, ArUploadHandlerFunction uploadHandler) {
        webServer->registerUploadRoute(uri, handler, uploadHandler);
    }

    // IComponent override: post-initialization hook
    void onComponentsReady(const Components::ComponentRegistry& registry) override {
        this->registry->discoverProviders(registry);
        // Subscribe to future add/remove events
        auto& reg = const_cast<Components::ComponentRegistry&>(registry);
        reg.addListener(this);
        
        // A provider loss or address change invalidates sockets bound to the old
        // interface/address, regardless of whether that provider is WiFi or Ethernet.
        on<NetworkEvents::NetworkProviderStateEvent>(NetworkEvents::EVENT_PROVIDER_STATE_CHANGED,
            [this](const NetworkEvents::NetworkProviderStateEvent& event) {
                if (!event.connected) webSocket->closeAllConnections();
            });
        on<NetworkEvents::NetworkProviderAddressEvent>(NetworkEvents::EVENT_PROVIDER_ADDRESS_CHANGED,
            [this](const NetworkEvents::NetworkProviderAddressEvent&) {
                webSocket->closeAllConnections();
            });
    }

    // CachingWebUIProvider implementation for self-registration
    String getWebUIName() const override { return "WebUI"; }
    String getWebUIVersion() const override { return metadata.version; }
    IWebUIProvider* getWebUIProvider() override { return this; }

protected:
    void buildContexts(std::vector<WebUIContext>& contexts) override {
        // IMPORTANT: Schema must use STATIC string literals only!
        // Dynamic values cause memory corruption when cached.
        // Real-time data comes from getWebUIData(), not from the schema.
        
        contexts.push_back(WebUIContext::headerInfo("webui_uptime", "Uptime", "dc-info")
            .withField(WebUIField("uptime", "Uptime", WebUIFieldType::Display, "--", "", true))
            .withRealTime(1000)
            .withAPI("/api/webui/uptime"));
        
        // Settings context - use static literals, not config.* members
        contexts.push_back(WebUIContext::settings("webui_settings", "Web Interface")
            .withField(WebUIField("theme", "Theme", WebUIFieldType::Select, "auto", "dark,light,auto"))
            .withField(WebUIField("primary_color", "Primary Color", WebUIFieldType::Text, "#007acc"))
            .withField(WebUIField("enable_auth", "Enable Authentication", WebUIFieldType::Boolean, "false"))
            .withField(WebUIField("username", "Username", WebUIFieldType::Text, "admin"))
            .withField(WebUIField("password", "Password", WebUIFieldType::Password, ""))
        );
    }

public:
    String getWebUIData(const String& contextId) override {
        if (contextId == "webui_uptime") {
            JsonDocument doc;
            uint32_t seconds = HAL::Platform::getMillis() / 1000;
            uint32_t days = seconds / 86400;
            seconds %= 86400;
            uint32_t hours = seconds / 3600;
            seconds %= 3600;
            uint32_t minutes = seconds / 60;
            seconds %= 60;

            String uptimeStr;
            if (days > 0) uptimeStr += String(days) + "d ";
            if (hours > 0 || days > 0) uptimeStr += String(hours) + "h ";
            if (minutes > 0 || hours > 0 || days > 0) uptimeStr += String(minutes) + "m ";
            uptimeStr += String(seconds) + "s";

            doc["uptime"] = uptimeStr;
            String json;
            serializeJson(doc, json);
            return json;
        }

        if (contextId == "webui_settings") {
            JsonDocument doc;
            doc["theme"] = config.theme;
            doc["primary_color"] = config.primaryColor;
            doc["enable_auth"] = config.enableAuth ? "true" : "false";
            doc["username"] = config.username;
            doc["password"] = ""; // Never send password back
            String json;
            serializeJson(doc, json);
            return json;
        }
        return "{}";
    }

    // Registry listener events
    void onComponentAdded(IComponent* comp) override {
        if (!comp) return;
        IWebUIProvider* provider = comp->getWebUIProvider();
        if (provider) {
            registerProviderWithComponent(provider, comp);
        }
    }

    void onComponentRemoved(IComponent* comp) override {
        registry->handleComponentRemoved(comp);
    }

    String handleWebUIRequest(const String& contextId, const String& endpoint, const String& method, const std::map<String, String>& params) override {
        if (contextId == "webui_settings" && method == "POST") {
            auto fieldIt = params.find("field");
            auto valueIt = params.find("value");
            if (fieldIt != params.end() && valueIt != params.end()) {
                const String& field = fieldIt->second;
                const String& value = valueIt->second;
                
                if (field == "theme") {
                    config.setTheme(value.c_str());
                } else if (field == "primary_color") {
                    config.setPrimaryColor(value.c_str());
                } else if (field == "enable_auth") {
                    config.enableAuth = (value == "true" || value == "1");
                } else if (field == "username") {
                    config.setUsername(value.c_str());
                } else if (field == "password") {
                    if (value.length() > 0) {
                        config.setPassword(value.c_str());
                    }
                } else {
                    return "{\"success\":false, \"error\":\"Unknown field\"}";
                }
                
                if (onConfigChanged) {
                    onConfigChanged(config);
                }
                return "{\"success\":true}";
            }
        }
        return "{\"success\":false, \"error\":\"Invalid request\"}";
    }

private:
    bool authenticate(AsyncWebServerRequest* request) {
        if (!config.enableAuth) return true;
        return request->authenticate(config.username, config.password);
    }

    /**
     * @brief Print a string with JSON escaping to a response stream
     */
    void printJsonEscaped(AsyncResponseStream* response, const String& str) {
        for (size_t i = 0; i < str.length(); i++) {
            char c = str[i];
            switch (c) {
                case '"': response->print("\\\""); break;
                case '\\': response->print("\\\\"); break;
                case '\n': response->print("\\n"); break;
                case '\r': response->print("\\r"); break;
                case '\t': response->print("\\t"); break;
                default:
                    if (c < 0x20) {
                        // Control character
                        char buf[7];
                        snprintf(buf, sizeof(buf), "\\u00%02x", (unsigned char)c);
                        response->print(buf);
                    } else {
                        response->write(c);
                    }
                    break;
            }
        }
    }
    
    /**
     * @brief Add CORS headers to response if enabled in config
     */
    void addCorsHeaders(AsyncWebServerResponse* response) {
        if (config.enableCORS) {
            response->addHeader("Access-Control-Allow-Origin", "*");
            response->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
            response->addHeader("Access-Control-Allow-Headers", "Content-Type, X-API-Key, Authorization");
        }
    }
    
    void setupApiRoutes() {
        // Polling endpoint for real-time updates AND schema delivery
        // Use ?schema=1 to get schema (avoids separate TCP connection that fails during TIME_WAIT)
        webServer->registerRoute("/api/ui/updates", HTTP_GET, [this](AsyncWebServerRequest* request) {
            if (config.enableAuth && !authenticate(request)) {
                return request->requestAuthentication();
            }
            
            // Check if client is requesting schema
            if (request->hasParam("schema")) {
                DLOG_I(LOG_WEB, "Schema via poll endpoint (heap: %u)", (unsigned)HAL::Platform::getFreeHeap());
                std::shared_ptr<WebUI::ProviderRegistry::SchemaChunkState> state = registry->prepareSchemaGeneration();
                AsyncWebServerResponse* response = request->beginChunkedResponse(
                    "application/json",
                    [state](uint8_t* buffer, size_t maxLen, size_t index) -> size_t {
                        size_t written = 0;
                        if (!state || state->finished) return 0;
                        if (!state->began) {
                            if (maxLen < 1) return RESPONSE_TRY_AGAIN;
                            buffer[written++] = '[';
                            state->began = true;
                        }
                        while (written < maxLen && !state->finished) {
                            if (state->serializingContext) {
                                size_t n = state->serializer.write(buffer + written, maxLen - written);
                                written += n;
                                if (state->serializer.isComplete()) {
                                    state->serializingContext = false;
                                    state->needComma = true;
                                    state->currentContextPtr = nullptr;
                                } else if (n == 0) break;
                                continue;
                            }
                            bool hasNext = false;
                            while (state->providerIndex < state->providers.size()) {
                                IWebUIProvider* provider = state->providers[state->providerIndex];
                                if (!provider || !provider->isWebUIEnabled()) {
                                    state->providerIndex++;
                                    state->contextIndexInProvider = 0;
                                    continue;
                                }
                                const WebUIContext* ctxPtr = provider->getContextAtRef(state->contextIndexInProvider);
                                if (ctxPtr) {
                                    state->currentContextPtr = ctxPtr;
                                    state->contextIndexInProvider++;
                                    hasNext = true;
                                    break;
                                }
                                state->providerIndex++;
                                state->contextIndexInProvider = 0;
                            }
                            if (!hasNext) {
                                if (written < maxLen) buffer[written++] = ']';
                                state->finished = true;
                                std::vector<IWebUIProvider*>().swap(state->providers);
                                return written;
                            }
                            if (!state->currentContextPtr || !state->currentContextPtr->getContextIdCStr()[0]) continue;
                            if (state->needComma) {
                                if (written < maxLen) {
                                    buffer[written++] = ',';
                                } else {
                                    state->contextIndexInProvider--;
                                    return written;
                                }
                            }
                            state->serializer.begin(*state->currentContextPtr);
                            state->serializingContext = true;
                            size_t n = state->serializer.write(buffer + written, maxLen - written);
                            written += n;
                            if (state->serializer.isComplete()) {
                                state->serializingContext = false;
                                state->needComma = true;
                                state->currentContextPtr = nullptr;
                            } else if (n == 0) break;
                        }
                        // If nothing was written but serialization isn't done,
                        // tell the server to retry instead of ending the response
                        if (written == 0 && !state->finished) return RESPONSE_TRY_AGAIN;
                        return written;
                    });
                addCorsHeaders(response);
                response->addHeader("Connection", "close");
                request->send(response);
                return;
            }
            
            // Normal polling update
            webSocket->onPollRequest();
            int len = buildUpdateJson(true);
            if (len > 0) {
                // Inject SSE hint into response so frontend can upgrade to SSE
                if (webSocket->isSSEEnabled()) {
                    if (len > 2 && wsBuffer_[len-1] == '}' && wsBuffer_[len-2] == '}') {
                        int extra = snprintf(wsBuffer_ + len - 1, sizeof(wsBuffer_) - len + 1,
                            ",\"_sse\":\"/api/ui/events\"}");
                        if (extra > 0) len += extra - 1;
                    }
                }
                AsyncWebServerResponse* response = request->beginResponse(200, "application/json", wsBuffer_);
                addCorsHeaders(response);
                response->addHeader("Connection", "close");
                request->send(response);
            } else {
                request->send(503, "application/json", "{\"error\":\"buffer overflow\"}");
            }
        });

        // GET endpoint for client→server UI actions — uses query params instead
        // of POST body to avoid body-parser heap allocation (~500B) that crashes
        // ESP8266 at <2.5KB free heap.
        webServer->registerRoute("/api/ui/action", HTTP_GET, [this](AsyncWebServerRequest* request) {
            if (config.enableAuth && !authenticate(request)) {
                return request->requestAuthentication();
            }
            
            String contextId, field, value;
            if (request->hasParam("contextId")) {
                contextId = request->getParam("contextId")->value();
            }
            if (request->hasParam("field")) {
                field = request->getParam("field")->value();
            }
            if (request->hasParam("value")) {
                value = request->getParam("value")->value();
            }
            
            if (contextId.length() > 0 && field.length() > 0) {
                String response = handleUIAction(contextId, field, value);
                request->send(200, "application/json", response);
            } else {
                request->send(400, "application/json", "{\"error\":\"Missing contextId or field\"}");
            }
        });

        // System info API - optimized for ESP8266: use snprintf instead of String concatenation
        webServer->registerRoute("/api/system/info", HTTP_GET, [this](AsyncWebServerRequest* request) {
            char sysInfo[128];
            snprintf(sysInfo, sizeof(sysInfo), "{\"uptime\":%u,\"heap\":%u,\"clients\":%d}",
                (unsigned)HAL::Platform::getMillis(), (unsigned)HAL::Platform::getFreeHeap(), getWebSocketClients());
            AsyncWebServerResponse* response = request->beginResponse(200, "application/json", sysInfo);
            addCorsHeaders(response);
            request->send(response);
        });
        
        // API components
        webServer->registerRoute("/api/components", HTTP_GET, [this](AsyncWebServerRequest* request) {
            if (config.enableAuth && !authenticate(request)) {
                return request->requestAuthentication();
            }
            
            AsyncResponseStream *response = request->beginResponseStream("application/json");
            addCorsHeaders(response);
            JsonDocument doc;
            registry->getComponentsList(doc);
            serializeJson(doc, *response);
            request->send(response);
        });

        // API components enable
        webServer->registerRoute("/api/components/enable", HTTP_POST, [this](AsyncWebServerRequest* request) {
            if (config.enableAuth && !authenticate(request)) {
                return request->requestAuthentication();
            }

            AsyncResponseStream *response = request->beginResponseStream("application/json");
            addCorsHeaders(response);
            
            String name;
            bool enabled = true;
            if (request->hasParam("name", true)) {
                name = request->getParam("name", true)->value();
            }
            if (request->hasParam("enabled", true)) {
                String v = request->getParam("enabled", true)->value();
                enabled = (v == "true" || v == "1" || v == "on");
            }

            auto result = registry->enableComponent(name, enabled);

            JsonDocument doc;
            doc["success"] = result.success;
            doc["name"] = result.name;
            doc["enabled"] = result.enabled;
            if (!result.warning.isEmpty()) {
                doc["warning"] = result.warning;
            }
            
            serializeJson(doc, *response);
            request->send(response);

            if (result.found) {
                webSocket->broadcastSchemaChange(name);
            }
        });

        // Context schema endpoint - loads full schema for a specific context
        webServer->registerRoute("/api/ui/context", HTTP_GET, [this](AsyncWebServerRequest* request) {
            if (config.enableAuth && !authenticate(request)) {
                return request->requestAuthentication();
            }

            if (!request->hasParam("id")) {
                AsyncWebServerResponse* response = request->beginResponse(400, "application/json",
                    "{\"error\":\"Missing 'id' parameter\"}");
                addCorsHeaders(response);
                request->send(response);
                return;
            }

            String contextId = request->getParam("id")->value();
            DLOG_I(LOG_WEB, "Loading context schema for: %s (heap: %u)", contextId.c_str(), HAL::Platform::getFreeHeap());

            IWebUIProvider* provider = registry->getProviderForContext(contextId);
            if (!provider) {
                AsyncWebServerResponse* response = request->beginResponse(404, "application/json",
                    "{\"error\":\"Context not found\"}");
                addCorsHeaders(response);
                request->send(response);
                return;
            }

            // Find the context from the provider
            WebUIContext foundContext;
            bool found = false;
            provider->forEachContext([&](const WebUIContext& ctx) {
                if (strcmp(ctx.getContextIdCStr(), contextId.c_str()) == 0) {
                    foundContext = ctx;
                    found = true;
                    return false; // Stop iteration
                }
                return true; // Continue
            });

            if (!found) {
                AsyncWebServerResponse* response = request->beginResponse(404, "application/json",
                    "{\"error\":\"Context not found in provider\"}");
                addCorsHeaders(response);
                request->send(response);
                return;
            }

            // Serialize the context
            JsonDocument doc;
            JsonObject obj = doc.to<JsonObject>();
            serializeContext(obj, foundContext);

            AsyncResponseStream *response = request->beginResponseStream("application/json");
            addCorsHeaders(response);
            serializeJson(doc, *response);
            request->send(response);

            DLOG_I(LOG_WEB, "Context schema sent for: %s (heap: %u)", contextId.c_str(), HAL::Platform::getFreeHeap());
        });

        // Schema endpoint - uses ResponseStream for better memory management than chunked
        // ESPAsyncWebServer's chunked response has known memory leak issues
        webServer->registerChunkedRoute("/api/ui/schema", HTTP_GET, [this](AsyncWebServerRequest* request) {
            if (config.enableAuth && !authenticate(request)) {
                request->requestAuthentication();
                return;
            }

            SchemaMemProbe& probe = schemaMemProbes[schemaProbeNext % SCHEMA_PROBE_SLOTS];
            schemaProbeNext = (uint8_t)(schemaProbeNext + 1);

            probe.active = true;
            probe.seq = ++schemaProbeSeq;
            const uint32_t schemaSeq = probe.seq;
            probe.stage = 0;
            probe.t0 = HAL::Platform::getMillis();
            probe.heapBefore = HAL::Platform::getFreeHeap();
            probe.maxBefore = HAL::Platform::getMaxAllocHeap();

            const uint32_t heapBefore = probe.heapBefore;
            const uint32_t maxBefore = probe.maxBefore;

            request->onDisconnect([schemaSeq, heapBefore, maxBefore]() {
                DLOG_D(LOG_WEB, "Schema disconnect #%u: heap=%u (delta=%d), max=%u (delta=%d)",
                       (unsigned)schemaSeq,
                       (unsigned)HAL::Platform::getFreeHeap(), (int)HAL::Platform::getFreeHeap() - (int)heapBefore,
                       (unsigned)HAL::Platform::getMaxAllocHeap(), (int)HAL::Platform::getMaxAllocHeap() - (int)maxBefore);
            });

            std::shared_ptr<WebUI::ProviderRegistry::SchemaChunkState> state = registry->prepareSchemaGeneration();

            AsyncWebServerResponse* response = request->beginChunkedResponse(
                "application/json",
                [state](uint8_t* buffer, size_t maxLen, size_t index) -> size_t {
                    size_t written = 0;

                    if (!state || state->finished) return 0;

                    if (!state->began) {
                        if (maxLen < 1) return 0;
                        buffer[written++] = '[';
                        state->began = true;
                    }

                    while (written < maxLen && !state->finished) {
                        if (state->serializingContext) {
                            size_t n = state->serializer.write(buffer + written, maxLen - written);
                            written += n;

                            if (state->serializer.isComplete()) {
                                state->serializingContext = false;
                                state->needComma = true;
                                state->currentContextPtr = nullptr;
                            } else if (n == 0) {
                                break;
                            }
                            continue;
                        }

                        bool hasNext = false;
                        while (state->providerIndex < state->providers.size()) {
                            IWebUIProvider* provider = state->providers[state->providerIndex];
                            if (!provider || !provider->isWebUIEnabled()) {
                                state->providerIndex++;
                                state->contextIndexInProvider = 0;
                                continue;
                            }

                            const WebUIContext* ctxPtr = provider->getContextAtRef(state->contextIndexInProvider);
                            if (ctxPtr) {
                                state->currentContextPtr = ctxPtr;
                                state->contextIndexInProvider++;
                                hasNext = true;
                                break;
                            }

                            state->providerIndex++;
                            state->contextIndexInProvider = 0;
                        }

                        if (!hasNext) {
                            if (written < maxLen) {
                                buffer[written++] = ']';
                            }
                            state->finished = true;
                            std::vector<IWebUIProvider*>().swap(state->providers);
                            return written;
                        }

                        if (!state->currentContextPtr || !state->currentContextPtr->getContextIdCStr()[0]) continue;

                        if (state->needComma) {
                            if (written < maxLen) {
                                buffer[written++] = ',';
                            } else {
                                state->contextIndexInProvider--;
                                return written;
                            }
                        }

                        state->serializer.begin(*state->currentContextPtr);
                        state->serializingContext = true;

                        size_t n = state->serializer.write(buffer + written, maxLen - written);
                        written += n;

                        if (state->serializer.isComplete()) {
                            state->serializingContext = false;
                            state->needComma = true;
                            state->currentContextPtr = nullptr;
                        } else if (n == 0) {
                            break;
                        }
                    }

                    return written;
                });

            addCorsHeaders(response);
            response->addHeader("Connection", "close");
            request->send(response);

            probe.heapAfterSend = HAL::Platform::getFreeHeap();
            probe.maxAfterSend = HAL::Platform::getMaxAllocHeap();
            DLOG_D(LOG_WEB, "Schema queued #%u: heap=%u (delta=%d), max=%u (delta=%d)",
                   (unsigned)probe.seq,
                   (unsigned)probe.heapAfterSend, (int)probe.heapAfterSend - (int)probe.heapBefore,
                   (unsigned)probe.maxAfterSend, (int)probe.maxAfterSend - (int)probe.maxBefore);
        });
    }
    
    // Duplicated helper to keep compilation working until I move it to a shared util or ProviderRegistry
    void serializeContext(JsonObject& obj, const WebUIContext& context) {
        obj["contextId"] = context.getContextIdCStr();
        obj["title"] = context.getTitleCStr();
        obj["icon"] = context.getIconCStr();
        obj["location"] = (int)context.location;
        obj["presentation"] = (int)context.presentation;
        obj["priority"] = context.priority;
        obj["apiEndpoint"] = context.getApiEndpointCStr();
        obj["alwaysInteractive"] = context.alwaysInteractive;
        
        if (context.hasCustomHtml()) obj["customHtml"] = context.getCustomHtmlCStr();
        if (context.hasCustomCss()) obj["customCss"] = context.getCustomCssCStr();
        if (context.hasCustomJs()) obj["customJs"] = context.getCustomJsCStr();

        JsonArray fields = obj["fields"].to<JsonArray>();
        for (const auto& field : context.fields) {
            JsonObject fieldObj = fields.add<JsonObject>();
            fieldObj["name"] = field.getNameCStr();
            fieldObj["label"] = field.getLabelCStr();
            fieldObj["type"] = (int)field.type;
            if (field.isMultiValue()) {
                JsonArray values = fieldObj["value"].to<JsonArray>();
                for (const String& value : field.values) values.add(value);
            } else {
                fieldObj["value"] = field.getValueCStr();
            }
            fieldObj["unit"] = field.getUnitCStr();
            fieldObj["readOnly"] = field.readOnly;
            fieldObj["minValue"] = field.minValue;
            fieldObj["maxValue"] = field.maxValue;
            fieldObj["endpoint"] = field.getEndpointCStr();
            if (!field.options.empty()) {
                JsonArray options = fieldObj["options"].to<JsonArray>();
                for (const auto& opt : field.options) options.add(opt);
            }
            if (!field.optionLabels.empty()) {
                JsonObject labels = fieldObj["optionLabels"].to<JsonObject>();
                for (const auto& pair : field.optionLabels) labels[pair.first] = pair.second;
            }
        }
    }

    // In polling mode, initial state is served via GET /api/ui/updates
    
    // Handle UI action — returns provider's JSON response (may contain errors)
    String handleUIAction(const String& contextId, const String& field, const String& value) {
        IWebUIProvider* provider = registry->getProviderForContext(contextId);
        if (provider) {
            std::map<String, String> params;
            params["field"] = field;
            params["value"] = value;
            String result = provider->handleWebUIRequest(contextId, "/", "POST", params);
            forceNextUpdate = true;
            return result;
        }
        return "{\"success\":false,\"error\":\"Unknown context\"}";
    }

    // Build JSON update into wsBuffer_. Returns length, or 0 on failure.
    // forceFull=true sends all contexts (for polling), false uses delta check (for SSE broadcast).
    int buildUpdateJson(bool forceFull) {
        char* buffer = wsBuffer_;
        const size_t bufSize = sizeof(wsBuffer_);
        int pos = DSNPRINTF_P(buffer, bufSize,
            "{\"system\":{\"uptime\":%u,\"heap\":%u,\"clients\":%d,\"device_name\":\"%s\"},\"contexts\":{",
            (unsigned)HAL::Platform::getMillis(), (unsigned)HAL::Platform::getFreeHeap(), getWebSocketClients(), config.deviceName);
        
        if (pos < 0 || pos >= (int)bufSize) return 0;
        
        int contextCount = 0;
        const auto& contextProviders = registry->getContextProviders();
        
        for (const auto& pair : contextProviders) {
            if (pos > (int)bufSize - 512) break;
            
            const String& contextId = pair.first;
            IWebUIProvider* provider = pair.second;
            
            // Delta check - skip unchanged data (only for SSE broadcast, not polling)
            if (!forceFull && !forceNextUpdate && !provider->hasDataChanged(contextId)) continue;
            
            String contextData = provider->getWebUIData(contextId);
            if (contextData.isEmpty() || contextData == "{}") continue;
            
            int needed = contextId.length() + contextData.length() + 5;
            if (pos + needed >= (int)bufSize - 10) break;
            
            if (contextCount > 0) buffer[pos++] = ',';
            
            int written = DSNPRINTF_P(buffer + pos, bufSize - pos,
                "\"%s\":%s", contextId.c_str(), contextData.c_str());
            
            if (written > 0 && pos + written < (int)bufSize) {
                pos += written;
                contextCount++;
            }
        }
        
        if (pos < (int)bufSize - 3) {
            buffer[pos++] = '}';
            buffer[pos++] = '}';
            buffer[pos] = '\0';
            return pos;
        }
        return 0;
    }

    void sendWebSocketUpdates() {
        // Skip entirely when no clients — saves building the JSON buffer
        if (webSocket->getClientCount() == 0) {
            forceNextUpdate = false;
            return;
        }

        // Skip when heap is critically low
        if (HAL::Platform::getFreeHeap() < 2000) {
            DLOG_W(LOG_WEB, "SSE broadcast skipped: heap=%u", (unsigned)HAL::Platform::getFreeHeap());
            return;
        }

        int len = buildUpdateJson(false);
        if (len > 0) {
            DLOG_D(LOG_WEB, "SSE broadcast: %d bytes, clients=%d", len, webSocket->getClientCount());
            webSocket->broadcast(wsBuffer_, len);
            forceNextUpdate = false;
        }
    }
};

// Static member definition (single shared WS buffer — saves 2KB BSS vs two separate buffers)
char WebUIComponent::wsBuffer_[WEBUI_WS_BUFFER_SIZE];

} // namespace Components
} // namespace DomoticsCore
