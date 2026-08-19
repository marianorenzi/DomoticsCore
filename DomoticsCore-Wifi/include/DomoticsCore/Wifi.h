#pragma once

/**
 * @file Wifi.h
 * @brief WiFi component for managing WiFi connectivity.
 * 
 * @example DomoticsCore-Wifi/examples/BasicWifi/src/main.cpp
 * @example DomoticsCore-Wifi/examples/WifiWithWebUI/src/main.cpp
 * 
 * @brief Declares the DomoticsCore WiFi component providing STA/AP management and async scanning.
 */

#include "Wifi_HAL.h"  // Hardware Abstraction Layer for WiFi
#include "DomoticsCore/Platform_HAL.h"  // For millis/delay abstractions
#include <DomoticsCore/INetworkProvider.h>
#include <DomoticsCore/NetworkEvents.h>
#include "DomoticsCore/IComponent.h"
#include "DomoticsCore/Logger.h"
#include "DomoticsCore/Timer.h"
#include "DomoticsCore/WifiEvents.h"
#include <ArduinoJson.h>

namespace DomoticsCore {
namespace Components {

// Use HAL WiFi abstraction for multi-platform support
// All calls use HAL::WiFiHAL:: prefix for clarity

/**
 * WiFi Component Configuration
 * 
 * Behavior:
 * - If ssid is empty: Device starts in AP mode (enableAP ignored, always true)
 * - If ssid is set: Device connects to WiFi, AP enabled only if enableAP=true
 */
struct WifiConfig {
    // Station (STA) mode settings
    String ssid = "";                   // WiFi network SSID (empty = AP-only mode)
    String password = "";               // WiFi network password
    bool autoConnect = true;            // Auto-connect on boot if SSID set
    
    // Access Point (AP) mode settings
    bool enableAP = false;              // Enable AP alongside STA (ignored if no SSID)
    String apSSID = "";                 // AP SSID (auto-generated if empty)
    String apPassword = "";             // AP password (open if empty)
    
    // Advanced settings
    uint32_t reconnectInterval = 5000;  // Reconnection interval in ms
    uint32_t connectionTimeout = 15000; // Connection timeout in ms
};

/**
 * @class DomoticsCore::Components::WifiComponent
 * @brief Manages WiFi connectivity including STA/AP modes and async scanning.
 * Uses HAL abstraction for multi-platform support (ESP32, ESP8266).
 *
 * Handles connection lifecycle, reconnection strategies, and exposes helpers for enabling AP,
 * switching credentials, and collecting scan results without blocking the event loop. Can be
 * paired with a WebUI provider to expose runtime settings.
 */
/**
 * WiFi component providing network connectivity
 * 
 * Note: For WiFi-specific checks, use isSTAConnected() and isAPEnabled().
 * The INetworkProvider::isConnected() method is implemented as hasConnectivity()
 * which returns true if either STA or AP mode is active.
 */
class WifiComponent : public IComponent, public INetworkProvider {
private:
    String ssid;
    String password;
    Utils::NonBlockingDelay reconnectTimer;
    Utils::NonBlockingDelay statusTimer;
    Utils::NonBlockingDelay connectionTimer;
    
    bool shouldConnect;
    bool isConnecting;
    unsigned long connectionStartTime;
    bool pendingModeUpdate_ = false;  // Deferred updateWifiMode() for low-heap devices
    bool pendingConfigSave_ = false;   // Deferred config save — avoids NVS writes during HTTP handler (OOM)
    
    // STA-only fallback: when heap too low for AP+STA, temporarily drop AP for STA attempt.
    // If STA fails within timeout, AP is re-enabled and wifiEnabled set to false.
    Utils::NonBlockingDelay staFallbackTimer_;  // §X: use NonBlockingDelay, not raw millis
    
    // Reboot-to-STA: when heap too low for live STA switch (browser connected via AP),
    // save config and reboot. On restart, heap is higher and STA can connect.
    bool pendingReboot_ = false;
    Utils::NonBlockingDelay rebootTimer_;  // §X: NonBlockingDelay for deferred reboot
    
    // Config save callback (set by SystemWebUISetup for persistence)
    std::function<void(const WifiConfig&)> configSaveCallback_;
    
    // New API state
    bool wifiEnabled;
    bool apEnabled;
    String apSSID_;
    String apPassword_;
    // Non-blocking scan state
    bool scanInProgress = false;
    bool providerAnnounced_ = false;
    bool lastPublishedConnectivity_ = false;
    String lastPublishedAddress_;
    String lastScanSummary_;
    static const unsigned long CONNECTION_TIMEOUT = 15000; // 15 seconds
    
public:
    /**
     * Constructor
     * @param ssid Wifi network name (empty = configure later)
     * @param password Wifi password
     */
    WifiComponent(const String& ssid = "", const String& password = "") 
        : ssid(ssid), password(password), 
          reconnectTimer(5000), statusTimer(30000), connectionTimer(100),
          shouldConnect(true), isConnecting(false), connectionStartTime(0),
          staFallbackTimer_(30000),
          rebootTimer_(1500),
          wifiEnabled(true), apEnabled(false) {
        staFallbackTimer_.disable();  // Only enabled during STA fallback
        rebootTimer_.disable();       // Only enabled when reboot-to-STA is pending
        // Initialize component metadata immediately for dependency resolution
        metadata.name = "Wifi";
        metadata.version = "1.5.0";
        metadata.author = "DomoticsCore";
        metadata.description = "Wifi connectivity management component";
    }
    
