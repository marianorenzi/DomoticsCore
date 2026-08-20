#pragma once

/**
 * @file MQTT.h
 * @brief Declares the DomoticsCore MQTT component.
 *
 * @example DomoticsCore-MQTT/examples/BasicMQTT/src/main.cpp
 * @example DomoticsCore-MQTT/examples/MQTTWithWebUI/src/main.cpp
 *
 * Uses DomoticsCore-Network readiness events for transport availability.
 */

#include <DomoticsCore/IComponent.h>
#include <DomoticsCore/Platform_HAL.h>  // For chip ID, platform info, and MQTT_MAX_PACKET_SIZE
#include <DomoticsCore/NetworkEvents.h>
#include <DomoticsCore/MQTT_HAL.h>      // Platform-abstracted MQTT client
#include <DomoticsCore/MQTTEvents.h>    // MQTT event constants
#include <DomoticsCore/Logger.h>
#include <DomoticsCore/Timer.h>
#include <DomoticsCore/Events.h>
#include <ArduinoJson.h>
#include <DomoticsCore/ArduinoJsonString.h>  // String converters for native tests
#include <functional>
#include <vector>
#include <map>

namespace DomoticsCore {
namespace Components {

// ============================================================================
// EventBus Event Structures for MQTT Communication
// ============================================================================

// Buffer sizes for MQTT events - fixed-size for safe memcpy in EventBus
// These are copied by value, so must be reasonable for ESP8266 stack/heap
constexpr size_t MQTT_EVENT_TOPIC_SIZE = 128;    ///< Max topic length
constexpr size_t MQTT_EVENT_PAYLOAD_SIZE = 700;  ///< Max payload (fits HA discovery ~600 bytes with headroom)

/**
 * @brief Event for publishing MQTT messages via EventBus
 *
 * Uses fixed-size char buffers for safe copy via EventBus memcpy.
 * Caller must copy strings into the buffers before emitting.
 *
 * Example usage:
 *   MQTTPublishEvent ev{};
 *   strncpy(ev.topic, myTopic.c_str(), sizeof(ev.topic) - 1);
 *   strncpy(ev.payload, jsonPayload.c_str(), sizeof(ev.payload) - 1);
 *   ev.qos = 1;
 *   ev.retain = false;
 *   emit("mqtt/publish", ev);
 */
struct MQTTPublishEvent {
    char topic[MQTT_EVENT_TOPIC_SIZE];      ///< MQTT topic (null-terminated)
    char payload[MQTT_EVENT_PAYLOAD_SIZE];  ///< Message payload (null-terminated)
    uint8_t qos = 0;                        ///< QoS level (0, 1, 2)
    bool retain = false;                    ///< Retain flag
};

/**
 * @brief Event for subscribing to MQTT topics via EventBus
 *
 * Uses fixed-size char buffer for safe copy via EventBus memcpy.
 */
struct MQTTSubscribeEvent {
    char topic[MQTT_EVENT_TOPIC_SIZE];  ///< Topic filter (supports wildcards)
    uint8_t qos = 0;                    ///< QoS level
};

/**
 * @brief Event for incoming MQTT messages via EventBus
 *
 * Uses fixed-size char buffers. Data is copied into these buffers
 * and remains valid during the entire event dispatch cycle.
 */
struct MQTTMessageEvent {
    char topic[MQTT_EVENT_TOPIC_SIZE];      ///< Message topic
    char payload[MQTT_EVENT_PAYLOAD_SIZE];  ///< Message payload
};

/**
 * @brief MQTT client configuration
 */
struct MQTTConfig {
    // Server
    String broker = "";                     ///< MQTT broker address
    uint16_t port = 1883;                   ///< MQTT broker port (1883 for plain, 8883 for TLS)
    bool useTLS = false;                    ///< Use TLS/SSL encryption
    uint16_t keepAlive = 60;                ///< Keep-alive interval in seconds

    // Authentication
    String username = "";                   ///< MQTT username (optional)
    String password = "";                   ///< MQTT password (optional)
    String clientId = "";                   ///< MQTT client ID (auto-generated if empty)

    // Last Will and Testament
    bool enableLWT = true;                  ///< Enable Last Will Testament
    String lwtTopic = "";                   ///< LWT topic (defaults to {clientId}/status)
    String lwtMessage = "offline";          ///< LWT message payload
    uint8_t lwtQoS = 1;                     ///< LWT QoS level (0, 1, or 2)
    bool lwtRetain = true;                  ///< Retain LWT message
    
    // Reconnection
    bool autoReconnect = true;              ///< Automatically reconnect on disconnect
    uint32_t reconnectDelay = 1000;         ///< Initial reconnection delay (ms)
    uint32_t maxReconnectDelay = 30000;     ///< Maximum reconnection delay (ms)
    
