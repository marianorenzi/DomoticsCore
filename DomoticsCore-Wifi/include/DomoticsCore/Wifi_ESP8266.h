#ifndef DOMOTICS_CORE_WIFI_ESP8266_H
#define DOMOTICS_CORE_WIFI_ESP8266_H

/**
 * @file Wifi_ESP8266.h
 * @brief ESP8266-specific WiFi implementation.
 */

#if DOMOTICS_PLATFORM_ESP8266

#include <ESP8266WiFi.h>

namespace DomoticsCore {
namespace HAL {
namespace WiFiImpl {

inline void init() {
    WiFi.persistent(false);       // Don't auto-save WiFi state to flash (we manage persistence)
    WiFi.setAutoConnect(false);   // Don't auto-connect STA on boot (saves ~4KB heap)
    WiFi.setAutoReconnect(false); // We handle reconnection ourselves
    WiFi.disconnect(true);        // Clear any residual STA connection from previous boot
    WiFi.mode(WIFI_OFF);          // Start clean — our code decides the mode
}

inline void setMode(WiFiHAL::Mode mode) {
    switch (mode) {
        case WiFiHAL::Mode::Off:         WiFi.mode(WIFI_OFF); break;
        case WiFiHAL::Mode::Station:     WiFi.mode(WIFI_STA); break;
        case WiFiHAL::Mode::AccessPoint: WiFi.mode(WIFI_AP); break;
        case WiFiHAL::Mode::StationAndAP: WiFi.mode(WIFI_AP_STA); break;
    }
}

inline void connect(const char* ssid, const char* password) {
    if (password && strlen(password) > 0) {
        WiFi.begin(ssid, password);
    } else {
        WiFi.begin(ssid);
    }
}

inline void disconnect() { WiFi.disconnect(); }

inline bool startAP(const char* ssid, const char* password) {
    if (password && strlen(password) > 0) {
        return WiFi.softAP(ssid, password);
    }
    return WiFi.softAP(ssid);
}

inline void stopAP() { WiFi.softAPdisconnect(true); }

inline WiFiHAL::Status getStatus() {
    switch (WiFi.status()) {
        case WL_CONNECTED:      return WiFiHAL::Status::Connected;
        case WL_DISCONNECTED:   return WiFiHAL::Status::Disconnected;
        case WL_CONNECT_FAILED: return WiFiHAL::Status::ConnectionFailed;
        default:                return WiFiHAL::Status::Connecting;
    }
}

inline bool isConnected() { return WiFi.status() == WL_CONNECTED; }
inline String getLocalIP() { return WiFi.localIP().toString(); }
inline String getAPIP() { return WiFi.softAPIP().toString(); }
inline String getSSID() { return WiFi.SSID(); }
inline int32_t getRSSI() { return WiFi.RSSI(); }
inline String getMacAddress() { return WiFi.macAddress(); }
inline void setHostname(const char* hostname) { WiFi.hostname(hostname); }
inline void setAutoReconnect(bool enabled) { WiFi.setAutoReconnect(enabled); }
inline int16_t scanNetworks(bool async) { return WiFi.scanNetworks(async); }
inline String getScannedSSID(uint8_t index) { return WiFi.SSID(index); }
inline int32_t getScannedRSSI(uint8_t index) { return WiFi.RSSI(index); }

inline WiFiHAL::Mode getMode() {
    WiFiMode_t mode = WiFi.getMode();
    switch (mode) {
        case WIFI_OFF:    return WiFiHAL::Mode::Off;
        case WIFI_STA:    return WiFiHAL::Mode::Station;
        case WIFI_AP:     return WiFiHAL::Mode::AccessPoint;
        case WIFI_AP_STA: return WiFiHAL::Mode::StationAndAP;
        default:          return WiFiHAL::Mode::Off;
    }
}

inline String getAPSSID() { return WiFi.softAPSSID(); }
inline uint8_t getAPStationCount() { return WiFi.softAPgetStationNum(); }
inline int16_t scanComplete() { return WiFi.scanComplete(); }
inline void scanDelete() { WiFi.scanDelete(); }
inline void disconnectAndOff() { WiFi.disconnect(true); WiFi.mode(WIFI_OFF); }
inline uint8_t getRawStatus() { return (uint8_t)WiFi.status(); }

} // namespace WiFiImpl

} // namespace HAL
} // namespace DomoticsCore

#endif // DOMOTICS_PLATFORM_ESP8266

#endif // DOMOTICS_CORE_WIFI_ESP8266_H
