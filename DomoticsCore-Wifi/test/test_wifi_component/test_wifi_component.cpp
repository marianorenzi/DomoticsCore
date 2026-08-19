// Unit tests for WifiComponent
// Tests: events, config, modes, lifecycle, INetworkProvider interface, edge cases
// Includes HeapTracker integration for memory leak detection

#include <unity.h>
#include <DomoticsCore/Core.h>
#include <DomoticsCore/Wifi.h>
#include <DomoticsCore/WifiEvents.h>
#include <DomoticsCore/Network.h>
#include <DomoticsCore/Testing/HeapTracker.h>

using namespace DomoticsCore;
using namespace DomoticsCore::Components;
using namespace DomoticsCore::Testing;

// Test state
static Core* testCore = nullptr;
static bool staConnectedReceived = false;
static bool staConnectedValue = false;
static bool apEnabledReceived = false;
static bool apEnabledValue = false;

void setUp(void) {
    testCore = new Core();
    staConnectedReceived = false;
    staConnectedValue = false;
    apEnabledReceived = false;
    apEnabledValue = false;
}

void tearDown(void) {
    if (testCore) {
        testCore->shutdown();
        delete testCore;
        testCore = nullptr;
    }
}

// ============================================================================
// WifiEvents Constants Tests
// ============================================================================

void test_wifi_events_constants_defined(void) {
    // Verify event constants are properly defined
    TEST_ASSERT_NOT_NULL(WifiEvents::EVENT_STA_CONNECTED);
    TEST_ASSERT_NOT_NULL(WifiEvents::EVENT_AP_ENABLED);

    // Verify they have expected values
    TEST_ASSERT_EQUAL_STRING("wifi/sta/connected", WifiEvents::EVENT_STA_CONNECTED);
    TEST_ASSERT_EQUAL_STRING("wifi/ap/enabled", WifiEvents::EVENT_AP_ENABLED);
}

// ============================================================================
// WifiComponent Creation Tests
// ============================================================================

void test_wifi_component_creation_default(void) {
    auto wifi = std::make_unique<WifiComponent>();

    TEST_ASSERT_EQUAL_STRING("Wifi", wifi->metadata.name);
    TEST_ASSERT_EQUAL_STRING("1.5.1", wifi->metadata.version);
}

void test_wifi_component_creation_with_credentials(void) {
    auto wifi = std::make_unique<WifiComponent>("TestSSID", "TestPassword");

    TEST_ASSERT_EQUAL_STRING("Wifi", wifi->metadata.name);
    TEST_ASSERT_EQUAL_STRING("TestSSID", wifi->getConfiguredSSID().c_str());
}

// ============================================================================
// WifiConfig Tests
// ============================================================================

void test_wifi_config_defaults(void) {
    WifiConfig config;

    TEST_ASSERT_TRUE(config.ssid.isEmpty());
    TEST_ASSERT_TRUE(config.password.isEmpty());
    TEST_ASSERT_TRUE(config.autoConnect);
    TEST_ASSERT_FALSE(config.enableAP);
    TEST_ASSERT_TRUE(config.apSSID.isEmpty());
    TEST_ASSERT_TRUE(config.apPassword.isEmpty());
    TEST_ASSERT_EQUAL_UINT32(5000, config.reconnectInterval);
    TEST_ASSERT_EQUAL_UINT32(15000, config.connectionTimeout);
}

void test_wifi_config_get_set(void) {
    auto wifi = std::make_unique<WifiComponent>();

    WifiConfig config;
    config.ssid = "MyNetwork";
    config.password = "MyPassword";
    config.autoConnect = true;
    config.enableAP = true;
    config.apSSID = "MyAP";
    config.apPassword = "APPassword";

    wifi->setConfig(config);

    WifiConfig retrieved = wifi->getConfig();
    TEST_ASSERT_EQUAL_STRING("MyNetwork", retrieved.ssid.c_str());
    TEST_ASSERT_EQUAL_STRING("MyPassword", retrieved.password.c_str());
    TEST_ASSERT_TRUE(retrieved.autoConnect);
    TEST_ASSERT_TRUE(retrieved.enableAP);
    TEST_ASSERT_EQUAL_STRING("MyAP", retrieved.apSSID.c_str());
    TEST_ASSERT_EQUAL_STRING("APPassword", retrieved.apPassword.c_str());
}