    ComponentStatus begin() override {
        DLOG_I(LOG_WIFI, "Initializing...");
        
        // If no STA credentials
        if (ssid.isEmpty()) {
            // If AP was pre-enabled (e.g., by System with deviceName-based SSID), start AP-only with that SSID
            if (apEnabled && apSSID_.length() > 0) {
                DLOG_I(LOG_WIFI, "No STA credentials - starting preconfigured AP: %s", apSSID_.c_str());
                HAL::WiFiHAL::setMode(HAL::WiFiHAL::Mode::AccessPoint);
                HAL::WiFiHAL::startAP(apSSID_.c_str(), apPassword_.isEmpty() ? nullptr : apPassword_.c_str());
                DLOG_I(LOG_WIFI, "AP IP address: %s", HAL::WiFiHAL::getAPIP().c_str());
                // Reflect state in internal flags so UI initial values are correct
                wifiEnabled = false;
                setStatus(ComponentStatus::Success);
                // Emit events for AP mode
                emit(WifiEvents::EVENT_AP_ENABLED, true);
                return ComponentStatus::Success;
            }
            // Otherwise, fall back to autogenerated AP (DomoticsCore-XXXXXX)
            DLOG_I(LOG_WIFI, "No STA credentials - starting default AP mode");
            ComponentStatus status = connectToWifi();
            setStatus(status);
            return status;
        }
        
        // Normal STA connection
        HAL::WiFiHAL::setMode(HAL::WiFiHAL::Mode::Station);
        HAL::WiFiHAL::setAutoReconnect(false);
        ComponentStatus status = connectToWifi();
        setStatus(status);
        return status;
    }
    
    /**
     * Called after all components ready - connect to WiFi if credentials were set late
     */
    void afterAllComponentsReady() override {
        NetworkEvents::NetworkProviderRegisteredEvent providerEvent{};
        providerEvent.provider = this;
        NetworkEvents::copyProviderId(providerEvent.providerId, getProviderId());
        emit(NetworkEvents::EVENT_PROVIDER_REGISTERED, providerEvent);
        providerAnnounced_ = true;
        lastPublishedConnectivity_ = isConnected();
        publishNetworkAddress(true);
        // WiFi mode changes are handled via deferred mechanism:
        // setConfig() sets pendingModeUpdate_ = true → loop() executes updateWifiMode()
        // This ensures mode changes run AFTER all setup (callbacks, providers) is complete.
        if (pendingModeUpdate_) {
            DLOG_I(LOG_WIFI, "WiFi mode update pending — will execute in loop() (heap: %u)", (unsigned)HAL::Platform::getFreeHeap());
        }
    }
    
