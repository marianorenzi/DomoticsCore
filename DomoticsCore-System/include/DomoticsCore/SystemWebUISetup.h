#ifndef DOMOTICS_CORE_SYSTEM_WEBUI_SETUP_H
#define DOMOTICS_CORE_SYSTEM_WEBUI_SETUP_H

/**
 * @file SystemWebUISetup.h
 * @brief Handles WebUI provider registration and configuration persistence callbacks.
 * 
 * This module creates WebUI providers for each component and sets up
 * the callbacks that persist configuration changes to Storage.
 */

#include <DomoticsCore/Core.h>
#include <DomoticsCore/Logger.h>
#include <DomoticsCore/Platform_HAL.h>
#include "SystemConfig.h"

// Storage component (optional)
#if __has_include(<DomoticsCore/Storage.h>)
#include <DomoticsCore/Storage.h>
#define WEBUI_SETUP_HAS_STORAGE 1
#else
#define WEBUI_SETUP_HAS_STORAGE 0
#endif

// WebUI and providers (optional)
#if __has_include(<DomoticsCore/WebUI.h>)
#include <DomoticsCore/WebUI.h>
#define WEBUI_SETUP_HAS_WEBUI 1

  #if __has_include(<DomoticsCore/NetworkWebUI.h>)
  #include <DomoticsCore/NetworkWebUI.h>
  #define WEBUI_SETUP_HAS_NETWORK_WEBUI 1
  #else
  #define WEBUI_SETUP_HAS_NETWORK_WEBUI 0
  #endif

  #if __has_include(<DomoticsCore/WifiWebUI.h>)
  #include <DomoticsCore/WifiWebUI.h>
  #define WEBUI_SETUP_HAS_WIFI_WEBUI 1
  #else
  #define WEBUI_SETUP_HAS_WIFI_WEBUI 0
  #endif

  #if __has_include(<DomoticsCore/NTPWebUI.h>)
  #include <DomoticsCore/NTPWebUI.h>
  #define WEBUI_SETUP_HAS_NTP_WEBUI 1
  #else
  #define WEBUI_SETUP_HAS_NTP_WEBUI 0
  #endif

  #if __has_include(<DomoticsCore/MQTTWebUI.h>)
  #include <DomoticsCore/MQTTWebUI.h>
  #define WEBUI_SETUP_HAS_MQTT_WEBUI 1
  #else
  #define WEBUI_SETUP_HAS_MQTT_WEBUI 0
  #endif

  #if __has_include(<DomoticsCore/OTAWebUI.h>)
  #include <DomoticsCore/OTAWebUI.h>
  #define WEBUI_SETUP_HAS_OTA_WEBUI 1
  #else
  #define WEBUI_SETUP_HAS_OTA_WEBUI 0
  #endif

  #if __has_include(<DomoticsCore/SystemInfoWebUI.h>)
  #include <DomoticsCore/SystemInfoWebUI.h>
  #define WEBUI_SETUP_HAS_SYSINFO_WEBUI 1
  #else
  #define WEBUI_SETUP_HAS_SYSINFO_WEBUI 0
  #endif

  #if __has_include(<DomoticsCore/RemoteConsoleWebUI.h>)
  #include <DomoticsCore/RemoteConsoleWebUI.h>
  #define WEBUI_SETUP_HAS_CONSOLE_WEBUI 1
  #else
  #define WEBUI_SETUP_HAS_CONSOLE_WEBUI 0
  #endif

  #if __has_include(<DomoticsCore/HomeAssistantWebUI.h>)
  #include <DomoticsCore/HomeAssistantWebUI.h>
  #define WEBUI_SETUP_HAS_HA_WEBUI 1
  #else
  #define WEBUI_SETUP_HAS_HA_WEBUI 0
  #endif

#else
#define WEBUI_SETUP_HAS_WEBUI 0
#endif

// Components
#if __has_include(<DomoticsCore/Wifi.h>)
#include <DomoticsCore/Wifi.h>
#endif