// ============================================================================
// WiFi Component State Tests
// ============================================================================

void test_wifi_component_initial_state(void) {
    auto wifi = std::make_unique<WifiComponent>();

    // On stub platform, WiFi is never connected
    TEST_ASSERT_FALSE(wifi->isSTAConnected());
    TEST_ASSERT_FALSE(wifi->isConnectionInProgress());
}

void test_wifi_component_ap_only_mode(void) {
    // Empty SSID = AP-only mode
    auto wifi = std::make_unique<WifiComponent>("", "");
    WifiComponent* wifiPtr = wifi.get();

    testCore->addComponent(std::move(wifi));
    testCore->begin();

    // On stub, AP operations return false but state flags should be set
    // The component should initialize successfully in AP mode
    TEST_ASSERT_EQUAL(ComponentStatus::Success, wifiPtr->getLastStatus());
}

// ============================================================================
// WiFi Event Emission Tests
// ============================================================================

void test_wifi_ap_enabled_event_on_enable(void) {
    // Subscribe to AP enabled event
    testCore->getEventBus().subscribe(WifiEvents::EVENT_AP_ENABLED, [](const void* payload) {
        apEnabledReceived = true;
        if (payload) {
            apEnabledValue = *static_cast<const bool*>(payload);
        }
    });

    auto wifi = std::make_unique<WifiComponent>();
    WifiComponent* wifiPtr = wifi.get();

    testCore->addComponent(std::move(wifi));
    testCore->begin();
    testCore->loop();

    // Enable AP mode - should emit EVENT_AP_ENABLED
    wifiPtr->enableAP("TestAP", "password123", true);
    testCore->loop();

    // On stub platform, startAP returns false, so event may not be emitted
    // But the component should not crash
    TEST_ASSERT_TRUE(wifiPtr->isAPEnabled()); // Internal flag should be set
}

void test_wifi_credentials_update(void) {
    auto wifi = std::make_unique<WifiComponent>();
    WifiComponent* wifiPtr = wifi.get();

    testCore->addComponent(std::move(wifi));
    testCore->begin();

    // Update credentials
    wifiPtr->setCredentials("NewSSID", "NewPassword", false);

    TEST_ASSERT_EQUAL_STRING("NewSSID", wifiPtr->getConfiguredSSID().c_str());
}

void test_wifi_network_info_json(void) {
    auto wifi = std::make_unique<WifiComponent>("TestNet", "TestPass");
    WifiComponent* wifiPtr = wifi.get();

    testCore->addComponent(std::move(wifi));
    testCore->begin();

    String info = wifiPtr->getNetworkInfo();

    // Should return valid JSON
    TEST_ASSERT_TRUE(info.length() > 0);
    TEST_ASSERT_TRUE(info.indexOf("type") >= 0);
    TEST_ASSERT_TRUE(info.indexOf("Wifi") >= 0);
}

void test_wifi_network_type(void) {
    auto wifi = std::make_unique<WifiComponent>();

    TEST_ASSERT_EQUAL_STRING("Wifi", wifi->getNetworkType().c_str());
}

void test_wifi_disconnect_reconnect(void) {
    auto wifi = std::make_unique<WifiComponent>("TestSSID", "TestPass");
    WifiComponent* wifiPtr = wifi.get();

    testCore->addComponent(std::move(wifi));
    testCore->begin();

    // Disconnect should not crash
    wifiPtr->disconnect();

    // Reconnect should not crash
    wifiPtr->reconnect();

    // Component should still be valid
    TEST_ASSERT_EQUAL_STRING("Wifi", wifiPtr->metadata.name);
}

