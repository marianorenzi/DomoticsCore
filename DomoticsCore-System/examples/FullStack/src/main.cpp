/**
 * @file main.cpp
 * @brief DomoticsCore - Full Stack Example
 * 
 * This example demonstrates the FULL STACK configuration:
 * - WiFi (with automatic AP mode fallback)
 * - LED (automatic status visualization)
 * - RemoteConsole (telnet debugging)
 * - WebUI (web interface on port 8080)
 * - NTP (time synchronization)
 * - Storage (persistent configuration)
 * - MQTT (message broker integration)
 * - Home Assistant (auto-discovery)
 * - OTA (over-the-air updates)
 * 
 * Perfect for:
 * - Complete IoT solutions
 * - Home automation
 * - Production deployments with MQTT
 * - Enterprise applications
 * 
 * Requires:
 * - MQTT broker (e.g., Mosquitto)
 * - OTA password (for security)
 */

#include <DomoticsCore/System.h>
#include <DomoticsCore/HAEvents.h>

using namespace DomoticsCore;
using namespace DomoticsCore::Components;

#define LOG_APP "APP"

// ============================================================================
// CONFIGURATION
// ============================================================================

// WiFi credentials
const char* WIFI_SSID = "";  // Leave empty for AP mode
const char* WIFI_PASSWORD = "";

// MQTT broker (required for FullStack)
const char* MQTT_BROKER = ""; // Leave empty to disable MQTT
const uint16_t MQTT_PORT = 1883;
const char* MQTT_USER = "";  // Optional
const char* MQTT_PASSWORD = "";  // Optional

// OTA password (required for security)
const char* OTA_PASSWORD = "admin123";  // CHANGE THIS!

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================

// Component references (obtained after system initialization)
HomeAssistant::HomeAssistantComponent* haPtr = nullptr;
MQTTComponent* mqttPtr = nullptr;

// State tracking
bool initialStatePublished = false;

// ============================================================================
// YOUR APPLICATION CODE
// ============================================================================

// Example: Temperature sensor (simulated)
float readTemperature() {
    // TODO: Replace with real sensor (DHT22, DS18B20, etc.)
    return 22.5 + (random(0, 100) / 100.0);
}

// Example: Relay control
void setRelay(bool state) {
    // TODO: Replace with real relay control
    digitalWrite(5, state ? HIGH : LOW);
    DLOG_I(LOG_APP, "Relay: %s", state ? "ON" : "OFF");
}

// ============================================================================
// SYSTEM SETUP
// ============================================================================

System* domotics = nullptr;

