// Unit tests for RemoteConsoleComponent
// Tests: creation, config, lifecycle, client handling, commands, log buffering

#include <unity.h>
#include <DomoticsCore/Core.h>
#include <DomoticsCore/RemoteConsole.h>
#include <DomoticsCore/NetworkEvents.h>
#include <DomoticsCore/Testing/HeapTracker.h>

using namespace DomoticsCore;
using namespace DomoticsCore::Components;

// Test state
static Core* testCore = nullptr;

void test_remoteconsole_tracks_generic_provider_addresses() {
    auto console = std::make_unique<RemoteConsoleComponent>();
    RemoteConsoleComponent* consolePtr = console.get();
    testCore->addComponent(std::move(console));
    TEST_ASSERT_TRUE(testCore->begin());

    NetworkEvents::NetworkProviderAddressEvent event{};
    NetworkEvents::copyProviderId(event.providerId, "ethernet");
    NetworkEvents::copyAddress(event.address, "192.168.1.10");
    testCore->getEventBus().publish(NetworkEvents::EVENT_PROVIDER_ADDRESS_CHANGED, event);
    testCore->loop();

    const auto& addresses = consolePtr->getNetworkAddresses();
    TEST_ASSERT_EQUAL_UINT32(1, addresses.size());
    TEST_ASSERT_EQUAL_STRING("192.168.1.10", addresses.at("ethernet").c_str());
}

void setUp(void) {
    testCore = new Core();
}

void tearDown(void) {
    if (testCore) {
        testCore->shutdown();
        delete testCore;
        testCore = nullptr;
    }
    // Clear static logger callbacks to prevent dangling pointers from deleted components
    // Note: callbacks are now properly removed via ID in shutdown()
}

// ============================================================================
// RemoteConsoleComponent Creation Tests
// ============================================================================

void test_remoteconsole_component_creation_default(void) {
    auto console = std::make_unique<RemoteConsoleComponent>();

    TEST_ASSERT_EQUAL_STRING("RemoteConsole", console->metadata.name);
    TEST_ASSERT_EQUAL_STRING("1.4.1", console->metadata.version);
}

void test_remoteconsole_component_creation_with_config(void) {
    RemoteConsoleConfig config;
    config.port = 2323;
    config.bufferSize = 1000;
    config.maxClients = 5;

    auto console = std::make_unique<RemoteConsoleComponent>(config);

    TEST_ASSERT_EQUAL_STRING("RemoteConsole", console->metadata.name);
}

// ============================================================================
// RemoteConsoleConfig Tests
// ============================================================================

void test_remoteconsole_config_defaults(void) {
    RemoteConsoleConfig config;

    TEST_ASSERT_TRUE(config.enabled);
    TEST_ASSERT_EQUAL_UINT16(23, config.port);
    TEST_ASSERT_FALSE(config.requireAuth);
    TEST_ASSERT_TRUE(config.password.isEmpty());
    TEST_ASSERT_EQUAL_UINT32(DOMOTICS_LOG_BUFFER_SIZE, config.bufferSize);
    TEST_ASSERT_TRUE(config.allowCommands);
    // Skip allowedIPs check (requires IPAddress stub implementation)
    // TEST_ASSERT_EQUAL(0, config.allowedIPs.size());
    TEST_ASSERT_TRUE(config.colorOutput);
    TEST_ASSERT_EQUAL_UINT32(3, config.maxClients);
    TEST_ASSERT_EQUAL(LOG_LEVEL_INFO, config.defaultLogLevel);
}

void test_remoteconsole_config_custom(void) {
    RemoteConsoleConfig config;
    config.enabled = false;
    config.port = 2323;
    config.requireAuth = true;
    config.password = "secret123";
    config.bufferSize = 1000;
    config.allowCommands = false;
    config.colorOutput = false;
    config.maxClients = 5;
    config.defaultLogLevel = LOG_LEVEL_DEBUG;

    auto console = std::make_unique<RemoteConsoleComponent>(config);
    RemoteConsoleComponent* consolePtr = console.get();

    testCore->addComponent(std::move(console));
    testCore->begin();

    // Disabled console should still return Success but not start server
    TEST_ASSERT_EQUAL(ComponentStatus::Success, consolePtr->getLastStatus());
}