    void loop() override {
        publishNetworkStateTransition();
        publishNetworkAddress(false);
        // Reboot-to-STA: deferred reboot after HTTP response completes
        if (pendingReboot_ && rebootTimer_.isEnabled() && rebootTimer_.isReady()) {
            DLOG_I(LOG_WIFI, "Rebooting to apply WiFi settings (STA needs more heap)...");
            HAL::Platform::restart();
        }
        
        // Deferred mode update: runs after HTTP response is sent and TCP freed,
        // giving ~500-1000B more heap than during request handling.
        if (pendingModeUpdate_) {
            pendingModeUpdate_ = false;
            DLOG_I(LOG_WIFI, "Executing deferred WiFi mode update (heap: %u)", (unsigned)HAL::Platform::getFreeHeap());
            updateWifiMode();
        }
        
        // Deferred config save: NVS writes deferred from HTTP handler to avoid OOM.
        // Runs after HTTP response sent → TCP buffers freed → more heap available.
        if (pendingConfigSave_ && configSaveCallback_) {
            pendingConfigSave_ = false;
            WifiConfig cfg = getConfig();
            configSaveCallback_(cfg);
            DLOG_I(LOG_WIFI, "Deferred config save complete (heap: %u)", (unsigned)HAL::Platform::getFreeHeap());
        }
        
        // STA fallback timer: check if STA connected or timed out (§X: NonBlockingDelay)
        if (staFallbackTimer_.isEnabled()) {
            if (HAL::WiFiHAL::isConnected()) {
                // STA connected — restart AP on STA's channel for AP+STA coexistence
                staFallbackTimer_.disable();
                isConnecting = false;
                setStatus(ComponentStatus::Success);
                DLOG_I(LOG_WIFI, "STA connected — IP: %s", HAL::WiFiHAL::getLocalIP().c_str());
                
                // Restart AP in AP+STA mode if heap can support AP overhead + HTTP serving.
                // AP costs ~2.5KB; HTTP needs ~4KB minimum (TCP + response buffers).
                // On memory-constrained devices, stay STA-only to keep WebUI functional.
                if (apEnabled) {
                    uint32_t heapNow = HAL::Platform::getFreeHeap();
                    const uint32_t apPlusHttpMin = 6500; // 2.5KB AP overhead + 4KB HTTP minimum
                    if (heapNow > apPlusHttpMin) {
                        HAL::WiFiHAL::setMode(HAL::WiFiHAL::Mode::StationAndAP);
                        HAL::WiFiHAL::startAP(apSSID_.c_str(), apPassword_.isEmpty() ? nullptr : apPassword_.c_str());
                        DLOG_I(LOG_WIFI, "AP restarted in AP+STA mode: %s (IP: %s)", apSSID_.c_str(), HAL::WiFiHAL::getAPIP().c_str());
                        emit(WifiEvents::EVENT_AP_ENABLED, true);
                    } else {
                        DLOG_W(LOG_WIFI, "Staying STA-only — heap %u too low for AP+STA+HTTP (need %u)", (unsigned)heapNow, (unsigned)apPlusHttpMin);
                    }
                }
                
                emit(WifiEvents::EVENT_STA_CONNECTED, true);
            } else if (staFallbackTimer_.isReady()) {
                // Timeout — restore AP, disable STA, save config to prevent boot loop
                staFallbackTimer_.disable();
                shouldConnect = false;
                isConnecting = false;
                wifiEnabled = false;
                
                HAL::WiFiHAL::disconnect();
                HAL::WiFiHAL::setMode(HAL::WiFiHAL::Mode::AccessPoint);
                HAL::WiFiHAL::startAP(apSSID_.c_str(), apPassword_.isEmpty() ? nullptr : apPassword_.c_str());
                DLOG_W(LOG_WIFI, "STA timeout — AP restored: %s (IP: %s)", apSSID_.c_str(), HAL::WiFiHAL::getAPIP().c_str());
                emit(WifiEvents::EVENT_AP_ENABLED, true);
                
                if (configSaveCallback_) {
                    WifiConfig cfg;
                    cfg.ssid = ssid;
                    cfg.password = password;
                    cfg.autoConnect = false;
                    cfg.enableAP = true;
                    cfg.apSSID = apSSID_;
                    cfg.apPassword = apPassword_;
                    configSaveCallback_(cfg);
                    DLOG_I(LOG_WIFI, "Config saved with autoConnect=false (prevents boot loop)");
                }
            }
        }
        
        // Skip Wifi connection logic if in AP mode (empty SSID)
        if (ssid.isEmpty()) {
            return; // AP-only mode handled; flags set in connectToWifi()
        }
        
        // Handle ongoing connection attempt
        if (isConnecting) {
            if (connectionTimer.isReady()) {
                bool connected = HAL::WiFiHAL::isConnected();
                
                if (connected) {
                    // Connection successful
                    isConnecting = false;
                    DLOG_I(LOG_WIFI, "Wifi connected successfully");
                    DLOG_I(LOG_WIFI, "IP address: %s", HAL::WiFiHAL::getLocalIP().c_str());
                    setStatus(ComponentStatus::Success);
                    // Emit events to trigger immediate WebUI update
                    emit(WifiEvents::EVENT_STA_CONNECTED, true);
                } else if (HAL::Platform::getMillis() - connectionStartTime > CONNECTION_TIMEOUT) {
                    // Connection timeout
                    isConnecting = false;
                    DLOG_E(LOG_WIFI, "Wifi connection timeout - status: %d", HAL::WiFiHAL::getRawStatus());
                    setStatus(ComponentStatus::TimeoutError);
                    emit(WifiEvents::EVENT_STA_CONNECTED, false);
                }
            }
        }
        
        // Handle reconnection attempts
        if (shouldConnect && !isConnecting && !isSTAConnected() && reconnectTimer.isReady()) {
            DLOG_I(LOG_WIFI, "Attempting Wifi reconnection...");
            startConnection();
        }
        
        // Periodic status updates
        if (statusTimer.isReady()) {
            if (isSTAConnected()) {
                DLOG_D(LOG_WIFI, "Wifi connected - IP: %s, RSSI: %d dBm", 
                      HAL::WiFiHAL::getLocalIP().c_str(), HAL::WiFiHAL::getRSSI());
            } else {
                DLOG_D(LOG_WIFI, "Wifi disconnected - status: %s", 
                      getConnectionStatusString().c_str());
            }
        }

        // Poll async scan completion without blocking
        if (scanInProgress) {
            int res = HAL::WiFiHAL::scanComplete();
            if (res == -2) {  // WIFI_SCAN_FAILED
                DLOG_W(LOG_WIFI, "Wifi async scan failed");
                lastScanSummary_ = "Scan failed";
                scanInProgress = false;
            } else if (res >= 0) {
                String summary;
                for (int i = 0; i < res && i < 10; ++i) {
                    if (i) summary += ", ";
                    summary += HAL::WiFiHAL::getScannedSSID(i) + " (" + String(HAL::WiFiHAL::getScannedRSSI(i)) + " dBm)";
                }
                lastScanSummary_ = summary;
                HAL::WiFiHAL::scanDelete();
                scanInProgress = false;
                DLOG_I(LOG_WIFI, "Async scan complete: %d networks", res);
            }
        }
    }
    
