#pragma once

/**
 * @file NTP.h
 * @brief Declares the DomoticsCore NTP component for network time synchronization.
 * 
 * Provides NTP time synchronization with timezone support, formatted time strings,
 * and uptime tracking. Uses HAL abstraction for multi-platform support.
 * 
 * @example DomoticsCore-NTP/examples/BasicNTP/src/main.cpp
 * @example DomoticsCore-NTP/examples/NTPWithWebUI/src/main.cpp
 */

#include "DomoticsCore/IComponent.h"
#include "DomoticsCore/Logger.h"
#include "DomoticsCore/Timer.h"
#include "DomoticsCore/NTPEvents.h"
#include "DomoticsCore/NetworkEvents.h"
#include "DomoticsCore/Platform_HAL.h"  // For millis/delay abstractions
#include "NTP_HAL.h"  // Hardware Abstraction Layer for NTP
#include <time.h>
#include <sys/time.h>
#include <vector>
#include <functional>

namespace DomoticsCore {
namespace Components {

// ========== Configuration ==========

/**
 * @brief NTP configuration structure
 */
struct NTPConfig {
    bool enabled = true;
    std::vector<String> servers = {"pool.ntp.org", "time.google.com", "time.cloudflare.com"};
    uint32_t syncInterval = 3600;    // seconds (1 hour)
    String timezone = "UTC0";         // POSIX TZ string
    uint32_t timeoutMs = 5000;       // Connection timeout
};

/**
 * @brief Common timezone presets
 */
namespace Timezones {
    constexpr const char* UTC = "UTC0";
    constexpr const char* EST = "EST5EDT,M3.2.0,M11.1.0";              // US Eastern
    constexpr const char* CST = "CST6CDT,M3.2.0,M11.1.0";              // US Central
    constexpr const char* MST = "MST7MDT,M3.2.0,M11.1.0";              // US Mountain
    constexpr const char* PST = "PST8PDT,M3.2.0,M11.1.0";              // US Pacific
    constexpr const char* CET = "CET-1CEST,M3.5.0,M10.5.0/3";          // Central European
    constexpr const char* GMT = "GMT0";                                 // Greenwich Mean Time
    constexpr const char* JST = "JST-9";                                // Japan
    constexpr const char* AEST = "AEST-10AEDT,M10.1.0,M4.1.0/3";      // Australia Eastern
    constexpr const char* IST = "IST-5:30";                            // India
    constexpr const char* NZST = "NZST-12NZDT,M9.5.0,M4.1.0/3";       // New Zealand
}

// ========== Statistics ==========

/**
 * @brief NTP synchronization statistics
 */
struct NTPStatistics {
    uint32_t syncCount = 0;
    uint32_t syncErrors = 0;
    time_t lastSyncTime = 0;
    uint32_t lastSyncDuration = 0;    // milliseconds
    time_t lastFailTime = 0;
    uint32_t consecutiveFailures = 0;
};

// ========== Component ==========

/**
 * @brief Network Time Protocol component
 * 
 * Provides NTP time synchronization with timezone support, formatted time strings,
 * and uptime tracking. Uses HAL abstraction for multi-platform support.
 * 
 * Features:
 * - Multiple NTP servers with automatic fallback
 * - Timezone management with DST support
 * - Configurable sync interval
 * - Sync status callbacks
 * - Formatted time strings (strftime)
 * - Uptime tracking
 * - Configuration persistence
 * 
 * @example Basic usage:
 * ```cpp
 * NTPConfig cfg;
 * cfg.timezone = Timezones::CET;
 * auto ntp = std::make_unique<NTPComponent>(cfg);
 * ntp->onSync([](bool success) {
 *     DLOG_I(LOG_NTP, "Time synced!");
 * });
 * core.addComponent(std::move(ntp));
 * ```
 */
class NTPComponent : public IComponent {
public:
    using SyncCallback = std::function<void(bool success)>;

    /**
     * @brief Construct NTP component with configuration
     * @param cfg NTP configuration
     */
    explicit NTPComponent(const NTPConfig& cfg = NTPConfig())
        : config(cfg), synced(false), syncInProgress(false), 
          bootTime(HAL::Platform::getMillis()), syncCallback(nullptr),
          syncTimeoutTimer(cfg.timeoutMs) {
        // Initialize component metadata immediately for dependency resolution
        metadata.name = "NTP";
        metadata.version = "1.3.0";
        metadata.author = "DomoticsCore";
        metadata.description = "Network Time Protocol synchronization component";
        syncTimeoutTimer.disable();  // Start disabled
        DLOG_D(LOG_NTP, "Component constructed");
    }