// ============================================================================
// RemoteConsole Lifecycle Tests
// ============================================================================

void test_remoteconsole_begin_enabled(void) {
    RemoteConsoleConfig config;
    config.enabled = true;
    config.port = 2323;

    auto console = std::make_unique<RemoteConsoleComponent>(config);
    RemoteConsoleComponent* consolePtr = console.get();

    testCore->addComponent(std::move(console));
    testCore->begin();

    TEST_ASSERT_EQUAL(ComponentStatus::Success, consolePtr->getLastStatus());
}

void test_remoteconsole_begin_disabled(void) {
    RemoteConsoleConfig config;
    config.enabled = false;

    auto console = std::make_unique<RemoteConsoleComponent>(config);
    RemoteConsoleComponent* consolePtr = console.get();

    testCore->addComponent(std::move(console));
    testCore->begin();

    TEST_ASSERT_EQUAL(ComponentStatus::Success, consolePtr->getLastStatus());
}

void test_remoteconsole_full_lifecycle(void) {
    RemoteConsoleConfig config;
    config.enabled = true;

    auto console = std::make_unique<RemoteConsoleComponent>(config);
    RemoteConsoleComponent* consolePtr = console.get();

    testCore->addComponent(std::move(console));

    // Begin
    testCore->begin();
    TEST_ASSERT_EQUAL(ComponentStatus::Success, consolePtr->getLastStatus());

    // Multiple loops should not crash
    for (int i = 0; i < 10; i++) {
        testCore->loop();
    }

    // Shutdown
    testCore->shutdown();
}

void test_remoteconsole_shutdown_returns_success(void) {
    auto console = std::make_unique<RemoteConsoleComponent>();
    RemoteConsoleComponent* consolePtr = console.get();

    testCore->addComponent(std::move(console));
    testCore->begin();

    ComponentStatus status = consolePtr->shutdown();
    TEST_ASSERT_EQUAL(ComponentStatus::Success, status);
}

// ============================================================================
// RemoteConsole Dependencies Tests
// ============================================================================

void test_remoteconsole_no_dependencies(void) {
    auto console = std::make_unique<RemoteConsoleComponent>();

    auto deps = console->getDependencies();
    // RemoteConsole has no hard dependencies (WiFi is optional)
    TEST_ASSERT_EQUAL(0, deps.size());
}

// ============================================================================
// RemoteConsole Configuration Tests
// ============================================================================

void test_remoteconsole_port_config(void) {
    RemoteConsoleConfig config;
    config.port = 8023;

    auto console = std::make_unique<RemoteConsoleComponent>(config);

    // Port should be configured
    TEST_ASSERT_NOT_NULL(console.get());
}

void test_remoteconsole_buffer_size_config(void) {
    RemoteConsoleConfig config;
    config.bufferSize = 2000;

    auto console = std::make_unique<RemoteConsoleComponent>(config);

    TEST_ASSERT_NOT_NULL(console.get());
}

void test_remoteconsole_max_clients_config(void) {
    RemoteConsoleConfig config;
    config.maxClients = 10;

    auto console = std::make_unique<RemoteConsoleComponent>(config);

    TEST_ASSERT_NOT_NULL(console.get());
}

void test_remoteconsole_authentication_config(void) {
    RemoteConsoleConfig config;
    config.requireAuth = true;
    config.password = "mypassword123";

    auto console = std::make_unique<RemoteConsoleComponent>(config);

    TEST_ASSERT_NOT_NULL(console.get());
}

void test_remoteconsole_log_level_config(void) {
    RemoteConsoleConfig config;
    config.defaultLogLevel = LOG_LEVEL_DEBUG;

    auto console = std::make_unique<RemoteConsoleComponent>(config);

    TEST_ASSERT_NOT_NULL(console.get());
}

// ============================================================================
// Edge Cases Tests
// ============================================================================

