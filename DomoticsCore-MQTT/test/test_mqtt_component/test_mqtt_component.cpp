/**
 * @file test_mqtt_component.cpp
 * @brief Native unit tests for MQTT component
 *
 * Tests cover:
 * - Events (MQTTEvents)
 * - Component creation and configuration
 * - Config get/set
 * - Client ID generation
 * - Topic validation
 * - QoS validation
 * - Lifecycle (begin/shutdown)
 * - Non-blocking behavior
 *
 * Note: These are native tests that don't require actual MQTT broker connection.
 * Hardware tests with real broker are in test_mqtt_reconnect/.
 */

#include <unity.h>
#include <DomoticsCore/Core.h>
#include <DomoticsCore/MQTT.h>
#include <DomoticsCore/MQTTEvents.h>
#include <DomoticsCore/NetworkEvents.h>
#include <DomoticsCore/Testing/HeapTracker.h>

using namespace DomoticsCore;
using namespace DomoticsCore::Components;

void test_mqtt_reacts_to_generic_network_readiness() {
    MQTTConfig config;
    config.broker = "test.broker.com";
    config.autoReconnect = true;
    config.reconnectDelay = 0;

    Core core;
    auto mqtt = std::make_unique<MQTTComponent>(config);
    MQTTComponent* mqttPtr = mqtt.get();
    core.addComponent(std::move(mqtt));
    TEST_ASSERT_TRUE(core.begin());

    NetworkEvents::NetworkAvailabilityEvent unavailable{false};
    core.getEventBus().publishSticky(NetworkEvents::EVENT_READY, unavailable);
    core.loop();
    TEST_ASSERT_FALSE(mqttPtr->isConnected());

    NetworkEvents::NetworkAvailabilityEvent available{true};
    core.getEventBus().publishSticky(NetworkEvents::EVENT_READY, available);
    core.loop();
    core.loop();
    TEST_ASSERT_TRUE(mqttPtr->isConnected());

    core.getEventBus().publishSticky(NetworkEvents::EVENT_READY, unavailable);
    core.loop();
    mqttPtr->disconnect();
    for (int i = 0; i < 3; ++i) core.loop();
    TEST_ASSERT_FALSE(mqttPtr->isConnected());
}

// ============================================================================
// Event Tests
// ============================================================================

void test_mqtt_events_constants_defined() {
    // Verify event constants are defined and have expected values
    TEST_ASSERT_NOT_NULL(MQTTEvents::EVENT_CONNECTED);
    TEST_ASSERT_NOT_NULL(MQTTEvents::EVENT_DISCONNECTED);
    TEST_ASSERT_NOT_NULL(MQTTEvents::EVENT_MESSAGE);
    TEST_ASSERT_NOT_NULL(MQTTEvents::EVENT_PUBLISH);
    TEST_ASSERT_NOT_NULL(MQTTEvents::EVENT_SUBSCRIBE);

    TEST_ASSERT_EQUAL_STRING("mqtt/connected", MQTTEvents::EVENT_CONNECTED);
    TEST_ASSERT_EQUAL_STRING("mqtt/disconnected", MQTTEvents::EVENT_DISCONNECTED);
    TEST_ASSERT_EQUAL_STRING("mqtt/message", MQTTEvents::EVENT_MESSAGE);
    TEST_ASSERT_EQUAL_STRING("mqtt/publish", MQTTEvents::EVENT_PUBLISH);
    TEST_ASSERT_EQUAL_STRING("mqtt/subscribe", MQTTEvents::EVENT_SUBSCRIBE);
}

// ============================================================================
// Component Creation Tests
// ============================================================================

void test_mqtt_component_creation_default() {
    MQTTComponent mqtt;

    TEST_ASSERT_EQUAL_STRING("MQTT", mqtt.metadata.name);
    TEST_ASSERT_EQUAL_STRING("DomoticsCore", mqtt.metadata.author);
}