    virtual ~NTPComponent() {
        if (ntpStarted) {
            HAL::NTP::stop();
        }
        DLOG_D(LOG_NTP, "Component destroyed");
    }

    // ========== IComponent Interface ==========

    ComponentStatus begin() override {
        DLOG_I(LOG_NTP, "Starting component...");
        
        if (!config.enabled) {
            DLOG_W(LOG_NTP, "Component disabled");
            return ComponentStatus::Success;
        }

        if (__dc_eventBus) {
            on<NetworkEvents::NetworkAvailabilityEvent>(NetworkEvents::EVENT_READY,
                [this](const NetworkEvents::NetworkAvailabilityEvent& event) {
                    networkAvailable = event.available;
                    if (event.available && !ntpStarted) startClient();
                }, true);
            return ComponentStatus::Success;
        }

        startClient();
        return ComponentStatus::Success;
    }

    bool isClientStarted() const { return ntpStarted; }

private:
    void startClient() {
        if (ntpStarted || !config.enabled) return;

        // Set timezone via HAL
        HAL::NTP::setTimezone(config.timezone.c_str());
        DLOG_I(LOG_NTP, "Timezone set to: %s", config.timezone.c_str());

        // Configure NTP servers via HAL
        const char* srv1 = config.servers.size() > 0 ? config.servers[0].c_str() : "pool.ntp.org";
        const char* srv2 = config.servers.size() > 1 ? config.servers[1].c_str() : nullptr;
        const char* srv3 = config.servers.size() > 2 ? config.servers[2].c_str() : nullptr;
        
        for (size_t i = 0; i < config.servers.size() && i < 3; i++) {
            DLOG_I(LOG_NTP, "NTP server %zu: %s", i, config.servers[i].c_str());
        }
        
        // Set sync interval via HAL
        HAL::NTP::setSyncInterval(config.syncInterval * 1000);  // Convert to ms
        
        // Initialize NTP client via HAL
        HAL::NTP::init(srv1, srv2, srv3);
        ntpStarted = true;
        DLOG_I(LOG_NTP, "SNTP client started via HAL");
    }

public:

    ComponentStatus shutdown() override {
        if (ntpStarted) {
            HAL::NTP::stop();
            ntpStarted = false;
            DLOG_I(LOG_NTP, "SNTP client stopped via HAL");
        }
        return ComponentStatus::Success;
    }

    void loop() override {
        if (!config.enabled) return;

        // Check if time has been synced
        time_t now = time(nullptr);
        bool currentlySynced = (now > 1000000000);  // After 2001-09-09
        
        // Detect sync completion (initial or subsequent)
        if (currentlySynced) {
            if (!synced) {
                // First sync ever
                synced = true;
                stats.syncCount++;
                stats.lastSyncTime = now;
                stats.consecutiveFailures = 0;
                
                if (syncInProgress) {
                    stats.lastSyncDuration = syncTimeoutTimer.elapsed();
                    syncTimeoutTimer.disable();
                    syncInProgress = false;
                    DLOG_I(LOG_NTP, "Initial time sync completed after %u ms: %s", stats.lastSyncDuration, getFormattedTime().c_str());
                } else {
                    DLOG_I(LOG_NTP, "Initial time sync completed: %s", getFormattedTime().c_str());
                }
                
                // Emit event for orchestration
                emit(NTPEvents::EVENT_SYNCED, true);
                
                if (syncCallback) {
                    syncCallback(true);
                }
            } else if (syncInProgress && now != stats.lastSyncTime) {
                // Subsequent sync (manual or automatic)
                stats.syncCount++;
                stats.lastSyncTime = now;
                stats.lastSyncDuration = syncTimeoutTimer.elapsed();
                stats.consecutiveFailures = 0;
                syncTimeoutTimer.disable();
                syncInProgress = false;
                
                DLOG_I(LOG_NTP, "Time re-synchronized after %u ms: %s", stats.lastSyncDuration, getFormattedTime().c_str());
                
                // Emit event for orchestration
                emit(NTPEvents::EVENT_SYNCED, true);
                
                if (syncCallback) {
                    syncCallback(true);
                }
            }
        }
        
        // Check for sync timeout using NonBlockingDelay timer
        if (syncInProgress && syncTimeoutTimer.isReady()) {
            syncInProgress = false;
            syncTimeoutTimer.disable();
            stats.syncErrors++;
            stats.consecutiveFailures++;
            stats.lastFailTime = time(nullptr);
            
            DLOG_W(LOG_NTP, "Sync timeout after %lu ms (no response from NTP servers)", syncTimeoutTimer.getInterval());
            
            // Emit event for orchestration
            emit(NTPEvents::EVENT_SYNC_FAILED, true);
            
            if (syncCallback) {
                syncCallback(false);
            }
        }
    }