void test_remoteconsole_zero_buffer_size(void) {
    RemoteConsoleConfig config;
    config.bufferSize = 0;

    auto console = std::make_unique<RemoteConsoleComponent>(config);
    RemoteConsoleComponent* consolePtr = console.get();

    testCore->addComponent(std::move(console));
    testCore->begin();

    // Should handle gracefully
    TEST_ASSERT_EQUAL(ComponentStatus::Success, consolePtr->getLastStatus());
}

void test_remoteconsole_zero_max_clients(void) {
    RemoteConsoleConfig config;
    config.maxClients = 0;

    auto console = std::make_unique<RemoteConsoleComponent>(config);
    RemoteConsoleComponent* consolePtr = console.get();

    testCore->addComponent(std::move(console));
    testCore->begin();

    // Should handle gracefully (no clients allowed)
    TEST_ASSERT_EQUAL(ComponentStatus::Success, consolePtr->getLastStatus());
}

void test_remoteconsole_multiple_config_changes(void) {
    auto console = std::make_unique<RemoteConsoleComponent>();
    RemoteConsoleComponent* consolePtr = console.get();

    testCore->addComponent(std::move(console));
    testCore->begin();

    // Component should remain stable through config
    TEST_ASSERT_EQUAL(ComponentStatus::Success, consolePtr->getLastStatus());
}

void test_remoteconsole_empty_password(void) {
    RemoteConsoleConfig config;
    config.requireAuth = true;
    config.password = "";  // Empty password

    auto console = std::make_unique<RemoteConsoleComponent>(config);

    // Should create successfully (validation happens at runtime)
    TEST_ASSERT_NOT_NULL(console.get());
}

void test_remoteconsole_color_output_disabled(void) {
    RemoteConsoleConfig config;
    config.colorOutput = false;

    auto console = std::make_unique<RemoteConsoleComponent>(config);
    RemoteConsoleComponent* consolePtr = console.get();

    testCore->addComponent(std::move(console));
    testCore->begin();

    TEST_ASSERT_EQUAL(ComponentStatus::Success, consolePtr->getLastStatus());
}

void test_remoteconsole_commands_disabled(void) {
    RemoteConsoleConfig config;
    config.allowCommands = false;

    auto console = std::make_unique<RemoteConsoleComponent>(config);
    RemoteConsoleComponent* consolePtr = console.get();

    testCore->addComponent(std::move(console));
    testCore->begin();

    TEST_ASSERT_EQUAL(ComponentStatus::Success, consolePtr->getLastStatus());
}

// ============================================================================
// Authentication Tests (R21)
// ============================================================================

void test_remoteconsole_auth_config_defaults(void) {
    RemoteConsoleConfig config;

    TEST_ASSERT_FALSE(config.requireAuth);
    TEST_ASSERT_TRUE(config.password.isEmpty());
    TEST_ASSERT_EQUAL_UINT32(10000, config.authTimeoutMs);
    TEST_ASSERT_TRUE(config.allowCommands);
}

void test_remoteconsole_auth_config_custom(void) {
    RemoteConsoleConfig config;
    config.requireAuth = true;
    config.password = "s3cret";
    config.authTimeoutMs = 5000;
    config.allowCommands = false;

    auto console = std::make_unique<RemoteConsoleComponent>(config);
    RemoteConsoleComponent* consolePtr = console.get();
    testCore->addComponent(std::move(console));
    testCore->begin();

    TEST_ASSERT_EQUAL(ComponentStatus::Success, consolePtr->getLastStatus());
}

void test_remoteconsole_auth_timeout_zero_means_no_timeout(void) {
    RemoteConsoleConfig config;
    config.requireAuth = true;
    config.password = "pass";
    config.authTimeoutMs = 0;

    auto console = std::make_unique<RemoteConsoleComponent>(config);
    RemoteConsoleComponent* consolePtr = console.get();
    testCore->addComponent(std::move(console));
    testCore->begin();

    // Run several loops — should not crash with timeout=0
    for (int i = 0; i < 20; i++) {
        testCore->loop();
    }

    TEST_ASSERT_EQUAL(ComponentStatus::Success, consolePtr->getLastStatus());
}