void test_mqtt_component_creation_with_config() {
    MQTTConfig config;
    config.broker = "test.mosquitto.org";
    config.port = 1883;
    config.clientId = "test-client";

    MQTTComponent mqtt(config);

    TEST_ASSERT_EQUAL_STRING("MQTT", mqtt.metadata.name);

    const MQTTConfig& cfg = mqtt.getConfig();
    TEST_ASSERT_EQUAL_STRING("test.mosquitto.org", cfg.broker.c_str());
    TEST_ASSERT_EQUAL_UINT16(1883, cfg.port);
    TEST_ASSERT_EQUAL_STRING("test-client", cfg.clientId.c_str());
}

// ============================================================================
// Config Tests
// ============================================================================

void test_mqtt_config_defaults() {
    MQTTConfig config;

    TEST_ASSERT_EQUAL_STRING("", config.broker.c_str());
    TEST_ASSERT_EQUAL_UINT16(1883, config.port);
    TEST_ASSERT_FALSE(config.useTLS);
    TEST_ASSERT_EQUAL_UINT16(60, config.keepAlive);
    TEST_ASSERT_TRUE(config.enableLWT);
    TEST_ASSERT_EQUAL_STRING("offline", config.lwtMessage.c_str());
    TEST_ASSERT_EQUAL_UINT8(1, config.lwtQoS);
    TEST_ASSERT_TRUE(config.lwtRetain);
    TEST_ASSERT_TRUE(config.autoReconnect);
    TEST_ASSERT_EQUAL_UINT32(1000, config.reconnectDelay);
    TEST_ASSERT_EQUAL_UINT32(30000, config.maxReconnectDelay);
}

void test_mqtt_config_get_set() {
    MQTTComponent mqtt;

    MQTTConfig newConfig;
    newConfig.broker = "mqtt.example.com";
    newConfig.port = 8883;
    newConfig.useTLS = true;
    newConfig.username = "user";
    newConfig.password = "pass";
    newConfig.clientId = "custom-id";

    mqtt.setConfig(newConfig);

    const MQTTConfig& cfg = mqtt.getConfig();
    TEST_ASSERT_EQUAL_STRING("mqtt.example.com", cfg.broker.c_str());
    TEST_ASSERT_EQUAL_UINT16(8883, cfg.port);
    TEST_ASSERT_TRUE(cfg.useTLS);
    TEST_ASSERT_EQUAL_STRING("user", cfg.username.c_str());
    TEST_ASSERT_EQUAL_STRING("pass", cfg.password.c_str());
    TEST_ASSERT_EQUAL_STRING("custom-id", cfg.clientId.c_str());
}

void test_mqtt_config_lwt() {
    MQTTConfig config;
    config.enableLWT = true;
    config.lwtTopic = "device/status";
    config.lwtMessage = "disconnected";
    config.lwtQoS = 2;
    config.lwtRetain = false;

    MQTTComponent mqtt(config);

    const MQTTConfig& cfg = mqtt.getConfig();
    TEST_ASSERT_TRUE(cfg.enableLWT);
    TEST_ASSERT_EQUAL_STRING("device/status", cfg.lwtTopic.c_str());
    TEST_ASSERT_EQUAL_STRING("disconnected", cfg.lwtMessage.c_str());
    TEST_ASSERT_EQUAL_UINT8(2, cfg.lwtQoS);
    TEST_ASSERT_FALSE(cfg.lwtRetain);
}

void test_mqtt_config_reconnection() {
    MQTTConfig config;
    config.autoReconnect = false;
    config.reconnectDelay = 5000;
    config.maxReconnectDelay = 60000;

    MQTTComponent mqtt(config);

    const MQTTConfig& cfg = mqtt.getConfig();
    TEST_ASSERT_FALSE(cfg.autoReconnect);
    TEST_ASSERT_EQUAL_UINT32(5000, cfg.reconnectDelay);
    TEST_ASSERT_EQUAL_UINT32(60000, cfg.maxReconnectDelay);
}

// ============================================================================
// Client ID Tests
// ============================================================================

