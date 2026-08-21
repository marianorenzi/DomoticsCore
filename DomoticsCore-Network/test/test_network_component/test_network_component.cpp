#include <unity.h>
#include <DomoticsCore/Core.h>
#include <DomoticsCore/Network.h>
#include <DomoticsCore/NetworkWebUI.h>
#include <DomoticsCore/Storage.h>

using namespace DomoticsCore;
using namespace DomoticsCore::Components;

class TestProvider : public IComponent, public INetworkProvider {
public:
    explicit TestProvider(const char* id, bool connected = false)
        : id_(id), connected_(connected) {
        metadata.name = id;
        metadata.version = "1.0.0";
    }

    ComponentStatus begin() override { return ComponentStatus::Success; }
    void loop() override {}
    void afterAllComponentsReady() override { announce(); }
    ComponentStatus shutdown() override {
        NetworkEvents::NetworkProviderIdEvent event{};
        NetworkEvents::copyProviderId(event.providerId, id_);
        emit(NetworkEvents::EVENT_PROVIDER_UNREGISTERED, event);
        return ComponentStatus::Success;
    }
    const char* getProviderId() const override { return id_; }
    bool isConnected() const override { return connected_; }
    String getLocalIP() const override { return connected_ ? "192.0.2.1" : ""; }
    String getNetworkType() const override { return "Test"; }
    String getConnectionStatus() const override { return connected_ ? "Connected" : "Disconnected"; }
    String getNetworkInfo() const override { return "{}"; }
    bool setRoutePriority(int priority) override {
        routePriority_ = priority;
        routePriorityCalls_++;
        return true;
    }
    int routePriority() const { return routePriority_; }
    int routePriorityCalls() const { return routePriorityCalls_; }

    void setConnected(bool connected) {
        if (connected_ == connected) return;
        connected_ = connected;
        NetworkEvents::NetworkProviderStateEvent event{};
        NetworkEvents::copyProviderId(event.providerId, id_);
        event.connected = connected;
        emit(NetworkEvents::EVENT_PROVIDER_STATE_CHANGED, event);
    }

private:
    void announce() {
        NetworkEvents::NetworkProviderRegisteredEvent event{};
        event.provider = this;
        NetworkEvents::copyProviderId(event.providerId, id_);
        emit(NetworkEvents::EVENT_PROVIDER_REGISTERED, event);
    }
    const char* id_;
    bool connected_;
    int routePriority_ = -1;
    int routePriorityCalls_ = 0;
};

void setUp() {}
void tearDown() {}

void test_registration_order_and_aggregate_ready_are_event_driven() {
    Core core;
    auto network = std::make_unique<NetworkComponent>();
    auto* networkPtr = network.get();
    auto wifi = std::make_unique<TestProvider>("wifi", false);
    auto* wifiPtr = wifi.get();
    auto ethernet = std::make_unique<TestProvider>("ethernet", true);
    core.addComponent(std::move(network));
    core.addComponent(std::move(wifi));
    core.addComponent(std::move(ethernet));

    TEST_ASSERT_TRUE(core.begin());
    core.loop();
    core.loop();

    TEST_ASSERT_EQUAL_UINT32(2, networkPtr->getProviderCount());
    TEST_ASSERT_EQUAL_STRING("wifi", networkPtr->getPriorities()[0].c_str());
    TEST_ASSERT_EQUAL_STRING("ethernet", networkPtr->getPriorities()[1].c_str());
    TEST_ASSERT_TRUE(networkPtr->isReady());

    bool replayed = false;
    core.on<NetworkEvents::NetworkAvailabilityEvent>(NetworkEvents::EVENT_READY,
        [&](const NetworkEvents::NetworkAvailabilityEvent& event) { replayed = event.available; }, true);
    TEST_ASSERT_TRUE(replayed);

    wifiPtr->setConnected(true);
    core.loop();
    TEST_ASSERT_TRUE(networkPtr->isReady());
}

void test_default_priority_precedes_registration_and_keeps_absent_ids() {
    Core core;
    std::vector<String> defaults{"ethernet", "cellular"};
    auto network = std::make_unique<NetworkComponent>(defaults);
    auto* networkPtr = network.get();
    core.addComponent(std::move(network));
    core.addComponent(std::make_unique<TestProvider>("wifi"));
    core.addComponent(std::make_unique<TestProvider>("ethernet"));
    TEST_ASSERT_TRUE(core.begin());
    core.loop();
    core.loop();
    auto priorities = networkPtr->getPriorities();
    TEST_ASSERT_EQUAL_UINT32(3, priorities.size());
    TEST_ASSERT_EQUAL_STRING("ethernet", priorities[0].c_str());
    TEST_ASSERT_EQUAL_STRING("cellular", priorities[1].c_str());
    TEST_ASSERT_EQUAL_STRING("wifi", priorities[2].c_str());
}

