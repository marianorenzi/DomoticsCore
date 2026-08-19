#ifndef DOMOTICS_CORE_SYSTEM_H
#define DOMOTICS_CORE_SYSTEM_H

/**
 * @file System.h
 * @brief Complete ready-to-use system orchestrator for DomoticsCore.
 * 
 * This is the "batteries included" component that provides automatic
 * WiFi connection, LED status visualization, remote console, state management,
 * and component orchestration.
 * 
 * @example DomoticsCore-System/examples/Minimal/src/main.cpp
 * @example DomoticsCore-System/examples/Standard/src/main.cpp
 * @example DomoticsCore-System/examples/FullStack/src/main.cpp
 */

#include <DomoticsCore/Core.h>
#include <DomoticsCore/LED.h>
#include <DomoticsCore/RemoteConsole.h>
#include <DomoticsCore/Wifi.h>
#include <DomoticsCore/Network.h>
#include <DomoticsCore/Logger.h>
#include <DomoticsCore/Events.h>
#include <DomoticsCore/Platform_HAL.h>     // For HAL::getChipId()
// Platform_HAL.h provides: getFreeHeap(), getChipModel(), getChipId()

// System submodules
#include "SystemConfig.h"
#include "SystemPersistence.h"
#include "SystemWebUISetup.h"

// Intentional deviation from Constitution IX (no #ifdef outside HAL):
// __has_include() enables the zero-config "just add components" developer experience.
// This is compile-time optional dependency detection, not platform-specific feature-flagging.
// 20 directives across: initial includes, component registration, event orchestration, console commands.

// Optional components - include if available
#if __has_include(<DomoticsCore/WebUI.h>)
#include <DomoticsCore/WebUI.h>
#endif

#if __has_include(<DomoticsCore/NTP.h>)
#include <DomoticsCore/NTP.h>
#include <DomoticsCore/NTPEvents.h>
#endif

#if __has_include(<DomoticsCore/Storage.h>)
#include <DomoticsCore/Storage.h>
#endif

#if __has_include(<DomoticsCore/MQTT.h>)
#include <DomoticsCore/MQTT.h>
#endif

#if __has_include(<DomoticsCore/OTA.h>)
#include <DomoticsCore/OTA.h>
#endif

#if __has_include(<DomoticsCore/SystemInfo.h>)
#include <DomoticsCore/SystemInfo.h>
#endif

#if __has_include(<DomoticsCore/HomeAssistant.h>)
#include <DomoticsCore/HomeAssistant.h>
#include <DomoticsCore/HAEvents.h>
#endif

#include <vector>
#include <functional>

#define LOG_SYSTEM "SYSTEM"

namespace DomoticsCore {

/**
 * @brief Complete ready-to-use system
 * 
 * The developer only needs to:
 * 1. Configure via SystemConfig or use presets (minimal/standard/fullStack)
 * 2. Add their custom sensors/actuators
 * 3. Call begin() and loop()
 * 
 * Everything else is handled automatically!
 */
class System {
private:
    SystemConfig config;
    Core core;
    
    // Component references (owned by core)
    Components::LEDComponent* led = nullptr;
    Components::RemoteConsoleComponent* console = nullptr;
    Components::WifiComponent* wifi = nullptr;
    Components::NetworkComponent* network = nullptr;
    
    // WebUI providers (managed by SystemHelpers::WebUIProviders)
    SystemHelpers::WebUIProviders webUIProviders;
    
    // System state
    SystemState state = SystemState::BOOTING;
    std::vector<std::function<void(SystemState, SystemState)>> stateCallbacks;
    bool initialized = false;
    
public:
    System(const SystemConfig& cfg = SystemConfig()) : config(cfg) {}
    
    ~System() {
        webUIProviders.cleanup();
    }
    