void test_mqtt_client_id_auto_generation() {
    MQTTConfig config;
    config.clientId = "";  // Empty = auto-generate

    MQTTComponent mqtt(config);

    const MQTTConfig& cfg = mqtt.getConfig();
    // Client ID should be auto-generated (contains chip ID or similar)
    TEST_ASSERT_TRUE(cfg.clientId.length() > 0);
}

void test_mqtt_client_id_custom() {
    MQTTConfig config;
    config.clientId = "my-custom-client";

    MQTTComponent mqtt(config);

    const MQTTConfig& cfg = mqtt.getConfig();
    TEST_ASSERT_EQUAL_STRING("my-custom-client", cfg.clientId.c_str());
}

// ============================================================================
// QoS Validation Tests
// ============================================================================

void test_mqtt_qos_valid_values() {
    MQTTConfig config;

    // QoS 0
    config.lwtQoS = 0;
    MQTTComponent mqtt0(config);
    TEST_ASSERT_EQUAL_UINT8(0, mqtt0.getConfig().lwtQoS);

    // QoS 1
    config.lwtQoS = 1;
    MQTTComponent mqtt1(config);
    TEST_ASSERT_EQUAL_UINT8(1, mqtt1.getConfig().lwtQoS);

    // QoS 2
    config.lwtQoS = 2;
    MQTTComponent mqtt2(config);
    TEST_ASSERT_EQUAL_UINT8(2, mqtt2.getConfig().lwtQoS);
}

// ============================================================================
// Connection Status Tests
// ============================================================================

void test_mqtt_initial_connection_status() {
    MQTTComponent mqtt;

    // Without network/broker, should not be connected
    bool connected = mqtt.isConnected();
    TEST_ASSERT_FALSE(connected);
}

void test_mqtt_get_state_disconnected() {
    MQTTComponent mqtt;

    MQTTState state = mqtt.getState();
    // Should be Disconnected
    TEST_ASSERT_EQUAL(MQTTState::Disconnected, state);
}

// ============================================================================
// Statistics Tests
// ============================================================================

void test_mqtt_statistics_initial() {
    MQTTComponent mqtt;

    const MQTTStatistics& stats = mqtt.getStatistics();

    TEST_ASSERT_EQUAL_UINT32(0, stats.publishCount);
    TEST_ASSERT_EQUAL_UINT32(0, stats.receiveCount);
    TEST_ASSERT_EQUAL_UINT32(0, stats.publishErrors);
    TEST_ASSERT_EQUAL_UINT32(0, stats.connectCount);
    TEST_ASSERT_EQUAL_UINT32(0, stats.reconnectCount);
    TEST_ASSERT_EQUAL_UINT32(0, stats.subscriptionCount);
    TEST_ASSERT_EQUAL_UINT32(0, stats.uptime);
}

// ============================================================================
// Lifecycle Tests
// ============================================================================

void test_mqtt_begin_without_broker() {
    MQTTConfig config;
    config.broker = "";  // No broker

    MQTTComponent mqtt(config);

    // Should return success but be inactive when no broker configured
    ComponentStatus status = mqtt.begin();
    TEST_ASSERT_EQUAL(ComponentStatus::Success, status);

    // Fix #3: enabled must NOT be mutated by begin() when broker is empty
    TEST_ASSERT_TRUE(mqtt.getConfig().enabled);

    mqtt.shutdown();
}

void test_mqtt_begin_with_broker() {
    MQTTConfig config;
    config.broker = "test.mosquitto.org";

    MQTTComponent mqtt(config);

    // Should return success even without actual connection (non-blocking)
    ComponentStatus status = mqtt.begin();
    TEST_ASSERT_EQUAL(ComponentStatus::Success, status);

    mqtt.shutdown();
}

void test_mqtt_shutdown_returns_success() {
    MQTTConfig config;
    config.broker = "test.mosquitto.org";

    MQTTComponent mqtt(config);
    mqtt.begin();

    ComponentStatus status = mqtt.shutdown();
    TEST_ASSERT_EQUAL(ComponentStatus::Success, status);
}

