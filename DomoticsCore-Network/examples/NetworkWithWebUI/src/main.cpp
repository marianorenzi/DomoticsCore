#include <Arduino.h>
#include <DomoticsCore/Core.h>
#include <DomoticsCore/Network.h>
#include <DomoticsCore/NetworkWebUI.h>
#include <DomoticsCore/Storage.h>
#include <DomoticsCore/WebUI.h>
#include <DomoticsCore/Wifi.h>

using namespace DomoticsCore;
using namespace DomoticsCore::Components;
using namespace DomoticsCore::Components::WebUI;

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

Core core;
NetworkComponent* network = nullptr;
WifiComponent* wifi = nullptr;
std::unique_ptr<NetworkWebUI> networkWebUI;

class MockNetworkProvider : public IComponent, public INetworkProvider {
public:
    explicit MockNetworkProvider(const char* providerId)
        : providerId_(providerId ? providerId : "") {
        metadata.name = providerId_.c_str();
        metadata.version = "1.0.0";
        metadata.description = "Network provider used to exercise NetworkWebUI";
        metadata.category = "Communication";
    }

    ComponentStatus begin() override {
        return ComponentStatus::Success;
    }
    
    void afterAllComponentsReady() override {
        NetworkEvents::NetworkProviderRegisteredEvent event{};
        NetworkEvents::copyProviderId(event.providerId, getProviderId());
        event.provider = this;
        emit(NetworkEvents::EVENT_PROVIDER_REGISTERED, event);
        Serial.printf("mock/provider/registered provider=%s\n", getProviderId());
    }

    void loop() override {}

    ComponentStatus shutdown() override {
        NetworkEvents::NetworkProviderIdEvent event{};
        NetworkEvents::copyProviderId(event.providerId, getProviderId());
        emit(NetworkEvents::EVENT_PROVIDER_UNREGISTERED, event);
        return ComponentStatus::Success;
    }

    const char* getProviderId() const override { return providerId_.c_str(); }
    bool isConnected() const override { return false; }
    String getLocalIP() const override { return ""; }
    String getNetworkType() const override { return "mock"; }
    String getConnectionStatus() const override { return "Mock"; }
    String getNetworkInfo() const override { return providerId_; }

    bool setRoutePriority(int priority) override {
        Serial.printf("mock/provider/priority provider=%s priority=%d\n",
            getProviderId(), priority);
        return true;
    }

private:
    String providerId_;
};

void setup() {
    Serial.begin(115200);
    delay(1000);

    core.addComponent(std::make_unique<StorageComponent>());

    auto networkOwner = std::make_unique<NetworkComponent>();
    network = networkOwner.get();
    core.addComponent(std::move(networkOwner));

    auto wifiOwner = std::make_unique<WifiComponent>(WIFI_SSID, WIFI_PASSWORD);
    wifi = wifiOwner.get();
    if (String(WIFI_SSID).isEmpty()) {
        wifi->enableAP("DomoticsCore-Network", "");
    }
    core.addComponent(std::move(wifiOwner));

    // Extra providers make priority ordering visible without Ethernet hardware.
    core.addComponent(std::make_unique<MockNetworkProvider>("ethernet"));
    core.addComponent(std::make_unique<MockNetworkProvider>("cellular"));

    WebUIConfig webUIConfig;
    webUIConfig.setDeviceName("DomoticsCore Network");
    webUIConfig.port = 80;
    webUIConfig.enableWebSocket = true;
    webUIConfig.useFileSystem = false;

    auto webUIOwner = std::make_unique<WebUIComponent>(webUIConfig);
    WebUIComponent* webUI = webUIOwner.get();
    core.addComponent(std::move(webUIOwner));

    // NetworkWebUI subscribes to provider registration before component startup.
    networkWebUI = std::make_unique<NetworkWebUI>(network);
    webUI->registerProviderWithComponent(networkWebUI.get(), network);

    core.on<NetworkEvents::NetworkAvailabilityEvent>(NetworkEvents::EVENT_READY,
        [](const NetworkEvents::NetworkAvailabilityEvent& event) {
            Serial.printf("network/ready=%s\n", event.available ? "true" : "false");
        }, true);

    core.on<NetworkEvents::NetworkProviderAddressEvent>(NetworkEvents::EVENT_PROVIDER_ADDRESS_CHANGED,
        [](const NetworkEvents::NetworkProviderAddressEvent& event) {
            Serial.printf("network/provider/address-changed provider=%s address=%s\n",
                event.providerId, event.address[0] ? event.address : "<none>");
            if (event.address[0]) {
                Serial.printf("WebUI: http://%s\n", event.address);
            }
        });

    if (!core.begin()) {
        Serial.println("Core initialization failed");
        return;
    }

    Serial.println("Open Settings > Network priority to reorder providers.");
}

void loop() {
    core.loop();
}