void test_wifi_scan_async(void) {
    auto wifi = std::make_unique<WifiComponent>();
    WifiComponent* wifiPtr = wifi.get();

    testCore->addComponent(std::move(wifi));
    testCore->begin();

    // Start async scan - should not crash on stub
    wifiPtr->startScanAsync();

    // Get scan summary - on stub it will show "Scanning..."
    String summary = wifiPtr->getLastScanSummary();
    TEST_ASSERT_TRUE(summary.length() > 0);
}

// ============================================================================
// INetworkProvider Interface Tests
// ============================================================================

void test_wifi_inetworkprovider_isconnected(void) {
    auto wifi = std::make_unique<WifiComponent>();

    // On stub, isConnected should return false (no STA) but may return true if AP enabled
    TEST_ASSERT_FALSE(wifi->isConnected()); // Initially no connectivity
}

void test_wifi_inetworkprovider_getlocalip(void) {
    auto wifi = std::make_unique<WifiComponent>();
    WifiComponent* wifiPtr = wifi.get();

    testCore->addComponent(std::move(wifi));
    testCore->begin();

    String ip = wifiPtr->getLocalIP();
    // On stub, returns "0.0.0.0"
    TEST_ASSERT_TRUE(ip.length() > 0);
}

void test_wifi_inetworkprovider_getconnectionstatus(void) {
    auto wifi = std::make_unique<WifiComponent>();
    WifiComponent* wifiPtr = wifi.get();

    testCore->addComponent(std::move(wifi));
    testCore->begin();

    String status = wifiPtr->getConnectionStatus();
    TEST_ASSERT_TRUE(status.length() > 0);
}

void test_wifi_publishes_provider_address_event(void) {
    bool received = false;
    String providerId;
    String address;
    testCore->on<NetworkEvents::NetworkProviderAddressEvent>(
        NetworkEvents::EVENT_PROVIDER_ADDRESS_CHANGED,
        [&](const NetworkEvents::NetworkProviderAddressEvent& event) {
            received = true;
            providerId = event.providerId;
            address = event.address;
        });

    testCore->addComponent(std::make_unique<NetworkComponent>());
    auto wifi = std::make_unique<WifiComponent>();
    wifi->enableAP("AddressEventTest", "");
    testCore->addComponent(std::move(wifi));
    TEST_ASSERT_TRUE(testCore->begin());
    for (int i = 0; i < 4; ++i) testCore->loop();

    TEST_ASSERT_TRUE(received);
    TEST_ASSERT_EQUAL_STRING("wifi", providerId.c_str());
    TEST_ASSERT_EQUAL_STRING("0.0.0.0", address.c_str());
}

// ============================================================================
// Mode Detection Tests
// ============================================================================

void test_wifi_mode_detection_initial(void) {
    auto wifi = std::make_unique<WifiComponent>("TestSSID", "TestPass");

    // Before begin, should not be in AP mode
    TEST_ASSERT_FALSE(wifi->isAPMode());
    TEST_ASSERT_FALSE(wifi->isSTAAPMode());
}

void test_wifi_has_connectivity(void) {
    auto wifi = std::make_unique<WifiComponent>();

    // On stub, no connectivity initially
    TEST_ASSERT_FALSE(wifi->hasConnectivity());
}

void test_wifi_is_wifi_enabled(void) {
    auto wifi = std::make_unique<WifiComponent>();

    // WiFi should be enabled by default
    TEST_ASSERT_TRUE(wifi->isWifiEnabled());
}

void test_wifi_is_ap_enabled_initial(void) {
    auto wifi = std::make_unique<WifiComponent>();

    // AP should be disabled by default
    TEST_ASSERT_FALSE(wifi->isAPEnabled());
}

// ============================================================================
// Mode Switching Tests
// ============================================================================