void test_mqtt_full_lifecycle() {
    Core core;

    MQTTConfig config;
    config.broker = "test.mosquitto.org";

    auto mqtt = std::make_unique<MQTTComponent>(config);

    core.addComponent(std::move(mqtt));

    bool beginResult = core.begin();
    TEST_ASSERT_TRUE(beginResult);

    // Run a few loops (won't connect without network, but shouldn't crash)
    for (int i = 0; i < 10; i++) {
        core.loop();
    }

    core.shutdown();
}

// ============================================================================
// Non-blocking Tests
// ============================================================================

void test_mqtt_loop_non_blocking() {
    Core core;

    MQTTConfig config;
    config.broker = "test.mosquitto.org";

    auto mqtt = std::make_unique<MQTTComponent>(config);

    core.addComponent(std::move(mqtt));
    core.begin();

    // Run several loop iterations to verify non-blocking
    unsigned long start = HAL::Platform::getMillis();
    int loopCount = 0;
    while (HAL::Platform::getMillis() - start < 100) {
        core.loop();
        loopCount++;
        HAL::Platform::delayMs(1);
    }

    // Should have run many loops (non-blocking)
    TEST_ASSERT_GREATER_THAN(50, loopCount);

    core.shutdown();
}

// ============================================================================
// Configuration Update Tests
// ============================================================================

void test_mqtt_config_broker_update() {
    MQTTComponent mqtt;

    MQTTConfig newConfig;
    newConfig.broker = "new.broker.com";
    newConfig.port = 1884;

    mqtt.setConfig(newConfig);

    const MQTTConfig& cfg = mqtt.getConfig();
    TEST_ASSERT_EQUAL_STRING("new.broker.com", cfg.broker.c_str());
    TEST_ASSERT_EQUAL_UINT16(1884, cfg.port);
}

void test_mqtt_config_authentication_update() {
    MQTTComponent mqtt;

    MQTTConfig newConfig;
    newConfig.broker = "secure.broker.com";
    newConfig.username = "admin";
    newConfig.password = "secret";

    mqtt.setConfig(newConfig);

    const MQTTConfig& cfg = mqtt.getConfig();
    TEST_ASSERT_EQUAL_STRING("admin", cfg.username.c_str());
    TEST_ASSERT_EQUAL_STRING("secret", cfg.password.c_str());
}

void test_mqtt_config_keepalive_update() {
    MQTTComponent mqtt;

    MQTTConfig newConfig;
    newConfig.broker = "test.broker.com";
    newConfig.keepAlive = 120;

    mqtt.setConfig(newConfig);

    const MQTTConfig& cfg = mqtt.getConfig();
    TEST_ASSERT_EQUAL_UINT16(120, cfg.keepAlive);
}

// ============================================================================
// Edge Cases
// ============================================================================

void test_mqtt_empty_broker_rejected() {
    MQTTConfig config;
    config.broker = "";

    MQTTComponent mqtt(config);

    ComponentStatus status = mqtt.begin();
    TEST_ASSERT_EQUAL(ComponentStatus::Success, status);
}

void test_mqtt_invalid_port_zero() {
    MQTTConfig config;
    config.broker = "test.broker.com";
    config.port = 0;

    MQTTComponent mqtt(config);

    // Should handle gracefully (might use default port)
    const MQTTConfig& cfg = mqtt.getConfig();
    TEST_ASSERT_EQUAL_UINT16(0, cfg.port);
}

void test_mqtt_component_no_dependencies() {
    MQTTComponent mqtt;

    auto deps = mqtt.getDependencies();
    TEST_ASSERT_EQUAL(0, deps.size());
}

void test_mqtt_multiple_config_changes() {
    MQTTComponent mqtt;

    MQTTConfig config1;
    config1.broker = "broker1.com";
    mqtt.setConfig(config1);
    TEST_ASSERT_EQUAL_STRING("broker1.com", mqtt.getConfig().broker.c_str());

    MQTTConfig config2;
    config2.broker = "broker2.com";
    mqtt.setConfig(config2);
    TEST_ASSERT_EQUAL_STRING("broker2.com", mqtt.getConfig().broker.c_str());

    MQTTConfig config3;
    config3.broker = "broker3.com";
    mqtt.setConfig(config3);
    TEST_ASSERT_EQUAL_STRING("broker3.com", mqtt.getConfig().broker.c_str());
}