    ComponentStatus shutdown() override {
        DLOG_I(LOG_WIFI, "Wifi Shutting down component...");
        shouldConnect = false;
        HAL::WiFiHAL::disconnectAndOff();
        if (providerAnnounced_) {
            if (lastPublishedConnectivity_) {
                NetworkEvents::NetworkProviderStateEvent stateEvent{};
                NetworkEvents::copyProviderId(stateEvent.providerId, getProviderId());
                stateEvent.connected = false;
                emit(NetworkEvents::EVENT_PROVIDER_STATE_CHANGED, stateEvent);
            }
            if (!lastPublishedAddress_.isEmpty()) {
                lastPublishedAddress_ = "";
                publishNetworkAddress(true);
            }
            NetworkEvents::NetworkProviderIdEvent providerEvent{};
            NetworkEvents::copyProviderId(providerEvent.providerId, getProviderId());
            emit(NetworkEvents::EVENT_PROVIDER_UNREGISTERED, providerEvent);
            providerAnnounced_ = false;
            lastPublishedConnectivity_ = false;
        }
        setStatus(ComponentStatus::Success);
        return ComponentStatus::Success;
    }
    
    
    // Wifi-specific methods
    
    /**
     * @brief Check if STA (station) mode is connected to a WiFi network
     * @return true if connected as a station to a WiFi network, false otherwise
     * 
     * This checks actual WiFi network connectivity (STA mode).
     */
    bool isSTAConnected() const {
        return HAL::WiFiHAL::isConnected();
    }
    
    /**
     * @brief Alias for isAPEnabled() for semantic clarity
     * @return true if AP mode is active, false otherwise
     */
    bool isAPConnected() const {
        return isAPEnabled();
    }
    
    /**
     * @brief Check if either STA or AP mode is active
     * @return true if STA connected OR AP enabled, false otherwise
     * 
     * Use this to check if WiFi subsystem has any connectivity.
     * For specific checks, use isSTAConnected() or isAPEnabled().
     */
    bool hasConnectivity() const {
        return isSTAConnected() || isAPEnabled();
    }
    
    // INetworkProvider interface implementation
    /**
     * @brief INetworkProvider interface: Check if network is available
     * @return true if WiFi has any connectivity (STA or AP), false otherwise
     * 
     * Note: For WiFi-specific checks, prefer isSTAConnected() or isAPEnabled().
     * This generic method returns true if WiFi subsystem has any connectivity.
     */
    bool isConnected() const override {
        return hasConnectivity();
    }

    const char* getProviderId() const override { return "wifi"; }
    
    String getLocalIP() const override {
        // In STA+AP mode, prioritize station IP for connectivity
        if (isSTAAPMode() && HAL::WiFiHAL::isConnected()) {
            return HAL::WiFiHAL::getLocalIP();
        }
        // In AP-only mode, return AP IP
        else if (isAPMode()) {
            return HAL::WiFiHAL::getAPIP();
        }
        // In station mode, return station IP
        return HAL::WiFiHAL::getLocalIP();
    }
    
    String getSSID() const {
        // In STA+AP mode, prioritize station SSID for connectivity
        if (isSTAAPMode() && HAL::WiFiHAL::isConnected()) {
            return HAL::WiFiHAL::getSSID();
        }
        // In AP-only mode, return AP SSID
        else if (isAPMode()) {
            return HAL::WiFiHAL::getAPSSID();
        }
        // In station mode, return station SSID
        return HAL::WiFiHAL::getSSID();
    }
    // Configured (target) SSID string (not necessarily connected)
    String getConfiguredSSID() const { return ssid; }
    
    int32_t getRSSI() const {
        return HAL::WiFiHAL::getRSSI();
    }
    
    String getMacAddress() const {
        return HAL::WiFiHAL::getMacAddress();
    }

    // Update credentials and (optionally) start reconnecting
    void setCredentials(const String& newSsid, const String& newPassword, bool reconnectNow = true) {
        ssid = newSsid;
        password = newPassword;
        if (reconnectNow) {
            shouldConnect = true;
            isConnecting = false;
            reconnectTimer.reset();
            startConnection();
        }
    }
    
    // INetworkProvider interface implementation
    String getNetworkType() const override {
        return "Wifi";
    }
    
    String getConnectionStatus() const override {
        return getConnectionStatusString();
    }
    
