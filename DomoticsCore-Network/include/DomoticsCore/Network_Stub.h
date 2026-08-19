#pragma once

#include <DomoticsCore/Platform_HAL.h>
#include <DomoticsCore/IPAddress_Stub.h>
#include <cstring>
#include <memory>
#include <vector>

namespace DomoticsCore { 
namespace HAL {

class NetworkClient {
    struct SharedState {
        bool connected = false;
        std::vector<uint8_t> writes;
        std::vector<uint8_t> reads;
        size_t readPosition = 0;
        uint32_t clientId = 0;
    };
    std::shared_ptr<SharedState> state_;
public:
    NetworkClient() : state_(std::make_shared<SharedState>()) {}
    explicit NetworkClient(bool connected, uint32_t id = 0) : NetworkClient() {
        state_->connected = connected; state_->clientId = id;
    }
    operator bool() const { return state_->connected; }
    bool operator==(const NetworkClient& other) const { return state_ == other.state_; }
    bool operator!=(const NetworkClient& other) const { return !(*this == other); }
    bool connected() const { return state_->connected; }
    void stop() { state_->connected = false; state_->writes.clear(); state_->reads.clear(); state_->readPosition = 0; }
    uint32_t remoteIP() const { return state_->clientId; }
    size_t write(uint8_t value) { state_->writes.push_back(value); return 1; }
    size_t write(const uint8_t* data, size_t size) { state_->writes.insert(state_->writes.end(), data, data + size); return size; }
    size_t write(const char* text) { return text ? write(reinterpret_cast<const uint8_t*>(text), std::strlen(text)) : 0; }
    size_t print(const char* text) { return write(text); }
    size_t print(const String& text) { return write(text.c_str()); }
    size_t println() { return write("\r\n"); }
    size_t println(const char* text) { size_t count = write(text); return count + println(); }
    size_t println(const String& text) { return println(text.c_str()); }
    int available() const { return int(state_->reads.size() - state_->readPosition); }
    int read() { return available() ? state_->reads[state_->readPosition++] : -1; }
    int read(uint8_t* data, size_t size) {
        size_t count = size < size_t(available()) ? size : size_t(available());
        for (size_t i = 0; i < count; ++i) data[i] = state_->reads[state_->readPosition++];
        return int(count);
    }
    int peek() const { return available() ? state_->reads[state_->readPosition] : -1; }
    void flush() { state_->writes.clear(); }
    const std::vector<uint8_t>& getWriteBuffer() const { return state_->writes; }
    std::string getWriteBufferAsString() const { return std::string(state_->writes.begin(), state_->writes.end()); }
    void clearWriteBuffer() { state_->writes.clear(); }
    void simulateIncomingData(const uint8_t* data, size_t size) { state_->reads.insert(state_->reads.end(), data, data + size); }
    void simulateIncomingData(const char* text) { if (text) simulateIncomingData(reinterpret_cast<const uint8_t*>(text), std::strlen(text)); }
    void simulateDisconnect() { state_->connected = false; }
};

class NetworkServer {
    uint16_t port_;
    bool listening_ = false;
    std::vector<NetworkClient> pendingClients_;
public:
    explicit NetworkServer(uint16_t port) : port_(port) {}
    void begin() { listening_ = true; }
    void end() { listening_ = false; pendingClients_.clear(); }
    void stop() { end(); }
    void setNoDelay(bool) {}
    bool hasClient() const { return !pendingClients_.empty(); }
    NetworkClient accept() {
        if (pendingClients_.empty()) return NetworkClient(false);
        NetworkClient client = pendingClients_.front(); pendingClients_.erase(pendingClients_.begin()); return client;
    }
    NetworkClient available() { return accept(); }
    NetworkClient simulateClient(bool connected = true, uint32_t id = 0x0A0B0C0D) {
        pendingClients_.push_back(NetworkClient(connected, id)); return pendingClients_.back();
    }
    uint16_t getPort() const { return port_; }
    bool isListening() const { return listening_; }
};

using SecureNetworkClient = NetworkClient;
using NetworkUDP = NetworkClient;
using WiFiClient = NetworkClient;
using WiFiServer = NetworkServer;

} // namespace HAL
} // namespace DomoticsCore