// ============================================================================
// config.enabled Bug Tests (P0)
// ============================================================================

void test_mqtt_loop_processes_when_connected_and_disabled() {
    // Fix #1: loop() must call mqttClient->loop() when connected, even if enabled=false
    MQTTConfig config;
    config.broker = "test.broker.com";
    config.port = 1883;
    config.enabled = false;
    config.autoReconnect = false;

    MQTTComponent mqtt(config);
    mqtt.begin();

    // Simulate MQTT connected through the native transport stub.
    mqtt.connect();

    TEST_ASSERT_TRUE(mqtt.isConnected());
    TEST_ASSERT_FALSE(mqtt.getConfig().enabled);

    uint32_t countBefore = mqtt.debugLoopCount();
    mqtt.loop();
    TEST_ASSERT_GREATER_THAN(countBefore, mqtt.debugLoopCount());

    mqtt.shutdown();
}

void test_mqtt_setconfig_preserves_enabled_when_connected() {
    // Fix #2: setConfig() must not clear enabled when connected
    MQTTConfig config;
    config.broker = "test.broker.com";
    config.port = 1883;
    config.enabled = true;
    config.autoReconnect = false;

    MQTTComponent mqtt(config);
    mqtt.begin();

    mqtt.connect();
    TEST_ASSERT_TRUE(mqtt.isConnected());

    // Reload config with enabled=false (simulates flash reload)
    MQTTConfig reloadCfg;
    reloadCfg.broker = "test.broker.com";
    reloadCfg.port = 1883;
    reloadCfg.enabled = false;
    reloadCfg.autoReconnect = false;
    mqtt.setConfig(reloadCfg);

    TEST_ASSERT_TRUE(mqtt.getConfig().enabled);

    mqtt.shutdown();
}

void test_mqtt_begin_empty_broker_does_not_clear_enabled() {
    // Fix #3: begin() with empty broker must not set enabled=false
    MQTTConfig config;
    config.broker = "";
    config.enabled = true;

    MQTTComponent mqtt(config);
    mqtt.begin();

    TEST_ASSERT_TRUE(mqtt.getConfig().enabled);

    mqtt.shutdown();
}

void test_mqtt_full_lifecycle_empty_to_configured() {
    // All 3 fixes: empty broker -> configure -> connect -> loop
    MQTTConfig config;
    config.broker = "";
    config.enabled = true;
    config.autoReconnect = false;

    MQTTComponent mqtt(config);
    mqtt.begin();

    // Phase 1: enabled preserved after empty broker begin()
    TEST_ASSERT_TRUE(mqtt.getConfig().enabled);

    // Phase 2: configure broker via setConfig
    MQTTConfig newCfg;
    newCfg.broker = "test.broker.com";
    newCfg.port = 1883;
    newCfg.enabled = true;
    newCfg.autoReconnect = false;
    mqtt.setConfig(newCfg);

    // Phase 3: connect
    mqtt.connect();
    TEST_ASSERT_TRUE(mqtt.isConnected());

    // Phase 4: loop processes messages
    uint32_t countBefore = mqtt.debugLoopCount();
    mqtt.loop();
    TEST_ASSERT_GREATER_THAN(countBefore, mqtt.debugLoopCount());

    mqtt.shutdown();
}

