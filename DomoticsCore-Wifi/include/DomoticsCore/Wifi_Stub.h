#ifndef DOMOTICS_CORE_WIFI_STUB_H
#define DOMOTICS_CORE_WIFI_STUB_H

/**
 * @file Wifi_Stub.h
 * @brief Stub WiFi implementation for unsupported platforms.
 */

#if !DOMOTICS_PLATFORM_ESP32 && !DOMOTICS_PLATFORM_ESP8266

namespace DomoticsCore {
namespace HAL {
namespace WiFiImpl {

inline bool stubbedConnected = false;
inline void setConnectedForTest(bool connected) { stubbedConnected = connected; }

inline void init() {}
inline void setMode(WiFiHAL::Mode) {}
inline void connect(const char*, const char*) {}
inline void disconnect() {}
inline bool startAP(const char*, const char*) { return false; }
inline void stopAP() {}
inline WiFiHAL::Status getStatus() { return WiFiHAL::Status::NotSupported; }
inline bool isConnected() { return stubbedConnected; }
inline String getLocalIP() { return "0.0.0.0"; }
inline String getAPIP() { return "0.0.0.0"; }
inline String getSSID() { return ""; }
inline int32_t getRSSI() { return 0; }
inline String getMacAddress() { return "00:00:00:00:00:00"; }
inline void setHostname(const char*) {}
inline void setAutoReconnect(bool) {}
inline int16_t scanNetworks(bool) { return 0; }
inline String getScannedSSID(uint8_t) { return ""; }
inline int32_t getScannedRSSI(uint8_t) { return 0; }
inline WiFiHAL::Mode getMode() { return WiFiHAL::Mode::Off; }
inline String getAPSSID() { return ""; }
inline uint8_t getAPStationCount() { return 0; }
inline int16_t scanComplete() { return 0; }
inline void scanDelete() {}
inline void disconnectAndOff() {}
inline uint8_t getRawStatus() { return 0; }

} // namespace WiFiImpl

} // namespace HAL
} // namespace DomoticsCore

#endif // Stub

#endif // DOMOTICS_CORE_WIFI_STUB_H