    /**
     * @brief Initialize the system
     * @return true if initialization successful
     */
    bool begin() {
        if (initialized) {
            DLOG_W(LOG_SYSTEM, "System already initialized");
            return true;
        }
        
        printBanner();
        autoDetectModel();
        state = SystemState::BOOTING;
        
        // 1. Register components
        registerLEDComponent();
        registerStorageComponent();
        registerNetworkComponent();
        registerWifiComponent();
        registerConsoleComponent();
        registerOptionalComponents();
        
        // 2. Initialize Core
        if (!core.begin()) {
            DLOG_E(LOG_SYSTEM, "Core initialization failed!");
            setState(SystemState::ERROR);
            return false;
        }
        
        // Heap guard threshold for post-init steps
        static constexpr uint32_t MIN_HEAP_POST_INIT = 3072;

        // 3. Load configurations from Storage
        if (HAL::getFreeHeap() >= MIN_HEAP_POST_INIT) {
            SystemHelpers::loadAllConfigs(core, config, wifi);
        } else {
            DLOG_W(LOG_SYSTEM, "Low heap (%u), skipping config loading", (unsigned)HAL::getFreeHeap());
        }
        
        // 4. Register WebUI providers
        if (HAL::getFreeHeap() >= MIN_HEAP_POST_INIT) {
            SystemHelpers::setupWebUIProviders(core, config, webUIProviders, wifi, console);
        } else {
            DLOG_W(LOG_SYSTEM, "Low heap (%u), skipping WebUI providers", (unsigned)HAL::getFreeHeap());
        }
        
        // 5. Setup event orchestration
        if (HAL::getFreeHeap() >= MIN_HEAP_POST_INIT) {
            setupEventOrchestration();
        } else {
            DLOG_W(LOG_SYSTEM, "Low heap (%u), skipping event orchestration", (unsigned)HAL::getFreeHeap());
        }
        
        // 6. Initialize boot diagnostics persistence
        if (HAL::getFreeHeap() >= MIN_HEAP_POST_INIT) {
            initBootDiagnosticsPersistence();
        } else {
            DLOG_W(LOG_SYSTEM, "Low heap (%u), skipping boot diagnostics", (unsigned)HAL::getFreeHeap());
        }
        
        // 7. System Ready
        setState(SystemState::READY);
        printReadyBanner();
        
        initialized = true;
        return true;
    }
    
    /**
     * @brief Main loop - call this in Arduino loop()
     */
    void loop() {
        core.loop();
    }
    
    // ========== Accessors ==========
    
    Core& getCore() { return core; }
    SystemState getState() const { return state; }
    Components::RemoteConsoleComponent* getConsole() { return console; }
    Components::WifiComponent* getWiFi() { return wifi; }
    Components::NetworkComponent* getNetwork() { return network; }
    
    void onStateChange(std::function<void(SystemState, SystemState)> callback) {
        if (stateCallbacks.size() >= 8) {
            DLOG_W(LOG_SYSTEM, "Max state callbacks reached (8), ignoring");
            return;
        }
        stateCallbacks.push_back(callback);
    }
    
    void registerCommand(const String& name, std::function<String(const String&)> handler) {
        if (console) console->registerCommand(name, handler);
    }
    
private:
    // ========== Initialization Helpers ==========
    
    void printBanner() {
        DLOG_I(LOG_SYSTEM, "========================================");
        DLOG_I(LOG_SYSTEM, "DomoticsCore System");
        DLOG_I(LOG_SYSTEM, "Device: %s v%s", config.deviceName.c_str(), config.firmwareVersion.c_str());
        DLOG_I(LOG_SYSTEM, "========================================");
    }
    
    void autoDetectModel() {
        if (config.model.isEmpty()) {
            config.model = HAL::getChipModel();
            DLOG_I(LOG_SYSTEM, "Auto-detected model: %s", config.model.c_str());
        }
        DLOG_I(LOG_SYSTEM, "Manufacturer: %s, Model: %s", config.manufacturer.c_str(), config.model.c_str());
    }
    
    void registerLEDComponent() {
        if (!config.enableLED) return;
        
        auto ledPtr = std::make_unique<Components::LEDComponent>();
        led = ledPtr.get();
        led->addSingleLED(config.ledPin, "status", 255, !config.ledActiveHigh);
        core.addComponent(std::move(ledPtr));
        
        if (led->begin() == Components::ComponentStatus::Success) {
            led->setActive(true);
            DLOG_I(LOG_SYSTEM, "✓ LED component initialized (early)");
        } else {
            DLOG_E(LOG_SYSTEM, "✗ LED initialization failed");
        }
    }
    