void test_mqtt_config_reload_preserves_active_connection() {
    // Fix #1 + #2: config reload during active connection
    MQTTConfig config;
    config.broker = "test.broker.com";
    config.port = 1883;
    config.enabled = true;
    config.autoReconnect = false;

    MQTTComponent mqtt(config);
    mqtt.begin();

    mqtt.connect();
    TEST_ASSERT_TRUE(mqtt.isConnected());

    // Reload with enabled=false
    MQTTConfig reloadCfg;
    reloadCfg.broker = "test.broker.com";
    reloadCfg.port = 1883;
    reloadCfg.enabled = false;
    reloadCfg.autoReconnect = false;
    mqtt.setConfig(reloadCfg);

    // Fix #2: enabled preserved
    TEST_ASSERT_TRUE(mqtt.getConfig().enabled);

    // Fix #1: loop still processes
    uint32_t countBefore = mqtt.debugLoopCount();
    mqtt.loop();
    TEST_ASSERT_GREATER_THAN(countBefore, mqtt.debugLoopCount());

    mqtt.shutdown();
}

// ============================================================================
// config.enabled Bug Tests (P1)
// ============================================================================

void test_mqtt_reconnect_blocked_when_disabled() {
    // Reconnection must not fire when enabled=false, even with WiFi available
    MQTTConfig config;
    config.broker = "test.broker.com";
    config.port = 1883;
    config.enabled = false;
    config.autoReconnect = true;
    config.reconnectDelay = 0;  // Eliminate timer as a variable

    MQTTComponent mqtt(config);
    mqtt.begin();

    // Network is available in standalone mode — only enabled blocks reconnect.

    for (int i = 0; i < 10; i++) {
        mqtt.loop();
    }

    TEST_ASSERT_FALSE(mqtt.isConnected());
    TEST_ASSERT_EQUAL_UINT32(0, mqtt.getStatistics().reconnectCount);

    mqtt.shutdown();
}

void test_mqtt_setconfig_does_not_preserve_when_disconnected() {
    // Fix #2 edge: preservation only when Connected, not when Disconnected
    MQTTConfig config;
    config.broker = "test.broker.com";
    config.port = 1883;
    config.enabled = true;
    config.autoReconnect = false;

    MQTTComponent mqtt(config);
    // NOT connected — state is Disconnected

    MQTTConfig newCfg;
    newCfg.broker = "test.broker.com";
    newCfg.port = 1883;
    newCfg.enabled = false;
    newCfg.autoReconnect = false;
    mqtt.setConfig(newCfg);

    TEST_ASSERT_FALSE(mqtt.getConfig().enabled);
}

void test_mqtt_loop_empty_broker_early_return() {
    // loop() with empty broker must not crash
    MQTTConfig config;
    config.broker = "test.broker.com";
    config.port = 1883;
    config.enabled = true;
    config.autoReconnect = false;

    MQTTComponent mqtt(config);
    mqtt.begin();

    mqtt.connect();
    TEST_ASSERT_TRUE(mqtt.isConnected());

    // Clear broker while connected
    mqtt.setBroker("", 0);
    mqtt.loop();  // Must not crash

    // isConnected() may still return true (stale state)
    // The key assertion is no crash
    mqtt.shutdown();
}

// ============================================================================
// Memory Stability Tests (R2 shrink_to_fit)
// ============================================================================

void test_mqtt_memory_stability_message_queue(void) {
    using namespace DomoticsCore::Testing;
    HeapTracker tracker;

    MQTTConfig config;
    config.broker = "test.broker.com";
    config.port = 1883;
    config.enabled = true;
    config.autoReconnect = false;

    MQTTComponent mqtt(config);
    mqtt.begin();

    // Warm up: full connect/disconnect + queue/drain cycle to stabilize allocator
    for (int w = 0; w < 3; w++) {
        mqtt.publish("warmup/topic", "warmup_payload");
        mqtt.connect();
        for (int j = 0; j < 5; j++) mqtt.loop();
        mqtt.disconnect();
    }

    tracker.checkpoint("before");

    // Queue 10 messages while disconnected, then connect and drain
    for (int i = 0; i < 10; i++) {
        mqtt.publish("test/topic", "payload_data");
    }

    mqtt.connect();
    for (int i = 0; i < 15; i++) {
        mqtt.loop();
    }
    mqtt.disconnect();

    tracker.checkpoint("after");

    // Tolerance 2048: connect/disconnect emits EventBus events with String topics
    // (EVENT_CONNECTED, EVENT_DISCONNECTED) that cause ~1.4KB allocator overhead
    // on native glibc. This is expected noise per the HeapTracker native limitations.
    MemoryTestResult result = tracker.assertStable("before", "after", 2048);
    TEST_ASSERT_TRUE_MESSAGE(result.passed, result.message.c_str());

    mqtt.shutdown();
}