void setup() {
    HAL::Platform::initializeLogging(115200);
    HAL::Platform::delayMs(1000);
    
    // ========================================================================
    // FULL STACK Configuration - Everything enabled!
    // ========================================================================
    SystemConfig config = SystemConfig::fullStack();
    config.deviceName = "FullStackDevice";
    config.firmwareVersion = "1.0.0";
    config.wifiSSID = WIFI_SSID;
    config.wifiPassword = WIFI_PASSWORD;
    
    // MQTT configuration
    config.mqttBroker = MQTT_BROKER;
    config.mqttPort = MQTT_PORT;
    config.mqttUser = MQTT_USER;
    config.mqttPassword = MQTT_PASSWORD;
    config.mqttClientId = config.deviceName;  // Use device name as client ID
    
    // OTA configuration
    config.otaPassword = OTA_PASSWORD;
    
    // Note: FullStack includes EVERYTHING:
    // - WiFi, LED, Console (Minimal)
    // - WebUI, NTP, Storage (Standard)
    // - MQTT, Home Assistant, OTA (FullStack), SystemInfo
    //
    // Requires MQTT broker and OTA password!
    
    // Create system
    domotics = new System(config);
    
    // Initialize system (AUTOMATIC: WiFi, LED, Console, State management)
    if (!domotics->begin()) {
        DLOG_E(LOG_APP, "System initialization failed!");
        // System in ERROR state - components still run (LED, Console, etc.)
        while (1) {
            domotics->loop();
            yield();  // Allow RTOS tasks to run
        }
    }
    
    // Register custom console commands
    domotics->registerCommand("temp", [](const String& args) {
        float temp = readTemperature();
        return String("Temperature: ") + String(temp, 1) + "°C\n";
    });
    
    domotics->registerCommand("relay", [](const String& args) {
        if (args == "on") {
            setRelay(true);
            return String("Relay turned ON\n");
        } else if (args == "off") {
            setRelay(false);
            return String("Relay turned OFF\n");
        } else {
            return String("Usage: relay on|off\n");
        }
    });
    
    // YOUR CUSTOM INITIALIZATION
    pinMode(5, OUTPUT);  // Relay pin
    digitalWrite(5, LOW);  // Start with relay OFF
    
    // ========================================================================
    // HOME ASSISTANT INTEGRATION
    // ========================================================================
    
    // Get component references
    mqttPtr = domotics->getCore().getComponent<MQTTComponent>("MQTT");
    haPtr = domotics->getCore().getComponent<HomeAssistant::HomeAssistantComponent>("HomeAssistant");
    
    if (haPtr && mqttPtr) {
        DLOG_I(LOG_APP, "Setting up Home Assistant entities...");
        
        // ====================================================================
        // ADD SENSORS
        // ====================================================================
        
        // Temperature sensor (simulated)
        haPtr->addSensor("temperature", "Temperature", "°C", "temperature", "mdi:thermometer");
        
        // System uptime sensor
        haPtr->addSensor("uptime", "Uptime", "s", "", "mdi:clock-outline");
        
        // Free heap sensor (system health)
        haPtr->addSensor("free_heap", "Free Heap", "bytes", "", "mdi:memory");
        
        // WiFi signal strength sensor
        auto* wifiComp = domotics->getWiFi();
        if (wifiComp) {
            haPtr->addSensor("wifi_signal", "WiFi Signal", "dBm", "signal_strength", "mdi:wifi");
        }
        
        // ====================================================================
        // ADD SWITCH (Relay control)
        // ====================================================================
        
        haPtr->addSwitch("relay", "Cooling Relay", "mdi:fan");

        // ====================================================================
        // ADD BUTTON (Restart device)
        // ====================================================================

        haPtr->addButton("restart", "Restart Device", "mdi:restart");
        
        DLOG_I(LOG_APP, "✓ Home Assistant entities created (%d entities)",
               haPtr->getStatistics().entityCount);

        // ====================================================================
        // EventBus Command Handlers (R26)
        // ====================================================================

        // Switch command handler
        domotics->getCore().getEventBus().subscribe(String(DomoticsCore::HAEvents::EVENT_COMMAND), [](const void* data) {
            auto& ev = *reinterpret_cast<const HAEvents::HACommandEvent*>(data);
            if (strcmp(ev.component, "switch") != 0) return;
            bool state = (strcmp(ev.command, "ON") == 0);
            setRelay(state);
            DLOG_I(LOG_APP, "Relay command from HA: %s", state ? "ON" : "OFF");
        }, nullptr);

        // Button command handler
        domotics->getCore().getEventBus().subscribe(String(DomoticsCore::HAEvents::EVENT_COMMAND), [](const void* data) {
            auto& ev = *reinterpret_cast<const HAEvents::HACommandEvent*>(data);
            if (strcmp(ev.component, "button") != 0) return;
            DLOG_I(LOG_APP, "Restart button pressed from Home Assistant");
            HAL::Platform::delayMs(1000);
            HAL::Platform::restart();
        }, nullptr);

        // Discovery is automatically published by the HomeAssistant component
        // when MQTT connects (via EventBus "mqtt/connected" event)
        DLOG_I(LOG_APP, "✓ Home Assistant integration ready (waiting for MQTT connection)");
    } else {
        if (!haPtr) {
            DLOG_W(LOG_APP, "⚠️  Home Assistant component not available");
            DLOG_I(LOG_APP, "   Make sure MQTT broker is configured");
        }
        if (!mqttPtr) {
            DLOG_W(LOG_APP, "⚠️  MQTT component not available");
        }
    }
    
    DLOG_I(LOG_APP, "Application ready!");
}