    void registerStorageComponent() {
#if __has_include(<DomoticsCore/Storage.h>)
        if (!config.enableStorage) return;
        
        Components::StorageConfig storageConfig;
        storageConfig.namespace_name = config.storageNamespace;
        core.addComponent(std::make_unique<Components::StorageComponent>(storageConfig));
        DLOG_I(LOG_SYSTEM, "✓ Storage component registered (namespace: %s)", config.storageNamespace.c_str());
#else
        if (config.enableStorage) {
            DLOG_W(LOG_SYSTEM, "⚠️  Storage requested but library not installed");
        }
#endif
    }
    
    void registerWifiComponent() {
        auto wifiPtr = std::make_unique<Components::WifiComponent>(config.wifiSSID, config.wifiPassword);
        wifi = wifiPtr.get();
        
        if (config.wifiSSID.isEmpty() && config.wifiAutoConfig) {
            String apSSID = config.wifiAPSSID;
            if (apSSID.isEmpty()) {
                uint64_t chipid = HAL::getChipId();
                char apBuf[64];
                snprintf(apBuf, sizeof(apBuf), "%s-%08X", config.deviceName.c_str(), (uint32_t)(chipid >> 32));
                apSSID = apBuf;
            }
            wifi->enableAP(apSSID, config.wifiAPPassword);
            DLOG_I(LOG_SYSTEM, "✓ WiFi AP mode enabled: %s", apSSID.c_str());
        }
        
        core.addComponent(std::move(wifiPtr));
        DLOG_I(LOG_SYSTEM, "✓ WiFi component configured");
    }

    void registerNetworkComponent() {
        auto networkPtr = std::make_unique<Components::NetworkComponent>(config.networkPriorities);
        network = networkPtr.get();
        core.addComponent(std::move(networkPtr));
        DLOG_I(LOG_SYSTEM, "✓ Network component registered");
    }
    
    void registerConsoleComponent() {
        if (!config.enableConsole) return;
        
        Components::RemoteConsoleConfig consoleConfig;
        consoleConfig.port = config.consolePort;
        consoleConfig.maxClients = config.consoleMaxClients;
        consoleConfig.defaultLogLevel = config.defaultLogLevel;
        
        auto consolePtr = std::make_unique<Components::RemoteConsoleComponent>(consoleConfig);
        console = consolePtr.get();
        
        console->registerCommand("status", [this](const String&) { return getSystemStatus(); });
        console->registerCommand("wifi", [this](const String&) { return getWiFiStatus(); });
        console->registerCommand("storage", [this](const String& args) { return getStorageContents(args); });
        console->registerCommand("bootdiag", [this](const String&) { return getBootDiagnostics(); });
        
        core.addComponent(std::move(consolePtr));
        DLOG_I(LOG_SYSTEM, "✓ RemoteConsole enabled (port %d)", config.consolePort);
    }
    
    void registerOptionalComponents() {
        registerWebUIComponent();
        registerNTPComponent();
        registerMQTTAndHAComponents();
        registerOTAComponent();
        registerSystemInfoComponent();
    }
    
    void registerWebUIComponent() {
#if __has_include(<DomoticsCore/WebUI.h>)
        if (!config.enableWebUI) return;
        
        Components::WebUIConfig webuiConfig;
        webuiConfig.port = config.webUIPort;
        webuiConfig.setDeviceName(config.deviceName.c_str());
        core.addComponent(std::make_unique<Components::WebUIComponent>(webuiConfig));
        DLOG_I(LOG_SYSTEM, "✓ WebUI component added (port %d)", config.webUIPort);
#else
        if (config.enableWebUI) DLOG_W(LOG_SYSTEM, "⚠️  WebUI requested but library not installed");
#endif
    }
    
    void registerNTPComponent() {
#if __has_include(<DomoticsCore/NTP.h>)
        if (!config.enableNTP) return;
        core.addComponent(std::make_unique<Components::NTPComponent>());
        DLOG_I(LOG_SYSTEM, "✓ NTP component added");
#else
        if (config.enableNTP) DLOG_W(LOG_SYSTEM, "⚠️  NTP requested but library not installed");
#endif
    }
    