    String getNetworkInfo() const override {
        JsonDocument info;
        info["type"] = "Wifi";
        info["sta_connected"] = isSTAConnected();
        info["ap_enabled"] = isAPEnabled();
        
        if (isSTAConnected()) {
            info["ssid"] = getSSID().c_str();
            info["ip_address"] = getLocalIP().c_str();
            info["signal_strength"] = getRSSI();
            info["mac_address"] = getMacAddress().c_str();
        }

        // AP mode info
        bool apMode = isAPMode();
        info["ap_mode"] = apMode;
        if (apMode) {
            info["ap_ssid"] = HAL::WiFiHAL::getAPSSID().c_str();
            info["ap_ip"] = HAL::WiFiHAL::getAPIP().c_str();
        }
        
        String result;
        serializeJson(info, result);
        return result;
    }

private:
    void publishNetworkStateTransition() {
        if (!providerAnnounced_) return;
        bool connected = isConnected();
        if (connected == lastPublishedConnectivity_) return;
        lastPublishedConnectivity_ = connected;
        NetworkEvents::NetworkProviderStateEvent event{};
        NetworkEvents::copyProviderId(event.providerId, getProviderId());
        event.connected = connected;
        emit(NetworkEvents::EVENT_PROVIDER_STATE_CHANGED, event);
    }

    void publishNetworkAddress(bool force) {
        if (!providerAnnounced_) return;
        String address = isConnected() ? getLocalIP() : String("");
        if (!force && address == lastPublishedAddress_) return;
        lastPublishedAddress_ = address;
        NetworkEvents::NetworkProviderAddressEvent event{};
        NetworkEvents::copyProviderId(event.providerId, getProviderId());
        NetworkEvents::copyAddress(event.address, address.c_str());
        emit(NetworkEvents::EVENT_PROVIDER_ADDRESS_CHANGED, event);
    }

public:
    
    void disconnect() {
        shouldConnect = false;
        HAL::WiFiHAL::disconnect();
        DLOG_I(LOG_WIFI, "Wifi manually disconnected");
    }
    
    void reconnect() {
        shouldConnect = true;
        reconnectTimer.reset();
        if (!isConnecting) {
            startConnection();
        }
        DLOG_I(LOG_WIFI, "Wifi reconnection requested");
    }
    
    bool isConnectionInProgress() const {
        return isConnecting;
    }
    
    String getDetailedStatus() const {
        String status;
        
        if (isAPMode()) {
            status = "Wifi Status: AP Mode Active";
            status += "\n  AP SSID: " + HAL::WiFiHAL::getAPSSID();
            status += "\n  AP IP: " + HAL::WiFiHAL::getAPIP();
            status += "\n  Clients: " + String(HAL::WiFiHAL::getAPStationCount());
            status += "\n  MAC: " + HAL::WiFiHAL::getMacAddress();
        } else {
            status = "Wifi Status: " + getConnectionStatusString();
            if (HAL::WiFiHAL::isConnected()) {
                status += "\n  IP: " + HAL::WiFiHAL::getLocalIP();
                status += "\n  SSID: " + HAL::WiFiHAL::getSSID();
                status += "\n  RSSI: " + String(HAL::WiFiHAL::getRSSI()) + " dBm";
                status += "\n  MAC: " + HAL::WiFiHAL::getMacAddress();
            }
            if (isConnecting) {
                unsigned long elapsed = HAL::Platform::getMillis() - connectionStartTime;
                status += "\n  Connecting... (" + String(elapsed / 1000) + "s)";
            }
        }
        
        return status;
    }
    
    bool scanNetworks(std::vector<String>& networks) {
        int n = HAL::WiFiHAL::scanNetworks(false);
        networks.clear();
        networks.shrink_to_fit();

        if (n == -1) {
            DLOG_E(LOG_WIFI, "Wifi scan failed");
            return false;
        }

        networks.reserve(static_cast<size_t>(n));
        DLOG_I(LOG_WIFI, "Found %d Wifi networks", n);
        for (int i = 0; i < n; i++) {
            String network = HAL::WiFiHAL::getScannedSSID(i) + " (" + String(HAL::WiFiHAL::getScannedRSSI(i)) + " dBm)";
            networks.push_back(network);
            DLOG_D(LOG_WIFI, "  %s", network.c_str());
        }
        
        return true;
    }

    // Start non-blocking scan (returns immediately)
    void startScanAsync() {
        if (scanInProgress) return;
        HAL::WiFiHAL::scanNetworks(true /* async */);
        scanInProgress = true;
        lastScanSummary_ = "Scanning...";
        DLOG_I(LOG_WIFI, "Started async WiFi scan");
    }

    String getLastScanSummary() const { return lastScanSummary_; }
    
    bool isSTAAPMode() const {
        return HAL::WiFiHAL::getMode() == HAL::WiFiHAL::Mode::StationAndAP;
    }
    
