/**
 * @file test_ntp_component.cpp
 * @brief Native unit tests for NTP component
 *
 * Tests cover:
 * - Events (NTPEvents)
 * - Component creation and configuration
 * - Config get/set
 * - Timezone presets
 * - Statistics
 * - Sync status
 * - Non-blocking behavior
 */

#include <unity.h>
#include <DomoticsCore/Core.h>
#include <DomoticsCore/NTP.h>
#include <DomoticsCore/NTPEvents.h>
#include <DomoticsCore/NetworkEvents.h>
#include <DomoticsCore/Testing/HeapTracker.h>

using namespace DomoticsCore;
using namespace DomoticsCore::Components;
using namespace DomoticsCore::Testing;

void test_ntp_waits_for_generic_network_readiness() {
    Core core;
    auto ntp = std::make_unique<NTPComponent>();
    NTPComponent* ntpPtr = ntp.get();
    core.addComponent(std::move(ntp));
    TEST_ASSERT_TRUE(core.begin());
    TEST_ASSERT_FALSE(ntpPtr->isClientStarted());

    NetworkEvents::NetworkAvailabilityEvent available{true};
    core.getEventBus().publishSticky(NetworkEvents::EVENT_READY, available);
    core.loop();
    TEST_ASSERT_TRUE(ntpPtr->isClientStarted());
}

// ============================================================================
// Event Tests
// ============================================================================

void test_ntp_events_constants_defined() {
    // Verify event constants are defined and have expected values
    TEST_ASSERT_NOT_NULL(NTPEvents::EVENT_SYNCED);
    TEST_ASSERT_NOT_NULL(NTPEvents::EVENT_SYNC_FAILED);

    TEST_ASSERT_EQUAL_STRING("ntp/synced", NTPEvents::EVENT_SYNCED);
    TEST_ASSERT_EQUAL_STRING("ntp/sync_failed", NTPEvents::EVENT_SYNC_FAILED);
}

// ============================================================================
// Component Creation Tests
// ============================================================================

void test_ntp_component_creation_default() {
    NTPComponent ntp;

    TEST_ASSERT_EQUAL_STRING("NTP", ntp.metadata.name);
    TEST_ASSERT_EQUAL_STRING("DomoticsCore", ntp.metadata.author);
}

void test_ntp_component_creation_with_config() {
    NTPConfig config;
    config.servers = {"pool.ntp.org", "time.google.com"};
    config.timezone = "CET-1CEST,M3.5.0,M10.5.0/3";
    config.syncInterval = 3600;

    NTPComponent ntp(config);

    TEST_ASSERT_EQUAL_STRING("NTP", ntp.metadata.name);

    const NTPConfig& cfg = ntp.getConfig();
    TEST_ASSERT_EQUAL_STRING("CET-1CEST,M3.5.0,M10.5.0/3", cfg.timezone.c_str());
    TEST_ASSERT_EQUAL_UINT32(3600, cfg.syncInterval);
}

// ============================================================================
// Config Tests
// ============================================================================

void test_ntp_config_defaults() {
    NTPConfig config;

    TEST_ASSERT_TRUE(config.enabled);
    TEST_ASSERT_EQUAL_UINT32(3600, config.syncInterval);
    TEST_ASSERT_EQUAL_STRING("UTC0", config.timezone.c_str());
    TEST_ASSERT_EQUAL_UINT32(5000, config.timeoutMs);
    TEST_ASSERT_EQUAL(3, config.servers.size());
}

void test_ntp_config_get_set() {
    NTPComponent ntp;

    NTPConfig newConfig;
    newConfig.timezone = "PST8PDT,M3.2.0,M11.1.0";
    newConfig.syncInterval = 7200;
    newConfig.enabled = false;

    ntp.setConfig(newConfig);

    const NTPConfig& cfg = ntp.getConfig();
    TEST_ASSERT_EQUAL_STRING("PST8PDT,M3.2.0,M11.1.0", cfg.timezone.c_str());
    TEST_ASSERT_EQUAL_UINT32(7200, cfg.syncInterval);
    TEST_ASSERT_FALSE(cfg.enabled);
}

void test_ntp_timezone_config() {
    NTPConfig config;
    config.timezone = "CET-1CEST,M3.5.0,M10.5.0/3";

    NTPComponent ntp(config);

    const NTPConfig& currentConfig = ntp.getConfig();
    TEST_ASSERT_EQUAL_STRING("CET-1CEST,M3.5.0,M10.5.0/3", currentConfig.timezone.c_str());
}

// ============================================================================
// Timezone Presets Tests
// ============================================================================