    // ========== Time Synchronization ==========

    /**
     * @brief Trigger immediate NTP sync
     * @return True if sync initiated
     */
    bool syncNow() {
        DLOG_I(LOG_NTP, "syncNow() called");
        
        if (!config.enabled) {
            DLOG_W(LOG_NTP, "Component disabled, cannot sync");
            return false;
        }
        
        if (syncInProgress) {
            DLOG_W(LOG_NTP, "Sync already in progress, ignoring request");
            return false;
        }

        DLOG_I(LOG_NTP, "Requesting immediate SNTP sync via HAL...");
        syncInProgress = true;
        
        // Use configured timeout for sync
        syncTimeoutTimer.setInterval(config.timeoutMs);
        syncTimeoutTimer.reset();
        syncTimeoutTimer.enable();
        
        // Request immediate sync via HAL (non-blocking)
        HAL::NTP::forceSync();
        
        DLOG_I(LOG_NTP, "SNTP sync requested, timeout: %u ms", config.timeoutMs);
        return true;
    }

    /**
     * @brief Check if time is synced
     * @return True if time has been synced at least once
     */
    bool isSynced() const {
        return synced && (time(nullptr) > 1000000000);
    }

    /**
     * @brief Get last successful sync time
     * @return Unix timestamp of last sync
     */
    time_t getLastSyncTime() const {
        return stats.lastSyncTime;
    }

    /**
     * @brief Get seconds until next automatic sync
     * @return Seconds until next sync, 0 if sync disabled
     */
    uint32_t getNextSyncIn() const {
        if (!isSynced() || !config.enabled) return 0;
        
        time_t now = time(nullptr);
        time_t elapsed = now - stats.lastSyncTime;
        
        if (elapsed >= (time_t)config.syncInterval) {
            return 0;
        }
        
        return config.syncInterval - elapsed;
    }

    // ========== Time Access ==========

    /**
     * @brief Get current Unix timestamp
     * @return Seconds since epoch (1970-01-01 00:00:00 UTC)
     */
    time_t getUnixTime() const {
        return time(nullptr);
    }

    /**
     * @brief Get local time as struct tm
     * @return Local time structure
     */
    struct tm getLocalTime() const {
        time_t now = time(nullptr);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        return timeinfo;
    }

    /**
     * @brief Get formatted time string
     * @param format strftime format string
     * @return Formatted time string
     * 
     * Common formats:
     * - "%Y-%m-%d %H:%M:%S" -> "2025-10-02 19:30:45"
     * - "%Y/%m/%d" -> "2025/10/02"
     * - "%H:%M" -> "19:30"
     * - "%A, %B %d, %Y" -> "Thursday, October 02, 2025"
     */
    String getFormattedTime(const char* format = "%Y-%m-%d %H:%M:%S") const {
        if (!isSynced()) {
            return "Not synced";
        }
        
        struct tm timeinfo = getLocalTime();
        char buffer[128];
        strftime(buffer, sizeof(buffer), format, &timeinfo);
        return String(buffer);
    }

    /**
     * @brief Get ISO 8601 formatted time string
     * @return Time string in format "2025-10-02T19:30:45+02:00"
     */
    String getISO8601() const {
        if (!isSynced()) {
            return "Not synced";
        }
        
        struct tm timeinfo = getLocalTime();
        char buffer[64];
        
        // Format date and time
        strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &timeinfo);
        String result = String(buffer);
        
        // Add timezone offset
        int offset = getGMTOffset();
        int offsetHours = offset / 3600;
        int offsetMinutes = abs(offset % 3600) / 60;
        