    /**
     * Check if currently in AP mode
     * @return true if in AP mode
     */
    bool isAPMode() const {
        HAL::WiFiHAL::Mode mode = HAL::WiFiHAL::getMode();
        return (mode == HAL::WiFiHAL::Mode::AccessPoint || mode == HAL::WiFiHAL::Mode::StationAndAP);
    }
    
    /**
     * Get AP mode information
     * @return JSON string with AP details
     */
    String getAPInfo() const {
        JsonDocument info;
        
        if (isAPMode()) {
            info["active"] = true;
            info["ssid"] = HAL::WiFiHAL::getAPSSID().c_str();
            info["ip"] = HAL::WiFiHAL::getAPIP().c_str();
            info["clients"] = HAL::WiFiHAL::getAPStationCount();
        } else {
            info["active"] = false;
        }
        
        String result;
        serializeJson(info, result);
        return result;
    }
    
    // Simple Wifi and AP management - hides mode complexity
    bool enableWifi(bool enable = true) {
        wifiEnabled = enable;
        return updateWifiMode();
    }
    
    bool enableAP(const String& apSSID, const String& apPassword = "", bool enable = true) {
        if (enable) {
            apSSID_ = apSSID;
            apPassword_ = apPassword;
            apEnabled = true;
        } else {
            apEnabled = false;
        }
        return updateWifiMode();
    }
    
    bool disableAP() {
        return enableAP("", "", false);
    }
    
    // Status methods that work with the new API
    bool isWifiEnabled() const { return wifiEnabled; }
    bool isAPEnabled() const { return apEnabled; }
    String getAPSSID() const { return apSSID_; }
    
    /**
     * @brief Get current WiFi configuration
     * @return Current WifiConfig (constructed from internal state)
     */
    WifiConfig getConfig() const {
        WifiConfig cfg;
        cfg.ssid = ssid;
        cfg.password = password;
        cfg.autoConnect = shouldConnect;
        cfg.enableAP = apEnabled;
        cfg.apSSID = apSSID_;
        cfg.apPassword = apPassword_;
        cfg.reconnectInterval = 5000; // Default from constructor
        cfg.connectionTimeout = CONNECTION_TIMEOUT;
        return cfg;
    }
    
    /**
     * @brief Set WiFi configuration
     * @param cfg New configuration to apply
     */
    void setConfig(const WifiConfig& cfg) {
        ssid = cfg.ssid;
        password = cfg.password;
        shouldConnect = cfg.autoConnect;
        wifiEnabled = cfg.autoConnect;  // Sync wifiEnabled with autoConnect for WebUI display
        apEnabled = cfg.enableAP;
        apSSID_ = cfg.apSSID;
        apPassword_ = cfg.apPassword;
        
        DLOG_I(LOG_WIFI, "Config updated: SSID=%s, autoConnect=%d, AP=%s (enabled=%d)", 
               ssid.c_str(), wifiEnabled, apSSID_.c_str(), apEnabled);
        
        // Schedule deferred mode update — runs in next loop() iteration,
        // after all setup (callbacks, providers) is complete.
        if (wifiEnabled || apEnabled) {
            pendingModeUpdate_ = true;
        }
    }
    
    // Set config save callback for persistence (called during STA fallback)
    void setConfigSaveCallback(std::function<void(const WifiConfig&)> callback) {
        configSaveCallback_ = callback;
    }
    
    
    // Schedule deferred mode update — runs in next loop() iteration
    // after HTTP response is sent and TCP buffers freed.
    void scheduleUpdateWifiMode() {
        pendingModeUpdate_ = true;
        DLOG_I(LOG_WIFI, "WiFi mode update scheduled for next loop (heap: %u)", (unsigned)HAL::Platform::getFreeHeap());
    }
    
    // Schedule deferred config save — NVS writes deferred from HTTP handler to loop()
    // to avoid OOM when heap is critical during request handling.
    void scheduleConfigSave() {
        pendingConfigSave_ = true;
    }
    
    // Lightweight STA credential setter — avoids constructing intermediate WifiConfig
    // (which allocates 6+ Strings) during HTTP handler when heap is critical.
    // Use instead of getConfig()/setConfig() pattern in low-heap paths.
    void setSTACredentials(const String& newSsid, const String& newPassword, bool enable) {
        ssid = newSsid;
        password = newPassword;
        shouldConnect = enable;
        wifiEnabled = enable;
        DLOG_I(LOG_WIFI, "STA credentials set: SSID='%s', enabled=%d (heap: %u)",
               ssid.c_str(), enable, (unsigned)HAL::Platform::getFreeHeap());
    }
    