    void registerMQTTAndHAComponents() {
#if __has_include(<DomoticsCore/MQTT.h>)
        if (!config.enableMQTT) return;
        
        Components::MQTTConfig mqttConfig;
        mqttConfig.broker = config.mqttBroker;
        mqttConfig.port = config.mqttPort;
        mqttConfig.username = config.mqttUser;
        mqttConfig.password = config.mqttPassword;
        mqttConfig.clientId = config.mqttClientId;
        mqttConfig.enabled = true;
        core.addComponent(std::make_unique<Components::MQTTComponent>(mqttConfig));
        DLOG_I(LOG_SYSTEM, "✓ MQTT component added");
        
#if __has_include(<DomoticsCore/HomeAssistant.h>)
        if (config.enableHomeAssistant) {
            using namespace Components::HomeAssistant;
            HAConfig haConfig;
            HA::setField(haConfig.deviceName, config.deviceName.c_str(), HA::MAX_DEVICE_NAME);
            HA::setField(haConfig.swVersion, config.firmwareVersion.c_str(), HA::MAX_SW_VERSION);
            HA::setField(haConfig.manufacturer, config.manufacturer.c_str(), HA::MAX_MANUFACTURER);
            HA::setField(haConfig.model, config.model.c_str(), HA::MAX_MODEL);
            // nodeId: copy, lowercase, replace spaces with underscores
            HA::setField(haConfig.nodeId, config.deviceName.c_str(), HA::MAX_NODE_ID);
            for (size_t i = 0; haConfig.nodeId[i]; i++) {
                if (haConfig.nodeId[i] == ' ') haConfig.nodeId[i] = '_';
                else haConfig.nodeId[i] = tolower((unsigned char)haConfig.nodeId[i]);
            }
            core.addComponent(std::make_unique<HomeAssistantComponent>(haConfig));
            DLOG_I(LOG_SYSTEM, "✓ HomeAssistant component added (nodeId: %s)", haConfig.nodeId);
        }
#endif
#else
        if (config.enableMQTT) DLOG_W(LOG_SYSTEM, "⚠️  MQTT requested but library not installed");
#endif
    }
    
    void registerOTAComponent() {
#if __has_include(<DomoticsCore/OTA.h>)
        if (!config.enableOTA) return;
        Components::OTAConfig otaConfig;
        otaConfig.bearerToken = config.otaPassword;
        core.addComponent(std::make_unique<Components::OTAComponent>(otaConfig));
        DLOG_I(LOG_SYSTEM, "✓ OTA component added");
#else
        if (config.enableOTA) DLOG_W(LOG_SYSTEM, "⚠️  OTA requested but library not installed");
#endif
    }
    
    void registerSystemInfoComponent() {
#if __has_include(<DomoticsCore/SystemInfo.h>)
        if (!config.enableSystemInfo) return;
        
        Components::SystemInfoConfig sysInfoConfig;
        sysInfoConfig.deviceName = config.deviceName;
        sysInfoConfig.manufacturer = config.manufacturer;
        sysInfoConfig.firmwareVersion = config.firmwareVersion;
        core.addComponent(std::make_unique<Components::SystemInfoComponent>(sysInfoConfig));
        DLOG_I(LOG_SYSTEM, "✓ SystemInfo component added");
#else
        if (config.enableSystemInfo) DLOG_W(LOG_SYSTEM, "⚠️  SystemInfo requested but library not installed");
#endif
    }
    
    // ========== Boot Diagnostics Persistence ==========
    
    /**
     * @brief Initialize boot diagnostics persistence via Storage
     * 
     * Loads boot_count from Storage, increments it, saves back,
     * and updates SystemInfo component with the value.
     */
    void initBootDiagnosticsPersistence() {
#if __has_include(<DomoticsCore/Storage.h>) && __has_include(<DomoticsCore/SystemInfo.h>)
        if (!config.enableStorage || !config.enableSystemInfo) return;
        
        auto* storage = core.getComponent<Components::StorageComponent>("Storage");
        auto* sysInfo = core.getComponent<Components::SystemInfoComponent>("System Info");
        
        if (!storage || !sysInfo) {
            DLOG_W(LOG_SYSTEM, "Boot diagnostics: Storage or SystemInfo not available");
            return;
        }
        
        // Load and increment boot count
        uint32_t bootCount = storage->getInt("boot_count", 0) + 1;
        storage->putInt("boot_count", static_cast<int32_t>(bootCount));
        
        // Get boot diagnostics from SystemInfo and update boot count
        const auto& diag = sysInfo->getBootDiagnostics();
        sysInfo->setBootCount(bootCount);
        
        // Persist last reset info for debugging
        storage->putInt("last_reset", static_cast<int32_t>(diag.resetReason));
        storage->putInt("last_heap", static_cast<int32_t>(diag.lastBootHeap));
        storage->putInt("last_minheap", static_cast<int32_t>(diag.lastBootMinHeap));
        
        DLOG_I(LOG_SYSTEM, "Boot #%u persisted (Reset: %s)", 
               bootCount, diag.getResetReasonString().c_str());
#endif
    }
    
