// MQTT Component Inline Implementations
// This file is included at the end of MQTT.h

namespace DomoticsCore {
namespace Components {

// Static members
inline MQTTComponent* MQTTComponent::instance = nullptr;

// Constructor
inline MQTTComponent::MQTTComponent(const MQTTConfig& cfg)
    : config(cfg)
    , mqttClient(new HAL::MQTT::MQTTClientImpl(config.useTLS))
    , state(MQTTState::Disconnected)
    , reconnectTimer(cfg.reconnectDelay)
    , stateChangeTime(0)
    , lastPublishTime(0)
    , publishCountThisSecond(0)
{
    instance = this;

    // Generate client ID if not provided
    if (config.clientId.isEmpty()) {
        config.clientId = generateClientId();
    }

    // Set default LWT topic if not provided
    if (config.enableLWT && config.lwtTopic.isEmpty()) {
        config.lwtTopic = config.clientId + "/status";
    }
    if (config.lwtQoS > 2) {
        DLOG_W(LOG_MQTT, "Invalid lwtQoS %u, clamping to 2", config.lwtQoS);
        config.lwtQoS = 2;
    }

    // Initialize metadata
    metadata.name = "MQTT";
    metadata.version = "1.4.2";
    metadata.author = "DomoticsCore";
    metadata.description = "MQTT client with auto-reconnection";
}

// Destructor
inline MQTTComponent::~MQTTComponent() {
    if (isConnected()) {
        disconnect();
    }
    delete mqttClient;
    mqttClient = nullptr;
    instance = nullptr;
}

// Lifecycle methods
inline ComponentStatus MQTTComponent::begin() {
    DLOG_I(LOG_MQTT, "Initializing");
    
    // CRITICAL: Register EventBus listeners FIRST, even if no config yet
    // This ensures listeners are ready when broker gets configured dynamically
    on<MQTTPublishEvent>(MQTTEvents::EVENT_PUBLISH, [this](const MQTTPublishEvent& ev) {
        publish(ev.topic, ev.payload, ev.qos, ev.retain);
    });

    on<MQTTSubscribeEvent>(MQTTEvents::EVENT_SUBSCRIBE, [this](const MQTTSubscribeEvent& ev) {
        subscribe(ev.topic, ev.qos);
    });

    if (__dc_eventBus) {
        networkReadinessManaged = true;
        on<NetworkEvents::NetworkAvailabilityEvent>(NetworkEvents::EVENT_READY,
            [this](const NetworkEvents::NetworkAvailabilityEvent& event) {
                networkAvailable = event.available;
                if (networkAvailable) reconnectTimer.reset();
            }, true);
    }
    
    DLOG_D(LOG_MQTT, "EventBus listeners registered (mqtt/publish, mqtt/subscribe)");
    
    // CRITICAL: Set PubSubClient callback BEFORE config check
    // This ensures callback is ready when broker gets configured dynamically
    mqttClient->setCallback(mqttCallback);
    DLOG_D(LOG_MQTT, "PubSubClient callback registered");
    
    // Config is loaded by SystemPersistence via setConfig()
    
    // Early return if no broker configured - MQTT stays inactive until configured
    if (config.broker.isEmpty()) {
        DLOG_I(LOG_MQTT, "No broker configured - MQTT inactive until configured");
        return ComponentStatus::Success;  // Success but inactive
    }
    
    if (!config.enabled) {
        DLOG_I(LOG_MQTT, "Component disabled in configuration");
        return ComponentStatus::Success;  // Success but inactive
    }
    
    mqttClient->setServer(config.broker.c_str(), config.port);
    mqttClient->setKeepAlive(config.keepAlive);
    // Buffer size is now set at connection
    
    // Standalone usage has no EventBus readiness source. Managed components wait
    // for network/ready and reconnect from loop().
    if (config.autoReconnect && networkAvailable) {
        connect();
    }
    
    DLOG_I(LOG_MQTT, "Initialized with broker %s:%d, client ID: %s", 
           config.broker.c_str(), config.port, config.clientId.c_str());
    DLOG_I(LOG_MQTT, "MQTT buffer size: %d bytes", MQTT_MAX_PACKET_SIZE);
    
    return ComponentStatus::Success;
}

inline void MQTTComponent::loop() {
    if (config.broker.isEmpty()) return;

    if (isConnected()) {
        // Always process an active connection even if config.enabled was
        // cleared after connection (e.g. by a config reload from flash).
        mqttClient->loop();
        updateStatistics();
        processMessageQueue();
    } else if (config.enabled && config.autoReconnect && networkAvailable) {
        // Only attempt reconnection when explicitly enabled
        handleReconnection();
    }
}

inline ComponentStatus MQTTComponent::shutdown() {
    DLOG_I(LOG_MQTT, "Shutting down");
    
    if (isConnected()) {
        disconnect();
    }
    
    return ComponentStatus::Success;
}

// Connection management
inline bool MQTTComponent::connect() {
    if (isConnected()) {
        DLOG_W(LOG_MQTT, "Already connected");
        return true;
    }
    
    if (networkReadinessManaged && !networkAvailable) {
        lastError = "Network unavailable";
        DLOG_D(LOG_MQTT, "Cannot connect to MQTT - network unavailable");
        return false;
    }
    
    if (config.broker.isEmpty()) {
        lastError = "No broker configured";
        return false;
    }
    
    state = MQTTState::Connecting;
    stateChangeTime = HAL::Platform::getMillis();
    
    bool success = connectInternal();
    
    if (success) {
        state = MQTTState::Connected;
        stateChangeTime = HAL::Platform::getMillis();
        reconnectTimer.setInterval(config.reconnectDelay);  // Reset to initial delay
        stats.connectCount++;
        stats.reconnectCount = 0;  // Reset failure counter on successful connection
        
        DLOG_I(LOG_MQTT, "Connected to %s:%d", config.broker.c_str(), config.port);
        
        // Resubscribe to all topics
        for (const auto& sub : subscriptions) {
            mqttClient->subscribe(sub.topic.c_str(), sub.qos);
        }
        
        // Emit event for decoupled components
        emit(MQTTEvents::EVENT_CONNECTED, true);
    } else {
        state = MQTTState::Error;
        stateChangeTime = HAL::Platform::getMillis();
        lastError = "Connection failed";
        DLOG_E(LOG_MQTT, "Connection failed");
    }
    
    return success;
}

inline void MQTTComponent::disconnect() {
    if (!isConnected()) return;

    mqttClient->disconnect();
    state = MQTTState::Disconnected;
    stateChangeTime = HAL::Platform::getMillis();

    DLOG_I(LOG_MQTT, "Disconnected from broker");

    // Emit event for decoupled components
    emit(MQTTEvents::EVENT_DISCONNECTED, true);
}

inline void MQTTComponent::resetReconnect() {
    stats.reconnectCount = 0;
    reconnectTimer.setInterval(config.reconnectDelay);
    reconnectTimer.enable();
    reconnectTimer.reset();
    state = MQTTState::Disconnected;
    lastError = "";
    DLOG_I(LOG_MQTT, "Reconnection reset - auto-retry re-enabled");
}

inline String MQTTComponent::getStateString() const {
    switch (state) {
        case MQTTState::Disconnected: return "Disconnected";
        case MQTTState::Connecting: return "Connecting";
        case MQTTState::Connected: return "Connected";
        case MQTTState::Error: return "Error";
        default: return "Unknown";
    }
}

// Publishing
inline bool MQTTComponent::publish(const String& topic, const String& payload, uint8_t qos, bool retain) {
    if (qos > 2) {
        DLOG_W(LOG_MQTT, "Invalid QoS %u for publish, clamping to 2", qos);
        qos = 2;
    }
    // Rate limit guard (tumbling window: reset counter every 1000ms)
    if (config.publishRateLimit > 0) {
        unsigned long now = HAL::getMillis();
        if (now - lastPublishTime >= 1000) {
            publishCountThisSecond = 0;
            lastPublishTime = now;
        }
        if (publishCountThisSecond >= config.publishRateLimit) {
            DLOG_W(LOG_MQTT, "Publish rate limit reached (%u/s), dropping message for '%s'",
                   config.publishRateLimit, topic.c_str());
            stats.publishErrors++;
            return false;
        }
    }

    if (!isConnected()) {
        // Queue size guard (0 = unlimited)
        if (config.maxQueueSize > 0 && messageQueue.size() >= config.maxQueueSize) {
            DLOG_W(LOG_MQTT, "Message queue full (%u/%u), dropping message for '%s'",
                   (unsigned)messageQueue.size(), config.maxQueueSize, topic.c_str());
            stats.publishErrors++;
            return false;
        }
        // Queue message for later if offline
        messageQueue.push_back({topic, payload, qos, retain});
        publishCountThisSecond++;
        return true;
    }
    
    DLOG_D(LOG_MQTT, "Publishing to topic '%s' (QoS %d, retain %s), size: %d bytes", 
           topic.c_str(), qos, retain ? "true" : "false", payload.length());
    
    bool success = mqttClient->publish(topic.c_str(), (const uint8_t*)payload.c_str(), payload.length(), retain);

    if (success) {
        stats.publishCount++;
        publishCountThisSecond++;
        DLOG_D(LOG_MQTT, "  ✓ Published successfully");
    } else {
        stats.publishErrors++;
        DLOG_E(LOG_MQTT, "  ✗ Publish failed! Client state: %d, buffer size: %d",
               mqttClient->state(), mqttClient->getBufferSize());
    }
    
    return success;
}

inline bool MQTTComponent::publishJSON(const String& topic, const JsonDocument& doc, uint8_t qos, bool retain) {
    String payload;
    serializeJson(doc, payload);
    return publish(topic, payload, qos, retain);
}

inline bool MQTTComponent::publishBinary(const String& topic, const uint8_t* data, size_t length, uint8_t qos, bool retain) {
    if (!isConnected()) return false;

    bool success = mqttClient->publish(topic.c_str(), data, length, retain);

    if (success) {
        stats.publishCount++;
    } else {
        stats.publishErrors++;
    }

    return success;
}

// Subscribing
inline bool MQTTComponent::subscribe(const String& topic, uint8_t qos) {
    if (qos > 2) {
        DLOG_W(LOG_MQTT, "Invalid QoS %u for subscribe, clamping to 2", qos);
        qos = 2;
    }
    // Check if already subscribed
    for (const auto& sub : subscriptions) {
        if (sub.topic == topic) {
            return true;
        }
    }
    
    // Max subscriptions guard (0 = unlimited)
    if (config.maxSubscriptions > 0 && subscriptions.size() >= config.maxSubscriptions) {
        DLOG_W(LOG_MQTT, "Max subscriptions reached (%u/%u), rejecting '%s'",
               (unsigned)subscriptions.size(), config.maxSubscriptions, topic.c_str());
        return false;
    }

    if (!isConnected()) {
        subscriptions.push_back({topic, qos});
        stats.subscriptionCount = subscriptions.size();
        return true;
    }

    bool success = mqttClient->subscribe(topic.c_str(), qos);

    if (success) {
        subscriptions.push_back({topic, qos});
        stats.subscriptionCount = subscriptions.size();
        DLOG_I(LOG_MQTT, "Subscribed to: %s (QoS %d)", topic.c_str(), qos);
    }

    return success;
}

inline bool MQTTComponent::unsubscribe(const String& topic) {
    bool success = mqttClient->unsubscribe(topic.c_str());

    if (success) {
        auto it = subscriptions.begin();
        while (it != subscriptions.end()) {
            if (it->topic == topic) {
                it = subscriptions.erase(it);
                break;
            } else {
                ++it;
            }
        }
        stats.subscriptionCount = subscriptions.size();
        subscriptions.shrink_to_fit();
        DLOG_I(LOG_MQTT, "Unsubscribed from: %s", topic.c_str());
    }

    return success;
}

inline void MQTTComponent::unsubscribeAll() {
    for (const auto& sub : subscriptions) {
        mqttClient->unsubscribe(sub.topic.c_str());
    }
    subscriptions.clear();
    subscriptions.shrink_to_fit();
    stats.subscriptionCount = 0;
}

inline std::vector<String> MQTTComponent::getActiveSubscriptions() const {
    std::vector<String> result;
    for (const auto& sub : subscriptions) {
        result.push_back(sub.topic);
    }
    return result;
}

// Callbacks removed - use EventBus for inter-component communication

// Configuration
inline void MQTTComponent::setConfig(const MQTTConfig& cfg) {
    // Preserve enabled flag if we already have an active connection.
    // A config reload from flash must not silently disable message processing.
    bool preserveEnabled = (state == MQTTState::Connected && !cfg.enabled);
    config = cfg;
    if (preserveEnabled) {
        DLOG_W(LOG_MQTT, "setConfig: preserving enabled=true for active connection");
        config.enabled = true;
    }
    // Ensure PubSubClient stores a valid pointer to the current broker string.
    // PubSubClient retains the const char* pointer passed to setServer without copying,
    // so if our String storage changes, the old pointer becomes invalid.
    if (!config.broker.isEmpty()) {
        mqttClient->setServer(config.broker.c_str(), config.port);
    }
    // Config persistence handled externally (SystemPersistence)
}

inline void MQTTComponent::setBroker(const String& broker, uint16_t port) {
    config.broker = broker;
    config.port = port;
    mqttClient->setServer(broker.c_str(), port);
}

inline void MQTTComponent::setCredentials(const String& username, const String& password) {
    config.username = username;
    config.password = password;
}

// Note: getName() already defined inline in MQTT.h

// Private methods
inline bool MQTTComponent::connectInternal() {
    bool success = false;
    // Defensive: refresh server pointer before attempting connection in case config changed recently
    if (!config.broker.isEmpty()) {
        mqttClient->setServer(config.broker.c_str(), config.port);
    }

    // Ensure buffer size is preserved across reconnections
    mqttClient->setBufferSize(MQTT_MAX_PACKET_SIZE);
    DLOG_D(LOG_MQTT, "MQTT buffer size set to %d bytes", MQTT_MAX_PACKET_SIZE);
    
    // Yield before blocking connection to prevent watchdog issues
    HAL::Platform::yield();

    if (config.enableLWT) {
        if (!config.username.isEmpty()) {
            success = mqttClient->connect(
                config.clientId.c_str(),
                config.username.c_str(),
                config.password.c_str(),
                config.lwtTopic.c_str(),
                config.lwtQoS,
                config.lwtRetain,
                config.lwtMessage.c_str()
            );
        } else {
            success = mqttClient->connect(
                config.clientId.c_str(),
                nullptr,
                nullptr,
                config.lwtTopic.c_str(),
                config.lwtQoS,
                config.lwtRetain,
                config.lwtMessage.c_str()
            );
        }
    } else {
        if (!config.username.isEmpty()) {
            success = mqttClient->connect(
                config.clientId.c_str(),
                config.username.c_str(),
                config.password.c_str()
            );
        } else {
            success = mqttClient->connect(config.clientId.c_str());
        }
    }

    // Yield after blocking connection to allow watchdog reset
    HAL::Platform::yield();

    return success;
}

inline void MQTTComponent::handleReconnection() {
    if (state == MQTTState::Connecting) return;
    
    if (!reconnectTimer.isReady()) return;
    
    // Exponential backoff for reconnection delay
    unsigned long currentDelay = reconnectTimer.getInterval();
    if (currentDelay < config.maxReconnectDelay) {
        unsigned long newDelay = currentDelay * 2;
        if (newDelay > config.maxReconnectDelay) {
            newDelay = config.maxReconnectDelay;
        }
        reconnectTimer.setInterval(newDelay);
    }
    
    DLOG_I(LOG_MQTT, "Attempting reconnection (delay: %lu ms)", reconnectTimer.getInterval());
    stats.reconnectCount++;
    
    // Reset timer and attempt connection
    reconnectTimer.reset();
    connect();
}

inline void MQTTComponent::processMessageQueue() {
    if (messageQueue.empty()) return;

    bool erased = false;
    auto it = messageQueue.begin();
    while (it != messageQueue.end() && isConnected()) {
        if (publish(it->topic, it->payload, it->qos, it->retain)) {
            it = messageQueue.erase(it);
            erased = true;
        } else {
            break;
        }
    }
    if (erased) messageQueue.shrink_to_fit();
}

inline void MQTTComponent::handleIncomingMessage(char* topic, byte* payload, unsigned int length) {
    stats.receiveCount++;

    // Copy data into fixed-size event buffers for safe EventBus transmission
    MQTTMessageEvent ev{};

    // Copy topic (ensure null-termination)
    strncpy(ev.topic, topic, MQTT_EVENT_TOPIC_SIZE - 1);
    ev.topic[MQTT_EVENT_TOPIC_SIZE - 1] = '\0';

    // Copy payload (ensure null-termination)
    size_t copyLen = (length < MQTT_EVENT_PAYLOAD_SIZE - 1) ? length : (MQTT_EVENT_PAYLOAD_SIZE - 1);
    memcpy(ev.payload, payload, copyLen);
    ev.payload[copyLen] = '\0';

    emit(MQTTEvents::EVENT_MESSAGE, ev);
}

inline void MQTTComponent::updateStatistics() {
    if (isConnected()) {
        stats.uptime = (HAL::Platform::getMillis() - stateChangeTime) / 1000;
    }
}

inline String MQTTComponent::generateClientId() {
    uint64_t chipId = HAL::getChipId();
    char clientId[32];
    snprintf(clientId, sizeof(clientId), "%s-%04x%08x", 
             HAL::getPlatformName(), (uint16_t)(chipId >> 32), (uint32_t)chipId);
    return String(clientId);
}

// Static callback
inline void MQTTComponent::mqttCallback(char* topic, byte* payload, unsigned int length) {
    if (instance) {
        instance->handleIncomingMessage(topic, payload, length);
    }
}

// Topic matching with wildcards (static)
inline bool MQTTComponent::topicMatches(const String& filter, const String& topic) {
    if (filter == topic) return true;
    if (filter == "#") return true;
    
    std::vector<String> filterParts;
    std::vector<String> topicParts;
    
    int start = 0;
    int end = filter.indexOf('/');
    while (end >= 0) {
        filterParts.push_back(filter.substring(start, end));
        start = end + 1;
        end = filter.indexOf('/', start);
    }
    filterParts.push_back(filter.substring(start));
    
    start = 0;
    end = topic.indexOf('/');
    while (end >= 0) {
        topicParts.push_back(topic.substring(start, end));
        start = end + 1;
        end = topic.indexOf('/', start);
    }
    topicParts.push_back(topic.substring(start));
    
    size_t fi = 0, ti = 0;
    while (fi < filterParts.size() && ti < topicParts.size()) {
        if (filterParts[fi] == "#") {
            return true;
        }
        if (filterParts[fi] != "+" && filterParts[fi] != topicParts[ti]) {
            return false;
        }
        fi++;
        ti++;
    }
    
    return fi == filterParts.size() && ti == topicParts.size();
}

} // namespace Components
} // namespace DomoticsCore