void test_wifi_enable_disable_wifi(void) {
    auto wifi = std::make_unique<WifiComponent>("TestSSID", "TestPass");
    WifiComponent* wifiPtr = wifi.get();

    testCore->addComponent(std::move(wifi));
    testCore->begin();

    // Disable WiFi
    wifiPtr->enableWifi(false);
    TEST_ASSERT_FALSE(wifiPtr->isWifiEnabled());

    // Re-enable WiFi
    wifiPtr->enableWifi(true);
    TEST_ASSERT_TRUE(wifiPtr->isWifiEnabled());
}

void test_wifi_enable_ap_with_ssid(void) {
    auto wifi = std::make_unique<WifiComponent>("TestSSID", "TestPass");
    WifiComponent* wifiPtr = wifi.get();

    testCore->addComponent(std::move(wifi));
    testCore->begin();

    // Enable AP
    wifiPtr->enableAP("MyAccessPoint", "appassword", true);

    TEST_ASSERT_TRUE(wifiPtr->isAPEnabled());
    TEST_ASSERT_EQUAL_STRING("MyAccessPoint", wifiPtr->getAPSSID().c_str());
}

void test_wifi_disable_ap(void) {
    auto wifi = std::make_unique<WifiComponent>();
    WifiComponent* wifiPtr = wifi.get();

    testCore->addComponent(std::move(wifi));
    testCore->begin();

    // Enable then disable AP
    wifiPtr->enableAP("TestAP", "", true);
    TEST_ASSERT_TRUE(wifiPtr->isAPEnabled());

    wifiPtr->disableAP();
    TEST_ASSERT_FALSE(wifiPtr->isAPEnabled());
}

// ============================================================================
// Lifecycle Tests
// ============================================================================

void test_wifi_full_lifecycle(void) {
    auto wifi = std::make_unique<WifiComponent>("TestSSID", "TestPass");
    WifiComponent* wifiPtr = wifi.get();

    testCore->addComponent(std::move(wifi));

    // Begin
    testCore->begin();
    TEST_ASSERT_EQUAL(ComponentStatus::Success, wifiPtr->getLastStatus());

    // Multiple loops should not crash
    for (int i = 0; i < 10; i++) {
        testCore->loop();
    }

    // Shutdown
    testCore->shutdown();
}

void test_wifi_shutdown_returns_success(void) {
    auto wifi = std::make_unique<WifiComponent>();
    WifiComponent* wifiPtr = wifi.get();

    testCore->addComponent(std::move(wifi));
    testCore->begin();

    ComponentStatus status = wifiPtr->shutdown();
    TEST_ASSERT_EQUAL(ComponentStatus::Success, status);
}

void test_wifi_no_dependencies(void) {
    auto wifi = std::make_unique<WifiComponent>();

    auto deps = wifi->getDependencies();
    TEST_ASSERT_EQUAL(0, deps.size());
}

// ============================================================================
// Status Methods Tests
// ============================================================================

void test_wifi_get_detailed_status(void) {
    auto wifi = std::make_unique<WifiComponent>("TestSSID", "TestPass");
    WifiComponent* wifiPtr = wifi.get();

    testCore->addComponent(std::move(wifi));
    testCore->begin();

    String status = wifiPtr->getDetailedStatus();
    TEST_ASSERT_TRUE(status.length() > 0);
    TEST_ASSERT_TRUE(status.indexOf("Wifi Status") >= 0);
}

void test_wifi_get_ap_info_json(void) {
    auto wifi = std::make_unique<WifiComponent>();
    WifiComponent* wifiPtr = wifi.get();

    testCore->addComponent(std::move(wifi));
    testCore->begin();

    String info = wifiPtr->getAPInfo();
    TEST_ASSERT_TRUE(info.length() > 0);
    TEST_ASSERT_TRUE(info.indexOf("active") >= 0);
}

void test_wifi_get_mac_address(void) {
    auto wifi = std::make_unique<WifiComponent>();
    WifiComponent* wifiPtr = wifi.get();

    testCore->addComponent(std::move(wifi));
    testCore->begin();

    String mac = wifiPtr->getMacAddress();
    // On stub, returns "00:00:00:00:00:00"
    TEST_ASSERT_TRUE(mac.length() > 0);
    TEST_ASSERT_TRUE(mac.indexOf(":") >= 0);
}