    // Update Wifi mode based on enabled features
    bool updateWifiMode() {
        // Guard: wifiEnabled with empty SSID is invalid (partial config from crash).
        // Disable STA to prevent useless connection attempts.
        if (wifiEnabled && ssid.isEmpty()) {
            DLOG_W(LOG_WIFI, "wifiEnabled=true but SSID empty — disabling STA (stale config)");
            wifiEnabled = false;
            shouldConnect = false;
        }
        
        DLOG_I(LOG_WIFI, "Updating Wifi mode - Wifi: %s, AP: %s", 
               wifiEnabled ? "enabled" : "disabled", 
               apEnabled ? "enabled" : "disabled");
        
        // Ultra-low heap guard: if heap is critically low, ANY WiFi SDK call
        // may crash. Disable STA and save config to break boot loops.
        if (wifiEnabled) {
            uint32_t heapNow = HAL::Platform::getFreeHeap();
            if (heapNow < 2000) {
                DLOG_W(LOG_WIFI, "CRITICAL: heap only %u — disabling WiFi STA to prevent crash", (unsigned)heapNow);
                wifiEnabled = false;
                shouldConnect = false;
                if (configSaveCallback_) {
                    WifiConfig cfg;
                    cfg.ssid = ssid;
                    cfg.password = password;
                    cfg.autoConnect = false;
                    cfg.enableAP = apEnabled;
                    cfg.apSSID = apSSID_;
                    cfg.apPassword = apPassword_;
                    configSaveCallback_(cfg);
                    DLOG_I(LOG_WIFI, "Config saved with autoConnect=false (heap guard)");
                }
                return false;
            }
        }
        
        if (wifiEnabled && apEnabled) {
            uint32_t freeHeap = HAL::Platform::getFreeHeap();
            
            if (freeHeap < 3500) {
                // Not enough heap for simultaneous AP+STA (ESP8266 needs ~3.5KB+).
                if (HAL::WiFiHAL::getAPStationCount() > 0) {
                    // Runtime: browser connected via AP — heap too low for WiFi.begin().
                    // Config save already scheduled via pendingConfigSave_ in loop()
                    // (runs before reboot timer fires). No NVS writes here to avoid OOM.
                    DLOG_W(LOG_WIFI, "AP client connected, heap too low (%u) — rebooting to apply WiFi settings", (unsigned)freeHeap);
                    pendingReboot_ = true;
                    rebootTimer_.reset();
                    rebootTimer_.enable();
                    return true;
                }
                // Boot time: no AP clients — try STA-only with AP fallback.
                // Stop AP to free ~1.5KB, then attempt STA for 30s.
                // If STA fails, AP is re-enabled and autoConnect saved as false.
                DLOG_W(LOG_WIFI, "Heap low for AP+STA (%u) — trying STA-only with AP fallback (30s)", (unsigned)freeHeap);
                HAL::WiFiHAL::stopAP();
                emit(WifiEvents::EVENT_AP_ENABLED, false);
                HAL::WiFiHAL::setMode(HAL::WiFiHAL::Mode::Station);
                
                shouldConnect = true;
                reconnectTimer.reset();
                
                // Activate fallback timer (§X: NonBlockingDelay)
                staFallbackTimer_.reset();
                staFallbackTimer_.enable();
                return true;
            }
            
            // Sufficient heap: direct simultaneous AP+STA (no AP disruption).
            // Dual-radio platforms (ESP32) can run AP and STA on different channels.
            if (freeHeap >= 10000) {
                DLOG_I(LOG_WIFI, "Enabling STA+AP mode (heap: %u)", (unsigned)freeHeap);
                HAL::WiFiHAL::setMode(HAL::WiFiHAL::Mode::StationAndAP);
                HAL::Platform::yield();
                
                bool apSuccess = HAL::WiFiHAL::startAP(apSSID_.c_str(), apPassword_.isEmpty() ? nullptr : apPassword_.c_str());
                if (apSuccess) {
                    DLOG_I(LOG_WIFI, "AP started: %s (IP: %s)", apSSID_.c_str(), HAL::WiFiHAL::getAPIP().c_str());
                    emit(WifiEvents::EVENT_AP_ENABLED, true);
                }
                
                shouldConnect = true;
                reconnectTimer.reset();
                return apSuccess;
            }
            
            // Constrained heap (3.5–10KB): single-radio channel sync.
            // AP and STA share one channel — stop AP first so STA can connect
            // to the router's channel, then restart AP locked to that channel
            // via the fallback success handler in loop().
            DLOG_I(LOG_WIFI, "Stopping AP for STA connection (heap: %u, channel sync)", (unsigned)freeHeap);
            HAL::WiFiHAL::stopAP();
            emit(WifiEvents::EVENT_AP_ENABLED, false);
            HAL::WiFiHAL::setMode(HAL::WiFiHAL::Mode::Station);
            
            shouldConnect = true;
            reconnectTimer.reset();
            
            // Activate fallback timer (§X: NonBlockingDelay)
            staFallbackTimer_.reset();
            staFallbackTimer_.enable();
            return true;
        } else if (wifiEnabled && !apEnabled) {
            // Only Wifi requested - use STA mode
            DLOG_I(LOG_WIFI, "Enabling station mode only");
            HAL::WiFiHAL::stopAP();
            emit(WifiEvents::EVENT_AP_ENABLED, false);
            HAL::Platform::yield();  // §X: non-blocking yield for WiFi stack
            HAL::WiFiHAL::setMode(HAL::WiFiHAL::Mode::Station);
            HAL::Platform::yield();
            shouldConnect = true;
            reconnectTimer.reset();
            return true;
        } else if (!wifiEnabled && apEnabled) {
            // Only AP requested - use AP mode
            shouldConnect = false;
            isConnecting = false;
            
            // Skip restart if AP is already running with correct SSID
            // (begin() already started it — restarting would cause brief AP dropout)
            if (HAL::WiFiHAL::getMode() == HAL::WiFiHAL::Mode::AccessPoint &&
                HAL::WiFiHAL::getAPSSID() == apSSID_) {
                DLOG_I(LOG_WIFI, "AP already active: %s (IP: %s) — no restart needed",
                       apSSID_.c_str(), HAL::WiFiHAL::getAPIP().c_str());
                return true;
            }
            
            DLOG_I(LOG_WIFI, "Enabling AP mode only");
            HAL::WiFiHAL::disconnect();
            HAL::WiFiHAL::setMode(HAL::WiFiHAL::Mode::AccessPoint);
            HAL::Platform::yield();  // §X: non-blocking yield for WiFi stack
            
            bool success = HAL::WiFiHAL::startAP(apSSID_.c_str(), apPassword_.isEmpty() ? nullptr : apPassword_.c_str());
            
            if (success) {
                DLOG_I(LOG_WIFI, "AP-only mode started: %s (IP: %s)", apSSID_.c_str(), HAL::WiFiHAL::getAPIP().c_str());
                emit(WifiEvents::EVENT_AP_ENABLED, true);
            }

            return success;
        } else {
            // Both disabled - turn off Wifi
            DLOG_I(LOG_WIFI, "Disabling all Wifi features");
            shouldConnect = false;
            isConnecting = false;
            HAL::WiFiHAL::stopAP();
            HAL::WiFiHAL::disconnectAndOff();
            return true;
        }
    }

private:
    ComponentStatus connectToWifi() {
        if (ssid.isEmpty()) {
            DLOG_I(LOG_WIFI, "Wifi SSID not configured - starting in AP mode");
            
            // Generate AP SSID from MAC address for uniqueness
            String macAddress = HAL::WiFiHAL::getMacAddress();
            macAddress.replace(":", "");
            String apSSID = "DomoticsCore-" + macAddress.substring(6); // Last 6 chars of MAC
            
            HAL::WiFiHAL::setMode(HAL::WiFiHAL::Mode::AccessPoint);
            HAL::WiFiHAL::startAP(apSSID.c_str()); // No password for easy access
            DLOG_I(LOG_WIFI, "AP mode started: %s (open network)", apSSID.c_str());
            DLOG_I(LOG_WIFI, "AP IP address: %s", HAL::WiFiHAL::getAPIP().c_str());
            // Reflect state in internal flags so UI initial values are correct
            apEnabled = true;
            wifiEnabled = false;
            apSSID_ = apSSID;
            // Emit events for AP mode
            emit(WifiEvents::EVENT_AP_ENABLED, true);
            return ComponentStatus::Success;
        }
        
        // Start non-blocking connection
        startConnection();
        
        // Return pending status - actual result will be determined in loop()
        return ComponentStatus::Success;
    }
    
