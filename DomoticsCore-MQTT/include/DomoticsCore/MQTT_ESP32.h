#pragma once

/**
 * @file MQTT_ESP32.h
 * @brief ESP32 implementation of MQTT HAL using PubSubClient
 */

// ESP32-specific MQTT buffer size
// ESP32 has ~520KB RAM, so we can afford larger buffers
#ifndef MQTT_MAX_PACKET_SIZE
#define MQTT_MAX_PACKET_SIZE 2048
#endif

#include <PubSubClient.h>
#include <NetworkClient.h>
#include <NetworkClientSecure.h>

namespace DomoticsCore {
namespace HAL {
namespace MQTT {

/**
 * @brief ESP32 MQTT client implementation
 *
 * Wraps PubSubClient for ESP32 platform
 */
class MQTTClientImpl : public MQTTClient {
private:
    PubSubClient client;
    NetworkClient networkClient;
    NetworkClientSecure networkClientSecure;
    bool useTLS;

public:
    /**
     * @brief Construct MQTT client for ESP32
     * @param useTLS_ Use TLS/SSL connection
     */
    explicit MQTTClientImpl(bool useTLS_ = false)
        : client(useTLS_ ? (Client&)networkClientSecure : (Client&)networkClient)
        , useTLS(useTLS_) {}

    bool connect(const char* id,
                const char* user = nullptr,
                const char* pass = nullptr,
                const char* willTopic = nullptr,
                uint8_t willQoS = 0,
                bool willRetain = false,
                const char* willMessage = nullptr) override {
        if (willTopic && willMessage) {
            return client.connect(id, user, pass, willTopic, willQoS, willRetain, willMessage);
        } else if (user && pass) {
            return client.connect(id, user, pass);
        } else {
            return client.connect(id);
        }
    }

    void disconnect() override {
        client.disconnect();
    }

    bool loop() override {
        return client.loop();
    }

    bool publish(const char* topic,
                const uint8_t* payload,
                unsigned int length,
                bool retained = false) override {
        return client.publish(topic, payload, length, retained);
    }

    bool subscribe(const char* topic, uint8_t qos = 0) override {
        return client.subscribe(topic, qos);
    }

    bool unsubscribe(const char* topic) override {
        return client.unsubscribe(topic);
    }

    void setServer(const char* domain, uint16_t port) override {
        client.setServer(domain, port);
    }

    void setCallback(void (*callback)(char*, uint8_t*, unsigned int)) override {
        client.setCallback(callback);
    }

    void setKeepAlive(uint16_t keepAlive) override {
        client.setKeepAlive(keepAlive);
    }

    bool setBufferSize(uint16_t size) override {
        return client.setBufferSize(size);
    }

    uint16_t getBufferSize() override {
        return client.getBufferSize();
    }

    int state() override {
        return client.state();
    }

    bool connected() override {
        return client.connected();
    }
};

} // namespace MQTT
} // namespace HAL
} // namespace DomoticsCore