void test_persisted_priority_overrides_default() {
    Core core;
    auto storage = std::make_unique<StorageComponent>();
    auto* storagePtr = storage.get();
    core.addComponent(std::move(storage));
    TEST_ASSERT_TRUE(storagePtr->begin() == ComponentStatus::Success);
    storagePtr->putString("network_order", "wifi,ethernet");
    storagePtr->shutdown();

    std::vector<String> defaults{"ethernet"};
    auto network = std::make_unique<NetworkComponent>(defaults);
    auto* networkPtr = network.get();
    core.addComponent(std::move(network));
    core.addComponent(std::make_unique<TestProvider>("ethernet"));
    core.addComponent(std::make_unique<TestProvider>("wifi"));
    TEST_ASSERT_TRUE(core.begin());
    core.loop();
    core.loop();
    TEST_ASSERT_EQUAL_STRING("wifi", networkPtr->getPriorities()[0].c_str());
}

void test_duplicate_registration_is_idempotent_and_id_conflict_rejected() {
    Core core;
    auto network = std::make_unique<NetworkComponent>();
    auto* networkPtr = network.get();
    auto provider = std::make_unique<TestProvider>("wifi");
    auto* providerPtr = provider.get();
    core.addComponent(std::move(network));
    core.addComponent(std::move(provider));
    TEST_ASSERT_TRUE(core.begin());
    core.loop();
    core.loop();

    NetworkEvents::NetworkProviderRegisteredEvent same{};
    same.provider = providerPtr;
    NetworkEvents::copyProviderId(same.providerId, "wifi");
    core.emit(NetworkEvents::EVENT_PROVIDER_REGISTERED, same);
    TestProvider conflict("wifi");
    same.provider = &conflict;
    core.emit(NetworkEvents::EVENT_PROVIDER_REGISTERED, same);
    core.loop();
    TEST_ASSERT_EQUAL_UINT32(1, networkPtr->getProviderCount());
}

void test_runtime_removal_clears_pointer_and_publishes_not_ready() {
    Core core;
    auto network = std::make_unique<NetworkComponent>();
    auto* networkPtr = network.get();
    core.addComponent(std::move(network));
    core.addComponent(std::make_unique<TestProvider>("ethernet", true));
    TEST_ASSERT_TRUE(core.begin());
    core.loop();
    core.loop();
    TEST_ASSERT_TRUE(networkPtr->isReady());

    TEST_ASSERT_TRUE(core.removeComponent("ethernet"));
    core.loop();
    TEST_ASSERT_EQUAL_UINT32(0, networkPtr->getProviderCount());
    TEST_ASSERT_FALSE(networkPtr->isReady());

    bool replayedAvailable = true;
    core.on<NetworkEvents::NetworkAvailabilityEvent>(NetworkEvents::EVENT_READY,
        [&](const NetworkEvents::NetworkAvailabilityEvent& event) { replayedAvailable = event.available; }, true);
    TEST_ASSERT_FALSE(replayedAvailable);
}

void test_repeated_priority_updates_remain_bounded() {
    NetworkComponent network;
    for (int i = 0; i < 100; ++i) {
        std::vector<String> order{i % 2 ? "wifi" : "ethernet", i % 2 ? "ethernet" : "wifi"};
        TEST_ASSERT_TRUE(network.setPriorities(order, false));
        TEST_ASSERT_EQUAL_UINT32(2, network.getPriorities().size());
    }
}

void test_network_hal_and_wifi_compatibility_aliases_share_state() {
    HAL::NetworkServer server(1234);
    server.begin();
    HAL::NetworkClient peer = server.simulateClient();
    HAL::WiFiClient legacy = server.accept();
    peer.simulateIncomingData("ping");
    TEST_ASSERT_EQUAL_INT(4, legacy.available());
    TEST_ASSERT_TRUE(server.isListening());
}

void test_provider_address_event_has_bounded_copied_payload() {
    NetworkEvents::NetworkProviderAddressEvent event{};
    NetworkEvents::copyProviderId(event.providerId, "wifi");
    NetworkEvents::copyAddress(event.address, "192.168.1.25");
    TEST_ASSERT_EQUAL_STRING("network/provider/address-changed", NetworkEvents::EVENT_PROVIDER_ADDRESS_CHANGED);
    TEST_ASSERT_EQUAL_STRING("wifi", event.providerId);
    TEST_ASSERT_EQUAL_STRING("192.168.1.25", event.address);
    TEST_ASSERT_TRUE((std::is_trivially_copyable<NetworkEvents::NetworkProviderAddressEvent>::value));
}