#if __has_include(<DomoticsCore/NTP.h>)
#include <DomoticsCore/NTP.h>
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

#if __has_include(<DomoticsCore/RemoteConsole.h>)
#include <DomoticsCore/RemoteConsole.h>
#endif

#if __has_include(<DomoticsCore/HomeAssistant.h>)
#include <DomoticsCore/HomeAssistant.h>
#endif

#define LOG_WEBUI_SETUP "WEBUI_SETUP"

// WebUI provider registration for all components
// Providers are created at startup and registered with WebUI.

namespace DomoticsCore {
namespace SystemHelpers {

/**
 * @brief Holds all WebUI provider pointers for cleanup
 */
struct WebUIProviders {
#if WEBUI_SETUP_HAS_WEBUI
  #if WEBUI_SETUP_HAS_NETWORK_WEBUI
    Components::WebUI::NetworkWebUI* network = nullptr;
  #endif
  #if WEBUI_SETUP_HAS_WIFI_WEBUI
    Components::WebUI::WifiWebUI* wifi = nullptr;
  #endif
  #if WEBUI_SETUP_HAS_NTP_WEBUI
    Components::WebUI::NTPWebUI* ntp = nullptr;
  #endif
  #if WEBUI_SETUP_HAS_MQTT_WEBUI
    Components::WebUI::MQTTWebUI* mqtt = nullptr;
  #endif
  #if WEBUI_SETUP_HAS_OTA_WEBUI
    Components::WebUI::OTAWebUI* ota = nullptr;
  #endif
  #if WEBUI_SETUP_HAS_SYSINFO_WEBUI
    Components::WebUI::SystemInfoWebUI* sysInfo = nullptr;
  #endif
  #if WEBUI_SETUP_HAS_CONSOLE_WEBUI
    Components::WebUI::RemoteConsoleWebUI* console = nullptr;
  #endif
  #if WEBUI_SETUP_HAS_HA_WEBUI
    Components::WebUI::HomeAssistantWebUI* ha = nullptr;
  #endif
#endif
    