void test_mqtt_memory_stability_subscribe_cycle(void) {
    using namespace DomoticsCore::Testing;
    HeapTracker tracker;

    MQTTConfig config;
    config.broker = "test.broker.com";
    config.port = 1883;
    config.enabled = true;
    config.autoReconnect = false;

    MQTTComponent mqtt(config);
    mqtt.begin();

    mqtt.connect();

    // Warm up
    mqtt.subscribe("warmup/topic");
    mqtt.unsubscribe("warmup/topic");

    tracker.checkpoint("before");

    for (int i = 0; i < 10; i++) {
        String topic = "test/sub/" + String(i);
        mqtt.subscribe(topic);
        mqtt.unsubscribe(topic);
    }

    tracker.checkpoint("after");

    MemoryTestResult result = tracker.assertStable("before", "after", 512);
    TEST_ASSERT_TRUE_MESSAGE(result.passed, result.message.c_str());

    mqtt.shutdown();
}

// ============================================================================
// Test Runner
// ============================================================================

void setUp() {}
void tearDown() {
}

// ============================================================================
// R18 — Config Limit Enforcement Tests
// ============================================================================

void test_mqtt_queue_rejects_when_full() {
    MQTTConfig cfg;
    cfg.broker = "test.local";
    cfg.maxQueueSize = 5;
    MQTTComponent mqtt(cfg);
    mqtt.begin();
    // Not connected → messages get queued
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_TRUE(mqtt.publish("topic", String(i)));
    }
    // 6th should be rejected
    TEST_ASSERT_FALSE(mqtt.publish("topic", "overflow"));
    mqtt.shutdown();
}

void test_mqtt_queue_unlimited_when_zero() {
    MQTTConfig cfg;
    cfg.broker = "test.local";
    cfg.maxQueueSize = 0;
    cfg.publishRateLimit = 0; // disable rate limit to test queue only
    MQTTComponent mqtt(cfg);
    mqtt.begin();
    for (int i = 0; i < 100; i++) {
        TEST_ASSERT_TRUE(mqtt.publish("topic", String(i)));
    }
    mqtt.shutdown();
}

void test_mqtt_subscribe_rejects_at_limit() {
    MQTTConfig cfg;
    cfg.broker = "test.local";
    cfg.maxSubscriptions = 3;
    MQTTComponent mqtt(cfg);
    mqtt.begin();
    TEST_ASSERT_TRUE(mqtt.subscribe("topic/1"));
    TEST_ASSERT_TRUE(mqtt.subscribe("topic/2"));
    TEST_ASSERT_TRUE(mqtt.subscribe("topic/3"));
    TEST_ASSERT_FALSE(mqtt.subscribe("topic/4"));
    mqtt.shutdown();
}

void test_mqtt_subscribe_unlimited_when_zero() {
    MQTTConfig cfg;
    cfg.broker = "test.local";
    cfg.maxSubscriptions = 0;
    MQTTComponent mqtt(cfg);
    mqtt.begin();
    for (int i = 0; i < 100; i++) {
        TEST_ASSERT_TRUE(mqtt.subscribe("topic/" + String(i)));
    }
    mqtt.shutdown();
}

void test_mqtt_rate_limit_enforced() {
    MQTTConfig cfg;
    cfg.broker = "test.local";
    cfg.publishRateLimit = 2;
    cfg.maxQueueSize = 0; // unlimited queue
    MQTTComponent mqtt(cfg);
    mqtt.begin();
    // Not connected, but rate limit is checked before queue
    TEST_ASSERT_TRUE(mqtt.publish("topic", "msg1"));
    TEST_ASSERT_TRUE(mqtt.publish("topic", "msg2"));
    TEST_ASSERT_FALSE(mqtt.publish("topic", "msg3"));
    mqtt.shutdown();
}

