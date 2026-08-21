#ifndef DOMOTICS_CORE_HAL_WIFI_H
#define DOMOTICS_CORE_HAL_WIFI_H

/**
 * @file Wifi_HAL.h
 * @brief WiFi Hardware Abstraction Layer.
 * 
 * Provides a unified WiFi interface across platforms:
 * - ESP32: Uses WiFi.h (ESP-IDF based)
 * - ESP8266: Uses ESP8266WiFi.h
 * - Other platforms: Stub (no WiFi)
 */

#include "DomoticsCore/Platform_HAL.h"

namespace DomoticsCore {
namespace HAL {
namespace WiFiHAL {

/**
 * @brief WiFi connection status
 */
enum class Status {
    Disconnected,
    Connecting,
    Connected,
    ConnectionFailed,
    NotSupported
};

/**
 * @brief WiFi mode
 */
enum class Mode {
    Off,
    Station,      // Client mode
    AccessPoint,  // AP mode
    StationAndAP  // Dual mode
};

/**
 * @brief Check if WiFi is supported on this platform
 */
inline bool isSupported() {
#if DOMOTICS_HAS_WIFI
    return true;
#else
    return false;
#endif
}

} // namespace WiFiHAL
} // namespace HAL
} // namespace DomoticsCore

// ============================================================================
// Platform-specific Implementation Files
// ============================================================================

#if DOMOTICS_PLATFORM_ESP32
    #include "Wifi_ESP32.h"
#elif DOMOTICS_PLATFORM_ESP8266
    #include "Wifi_ESP8266.h"
#else
    #include "Wifi_Stub.h"
#endif

// ============================================================================
// Backward-Compatible API (delegates to WiFiImpl namespace)
// ============================================================================

namespace DomoticsCore {
namespace HAL {
namespace WiFiHAL {

inline void init() { WiFiImpl::init(); }
inline void setMode(Mode mode) { WiFiImpl::setMode(mode); }
inline void connect(const char* ssid, const char* password = nullptr) { WiFiImpl::connect(ssid, password); }
inline void disconnect() { WiFiImpl::disconnect(); }
inline bool startAP(const char* ssid, const char* password = nullptr) { return WiFiImpl::startAP(ssid, password); }
inline void stopAP() { WiFiImpl::stopAP(); }
inline Status getStatus() { return WiFiImpl::getStatus(); }
inline bool isConnected() { return WiFiImpl::isConnected(); }
inline String getLocalIP() { return WiFiImpl::getLocalIP(); }
inline String getAPIP() { return WiFiImpl::getAPIP(); }
inline String getSSID() { return WiFiImpl::getSSID(); }
inline int32_t getRSSI() { return WiFiImpl::getRSSI(); }
inline String getMacAddress() { return WiFiImpl::getMacAddress(); }
inline void setHostname(const char* hostname) { WiFiImpl::setHostname(hostname); }
inline void setAutoReconnect(bool enabled) { WiFiImpl::setAutoReconnect(enabled); }
inline int16_t scanNetworks(bool async = false) { return WiFiImpl::scanNetworks(async); }
inline String getScannedSSID(uint8_t index) { return WiFiImpl::getScannedSSID(index); }
inline int32_t getScannedRSSI(uint8_t index) { return WiFiImpl::getScannedRSSI(index); }
inline Mode getMode() { return WiFiImpl::getMode(); }
inline String getAPSSID() { return WiFiImpl::getAPSSID(); }
inline uint8_t getAPStationCount() { return WiFiImpl::getAPStationCount(); }
inline int16_t scanComplete() { return WiFiImpl::scanComplete(); }
inline void scanDelete() { WiFiImpl::scanDelete(); }
inline void disconnectAndOff() { WiFiImpl::disconnectAndOff(); }
inline uint8_t getRawStatus() { return WiFiImpl::getRawStatus(); }
inline bool setRoutePriority(int priority) { return WiFiImpl::setRoutePriority(priority); }

} // namespace WiFiHAL
} // namespace HAL
} // namespace DomoticsCore

#endif // DOMOTICS_CORE_HAL_WIFI_H