// ============================================================================
// MAIN LOOP
// ============================================================================

// Non-blocking timers
DomoticsCore::Utils::NonBlockingDelay sensorTimer(10000);  // 10 seconds - sensor readings
DomoticsCore::Utils::NonBlockingDelay mqttPublishTimer(5000);  // 5 seconds - MQTT publish
DomoticsCore::Utils::NonBlockingDelay heartbeatTimer(30000);  // 30 seconds - system status

void loop() {
    // System loop (handles everything automatically)
    domotics->loop();
    
    // ========================================================================
    // PUBLISH INITIAL STATE (once HA is ready)
    // ========================================================================
    if (!initialStatePublished && haPtr && haPtr->isReady()) {
        bool currentRelayState = digitalRead(5) == HIGH;
        haPtr->publishState("relay", currentRelayState);
        initialStatePublished = true;
        DLOG_I(LOG_APP, "✓ Published initial relay state: %s", currentRelayState ? "ON" : "OFF");
    }
    
    // ========================================================================
    // SENSOR READING & AUTOMATIC CONTROL
    // ========================================================================
    if (sensorTimer.isReady()) {
        float temp = readTemperature();
        DLOG_I(LOG_APP, "Temperature: %.1f°C", temp);
        
        // Example: Automatic temperature control
        // When local control changes the relay, publish state to HA
        bool currentRelayState = digitalRead(5) == HIGH;
        bool shouldBeOn = (temp > 25.0);
        bool shouldBeOff = (temp < 20.0);
        
        if (shouldBeOn && !currentRelayState) {
            setRelay(true);
            if (haPtr && haPtr->isMQTTConnected()) {
                haPtr->publishState("relay", true);
                DLOG_I(LOG_APP, "🌡️ Auto control: Relay ON (temp=%.1f°C)", temp);
            }
        } else if (shouldBeOff && currentRelayState) {
            setRelay(false);
            if (haPtr && haPtr->isMQTTConnected()) {
                haPtr->publishState("relay", false);
                DLOG_I(LOG_APP, "🌡️ Auto control: Relay OFF (temp=%.1f°C)", temp);
            }
        }
    }
    
    // ========================================================================
    // MQTT STATE PUBLISHING (to Home Assistant)
    // ========================================================================
    if (mqttPublishTimer.isReady() && haPtr && haPtr->isMQTTConnected()) {
        // Read current values
        float temp = readTemperature();
        uint32_t uptime = HAL::Platform::getMillis() / 1000;
        uint32_t freeHeap = HAL::Platform::getFreeHeap();
        
        // Publish sensor states
        haPtr->publishState("temperature", temp);
        haPtr->publishState("uptime", (float)uptime);
        haPtr->publishState("free_heap", (float)freeHeap);
        
        // Publish WiFi signal if available
        auto* wifiComp = domotics->getWiFi();
        if (wifiComp && wifiComp->isSTAConnected()) {
            // Get RSSI from WifiComponent (no need for WiFi.h)
            int32_t rssi = wifiComp->getRSSI();
            haPtr->publishState("wifi_signal", (float)rssi);
        }
        
        DLOG_D(LOG_APP, "📡 Published to HA: Temp=%.1f°C, Uptime=%ds, Heap=%d",
               temp, uptime, freeHeap);
    }
    
    // Note: When relay changes via HA command, state is auto-published by HA component
    // This section is only for automatic temperature control changes (above)
    // If you need to publish state after local/automatic control, do it explicitly
    
    // ========================================================================
    // SYSTEM HEARTBEAT (periodic status)
    // ========================================================================
    if (heartbeatTimer.isReady()) {
        DLOG_I(LOG_APP, "💚 System alive - Uptime: %lus, MQTT: %s, HA entities: %d",
               (unsigned long)(HAL::Platform::getMillis() / 1000),
               (mqttPtr && mqttPtr->isConnected()) ? "connected" : "disconnected",
               haPtr ? haPtr->getStatistics().entityCount : 0);
    }
}