    void cleanup() {
#if WEBUI_SETUP_HAS_WEBUI
  #if WEBUI_SETUP_HAS_NETWORK_WEBUI
        delete network; network = nullptr;
  #endif
  #if WEBUI_SETUP_HAS_WIFI_WEBUI
        delete wifi; wifi = nullptr;
  #endif
  #if WEBUI_SETUP_HAS_NTP_WEBUI
        delete ntp; ntp = nullptr;
  #endif
  #if WEBUI_SETUP_HAS_MQTT_WEBUI
        delete mqtt; mqtt = nullptr;
  #endif
  #if WEBUI_SETUP_HAS_OTA_WEBUI
        delete ota; ota = nullptr;
  #endif
  #if WEBUI_SETUP_HAS_SYSINFO_WEBUI
        delete sysInfo; sysInfo = nullptr;
  #endif
  #if WEBUI_SETUP_HAS_CONSOLE_WEBUI
        delete console; console = nullptr;
  #endif
  #if WEBUI_SETUP_HAS_HA_WEBUI
        delete ha; ha = nullptr;
  #endif
#endif
    }
};

/**
 * @brief Register all WebUI providers and setup persistence callbacks
 */
inline void setupWebUIProviders(
    Core& core,
    SystemConfig& config,
    WebUIProviders& providers,
    Components::NetworkComponent* network,
    Components::WifiComponent* wifi,
    Components::RemoteConsoleComponent* console
) {
#if WEBUI_SETUP_HAS_WEBUI
    auto* webuiComponent = core.getComponent<Components::WebUIComponent>("WebUI");
    if (!webuiComponent) {
        DLOG_E(LOG_WEBUI_SETUP, "WebUI component NOT found!");
        return;
    }
    
    DLOG_I(LOG_WEBUI_SETUP, "Registering WebUI providers... (heap: %u)", HAL::getFreeHeap());
    
    // Minimum heap to keep after each provider for HTTP serving.
    // Browser sends 3+ parallel requests (HTML, CSS, JS, WS upgrade).
    // AsyncWebServer needs ~1-2KB per concurrent connection.
    // Value must exceed post-provider heap to avoid OOM on page load.
    static constexpr uint32_t MIN_HEAP_PER_PROVIDER = 6500;
    
#if WEBUI_SETUP_HAS_STORAGE
    auto* storage = core.getComponent<Components::StorageComponent>("Storage");
#endif

    // Generic Network WebUI provider
#if WEBUI_SETUP_HAS_NETWORK_WEBUI
    if (HAL::getFreeHeap() < MIN_HEAP_PER_PROVIDER) {
        DLOG_W(LOG_WEBUI_SETUP, "Heap low (%u), skipping Network WebUI provider", HAL::getFreeHeap());
    } else if (network) {
        providers.network = new Components::WebUI::NetworkWebUI(network);
        webuiComponent->registerProviderWithComponent(providers.network, network);
        DLOG_I(LOG_WEBUI_SETUP, "Network WebUI provider registered (heap: %u)", HAL::getFreeHeap());
    }
#endif
    
    // WiFi WebUI provider
#if WEBUI_SETUP_HAS_WIFI_WEBUI
    // Define save lambda in wider scope so both WifiComponent and WifiWebUI can use it.
    // ALWAYS set on WifiComponent (needed for STA fallback auto-save to break boot loops).
#if WEBUI_SETUP_HAS_STORAGE
    std::function<void(const Components::WifiConfig&)> saveWifiConfig;
    if (wifi && storage) {
        saveWifiConfig = [storage](const Components::WifiConfig& cfg) {
            DLOG_I(LOG_WEBUI_SETUP, "Saving WiFi config");
            storage->putString("wifi_ssid", cfg.ssid);
            storage->putString("wifi_pass", cfg.password);
            storage->putBool("wifi_autocon", cfg.autoConnect);
            storage->putBool("wifi_ap_en", cfg.enableAP);
            storage->putString("wifi_ap_ssid", cfg.apSSID);
            storage->putString("wifi_ap_pass", cfg.apPassword);
        };
        wifi->setConfigSaveCallback(saveWifiConfig);
    }
#endif
    if (HAL::getFreeHeap() < MIN_HEAP_PER_PROVIDER) {
        DLOG_W(LOG_WEBUI_SETUP, "Heap low (%u), skipping WiFi WebUI provider", HAL::getFreeHeap());
    } else if (wifi) {
        providers.wifi = new Components::WebUI::WifiWebUI(wifi);
        
#if WEBUI_SETUP_HAS_STORAGE
        if (saveWifiConfig) {
            providers.wifi->setConfigSaveCallback(saveWifiConfig);
        }
#endif
        webuiComponent->registerProviderWithComponent(providers.wifi, wifi);
        DLOG_I(LOG_WEBUI_SETUP, "✓ WiFi WebUI provider registered (heap: %u)", HAL::getFreeHeap());
    }
#endif
    
    // NTP WebUI provider
#if WEBUI_SETUP_HAS_NTP_WEBUI
    if (HAL::getFreeHeap() < MIN_HEAP_PER_PROVIDER) {
        DLOG_W(LOG_WEBUI_SETUP, "Heap low (%u), skipping remaining providers", HAL::getFreeHeap());
        return;
    }
    auto* ntpComponent = core.getComponent<Components::NTPComponent>("NTP");
    if (ntpComponent) {
        // Register timezone options endpoint - streams options from flash
        webuiComponent->registerApiRoute("/api/ntp/timezones", HTTP_GET, [](AsyncWebServerRequest* request) {
            AsyncResponseStream *response = request->beginResponseStream("application/json");
            response->print("[");
            for (size_t i = 0; i < Components::WebUI::TIMEZONE_LOOKUP_COUNT; ++i) {
                if (i > 0) response->print(",");
                response->print("{\"value\":\"");
                response->print(Components::WebUI::TIMEZONE_LOOKUP[i].posix);
                response->print("\",\"label\":\"");
                response->print(Components::WebUI::TIMEZONE_LOOKUP[i].friendly);
                response->print("\"}");
            }
            response->print("]");
            request->send(response);
        });

        providers.ntp = new Components::WebUI::NTPWebUI(ntpComponent);

#if WEBUI_SETUP_HAS_STORAGE
        if (storage) {
            providers.ntp->setConfigSaveCallback([storage](const Components::NTPConfig& cfg) {
                DLOG_I(LOG_WEBUI_SETUP, "Saving NTP config");
                storage->putBool("ntp_enabled", cfg.enabled);
                storage->putString("ntp_timezone", cfg.timezone);
                storage->putInt("ntp_interval", (int)cfg.syncInterval);
                String serversStr;
                for (size_t i = 0; i < cfg.servers.size(); i++) {
                    if (i > 0) serversStr += ",";
                    serversStr += cfg.servers[i];
                }
                storage->putString("ntp_servers", serversStr);
            });
        }
#endif
        webuiComponent->registerProviderWithComponent(providers.ntp, ntpComponent);
        DLOG_I(LOG_WEBUI_SETUP, "✓ NTP WebUI provider registered (heap: %u)", HAL::getFreeHeap());
    }
#endif
    
    // MQTT WebUI provider
#if WEBUI_SETUP_HAS_MQTT_WEBUI
    if (HAL::getFreeHeap() < MIN_HEAP_PER_PROVIDER) {
        DLOG_W(LOG_WEBUI_SETUP, "Heap low (%u), skipping remaining providers", HAL::getFreeHeap());
        return;
    }
    auto* mqttComponent = core.getComponent<Components::MQTTComponent>("MQTT");
    if (mqttComponent) {
        providers.mqtt = new Components::WebUI::MQTTWebUI(mqttComponent);
        
#if WEBUI_SETUP_HAS_STORAGE
        if (storage) {
            providers.mqtt->setConfigSaveCallback([storage](const Components::MQTTConfig& cfg) {
                DLOG_I(LOG_WEBUI_SETUP, "Saving MQTT config");
                storage->putString("mqtt_broker", cfg.broker);
                storage->putInt("mqtt_port", cfg.port);
                storage->putString("mqtt_user", cfg.username);
                storage->putString("mqtt_pass", cfg.password);
                storage->putString("mqtt_clientid", cfg.clientId);
                storage->putBool("mqtt_enabled", cfg.enabled);
            });
        }
#endif
        webuiComponent->registerProviderWithComponent(providers.mqtt, mqttComponent);
        DLOG_I(LOG_WEBUI_SETUP, "✓ MQTT WebUI provider registered");
    }
#endif
    
    // OTA WebUI provider
#if WEBUI_SETUP_HAS_OTA_WEBUI
    if (HAL::getFreeHeap() < MIN_HEAP_PER_PROVIDER) {
        DLOG_W(LOG_WEBUI_SETUP, "Heap low (%u), skipping remaining providers", HAL::getFreeHeap());
        return;
    }
    auto* otaComponent = core.getComponent<Components::OTAComponent>("OTA");
    if (otaComponent) {
        providers.ota = new Components::WebUI::OTAWebUI(otaComponent);
        webuiComponent->registerProviderWithComponent(providers.ota, otaComponent);
        providers.ota->init(webuiComponent);
        DLOG_I(LOG_WEBUI_SETUP, "✓ OTA WebUI provider registered");
    }
#endif
    
    // SystemInfo WebUI provider
#if WEBUI_SETUP_HAS_SYSINFO_WEBUI
    if (HAL::getFreeHeap() < MIN_HEAP_PER_PROVIDER) {
        DLOG_W(LOG_WEBUI_SETUP, "Heap low (%u), skipping remaining providers", HAL::getFreeHeap());
        return;
    }
    auto* sysInfoComponent = core.getComponent<Components::SystemInfoComponent>("System Info");
    if (sysInfoComponent) {
        providers.sysInfo = new Components::WebUI::SystemInfoWebUI(sysInfoComponent);
        
#if WEBUI_SETUP_HAS_STORAGE
        if (storage) {
            providers.sysInfo->setDeviceNameCallback([storage, &config, sysInfoComponent](const String& deviceName) {
                DLOG_I(LOG_WEBUI_SETUP, "Saving device name: '%s'", deviceName.c_str());
                storage->putString("device_name", deviceName);
                config.deviceName = deviceName;
                Components::SystemInfoConfig siCfg = sysInfoComponent->getConfig();
                siCfg.deviceName = deviceName;
                sysInfoComponent->setConfig(siCfg);
            });
        }
#endif
        webuiComponent->registerProviderWithComponent(providers.sysInfo, sysInfoComponent);
        DLOG_I(LOG_WEBUI_SETUP, "✓ SystemInfo WebUI provider registered");
    }
#endif
    
    // RemoteConsole WebUI provider
#if WEBUI_SETUP_HAS_CONSOLE_WEBUI
    if (HAL::getFreeHeap() < MIN_HEAP_PER_PROVIDER) {
        DLOG_W(LOG_WEBUI_SETUP, "Heap low (%u), skipping remaining providers", HAL::getFreeHeap());
        return;
    }
    if (console) {
        providers.console = new Components::WebUI::RemoteConsoleWebUI(console);
        webuiComponent->registerProviderWithComponent(providers.console, console);
        providers.console->init(webuiComponent);
        DLOG_I(LOG_WEBUI_SETUP, "✓ RemoteConsole WebUI provider registered (heap: %u)", HAL::getFreeHeap());
    }
#endif
    
    // HomeAssistant WebUI provider
#if WEBUI_SETUP_HAS_HA_WEBUI
    if (HAL::getFreeHeap() < MIN_HEAP_PER_PROVIDER) {
        DLOG_W(LOG_WEBUI_SETUP, "Heap low (%u), skipping remaining providers", HAL::getFreeHeap());
        return;
    }
    auto* haComponent = core.getComponent<Components::HomeAssistant::HomeAssistantComponent>("HomeAssistant");
    if (haComponent) {
        providers.ha = new Components::WebUI::HomeAssistantWebUI(haComponent);
        
#if WEBUI_SETUP_HAS_STORAGE
        if (storage) {
            providers.ha->setConfigSaveCallback([storage](const Components::HomeAssistant::HAConfig& cfg) {
                DLOG_I(LOG_WEBUI_SETUP, "Saving HomeAssistant config");
                storage->putString("ha_nodeid", cfg.nodeId);
                storage->putString("ha_device_name", cfg.deviceName);
                storage->putString("ha_disc_prefix", cfg.discoveryPrefix);
            });
        }
#endif
        webuiComponent->registerProviderWithComponent(providers.ha, haComponent);
        DLOG_I(LOG_WEBUI_SETUP, "✓ HomeAssistant WebUI provider registered");
    }
#endif
    
    // WebUI self-persistence callback
#if WEBUI_SETUP_HAS_STORAGE
    if (storage) {
        webuiComponent->setConfigCallback([storage](const Components::WebUIConfig& cfg) {
            DLOG_I(LOG_WEBUI_SETUP, "Saving WebUI config");
            storage->putString("webui_theme", cfg.theme);
            storage->putString("device_name", cfg.deviceName);
            storage->putString("webui_color", cfg.primaryColor);
            storage->putBool("webui_auth", cfg.enableAuth);
            storage->putString("webui_user", cfg.username);
            if (strlen(cfg.password) > 0) {
                storage->putString("webui_pass", cfg.password);
            }
        });
    }
#endif
    
#endif // WEBUI_SETUP_HAS_WEBUI
}

} // namespace SystemHelpers
} // namespace DomoticsCore

#endif // DOMOTICS_CORE_SYSTEM_WEBUI_SETUP_H