        char tzBuffer[12];
        snprintf(tzBuffer, sizeof(tzBuffer), "%+03d:%02d", offsetHours, offsetMinutes);
        result += tzBuffer;
        
        return result;
    }

    // ========== Uptime ==========

    /**
     * @brief Get milliseconds since boot
     * @return Uptime in milliseconds
     */
    uint64_t getUptimeMs() const {
        return HAL::Platform::getMillis() - bootTime;
    }

    /**
     * @brief Get formatted uptime string
     * @return Uptime string in format "2d 5h 32m 15s"
     */
    String getFormattedUptime() const {
        uint64_t ms = getUptimeMs();
        uint32_t seconds = ms / 1000;
        
        uint32_t days = seconds / 86400;
        seconds %= 86400;
        uint32_t hours = seconds / 3600;
        seconds %= 3600;
        uint32_t minutes = seconds / 60;
        seconds %= 60;
        
        String result;
        if (days > 0) result += String(days) + "d ";
        if (hours > 0 || days > 0) result += String(hours) + "h ";
        if (minutes > 0 || hours > 0 || days > 0) result += String(minutes) + "m ";
        result += String(seconds) + "s";
        
        return result;
    }

    // ========== Timezone Management ==========

    /**
     * @brief Set timezone using POSIX TZ string
     * @param tz POSIX timezone string (e.g., "CET-1CEST,M3.5.0,M10.5.0/3")
     */
    void setTimezone(const String& tz) {
        config.timezone = tz;
        HAL::NTP::setTimezone(tz.c_str());
        DLOG_I(LOG_NTP, "Timezone changed to: %s", tz.c_str());
    }

    /**
     * @brief Get current timezone string
     * @return POSIX timezone string
     */
    String getTimezone() const {
        return config.timezone;
    }

    /**
     * @brief Get GMT offset in seconds
     * @return Offset from GMT in seconds (positive for east, negative for west)
     */
    int getGMTOffset() const {
        time_t now = time(nullptr);
        struct tm utc_tm;
        struct tm local_tm;
        gmtime_r(&now, &utc_tm);
        localtime_r(&now, &local_tm);
        
        // Calculate offset in seconds
        time_t utc_time = mktime(&utc_tm);
        time_t local_time = mktime(&local_tm);
        return (int)(local_time - utc_time);
    }

    /**
     * @brief Check if currently in Daylight Saving Time
     * @return True if DST is active
     */
    bool isDST() const {
        struct tm timeinfo = getLocalTime();
        return timeinfo.tm_isdst > 0;
    }

    // ========== Configuration ==========

    /**
     * @brief Get current NTP configuration
     * @return Current NTPConfig
     */
    const NTPConfig& getConfig() const {
        return config;
    }

    /**
     * @brief Update configuration
     * @param cfg New configuration
     */
    void setConfig(const NTPConfig& cfg) {
        // Compute diffs BEFORE mutating config
        bool needsRestart = (cfg.enabled != config.enabled) ||
                            (cfg.servers != config.servers) ||
                            (cfg.syncInterval != config.syncInterval);
        bool tzChanged = (cfg.timezone != config.timezone);

        // Apply new config
        config = cfg;

        // Apply timezone immediately without restart
        if (tzChanged) {
            setTimezone(config.timezone);
        }

        if (needsRestart) {
            if (ntpStarted) {
                HAL::NTP::stop();
                ntpStarted = false;
            }
            if (config.enabled && (!__dc_eventBus || networkAvailable)) startClient();
        }
    }

    // ========== Callbacks ==========

    /**
     * @brief Register callback for sync events
     * @param callback Function called after each sync attempt (success/failure)
     */
    void onSync(SyncCallback callback) {
        syncCallback = callback;
    }

    // ========== Statistics ==========

    /**
     * @brief Get synchronization statistics
     * @return Statistics structure
     */
    const NTPStatistics& getStatistics() const {
        return stats;
    }

private:
    NTPConfig config;
    NTPStatistics stats;
    bool synced;
    bool syncInProgress;
    bool ntpStarted = false;
    bool networkAvailable = false;
    uint32_t bootTime;
    SyncCallback syncCallback;
    Utils::NonBlockingDelay syncTimeoutTimer;  // Timer for sync timeout tracking
};

} // namespace Components
} // namespace DomoticsCore