void test_wifi_get_rssi(void) {
    auto wifi = std::make_unique<WifiComponent>();
    WifiComponent* wifiPtr = wifi.get();

    testCore->addComponent(std::move(wifi));
    testCore->begin();

    int32_t rssi = wifiPtr->getRSSI();
    // On stub, returns 0
    TEST_ASSERT_EQUAL_INT32(0, rssi);
}

void test_wifi_get_ssid_configured(void) {
    auto wifi = std::make_unique<WifiComponent>("ConfiguredSSID", "pass");

    TEST_ASSERT_EQUAL_STRING("ConfiguredSSID", wifi->getConfiguredSSID().c_str());
}

// ============================================================================
// Edge Cases Tests
// ============================================================================

void test_wifi_empty_ssid_starts_ap_mode(void) {
    auto wifi = std::make_unique<WifiComponent>("", "");
    WifiComponent* wifiPtr = wifi.get();

    testCore->addComponent(std::move(wifi));
    testCore->begin();

    // With empty SSID, component should initialize successfully in AP mode
    TEST_ASSERT_EQUAL(ComponentStatus::Success, wifiPtr->getLastStatus());
}

void test_wifi_config_multiple_updates(void) {
    auto wifi = std::make_unique<WifiComponent>();
    WifiComponent* wifiPtr = wifi.get();

    testCore->addComponent(std::move(wifi));
    testCore->begin();

    // Multiple config updates should not crash
    for (int i = 0; i < 5; i++) {
        WifiConfig config;
        config.ssid = "Network" + String(i);
        config.password = "Pass" + String(i);
        wifiPtr->setConfig(config);
    }

    WifiConfig final = wifiPtr->getConfig();
    TEST_ASSERT_EQUAL_STRING("Network4", final.ssid.c_str());
}

void test_wifi_credentials_with_reconnect(void) {
    auto wifi = std::make_unique<WifiComponent>();
    WifiComponent* wifiPtr = wifi.get();

    testCore->addComponent(std::move(wifi));
    testCore->begin();

    // setCredentials with reconnectNow=true should trigger connection attempt
    wifiPtr->setCredentials("NewNetwork", "NewPass", true);

    // On stub, connection will fail but component should be stable
    TEST_ASSERT_EQUAL_STRING("NewNetwork", wifiPtr->getConfiguredSSID().c_str());
    TEST_ASSERT_TRUE(wifiPtr->isConnectionInProgress());
}

void test_wifi_scan_networks_sync(void) {
    auto wifi = std::make_unique<WifiComponent>();
    WifiComponent* wifiPtr = wifi.get();

    testCore->addComponent(std::move(wifi));
    testCore->begin();

    std::vector<String> networks;
    bool result = wifiPtr->scanNetworks(networks);

    // On stub, scan returns 0 networks
    TEST_ASSERT_TRUE(result); // Should not fail
    TEST_ASSERT_EQUAL(0, networks.size());
}

void test_wifi_network_info_contains_all_fields(void) {
    auto wifi = std::make_unique<WifiComponent>("TestNet", "TestPass");
    WifiComponent* wifiPtr = wifi.get();

    testCore->addComponent(std::move(wifi));
    testCore->begin();

    String info = wifiPtr->getNetworkInfo();

    // Verify all expected fields are present
    TEST_ASSERT_TRUE(info.indexOf("\"type\"") >= 0);
    TEST_ASSERT_TRUE(info.indexOf("\"sta_connected\"") >= 0);
    TEST_ASSERT_TRUE(info.indexOf("\"ap_enabled\"") >= 0);
    TEST_ASSERT_TRUE(info.indexOf("\"ap_mode\"") >= 0);
}

// ============================================================================
// Memory Leak Detection Tests (HeapTracker)
// ============================================================================