void test_remoteconsole_auth_require_with_password(void) {
    RemoteConsoleConfig config;
    config.requireAuth = true;
    config.password = "mypass";

    auto console = std::make_unique<RemoteConsoleComponent>(config);
    RemoteConsoleComponent* consolePtr = console.get();
    testCore->addComponent(std::move(console));
    testCore->begin();

    // Component should start successfully even with auth enabled
    TEST_ASSERT_EQUAL(ComponentStatus::Success, consolePtr->getLastStatus());

    // Multiple loops should not crash
    for (int i = 0; i < 10; i++) {
        testCore->loop();
    }
}

void test_remoteconsole_allow_commands_false_lifecycle(void) {
    RemoteConsoleConfig config;
    config.allowCommands = false;

    auto console = std::make_unique<RemoteConsoleComponent>(config);
    RemoteConsoleComponent* consolePtr = console.get();
    testCore->addComponent(std::move(console));
    testCore->begin();

    TEST_ASSERT_EQUAL(ComponentStatus::Success, consolePtr->getLastStatus());

    // Full lifecycle with commands disabled
    for (int i = 0; i < 10; i++) {
        testCore->loop();
    }
    testCore->shutdown();
}

void test_remoteconsole_auth_protocol_flow(void) {
    // Protocol-level test: verify auth blocks commands, correct password grants access
    RemoteConsoleConfig config;
    config.enabled = true;
    config.requireAuth = true;
    config.password = "s3cret";
    config.authTimeoutMs = 0;  // No timeout for this test

    auto console = std::make_unique<RemoteConsoleComponent>(config);
    RemoteConsoleComponent* consolePtr = console.get();
    testCore->addComponent(std::move(console));
    testCore->begin();

    // Simulate a client connection — returns a handle sharing state with the accepted copy
    HAL::NetworkClient clientHandle = consolePtr->getServer()->simulateClient(true, 42);

    // First loop: accept the client, send welcome
    testCore->loop();

    // Verify welcome message mentions auth
    std::string output = clientHandle.getWriteBufferAsString();
    TEST_ASSERT_TRUE_MESSAGE(
        output.find("Authentication required") != std::string::npos,
        "Welcome should mention auth required");
    clientHandle.clearWriteBuffer();

    // Try a command before auth — should be blocked
    clientHandle.simulateIncomingData("info\n");
    testCore->loop();

    output = clientHandle.getWriteBufferAsString();
    TEST_ASSERT_TRUE_MESSAGE(
        output.find("Authentication required") != std::string::npos,
        "Command before auth should be blocked");
    clientHandle.clearWriteBuffer();

    // Wrong password
    clientHandle.simulateIncomingData("auth wrongpass\n");
    testCore->loop();

    output = clientHandle.getWriteBufferAsString();
    TEST_ASSERT_TRUE_MESSAGE(
        output.find("Authentication failed") != std::string::npos,
        "Wrong password should fail");
    clientHandle.clearWriteBuffer();

    // Correct password
    clientHandle.simulateIncomingData("auth s3cret\n");
    testCore->loop();

    output = clientHandle.getWriteBufferAsString();
    TEST_ASSERT_TRUE_MESSAGE(
        output.find("Authentication successful") != std::string::npos,
        "Correct password should succeed");
    clientHandle.clearWriteBuffer();

    // Now commands should work
    clientHandle.simulateIncomingData("heap\n");
    testCore->loop();

    output = clientHandle.getWriteBufferAsString();
    TEST_ASSERT_TRUE_MESSAGE(
        output.find("Free Heap") != std::string::npos,
        "Commands should work after auth");
}

void test_remoteconsole_auth_and_commands_disabled(void) {
    RemoteConsoleConfig config;
    config.requireAuth = true;
    config.password = "secret";
    config.allowCommands = false;

    auto console = std::make_unique<RemoteConsoleComponent>(config);
    RemoteConsoleComponent* consolePtr = console.get();
    testCore->addComponent(std::move(console));
    testCore->begin();

    TEST_ASSERT_EQUAL(ComponentStatus::Success, consolePtr->getLastStatus());

    // Full lifecycle with both auth and commands disabled
    for (int i = 0; i < 10; i++) {
        testCore->loop();
    }
    testCore->shutdown();
}

// ============================================================================
// Memory Leak Tests
// ============================================================================