    // ========== Event Orchestration ==========
    
    void setupEventOrchestration() {
        DLOG_I(LOG_SYSTEM, "Setting up component event orchestration...");
        
        // WiFi → MQTT
#if __has_include(<DomoticsCore/MQTT.h>)
        auto* mqttComp = core.getComponent<Components::MQTTComponent>("MQTT");
        if (mqttComp && wifi) {
            core.getEventBus().subscribe(WifiEvents::EVENT_STA_CONNECTED, [mqttComp](const void* payload) {
                if (payload && *static_cast<const bool*>(payload)) {
                    DLOG_I(LOG_SYSTEM, "📶 WiFi connected → triggering MQTT connection");
                    mqttComp->connect();
                }
            });
            DLOG_I(LOG_SYSTEM, "✓ WiFi → MQTT orchestration configured");

            if (wifi->isSTAConnected()) {
                DLOG_I(LOG_SYSTEM, "📶 WiFi already connected → triggering MQTT");
                mqttComp->connect();
            }
        }
#endif
        
        // NTP event logging
#if __has_include(<DomoticsCore/NTP.h>)
        auto* ntpComp = core.getComponent<Components::NTPComponent>("NTP");
        if (ntpComp && wifi) {
            core.getEventBus().subscribe(NTPEvents::EVENT_SYNCED, [](const void*) {
                DLOG_I(LOG_SYSTEM, "NTP time synchronized");
            });
            DLOG_I(LOG_SYSTEM, "NTP event monitoring configured");
        }
#endif
        
        // HA event logging
#if __has_include(<DomoticsCore/MQTT.h>) && __has_include(<DomoticsCore/HomeAssistant.h>)
        auto* haComp = core.getComponent<Components::HomeAssistant::HomeAssistantComponent>("HomeAssistant");
        if (haComp) {
            core.getEventBus().subscribe(HAEvents::EVENT_DISCOVERY_PUBLISHED, [](const void* payload) {
                int count = payload ? *static_cast<const int*>(payload) : 0;
                DLOG_I(LOG_SYSTEM, "Home Assistant discovery published (%d entities)", count);
            });
            DLOG_I(LOG_SYSTEM, "MQTT -> HomeAssistant orchestration configured");
        }
#endif
    }
    
    void printReadyBanner() {
        DLOG_I(LOG_SYSTEM, "========================================");
        DLOG_I(LOG_SYSTEM, "System Ready!");
        if (wifi) {
            String ip = wifi->getLocalIP();
            if (wifi->isSTAConnected()) {
                DLOG_I(LOG_SYSTEM, "WiFi: %s", wifi->getSSID().c_str());
                DLOG_I(LOG_SYSTEM, "IP: %s", ip.c_str());
            } else if (wifi->isAPEnabled()) {
                DLOG_I(LOG_SYSTEM, "WiFi: AP Mode - %s", wifi->getAPSSID().c_str());
                DLOG_I(LOG_SYSTEM, "IP: %s", ip.c_str());
            }
            if (config.enableConsole) {
                DLOG_I(LOG_SYSTEM, "Telnet: telnet %s %d", ip.c_str(), config.consolePort);
            }
            if (config.enableWebUI) {
                DLOG_I(LOG_SYSTEM, "WebUI: http://%s:%d", ip.c_str(), config.webUIPort);
            }
        }
        DLOG_I(LOG_SYSTEM, "========================================");
    }
    
    // ========== State Management ==========
    
    void setState(SystemState newState) {
        if (newState == state) return;
        
        SystemState oldState = state;
        state = newState;
        
        DLOG_I(LOG_SYSTEM, "State: %s → %s", systemStateToString(oldState), systemStateToString(newState));
        updateLEDPattern(newState);
        
        for (auto& callback : stateCallbacks) {
            callback(oldState, newState);
        }
    }
    