    // Publishing
    uint16_t maxQueueSize = 100;            ///< Max queued messages when offline (0 = unlimited). Tumbling window.
    uint8_t publishRateLimit = 10;          ///< Max messages per second (0 = unlimited). Tumbling 1s window.

    // Subscriptions
    uint8_t maxSubscriptions = 50;          ///< Maximum number of subscriptions (0 = unlimited)

    // Component
    bool enabled = true;                    ///< Enable MQTT component
};

/**
 * @brief MQTT connection state
 */
enum class MQTTState {
    Disconnected,   ///< Not connected
    Connecting,     ///< Connection in progress
    Connected,      ///< Connected to broker
    Error          ///< Error state
};

/**
 * @brief MQTT statistics
 */
struct MQTTStatistics {
    uint32_t connectCount = 0;          ///< Total connection attempts
    uint32_t reconnectCount = 0;        ///< Total reconnection attempts
    uint32_t publishCount = 0;          ///< Total messages published
    uint32_t publishErrors = 0;         ///< Failed publish attempts
    uint32_t receiveCount = 0;          ///< Total messages received
    uint32_t subscriptionCount = 0;     ///< Active subscriptions
    uint32_t uptime = 0;                ///< Seconds connected
    uint32_t lastLatency = 0;           ///< Last measured latency (ms)
};

/**
 * @brief MQTT Client Component
 * 
 * Provides MQTT client functionality with auto-reconnection, QoS support,
 * topic management, and message callbacks. Uses PubSubClient library for
 * protocol implementation.
 * 
 * Features:
 * - Auto-connect and auto-reconnect with exponential backoff
 * - QoS levels 0, 1, 2
 * - Wildcard subscriptions (+ and #)
 * - Message queuing for offline buffering
 * - JSON helper methods
 * - Last Will Testament support
 * - TLS/SSL support
 * - Connection statistics
 * 
 * @example BasicMQTT
 * ```cpp
 * MQTTConfig cfg;
 * cfg.broker = "mqtt.example.com";
 * cfg.username = "user";
 * cfg.password = "pass";
 *
 * auto mqtt = std::make_unique<MQTTComponent>(cfg);
 * auto* mqttPtr = mqtt.get();
 * core.addComponent(std::move(mqtt));
 *
 * mqttPtr->subscribe("home/sensors/#", 1);
 * mqttPtr->on<MQTTMessageEvent>("mqtt/message", [](const MQTTMessageEvent& ev) {
 *     DLOG_I("APP", "Received on %s: %s", ev.topic, ev.payload);
 * });
 * ```
 */
class MQTTComponent : public IComponent {
public:
    // Callbacks removed - use EventBus for inter-component communication
    // Events: "mqtt/connected", "mqtt/disconnected", "mqtt/message", "mqtt/publish", "mqtt/subscribe"

    /**
     * @brief Construct MQTT component with configuration
     * @param config MQTT configuration
     */
    explicit MQTTComponent(const MQTTConfig& config = MQTTConfig());
    
    /**
     * @brief Destructor
     */
    virtual ~MQTTComponent();

    // ========== IComponent Interface ==========
    
    std::vector<Dependency> getDependencies() const override { return {}; }
    
    ComponentStatus begin() override;
    void loop() override;
    ComponentStatus shutdown() override;

    // ========== Connection Management ==========
    
    /**
     * @brief Connect to MQTT broker
     * @return true if connection initiated successfully
     */
    bool connect();
    
    /**
     * @brief Disconnect from MQTT broker
     */
    void disconnect();
    
    /**
     * @brief Reset reconnection and enable auto-retry
     * Call this after updating broker configuration via WebUI
     */
    void resetReconnect();
    
    /**
     * Check if connected to broker
     * @return true if connected
     */
    bool isConnected() const {
        return mqttClient && mqttClient->connected() && state == MQTTState::Connected;
    }

    /** @brief Debug: get loop() call count from underlying client */
    uint32_t debugLoopCount() const {
        return mqttClient ? mqttClient->getLoopCallCount() : 0;
    }
    
    /**
     * @brief Get current connection state
     * @return Current state
     */
    MQTTState getState() const { return state; }
    
    /**
     * @brief Get state as string
     * @return State name
     */
    String getStateString() const;

    // ========== Publishing ==========
    
    /**
     * @brief Publish message to topic
     * @param topic MQTT topic
     * @param payload Message payload
     * @param qos Quality of Service level (0, 1, 2)
     * @param retain Retain message flag
     * @return true if published successfully
     */
    bool publish(const String& topic, const String& payload, uint8_t qos = 0, bool retain = false);
    