    void startConnection() {
        if (isConnecting) return; // Already connecting
        
        // Heap guard: WiFi.begin() + WPA handshake needs ~1.5-2KB.
        // Checked here (not in HTTP handler) because HTTP buffers are freed by now.
        uint32_t freeHeap = HAL::Platform::getFreeHeap();
        if (freeHeap < 2500) {
            DLOG_W(LOG_WIFI, "Deferring WiFi connect: heap too low (%u bytes, need 2500+)", (unsigned)freeHeap);
            return; // Will retry on next reconnectTimer tick
        }
        
        DLOG_I(LOG_WIFI, "Connecting to Wifi: %s (heap: %u)", ssid.c_str(), (unsigned)freeHeap);
        HAL::WiFiHAL::connect(ssid.c_str(), password.c_str());
        
        isConnecting = true;
        connectionStartTime = HAL::Platform::getMillis();
        connectionTimer.reset();
    }
    
    // Additional utility methods
    String getConnectionStatusString() const {
        uint8_t status = HAL::WiFiHAL::getRawStatus();
        switch (status) {
            case 0: return "Idle";            // WL_IDLE_STATUS
            case 1: return "SSID not available"; // WL_NO_SSID_AVAIL
            case 2: return "Scan completed";   // WL_SCAN_COMPLETED
            case 3: return "Connected";        // WL_CONNECTED
            case 4: return "Connection failed"; // WL_CONNECT_FAILED
            case 5: return "Connection lost";  // WL_CONNECTION_LOST
            case 6: return "Disconnected";     // WL_DISCONNECTED
            default: return "Unknown (" + String(status) + ")";
        }
    }
};


} // namespace Components
} // namespace DomoticsCore