/**
 * @brief Test that simulates long-running logging to detect memory leaks
 * 
 * This test reproduces the OOM crash observed overnight where heap dropped
 * from 30KB to 0 due to std::deque<LogEntry> not releasing memory on pop_front()
 */
void test_remoteconsole_log_buffer_no_memory_leak(void) {
    RemoteConsoleConfig config;
    config.bufferSize = 100;  // Small buffer to force rotation quickly
    config.enabled = true;
    
    auto console = std::make_unique<RemoteConsoleComponent>(config);
    RemoteConsoleComponent* consolePtr = console.get();
    
    testCore->addComponent(std::move(console));
    testCore->begin();
    
    // Measure initial heap
    size_t heapBefore = HAL::getFreeHeap();
    
    // Simulate long-running logging (5000 log entries = 50x buffer rotation)
    // This should NOT leak memory if the circular buffer is implemented correctly
    for (int i = 0; i < 5000; i++) {
        // Simulate what DLOG_I does - calls log() via callback
        consolePtr->log(LOG_LEVEL_INFO, "TEST", ("Log message number " + String(i)).c_str());
    }
    
    // Force cleanup
    consolePtr->clearBuffer();
    
    size_t heapAfter = HAL::getFreeHeap();
    
    // Calculate leak per iteration
    int heapDelta = (int)heapBefore - (int)heapAfter;
    float leakPerLog = heapDelta / 5000.0f;
    
    printf("\n[MEMORY TEST] Log buffer rotation x5000:\n");
    printf("  Heap before: %zu bytes\n", heapBefore);
    printf("  Heap after:  %zu bytes\n", heapAfter);
    printf("  Delta: %d bytes (%.2f bytes/log)\n", heapDelta, leakPerLog);
    
    // Allow some tolerance for fragmentation, but should be < 1 byte per log
    // With the bug, this would be ~56 bytes per log = 280KB leak!
    TEST_ASSERT_LESS_THAN_MESSAGE(5000, heapDelta, 
        "Memory leak detected in log buffer! Each log leaks memory.");
}

/**
 * @brief Test rapid log buffer fill/clear cycles
 */
void test_remoteconsole_rapid_buffer_cycles_no_leak(void) {
    RemoteConsoleConfig config;
    config.bufferSize = 50;
    config.enabled = true;
    
    auto console = std::make_unique<RemoteConsoleComponent>(config);
    RemoteConsoleComponent* consolePtr = console.get();
    
    testCore->addComponent(std::move(console));
    testCore->begin();
    
    size_t heapBefore = HAL::getFreeHeap();
    
    // 100 cycles of fill + clear
    for (int cycle = 0; cycle < 100; cycle++) {
        // Fill buffer
        for (int i = 0; i < 60; i++) {  // More than bufferSize to trigger rotation
            consolePtr->log(LOG_LEVEL_INFO, "CYCLE", ("Cycle " + String(cycle) + " msg " + String(i)).c_str());
        }
        // Clear buffer
        consolePtr->clearBuffer();
    }
    
    size_t heapAfter = HAL::getFreeHeap();
    int heapDelta = (int)heapBefore - (int)heapAfter;
    
    printf("\n[MEMORY TEST] Rapid buffer cycles x100:\n");
    printf("  Heap delta: %d bytes\n", heapDelta);
    
    // Should have zero or minimal leak after clear
    TEST_ASSERT_LESS_THAN_MESSAGE(1000, heapDelta,
        "Memory leak in rapid buffer cycles!");
}

// ============================================================================
// Memory Stability Tests (R4 shrink_to_fit)
// ============================================================================

void test_remoteconsole_memory_stability_single_connect(void) {
    using namespace DomoticsCore::Testing;
    HeapTracker tracker;

    RemoteConsoleConfig config;
    config.enabled = true;
    config.port = 2324;

    auto console = std::make_unique<RemoteConsoleComponent>(config);
    RemoteConsoleComponent* consolePtr = console.get();
    testCore->addComponent(std::move(console));
    testCore->begin();

    // Warm up: setPort cycle to stabilize allocator
    consolePtr->setPort(2325);
    consolePtr->setPort(2326);

    tracker.checkpoint("before");

    // Single cycle: setPort triggers clients.clear() + clientBuffers.clear() + shrink_to_fit()
    consolePtr->setPort(2327);

    tracker.checkpoint("after");

    MemoryTestResult result = tracker.assertStable("before", "after", 512);
    TEST_ASSERT_TRUE_MESSAGE(result.passed, result.message.c_str());
}