    void updateLEDPattern(SystemState state) {
        if (!led) return;
        
        switch (state) {
            case SystemState::BOOTING:
                led->setLEDEffect("status", Components::LEDEffect::Blink, 200);
                break;
            case SystemState::WIFI_CONNECTING:
                led->setLEDEffect("status", Components::LEDEffect::Blink, 1000);
                break;
            case SystemState::WIFI_CONNECTED:
                led->setLEDEffect("status", Components::LEDEffect::Pulse, 2000);
                break;
            case SystemState::SERVICES_STARTING:
                led->setLEDEffect("status", Components::LEDEffect::Fade, 1500);
                break;
            case SystemState::READY:
                led->setLEDEffect("status", Components::LEDEffect::Breathing, 3000);
                break;
            case SystemState::ERROR:
                led->setLEDEffect("status", Components::LEDEffect::Blink, 300);
                break;
            case SystemState::OTA_UPDATE:
                led->setLED("status", Components::LEDColor::White(), 255);
                break;
            case SystemState::SHUTDOWN:
                led->setLED("status", Components::LEDColor::Off(), 0);
                break;
        }
    }
    
    // ========== Console Commands ==========
    
    String getSystemStatus() {
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "System Status:\n"
                 "  Device: %s v%s\n"
                 "  Uptime: %lus\n"
                 "  Free Heap: %lu bytes\n"
                 "  State: %s\n",
                 config.deviceName.c_str(), config.firmwareVersion.c_str(),
                 (unsigned long)(HAL::getMillis() / 1000),
                 (unsigned long)HAL::getFreeHeap(),
                 systemStateToString(state));
        return String(buf);
    }
    
    String getWiFiStatus() {
        return wifi ? wifi->getDetailedStatus() : "WiFi Status: Not initialized\n";
    }
    
    String getStorageContents(const String& args) {
#if __has_include(<DomoticsCore/Storage.h>)
        auto* storage = core.getComponent<Components::StorageComponent>("Storage");
        if (!storage) return "Storage: Not available\n";
        return storage->dumpContents();
#else
        return "Storage: Not compiled in\n";
#endif
    }
    
    String getBootDiagnostics() {
#if __has_include(<DomoticsCore/SystemInfo.h>)
        auto* sysInfo = core.getComponent<Components::SystemInfoComponent>("System Info");
        if (!sysInfo) return "Boot Diagnostics: SystemInfo not available\n";
        
        const auto& diag = sysInfo->getBootDiagnostics();
        if (!diag.valid) return "Boot Diagnostics: Not captured\n";
        
        char buf[512];
        int pos = snprintf(buf, sizeof(buf),
                 "Boot Diagnostics:\n"
                 "  Boot Count: %lu\n"
                 "  Reset Reason: %s\n"
                 "  Boot Heap: %lu bytes\n"
                 "  Boot Min Heap: %lu bytes\n",
                 (unsigned long)diag.bootCount,
                 diag.getResetReasonString().c_str(),
                 (unsigned long)diag.lastBootHeap,
                 (unsigned long)diag.lastBootMinHeap);
        if (pos < 0) pos = 0;
        if ((size_t)pos >= sizeof(buf)) pos = sizeof(buf) - 1;

        if (diag.wasUnexpectedReset()) {
            pos += snprintf(buf + pos, sizeof(buf) - pos,
                            "  WARNING: Previous boot ended unexpectedly!\n");
            if (pos < 0) pos = 0;
            if ((size_t)pos >= sizeof(buf)) pos = sizeof(buf) - 1;
        }

        // Also show persisted history from Storage
#if __has_include(<DomoticsCore/Storage.h>)
        auto* storage = core.getComponent<Components::StorageComponent>("Storage");
        if (storage) {
            snprintf(buf + pos, sizeof(buf) - pos,
                     "\nPersisted Data:\n"
                     "  boot_count: %d\n"
                     "  last_reset: %d\n"
                     "  last_heap: %d\n"
                     "  last_minheap: %d\n",
                     storage->getInt("boot_count", 0),
                     storage->getInt("last_reset", -1),
                     storage->getInt("last_heap", 0),
                     storage->getInt("last_minheap", 0));
        }
#endif
        return String(buf);
#else
        return "Boot Diagnostics: SystemInfo not compiled in\n";
#endif
    }
};

} // namespace DomoticsCore

#endif // DOMOTICS_CORE_SYSTEM_H