void test_wifi_memory_stability_lifecycle() {
    HeapTracker tracker;
    
    tracker.checkpoint("before");
    
    // Create and destroy WiFi components multiple times
    for (int i = 0; i < 5; i++) {
        WifiComponent wifi;
        WifiConfig config;
        config.ssid = "TestNetwork";
        config.password = "TestPassword";
        wifi.setConfig(config);
        // Component destroyed at end of scope
    }
    
    tracker.checkpoint("after");
    
    MemoryTestResult result = tracker.assertStable("before", "after", 512);
    TEST_ASSERT_TRUE_MESSAGE(result.passed, result.message.c_str());
}

void test_wifi_memory_stability_config_changes() {
    HeapTracker tracker;
    WifiComponent wifi;
    
    tracker.checkpoint("baseline");
    
    // Perform many config changes
    for (int i = 0; i < 20; i++) {
        WifiConfig config;
        config.ssid = "Network" + String(i);
        config.password = "Pass" + String(i);
        wifi.setConfig(config);
    }
    
    MemoryTestResult result = tracker.assertNoGrowth("baseline", 256);
    TEST_ASSERT_TRUE_MESSAGE(result.passed, result.message.c_str());
}

// ============================================================================
// Test Runner
// ============================================================================

int main(int argc, char **argv) {
    UNITY_BEGIN();

    // Event constants tests
    RUN_TEST(test_wifi_events_constants_defined);

    // Component creation tests
    RUN_TEST(test_wifi_component_creation_default);
    RUN_TEST(test_wifi_component_creation_with_credentials);

    // Config tests
    RUN_TEST(test_wifi_config_defaults);
    RUN_TEST(test_wifi_config_get_set);

    // State tests
    RUN_TEST(test_wifi_component_initial_state);
    RUN_TEST(test_wifi_component_ap_only_mode);

    // Event and behavior tests
    RUN_TEST(test_wifi_ap_enabled_event_on_enable);
    RUN_TEST(test_wifi_credentials_update);
    RUN_TEST(test_wifi_network_info_json);
    RUN_TEST(test_wifi_network_type);
    RUN_TEST(test_wifi_disconnect_reconnect);
    RUN_TEST(test_wifi_scan_async);

    // INetworkProvider interface tests
    RUN_TEST(test_wifi_inetworkprovider_isconnected);
    RUN_TEST(test_wifi_inetworkprovider_getlocalip);
    RUN_TEST(test_wifi_inetworkprovider_getconnectionstatus);
    RUN_TEST(test_wifi_publishes_provider_address_event);

    // Mode detection tests
    RUN_TEST(test_wifi_mode_detection_initial);
    RUN_TEST(test_wifi_has_connectivity);
    RUN_TEST(test_wifi_is_wifi_enabled);
    RUN_TEST(test_wifi_is_ap_enabled_initial);

    // Mode switching tests
    RUN_TEST(test_wifi_enable_disable_wifi);
    RUN_TEST(test_wifi_enable_ap_with_ssid);
    RUN_TEST(test_wifi_disable_ap);

    // Lifecycle tests
    RUN_TEST(test_wifi_full_lifecycle);
    RUN_TEST(test_wifi_shutdown_returns_success);
    RUN_TEST(test_wifi_no_dependencies);

    // Status methods tests
    RUN_TEST(test_wifi_get_detailed_status);
    RUN_TEST(test_wifi_get_ap_info_json);
    RUN_TEST(test_wifi_get_mac_address);
    RUN_TEST(test_wifi_get_rssi);
    RUN_TEST(test_wifi_get_ssid_configured);

    // Edge cases tests
    RUN_TEST(test_wifi_empty_ssid_starts_ap_mode);
    RUN_TEST(test_wifi_config_multiple_updates);
    RUN_TEST(test_wifi_credentials_with_reconnect);
    RUN_TEST(test_wifi_scan_networks_sync);
    RUN_TEST(test_wifi_network_info_contains_all_fields);

    // Memory leak detection tests (HeapTracker)
    RUN_TEST(test_wifi_memory_stability_lifecycle);
    RUN_TEST(test_wifi_memory_stability_config_changes);

    return UNITY_END();
}