    /**
     * @brief Publish JSON document to topic
     * @param topic MQTT topic
     * @param doc JSON document
     * @param qos Quality of Service level
     * @param retain Retain message flag
     * @return true if published successfully
     */
    bool publishJSON(const String& topic, const JsonDocument& doc, uint8_t qos = 0, bool retain = false);
    
    /**
     * @brief Publish binary data to topic
     * @param topic MQTT topic
     * @param data Binary data pointer
     * @param length Data length
     * @param qos Quality of Service level
     * @param retain Retain message flag
     * @return true if published successfully
     */
    bool publishBinary(const String& topic, const uint8_t* data, size_t length, uint8_t qos = 0, bool retain = false);

    // ========== Subscribing ==========
    
    /**
     * @brief Subscribe to topic
     * @param topic MQTT topic (supports wildcards + and #)
     * @param qos Quality of Service level
     * @return true if subscribed successfully
     */
    bool subscribe(const String& topic, uint8_t qos = 0);
    
    /**
     * @brief Unsubscribe from topic
     * @param topic MQTT topic
     * @return true if unsubscribed successfully
     */
    bool unsubscribe(const String& topic);
    
    /**
     * @brief Unsubscribe from all topics
     */
    void unsubscribeAll();
    
    /**
     * @brief Get list of active subscriptions
     * @return Vector of subscribed topics
     */
    std::vector<String> getActiveSubscriptions() const;

    // ========== EventBus Communication ==========
    // This component uses EventBus for decoupled communication:
    // - Emits: "mqtt/connected", "mqtt/disconnected", "mqtt/message"
    // - Listens: "mqtt/publish", "mqtt/subscribe"

    // ========== Configuration ==========
    
    /**
     * @brief Set configuration
     * @param cfg New configuration
     */
    void setConfig(const MQTTConfig& cfg);
    
    /**
     * Get current MQTT configuration
     * @return MQTT configuration reference
     */
    const MQTTConfig& getConfig() const { return config; }
    
    /**
     * @brief Set broker address and port
     * @param broker Broker hostname or IP
     * @param port Broker port
     */
    void setBroker(const String& broker, uint16_t port = 1883);
    
    /**
     * @brief Set authentication credentials
     * @param username Username
     * @param password Password
     */
    void setCredentials(const String& username, const String& password);

    // ========== Statistics & Diagnostics ==========
    
    /**
     * @brief Get connection statistics
     * @return Statistics structure
     */
    const MQTTStatistics& getStatistics() const { return stats; }
    
    /**
     * @brief Get number of queued messages
     * @return Queue size
     */
    size_t getQueuedMessageCount() const { return messageQueue.size(); }
    
    /**
     * @brief Get last error message
     * @return Error string
     */
    String getLastError() const { return lastError; }

    // ========== Helper Functions ==========
    
    /**
     * @brief Check if topic matches filter (with wildcards)
     * @param filter Topic filter (can contain + and #)
     * @param topic Actual topic
     * @return true if matches
     */
    static bool topicMatches(const String& filter, const String& topic);

private:
    // Configuration
    MQTTConfig config;

    // MQTT client (using HAL for platform independence)
    HAL::MQTT::MQTTClientImpl* mqttClient;

    // State management
    MQTTState state;
    bool networkAvailable = false;
    bool networkReadinessManaged = false;
    String lastError;
    Utils::NonBlockingDelay reconnectTimer;
    unsigned long stateChangeTime;
    
    // Statistics
    MQTTStatistics stats;
    
    // Subscriptions
    struct Subscription {
        String topic;
        uint8_t qos;
    };
    std::vector<Subscription> subscriptions;
    
    // EventBus subscriptions (managed in begin/shutdown)
    
    // Message queue (for offline buffering)
    struct QueuedMessage {
        String topic;
        String payload;
        uint8_t qos;
        bool retain;
    };
    std::vector<QueuedMessage> messageQueue;
    
    // Rate limiting
    unsigned long lastPublishTime;
    uint8_t publishCountThisSecond;
    
    // Internal methods
    bool connectInternal();
    void handleReconnection();
    void processMessageQueue();
    void handleIncomingMessage(char* topic, byte* payload, unsigned int length);
    void updateStatistics();
    String generateClientId();
    
    // Accepted deviation from Constitution XIII (no singleton abuse):
    // PubSubClient's C-style callback (void(*)(char*, byte*, unsigned int)) does not support
    // user-data pointers. Static instance is required for callback routing.
    // Consider std::function wrapper if PubSubClient is ever replaced.
    static void mqttCallback(char* topic, byte* payload, unsigned int length);
    static MQTTComponent* instance;  // For static callback
};

} // namespace Components
} // namespace DomoticsCore

// Include inline implementations
#include "MQTT_impl.h"
