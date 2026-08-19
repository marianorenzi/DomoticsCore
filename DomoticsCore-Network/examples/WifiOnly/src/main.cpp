#include <Arduino.h>
#include <DomoticsCore/Core.h>
#include <DomoticsCore/Network.h>
#include <DomoticsCore/Wifi.h>

using namespace DomoticsCore;
using namespace DomoticsCore::Components;

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

Core core;
WifiComponent* wifi = nullptr;
NetworkComponent* network = nullptr;
HAL::NetworkServer probeServer(2323);
HAL::NetworkClient probeClient;
unsigned long lastAction = 0;
bool disconnectedOnce = false;
bool reconnectRequested = false;

void handleProbeServer() {
    if (!probeClient || !probeClient.connected()) {
        if (probeClient) probeClient.stop();

        HAL::NetworkClient candidate = probeServer.accept();
        if (candidate) {
            probeClient = candidate;
            Serial.printf("probe/client-connected remote=%s\n",
                probeClient.remoteIP().toString().c_str());
            probeClient.println("DomoticsCore Network probe ready; input will be echoed.");
        }
    }

    uint8_t buffer[128];
    while (probeClient && probeClient.connected() && probeClient.available() > 0) {
        size_t available = static_cast<size_t>(probeClient.available());
        size_t requested = available < sizeof(buffer) ? available : sizeof(buffer);
        int received = probeClient.read(buffer, requested);
        if (received <= 0) break;

        Serial.printf("probe/received bytes=%d data=", received);
        Serial.write(buffer, static_cast<size_t>(received));
        Serial.println();
        probeClient.write(buffer, static_cast<size_t>(received));
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    auto networkOwner = std::make_unique<NetworkComponent>();
    network = networkOwner.get();
    core.addComponent(std::move(networkOwner));

    auto wifiOwner = std::make_unique<WifiComponent>(WIFI_SSID, WIFI_PASSWORD);
    wifi = wifiOwner.get();
    if (String(WIFI_SSID).isEmpty()) wifi->enableAP("DomoticsCore-Network-Test", "");
    core.addComponent(std::move(wifiOwner));

    core.on<NetworkEvents::NetworkAvailabilityEvent>(NetworkEvents::EVENT_READY,
        [](const NetworkEvents::NetworkAvailabilityEvent& event) {
            Serial.printf("network/ready=%s\n", event.available ? "true" : "false");
        }, true);

    core.on<NetworkEvents::NetworkProviderRegisteredEvent>(NetworkEvents::EVENT_PROVIDER_REGISTERED,
        [](const NetworkEvents::NetworkProviderRegisteredEvent& event) {
            Serial.printf("network/provider/registered provider=%s connected=%s ip=%s\n",
                event.providerId,
                event.provider && event.provider->isConnected() ? "true" : "false",
                event.provider ? event.provider->getLocalIP().c_str() : "<unavailable>");
        });

    core.on<NetworkEvents::NetworkProviderStateEvent>(NetworkEvents::EVENT_PROVIDER_STATE_CHANGED,
        [](const NetworkEvents::NetworkProviderStateEvent& event) {
            Serial.printf("network/provider/state-changed provider=%s connected=%s\n",
                event.providerId,
                event.connected ? "true" : "false");
        });

    core.on<NetworkEvents::NetworkProviderAddressEvent>(NetworkEvents::EVENT_PROVIDER_ADDRESS_CHANGED,
        [](const NetworkEvents::NetworkProviderAddressEvent& event) {
            Serial.printf("network/provider/address-changed provider=%s address=%s\n",
                event.providerId,
                event.address[0] ? event.address : "<none>");
        });

    if (!core.begin()) {
        Serial.println("Core initialization failed");
        return;
    }

    probeServer.begin();
    Serial.println("probe/listening port=2323");
    Serial.printf("provider=%s ip=%s\n", wifi->getProviderId(), wifi->getLocalIP().c_str());
}

void loop() {
    core.loop();
    handleProbeServer();

    // With STA credentials, exercise one disconnect/reconnect cycle after startup.
    if (!String(WIFI_SSID).isEmpty() && !disconnectedOnce && millis() - lastAction > 20000) {
        disconnectedOnce = true;
        lastAction = millis();
        wifi->disconnect();
        Serial.println("STA disconnect requested");
    } else if (disconnectedOnce && !reconnectRequested && millis() - lastAction > 5000 && !wifi->isSTAConnected()) {
        reconnectRequested = true;
        wifi->reconnect();
        Serial.println("STA reconnect requested");
    }
}