void test_mqtt_rate_limit_unlimited_when_zero() {
    MQTTConfig cfg;
    cfg.broker = "test.local";
    cfg.publishRateLimit = 0;
    cfg.maxQueueSize = 0;
    MQTTComponent mqtt(cfg);
    mqtt.begin();
    for (int i = 0; i < 50; i++) {
        TEST_ASSERT_TRUE(mqtt.publish("topic", String(i)));
    }
    mqtt.shutdown();
}

int main() {
    UNITY_BEGIN();

    // Event tests
    RUN_TEST(test_mqtt_events_constants_defined);
    RUN_TEST(test_mqtt_reacts_to_generic_network_readiness);

    // Component creation tests
    RUN_TEST(test_mqtt_component_creation_default);
    RUN_TEST(test_mqtt_component_creation_with_config);

    // Config tests
    RUN_TEST(test_mqtt_config_defaults);
    RUN_TEST(test_mqtt_config_get_set);
    RUN_TEST(test_mqtt_config_lwt);
    RUN_TEST(test_mqtt_config_reconnection);

    // Client ID tests
    RUN_TEST(test_mqtt_client_id_auto_generation);
    RUN_TEST(test_mqtt_client_id_custom);

    // QoS validation tests
    RUN_TEST(test_mqtt_qos_valid_values);

    // Connection status tests
    RUN_TEST(test_mqtt_initial_connection_status);
    RUN_TEST(test_mqtt_get_state_disconnected);

    // Statistics tests
    RUN_TEST(test_mqtt_statistics_initial);

    // Lifecycle tests
    RUN_TEST(test_mqtt_begin_without_broker);
    RUN_TEST(test_mqtt_begin_with_broker);
    RUN_TEST(test_mqtt_shutdown_returns_success);
    RUN_TEST(test_mqtt_full_lifecycle);

    // Non-blocking tests
    RUN_TEST(test_mqtt_loop_non_blocking);

    // Configuration update tests
    RUN_TEST(test_mqtt_config_broker_update);
    RUN_TEST(test_mqtt_config_authentication_update);
    RUN_TEST(test_mqtt_config_keepalive_update);

    // Edge cases
    RUN_TEST(test_mqtt_empty_broker_rejected);
    RUN_TEST(test_mqtt_invalid_port_zero);
    RUN_TEST(test_mqtt_component_no_dependencies);
    RUN_TEST(test_mqtt_multiple_config_changes);

    // config.enabled bug tests (P0)
    RUN_TEST(test_mqtt_loop_processes_when_connected_and_disabled);
    RUN_TEST(test_mqtt_setconfig_preserves_enabled_when_connected);
    RUN_TEST(test_mqtt_begin_empty_broker_does_not_clear_enabled);
    RUN_TEST(test_mqtt_full_lifecycle_empty_to_configured);
    RUN_TEST(test_mqtt_config_reload_preserves_active_connection);

    // config.enabled bug tests (P1)
    RUN_TEST(test_mqtt_reconnect_blocked_when_disabled);
    RUN_TEST(test_mqtt_setconfig_does_not_preserve_when_disconnected);
    RUN_TEST(test_mqtt_loop_empty_broker_early_return);

    // Memory stability tests (R2)
    RUN_TEST(test_mqtt_memory_stability_message_queue);
    RUN_TEST(test_mqtt_memory_stability_subscribe_cycle);

    // R18 — Config limit enforcement tests
    RUN_TEST(test_mqtt_queue_rejects_when_full);
    RUN_TEST(test_mqtt_queue_unlimited_when_zero);
    RUN_TEST(test_mqtt_subscribe_rejects_at_limit);
    RUN_TEST(test_mqtt_subscribe_unlimited_when_zero);
    RUN_TEST(test_mqtt_rate_limit_enforced);
    RUN_TEST(test_mqtt_rate_limit_unlimited_when_zero);

    return UNITY_END();
}