void test_remoteconsole_memory_stability_multi_connect(void) {
    using namespace DomoticsCore::Testing;
    HeapTracker tracker;

    RemoteConsoleConfig config;
    config.enabled = true;
    config.port = 2330;

    auto console = std::make_unique<RemoteConsoleComponent>(config);
    RemoteConsoleComponent* consolePtr = console.get();
    testCore->addComponent(std::move(console));
    testCore->begin();

    // Warm up
    consolePtr->setPort(2331);

    tracker.checkpoint("before");

    // 10 cycles of setPort (exercises clients.clear + shrink_to_fit + server recreation)
    for (int i = 0; i < 10; i++) {
        consolePtr->setPort(2340 + i);
    }

    tracker.checkpoint("after");

    // Tolerance 2048: setPort() creates/deletes NetworkServer objects (new/delete)
    // causing glibc allocator overhead on native platform. NetworkClient uses
    // shared_ptr for state, increasing per-client allocation.
    MemoryTestResult result = tracker.assertStable("before", "after", 2048);
    TEST_ASSERT_TRUE_MESSAGE(result.passed, result.message.c_str());
}

// ============================================================================
// Test Runner
// ============================================================================

int main(int argc, char **argv) {
    UNITY_BEGIN();

    // Component creation tests
    RUN_TEST(test_remoteconsole_tracks_generic_provider_addresses);
    RUN_TEST(test_remoteconsole_component_creation_default);
    RUN_TEST(test_remoteconsole_component_creation_with_config);

    // Config tests
    RUN_TEST(test_remoteconsole_config_defaults);
    RUN_TEST(test_remoteconsole_config_custom);

    // Lifecycle tests
    RUN_TEST(test_remoteconsole_begin_enabled);
    RUN_TEST(test_remoteconsole_begin_disabled);
    RUN_TEST(test_remoteconsole_full_lifecycle);
    RUN_TEST(test_remoteconsole_shutdown_returns_success);

    // Dependencies tests
    RUN_TEST(test_remoteconsole_no_dependencies);

    // Configuration tests
    RUN_TEST(test_remoteconsole_port_config);
    RUN_TEST(test_remoteconsole_buffer_size_config);
    RUN_TEST(test_remoteconsole_max_clients_config);
    RUN_TEST(test_remoteconsole_authentication_config);
    RUN_TEST(test_remoteconsole_log_level_config);

    // Edge cases tests
    RUN_TEST(test_remoteconsole_zero_buffer_size);
    RUN_TEST(test_remoteconsole_zero_max_clients);
    RUN_TEST(test_remoteconsole_multiple_config_changes);
    RUN_TEST(test_remoteconsole_empty_password);
    RUN_TEST(test_remoteconsole_color_output_disabled);
    RUN_TEST(test_remoteconsole_commands_disabled);

    // Authentication tests (R21)
    RUN_TEST(test_remoteconsole_auth_config_defaults);
    RUN_TEST(test_remoteconsole_auth_config_custom);
    RUN_TEST(test_remoteconsole_auth_timeout_zero_means_no_timeout);
    RUN_TEST(test_remoteconsole_auth_require_with_password);
    RUN_TEST(test_remoteconsole_allow_commands_false_lifecycle);
    RUN_TEST(test_remoteconsole_auth_and_commands_disabled);
    RUN_TEST(test_remoteconsole_auth_protocol_flow);

    // Memory leak tests
    RUN_TEST(test_remoteconsole_log_buffer_no_memory_leak);
    RUN_TEST(test_remoteconsole_rapid_buffer_cycles_no_leak);

    // Memory stability tests (R4 shrink_to_fit)
    RUN_TEST(test_remoteconsole_memory_stability_single_connect);
    RUN_TEST(test_remoteconsole_memory_stability_multi_connect);

    return UNITY_END();
}