void test_ntp_timezone_presets() {
    TEST_ASSERT_EQUAL_STRING("UTC0", Timezones::UTC);
    TEST_ASSERT_EQUAL_STRING("EST5EDT,M3.2.0,M11.1.0", Timezones::EST);
    TEST_ASSERT_EQUAL_STRING("CST6CDT,M3.2.0,M11.1.0", Timezones::CST);
    TEST_ASSERT_EQUAL_STRING("MST7MDT,M3.2.0,M11.1.0", Timezones::MST);
    TEST_ASSERT_EQUAL_STRING("PST8PDT,M3.2.0,M11.1.0", Timezones::PST);
    TEST_ASSERT_EQUAL_STRING("CET-1CEST,M3.5.0,M10.5.0/3", Timezones::CET);
    TEST_ASSERT_EQUAL_STRING("GMT0", Timezones::GMT);
    TEST_ASSERT_EQUAL_STRING("JST-9", Timezones::JST);
}

// ============================================================================
// Sync Status Tests
// ============================================================================

void test_ntp_initial_sync_status() {
    NTPComponent ntp;

    // Without network/time sync, should not be synced
    // Note: In native tests, time(nullptr) returns a stub value
    bool synced = ntp.isSynced();
    TEST_ASSERT_FALSE(synced);
}

void test_ntp_sync_now_disabled() {
    NTPConfig config;
    config.enabled = false;

    NTPComponent ntp(config);

    // syncNow should return false when disabled
    bool result = ntp.syncNow();
    TEST_ASSERT_FALSE(result);
}

void test_ntp_sync_now_enabled() {
    NTPConfig config;
    config.enabled = true;

    NTPComponent ntp(config);
    ntp.begin();

    // syncNow should return true when enabled (initiates sync)
    bool result = ntp.syncNow();
    TEST_ASSERT_TRUE(result);

    // Second call should return false (sync already in progress)
    result = ntp.syncNow();
    TEST_ASSERT_FALSE(result);

    ntp.shutdown();
}

// ============================================================================
// Statistics Tests
// ============================================================================

void test_ntp_statistics_initial() {
    NTPComponent ntp;

    const NTPStatistics& stats = ntp.getStatistics();

    TEST_ASSERT_EQUAL_UINT32(0, stats.syncCount);
    TEST_ASSERT_EQUAL_UINT32(0, stats.syncErrors);
    TEST_ASSERT_EQUAL(0, stats.lastSyncTime);
    TEST_ASSERT_EQUAL_UINT32(0, stats.lastSyncDuration);
    TEST_ASSERT_EQUAL_UINT32(0, stats.consecutiveFailures);
}

// ============================================================================
// Lifecycle Tests
// ============================================================================

void test_ntp_begin_returns_success() {
    NTPComponent ntp;

    ComponentStatus status = ntp.begin();
    TEST_ASSERT_EQUAL(ComponentStatus::Success, status);

    ntp.shutdown();
}

void test_ntp_begin_disabled_returns_success() {
    NTPConfig config;
    config.enabled = false;

    NTPComponent ntp(config);

    ComponentStatus status = ntp.begin();
    TEST_ASSERT_EQUAL(ComponentStatus::Success, status);
}

void test_ntp_shutdown_returns_success() {
    NTPComponent ntp;
    ntp.begin();

    ComponentStatus status = ntp.shutdown();
    TEST_ASSERT_EQUAL(ComponentStatus::Success, status);
}

void test_ntp_full_lifecycle() {
    Core core;

    NTPConfig config;
    config.servers.clear();  // No server = don't try to sync

    auto ntp = std::make_unique<NTPComponent>(config);

    core.addComponent(std::move(ntp));

    bool beginResult = core.begin();
    TEST_ASSERT_TRUE(beginResult);

    // Run a few loops
    for (int i = 0; i < 10; i++) {
        core.loop();
    }

    core.shutdown();
}

// ============================================================================
// Non-blocking Tests
// ============================================================================