void test_route_priorities_follow_inverse_order_and_reapply_after_change() {
    Core core;
    auto network = std::make_unique<NetworkComponent>();
    auto* networkPtr = network.get();
    auto wifi = std::make_unique<TestProvider>("wifi");
    auto* wifiPtr = wifi.get();
    auto ethernet = std::make_unique<TestProvider>("ethernet");
    auto* ethernetPtr = ethernet.get();
    core.addComponent(std::move(network));
    core.addComponent(std::move(wifi));
    core.addComponent(std::move(ethernet));
    TEST_ASSERT_TRUE(core.begin());
    core.loop();
    core.loop();

    TEST_ASSERT_GREATER_THAN(ethernetPtr->routePriority(), wifiPtr->routePriority());
    int wifiCalls = wifiPtr->routePriorityCalls();
    int ethernetCalls = ethernetPtr->routePriorityCalls();

    TEST_ASSERT_TRUE(networkPtr->setPriorities({"ethernet", "wifi"}, false));
    TEST_ASSERT_GREATER_THAN(wifiPtr->routePriority(), ethernetPtr->routePriority());
    TEST_ASSERT_GREATER_THAN(wifiCalls, wifiPtr->routePriorityCalls());
    TEST_ASSERT_GREATER_THAN(ethernetCalls, ethernetPtr->routePriorityCalls());
}

void test_network_webui_exposes_and_saves_ordered_priorities() {
    Core core;
    auto network = std::make_unique<NetworkComponent>();
    auto* networkPtr = network.get();
    core.addComponent(std::move(network));
    core.addComponent(std::make_unique<TestProvider>("wifi"));
    core.addComponent(std::make_unique<TestProvider>("ethernet"));
    TEST_ASSERT_TRUE(core.begin());
    core.loop();
    core.loop();

    WebUI::NetworkWebUI webui(networkPtr);
    JsonDocument initial;
    String initialJson = webui.getWebUIData("network_settings");
    TEST_ASSERT_FALSE(deserializeJson(initial, initialJson));
    TEST_ASSERT_EQUAL_STRING("wifi", initial["priorities"][0]);
    TEST_ASSERT_EQUAL_STRING("ethernet", initial["priorities"][1]);

    std::map<String, String> params{{"field", "priorities"},
        {"value", "[\"ethernet\",\"wifi\"]"}};
    JsonDocument response;
    String responseJson = webui.handleWebUIRequest("network_settings", "/", "POST", params);
    deserializeJson(response, responseJson);
    TEST_ASSERT_TRUE(response["success"]);
    TEST_ASSERT_EQUAL_STRING("ethernet", networkPtr->getPriorities()[0].c_str());
    TEST_ASSERT_EQUAL_STRING("wifi", networkPtr->getPriorities()[1].c_str());
}

void test_network_webui_marks_priorities_dirty_on_provider_lifecycle() {
    Core core;
    auto network = std::make_unique<NetworkComponent>();
    auto* networkPtr = network.get();
    core.addComponent(std::move(network));
    TEST_ASSERT_TRUE(core.begin());
    WebUI::NetworkWebUI webui(networkPtr);
    TEST_ASSERT_TRUE(webui.hasDataChanged("network_settings"));
    TEST_ASSERT_FALSE(webui.hasDataChanged("network_settings"));

    TestProvider ethernet("ethernet");
    NetworkEvents::NetworkProviderRegisteredEvent registered{};
    registered.provider = &ethernet;
    NetworkEvents::copyProviderId(registered.providerId, "ethernet");
    core.emit(NetworkEvents::EVENT_PROVIDER_REGISTERED, registered);
    core.loop();
    TEST_ASSERT_TRUE(webui.hasDataChanged("network_settings"));
    TEST_ASSERT_FALSE(webui.hasDataChanged("network_settings"));

    NetworkEvents::NetworkProviderIdEvent unregistered{};
    NetworkEvents::copyProviderId(unregistered.providerId, "ethernet");
    core.emit(NetworkEvents::EVENT_PROVIDER_UNREGISTERED, unregistered);
    core.loop();
    TEST_ASSERT_TRUE(webui.hasDataChanged("network_settings"));
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_registration_order_and_aggregate_ready_are_event_driven);
    RUN_TEST(test_default_priority_precedes_registration_and_keeps_absent_ids);
    RUN_TEST(test_persisted_priority_overrides_default);
    RUN_TEST(test_duplicate_registration_is_idempotent_and_id_conflict_rejected);
    RUN_TEST(test_runtime_removal_clears_pointer_and_publishes_not_ready);
    RUN_TEST(test_repeated_priority_updates_remain_bounded);
    RUN_TEST(test_network_hal_and_wifi_compatibility_aliases_share_state);
    RUN_TEST(test_provider_address_event_has_bounded_copied_payload);
    RUN_TEST(test_route_priorities_follow_inverse_order_and_reapply_after_change);
    RUN_TEST(test_network_webui_exposes_and_saves_ordered_priorities);
    RUN_TEST(test_network_webui_marks_priorities_dirty_on_provider_lifecycle);
    return UNITY_END();
}