void test_ntp_loop_non_blocking() {
    Core core;

    NTPConfig config;
    config.servers.clear();  // No server = don't try to sync

    auto ntp = std::make_unique<NTPComponent>(config);

    core.addComponent(std::move(ntp));
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

void test_ntp_uses_nonblocking_delay() {
    Core core;

    NTPConfig config;
    config.syncInterval = 1000;  // 1 second interval

    auto ntp = std::make_unique<NTPComponent>(config);

    core.addComponent(std::move(ntp));
    core.begin();

    // Run multiple loops - should not block
    unsigned long start = HAL::Platform::getMillis();
    int iterations = 0;

    while (HAL::Platform::getMillis() - start < 50) {
        core.loop();
        iterations++;
    }

    // Should complete many iterations without blocking
    TEST_ASSERT_GREATER_THAN(10, iterations);

    core.shutdown();
}

// ============================================================================
// Time Methods Tests
// ============================================================================

void test_ntp_get_timezone() {
    NTPConfig config;
    config.timezone = "EST5EDT,M3.2.0,M11.1.0";

    NTPComponent ntp(config);

    TEST_ASSERT_EQUAL_STRING("EST5EDT,M3.2.0,M11.1.0", ntp.getTimezone().c_str());
}

void test_ntp_set_timezone() {
    NTPComponent ntp;

    ntp.setTimezone("JST-9");

    TEST_ASSERT_EQUAL_STRING("JST-9", ntp.getTimezone().c_str());
}

void test_ntp_get_formatted_time_not_synced() {
    NTPComponent ntp;

    // Without sync, should return "Not synced"
    String formatted = ntp.getFormattedTime();
    TEST_ASSERT_EQUAL_STRING("Not synced", formatted.c_str());
}

void test_ntp_get_iso8601_not_synced() {
    NTPComponent ntp;

    // Without sync, should return "Not synced"
    String iso = ntp.getISO8601();
    TEST_ASSERT_EQUAL_STRING("Not synced", iso.c_str());
}

void test_ntp_get_unix_time() {
    NTPComponent ntp;

    // Should return time_t value (from system)
    time_t unixTime = ntp.getUnixTime();
    TEST_ASSERT_TRUE(unixTime >= 0);
}

// ============================================================================
// Uptime Tests
// ============================================================================

void test_ntp_get_uptime_ms() {
    NTPComponent ntp;

    uint64_t uptime = ntp.getUptimeMs();
    TEST_ASSERT_TRUE(uptime >= 0);
}

void test_ntp_get_formatted_uptime() {
    NTPComponent ntp;

    String uptime = ntp.getFormattedUptime();
    TEST_ASSERT_TRUE(uptime.length() > 0);
    // Should contain 's' for seconds at minimum
    TEST_ASSERT_TRUE(uptime.indexOf('s') >= 0);
}

// ============================================================================
// Callback Tests
// ============================================================================

void test_ntp_on_sync_callback() {
    NTPComponent ntp;

    bool callbackCalled = false;
    bool callbackSuccess = false;

    ntp.onSync([&](bool success) {
        callbackCalled = true;
        callbackSuccess = success;
    });

    // Callback should be registered but not called yet
    TEST_ASSERT_FALSE(callbackCalled);
}

// ============================================================================
// Next Sync Tests
// ============================================================================

void test_ntp_get_next_sync_in_not_synced() {
    NTPComponent ntp;

    // When not synced, should return 0
    uint32_t nextSync = ntp.getNextSyncIn();
    TEST_ASSERT_EQUAL_UINT32(0, nextSync);
}

void test_ntp_get_next_sync_in_disabled() {
    NTPConfig config;
    config.enabled = false;

    NTPComponent ntp(config);

    uint32_t nextSync = ntp.getNextSyncIn();
    TEST_ASSERT_EQUAL_UINT32(0, nextSync);
}

// ============================================================================
// Config Update Tests
// ============================================================================

void test_ntp_config_servers_update() {
    NTPComponent ntp;

    NTPConfig newConfig;
    newConfig.servers = {"custom.ntp.org"};

    ntp.setConfig(newConfig);

    const NTPConfig& cfg = ntp.getConfig();
    TEST_ASSERT_EQUAL(1, cfg.servers.size());
    TEST_ASSERT_EQUAL_STRING("custom.ntp.org", cfg.servers[0].c_str());
}

void test_ntp_config_timeout_update() {
    NTPComponent ntp;

    NTPConfig newConfig;
    newConfig.timeoutMs = 10000;

    ntp.setConfig(newConfig);

    const NTPConfig& cfg = ntp.getConfig();
    TEST_ASSERT_EQUAL_UINT32(10000, cfg.timeoutMs);
}

// ============================================================================
// Edge Cases
// ============================================================================

void test_ntp_empty_servers() {
    NTPConfig config;
    config.servers.clear();

    NTPComponent ntp(config);

    ComponentStatus status = ntp.begin();
    TEST_ASSERT_EQUAL(ComponentStatus::Success, status);

    ntp.shutdown();
}

void test_ntp_multiple_timezone_changes() {
    NTPComponent ntp;

    ntp.setTimezone("UTC0");
    TEST_ASSERT_EQUAL_STRING("UTC0", ntp.getTimezone().c_str());

    ntp.setTimezone("CET-1CEST,M3.5.0,M10.5.0/3");
    TEST_ASSERT_EQUAL_STRING("CET-1CEST,M3.5.0,M10.5.0/3", ntp.getTimezone().c_str());

    ntp.setTimezone("JST-9");
    TEST_ASSERT_EQUAL_STRING("JST-9", ntp.getTimezone().c_str());
}

void test_ntp_component_no_dependencies() {
    NTPComponent ntp;

    auto deps = ntp.getDependencies();
    TEST_ASSERT_EQUAL(0, deps.size());
}

// ============================================================================
// Memory Leak Detection Tests (HeapTracker)
// ============================================================================

void test_ntp_memory_stability_lifecycle() {
    HeapTracker tracker;
    
    tracker.checkpoint("before");
    
    for (int i = 0; i < 5; i++) {
        NTPComponent ntp;
        NTPConfig config;
        config.servers = {"pool.ntp.org"};
        config.timezone = "UTC0";
        ntp.setConfig(config);
    }
    
    tracker.checkpoint("after");
    
    MemoryTestResult result = tracker.assertStable("before", "after", 512);
    TEST_ASSERT_TRUE_MESSAGE(result.passed, result.message.c_str());
}

void test_ntp_memory_stability_config_changes() {
    HeapTracker tracker;
    NTPComponent ntp;
    
    tracker.checkpoint("baseline");
    
    for (int i = 0; i < 10; i++) {
        NTPConfig config;
        config.servers = {"server" + String(i) + ".ntp.org"};
        config.timezone = "CET-1";
        ntp.setConfig(config);
    }
    
    MemoryTestResult result = tracker.assertNoGrowth("baseline", 256);
    TEST_ASSERT_TRUE_MESSAGE(result.passed, result.message.c_str());
}

// ============================================================================
// Test Runner
// ============================================================================

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();

    // Event tests
    RUN_TEST(test_ntp_events_constants_defined);

    // Component creation tests
    RUN_TEST(test_ntp_component_creation_default);
    RUN_TEST(test_ntp_component_creation_with_config);

    // Config tests
    RUN_TEST(test_ntp_config_defaults);
    RUN_TEST(test_ntp_config_get_set);
    RUN_TEST(test_ntp_timezone_config);

    // Timezone presets tests
    RUN_TEST(test_ntp_timezone_presets);

    // Sync status tests
    RUN_TEST(test_ntp_initial_sync_status);
    RUN_TEST(test_ntp_sync_now_disabled);
    RUN_TEST(test_ntp_sync_now_enabled);

    // Statistics tests
    RUN_TEST(test_ntp_statistics_initial);

    // Lifecycle tests
    RUN_TEST(test_ntp_waits_for_generic_network_readiness);
    RUN_TEST(test_ntp_begin_returns_success);
    RUN_TEST(test_ntp_begin_disabled_returns_success);
    RUN_TEST(test_ntp_shutdown_returns_success);
    RUN_TEST(test_ntp_full_lifecycle);

    // Non-blocking tests
    RUN_TEST(test_ntp_loop_non_blocking);
    RUN_TEST(test_ntp_uses_nonblocking_delay);

    // Time methods tests
    RUN_TEST(test_ntp_get_timezone);
    RUN_TEST(test_ntp_set_timezone);
    RUN_TEST(test_ntp_get_formatted_time_not_synced);
    RUN_TEST(test_ntp_get_iso8601_not_synced);
    RUN_TEST(test_ntp_get_unix_time);

    // Uptime tests
    RUN_TEST(test_ntp_get_uptime_ms);
    RUN_TEST(test_ntp_get_formatted_uptime);

    // Callback tests
    RUN_TEST(test_ntp_on_sync_callback);

    // Next sync tests
    RUN_TEST(test_ntp_get_next_sync_in_not_synced);
    RUN_TEST(test_ntp_get_next_sync_in_disabled);

    // Config update tests
    RUN_TEST(test_ntp_config_servers_update);
    RUN_TEST(test_ntp_config_timeout_update);

    // Edge cases
    RUN_TEST(test_ntp_empty_servers);
    RUN_TEST(test_ntp_multiple_timezone_changes);
    RUN_TEST(test_ntp_component_no_dependencies);

    // Memory leak detection tests
    RUN_TEST(test_ntp_memory_stability_lifecycle);
    RUN_TEST(test_ntp_memory_stability_config_changes);

    return UNITY_END();
}
