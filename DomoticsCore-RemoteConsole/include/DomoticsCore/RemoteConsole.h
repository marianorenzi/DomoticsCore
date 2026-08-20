#pragma once

/**
 * @file RemoteConsole.h
 * @brief Telnet-based remote console for log streaming and command execution
 */

#include <DomoticsCore/IComponent.h>
#include <DomoticsCore/Logger.h>
#include <DomoticsCore/Platform_HAL.h>    // For restart()
#include <DomoticsCore/Network_HAL.h>
#include <DomoticsCore/NetworkEvents.h>
// Platform_HAL.h provides: getFreeHeap(), getChipModel(), getChipRevision(), getCpuFreqMHz()
#include <vector>
#include <map>
#include <functional>

namespace DomoticsCore {
namespace Components {

// Log entry structure - uses String for compact storage
struct LogEntry {
    uint32_t timestamp;
    LogLevel level;
    String tag;
    String message;
    
    LogEntry() : timestamp(0), level(LOG_LEVEL_INFO) {}
    LogEntry(uint32_t ts, LogLevel lvl, const char* t, const char* msg)
        : timestamp(ts), level(lvl), tag(t), message(msg) {}
};

// Remote console configuration
struct RemoteConsoleConfig {
    bool enabled = true;
    uint16_t port = 23;                    // Telnet port
    bool requireAuth = false;              // Password authentication
    String password = "";                  // Auth password
    uint32_t bufferSize = DOMOTICS_LOG_BUFFER_SIZE;  // Platform-specific (ESP8266=5, ESP32=100)
    bool allowCommands = true;             // Enable command execution
    uint32_t authTimeoutMs = 10000;        // Auth timeout (10s default, 0 = no timeout)
    std::vector<HAL::IPAddress> allowedIPs;     // IP whitelist (empty = all allowed)
    bool colorOutput = true;               // ANSI color codes
    uint32_t maxClients = 3;               // Max concurrent connections
    LogLevel defaultLogLevel = LOG_LEVEL_INFO;  // Initial log level
};

// Command handler function type
typedef std::function<String(const String& args)> CommandHandler;

/**
 * @class RemoteConsoleComponent
 * @brief Telnet server for remote log viewing and command execution
 * 
 * Features:
 * - Real-time log streaming via Telnet
 * - Circular log buffer with configurable size
 * - Runtime log level control
 * - Command processor with extensible commands
 * - ANSI color-coded output
 * - Password authentication
 * - IP whitelist support
 */
class RemoteConsoleComponent : public IComponent {
private:
    RemoteConsoleConfig config;
    HAL::NetworkServer* telnetServer = nullptr;
    LoggerCallbacks::CallbackId loggerCallbackId_ = 0;
    uint32_t nextClientId = 1;
    std::vector<std::pair<uint32_t, HAL::NetworkClient>> clients;
    std::map<String, String> networkAddresses;
    
    // Circular buffer for log entries - grows lazily to avoid OOM on startup
    std::vector<LogEntry> logBuffer;
    size_t logBufferHead = 0;    // Next write position
    size_t logBufferCount = 0;   // Current number of entries
    
    std::map<String, CommandHandler> commands;
    std::map<uint32_t, String> clientBuffers;  // Per-client command buffers (key = client ID)
    std::map<uint32_t, bool> clientAuthenticated;
    std::map<uint32_t, unsigned long> clientConnectTime;
    LogLevel currentLogLevel;
    std::vector<String> tagFilter;  // Empty = show all
    bool connectionInfoDisplayed = false;  // Track if we've shown connection info
    bool rebootPending = false;
    unsigned long rebootRequestedAt = 0;

public:
    RemoteConsoleComponent(const RemoteConsoleConfig& cfg = RemoteConsoleConfig())
        : config(cfg), currentLogLevel(cfg.defaultLogLevel) {
        
        metadata.name = "RemoteConsole";
        metadata.version = "1.4.2";
        metadata.author = "DomoticsCore";
        metadata.description = "Telnet-based remote console with log streaming";
        metadata.category = "Debug";
        metadata.tags = {"telnet", "console", "debug", "logging"};
        
        // Register built-in commands
        registerBuiltInCommands();
    }

    uint16_t getPort() const { return config.port; }
    HAL::NetworkServer* getServer() const { return telnetServer; }
    const std::map<String, String>& getNetworkAddresses() const { return networkAddresses; }

    LogLevel getLogLevel() const { return currentLogLevel; }

    bool setPort(uint16_t port) {
        if (port == 0) return false;
        if (config.port == port) return true;

        config.port = port;

        if (!config.enabled) {
            return true;
        }

        if (!telnetServer) {
            return true;
        }

        for (auto& [cid, client] : clients) {
            if (client.connected()) {
                client.println("\nRemoteConsole port changed - disconnecting...");
                client.stop();
            }
        }
        clients.clear();
        clientBuffers.clear();
        clientAuthenticated.clear();
        clientConnectTime.clear();
        clients.shrink_to_fit();

        telnetServer->stop();
        delete telnetServer;
        telnetServer = nullptr;

        telnetServer = new HAL::NetworkServer(config.port);
        telnetServer->begin();
        telnetServer->setNoDelay(true);

        DLOG_I(LOG_CONSOLE, "RemoteConsole restarted on port %d", config.port);
        return true;
    }
    
    ~RemoteConsoleComponent() {
        if (telnetServer) {
            delete telnetServer;
        }
    }
    
    
    ComponentStatus begin() override {
        if (!config.enabled) {
            DLOG_I(LOG_CONSOLE, "RemoteConsole disabled in config");
            setStatus(ComponentStatus::Success);
            return ComponentStatus::Success;
        }
        
        // Register logger callback
        loggerCallbackId_ = LoggerCallbacks::addCallback([this](LogLevel level, const char* tag, const char* msg) {
            this->log(level, tag, msg);
        });
        
        // Start the server before a provider is ready; lwIP binds it generically.
        on<NetworkEvents::NetworkProviderAddressEvent>(NetworkEvents::EVENT_PROVIDER_ADDRESS_CHANGED,
            [this](const NetworkEvents::NetworkProviderAddressEvent& event) {
                networkAddresses[String(event.providerId)] = String(event.address);
                connectionInfoDisplayed = false;
            });

        telnetServer = new HAL::NetworkServer(config.port);
        telnetServer->begin();
        telnetServer->setNoDelay(true);
        
        DLOG_I(LOG_CONSOLE, "RemoteConsole started on port %d", config.port);
        
        setStatus(ComponentStatus::Success);
        return ComponentStatus::Success;
    }
    
    void onComponentsReady(const ComponentRegistry& /*registry*/) override {
        // Display connection info for any provider address already announced.
        displayConnectionInfo();
    }
    
    void loop() override {
        // Non-blocking reboot (R9 — must be before status guard)
        if (rebootPending && (HAL::Platform::getMillis() - rebootRequestedAt >= 100)) {
            rebootPending = false;
            HAL::restart();
        }

        if (getLastStatus() != ComponentStatus::Success || !telnetServer) return;
        
        if (!connectionInfoDisplayed && !getNetworkAddresses().empty()) {
            displayConnectionInfo();
        }
        
        // Accept new clients (Task 28)
        if (telnetServer->hasClient()) {
            HAL::NetworkClient newClient = telnetServer->accept();

            if (newClient) {
                // Check max clients
                if (clients.size() >= config.maxClients) {
                    newClient.println("Max clients reached. Disconnecting.");
                    newClient.stop();
                } else if (!isIPAllowed(newClient.remoteIP())) {
                    newClient.println("IP not allowed. Disconnecting.");
                    newClient.stop();
                } else {
                    uint32_t clientId = nextClientId++;
                    clients.push_back({clientId, newClient});
                    clientAuthenticated[clientId] = !config.requireAuth;
                    clientConnectTime[clientId] = HAL::Platform::getMillis();
                    clientBuffers[clientId] = "";

                    DLOG_I(LOG_CONSOLE, "Client connected: #%u", clientId);

                    sendWelcome(clients.back().second);
                }
            }
        }

        // Auth timeout pass — separate from handleClient to avoid processing partially-disconnected clients
        if (config.requireAuth && config.authTimeoutMs > 0) {
            unsigned long now = HAL::Platform::getMillis();
            for (auto it = clients.begin(); it != clients.end(); ) {
                uint32_t cid = it->first;
                HAL::NetworkClient& client = it->second;
                if (!clientAuthenticated[cid] && (now - clientConnectTime[cid]) >= config.authTimeoutMs) {
                    client.println("Authentication timeout. Disconnecting.");
                    client.stop();
                    clientAuthenticated.erase(cid);
                    clientConnectTime.erase(cid);
                    clientBuffers.erase(cid);
                    it = clients.erase(it);
                } else {
                    ++it;
                }
            }
        }

        // Handle existing clients
        bool erased = false;
        for (auto it = clients.begin(); it != clients.end(); ) {
            uint32_t cid = it->first;
            HAL::NetworkClient& client = it->second;
            if (!client.connected()) {
                // Clean up all client state (Task 29)
                clientBuffers.erase(cid);
                clientAuthenticated.erase(cid);
                clientConnectTime.erase(cid);

                DLOG_I(LOG_CONSOLE, "Client disconnected: #%u", cid);
                it = clients.erase(it);
                erased = true;
            } else {
                handleClient(cid, client);
                ++it;
            }
        }
        if (erased) clients.shrink_to_fit();
    }
    
    ComponentStatus shutdown() override {
        LoggerCallbacks::removeCallback(loggerCallbackId_);
        if (telnetServer) {
            // Disconnect all clients
            for (auto& [cid, client] : clients) {
                client.println("\nRemoteConsole shutting down...");
                client.stop();
            }
            clients.clear();
            clientBuffers.clear();
            clientAuthenticated.clear();
            clientConnectTime.clear();
            clients.shrink_to_fit();

            telnetServer->stop();
            delete telnetServer;
            telnetServer = nullptr;
        }
        
        DLOG_I(LOG_CONSOLE, "RemoteConsole shut down");
        setStatus(ComponentStatus::Success);
        return ComponentStatus::Success;
    }
    
    /**
     * @brief Log a message to the buffer and connected clients
     */
    void log(LogLevel level, const char* tag, const char* message) {
        if (level > currentLogLevel) return;
        
        // Check tag filter
        if (!tagFilter.empty()) {
            bool tagMatch = false;
            for (const auto& filter : tagFilter) {
                if (filter == tag) {
                    tagMatch = true;
                    break;
                }
            }
            if (!tagMatch) return;
        }
        
        // Create entry on stack first (no heap allocation)
        LogEntry entry(HAL::Platform::getMillis(), level, tag, message);
        
        // Add to circular buffer - grow lazily up to max size
        if (config.bufferSize > 0) {
            if (logBufferCount < config.bufferSize) {
                // Buffer not full yet - just append
                logBuffer.push_back(entry);
                logBufferCount++;
            } else {
                // Buffer full - overwrite oldest (circular)
                logBuffer[logBufferHead] = entry;
            }
            logBufferHead = (logBufferHead + 1) % config.bufferSize;
        }
        
        // Send to connected clients (only authenticated ones when auth required)
        if (!clients.empty()) {
            String formatted = formatLogEntry(entry);
            for (auto& [cid, client] : clients) {
                if (client.connected() && clientAuthenticated.count(cid) && clientAuthenticated[cid]) {
                    client.print(formatted);
                }
            }
        }
    }
    
    /**
     * @brief Register a custom command
     */
    void registerCommand(const String& cmd, CommandHandler handler) {
        commands[cmd] = handler;
        DLOG_D(LOG_CONSOLE, "Registered command: %s", cmd.c_str());
    }
    
    /**
     * @brief Set runtime log level
     */
    void setLogLevel(LogLevel level) {
        currentLogLevel = level;
        DLOG_I(LOG_CONSOLE, "Log level set to: %d", level);
    }
    
    /**
     * @brief Set tag filter (empty = show all)
     */
    void setTagFilter(const std::vector<String>& tags) {
        tagFilter = tags;
    }
    
    /**
     * @brief Clear log buffer and release memory
     */
    void clearBuffer() {
        logBuffer.clear();
        logBuffer.shrink_to_fit();  // Release memory back to heap
        logBufferHead = 0;
        logBufferCount = 0;
        DLOG_I(LOG_CONSOLE, "Log buffer cleared");
    }
    
    /**
     * @brief Get recent logs from circular buffer
     */
    std::vector<LogEntry> getRecentLogs(uint32_t count = 100) {
        std::vector<LogEntry> result;
        if (logBufferCount == 0 || config.bufferSize == 0) return result;
        
        uint32_t actualCount = (count < logBufferCount) ? count : logBufferCount;
        result.reserve(actualCount);
        
        // Calculate start position in circular buffer
        size_t startIdx;
        if (logBufferCount < config.bufferSize) {
            // Buffer not full yet - start from beginning
            startIdx = (logBufferCount > actualCount) ? (logBufferCount - actualCount) : 0;
        } else {
            // Buffer is full - oldest is at logBufferHead
            startIdx = (logBufferHead + config.bufferSize - actualCount) % config.bufferSize;
        }
        
        for (uint32_t i = 0; i < actualCount; i++) {
            size_t idx = (startIdx + i) % config.bufferSize;
            result.push_back(logBuffer[idx]);
        }
        
        return result;
    }

private:
    void registerBuiltInCommands() {
        // Help command
        registerCommand("help", [this](const String& args) {
            String help = "\nAvailable commands:\n";
            help += "  help              - Show this help\n";
            help += "  clear             - Clear log buffer\n";
            help += "  level <level>     - Set log level (0-4: NONE/ERROR/WARN/INFO/DEBUG)\n";
            help += "  filter <tag>      - Filter logs by tag (empty = show all)\n";
            help += "  info              - System information\n";
            help += "  heap              - Memory usage\n";
            help += "  reboot            - Restart device\n";
            help += "  auth <password>   - Authenticate (if auth required)\n";
            help += "  quit              - Disconnect\n";
            
            // Add custom commands
            for (const auto& cmd : commands) {
                if (cmd.first != "help" && cmd.first != "clear" && 
                    cmd.first != "level" && cmd.first != "filter" &&
                    cmd.first != "info" && cmd.first != "heap" && 
                    cmd.first != "reboot" && cmd.first != "quit") {
                    help += "  " + cmd.first + "\n";
                }
            }
            
            return help;
        });
        
        // Clear command
        registerCommand("clear", [this](const String& args) {
            clearBuffer();
            return "Log buffer cleared\n";
        });
        
        // Level command
        registerCommand("level", [this](const String& args) -> String {
            char buf[128];
            if (args.isEmpty()) {
                snprintf(buf, sizeof(buf), "Current log level: %d\n", (int)currentLogLevel);
                return String(buf);
            }

            int level = args.toInt();
            if (level < 0 || level > 4) {
                return String("Invalid level. Use 0-4 (NONE/ERROR/WARN/INFO/DEBUG)\n");
            }

            setLogLevel((LogLevel)level);
            snprintf(buf, sizeof(buf), "Log level set to: %d\n", level);
            return String(buf);
        });
        
        // Filter command
        registerCommand("filter", [this](const String& args) -> String {
            if (args.isEmpty()) {
                tagFilter.clear();
                return String("Tag filter cleared (showing all)\n");
            }

            tagFilter.clear();
            tagFilter.push_back(args);
            char buf[128];
            snprintf(buf, sizeof(buf), "Filtering logs by tag: %s\n", args.c_str());
            return String(buf);
        });
        
        // Info command
        registerCommand("info", [this](const String& args) {
            char buf[384];
            const auto& addresses = getNetworkAddresses();
            snprintf(buf, sizeof(buf),
                     "\nSystem Information:\n"
                     "  Uptime: %lus\n"
                     "  Free Heap: %lu bytes\n"
                     "  Chip: %s Rev%d\n"
                     "  CPU Freq: %lu MHz\n"
                     "  Network: ",
                     (unsigned long)(HAL::Platform::getMillis() / 1000),
                     (unsigned long)HAL::getFreeHeap(),
                     HAL::getChipModel().c_str(), HAL::getChipRevision(),
                     (unsigned long)HAL::getCpuFreqMHz());
            if (addresses.empty()) {
                snprintf(buf+strlen(buf), sizeof(buf)-strlen(buf), "unavailable\n");
            } else {
                for (const auto& entry : addresses) {
                    if (entry.second.isEmpty()) continue;
                    snprintf(buf+strlen(buf), sizeof(buf)-strlen(buf), "\n    %s: %s",
                        entry.first.c_str(), entry.second.c_str());
                }
                snprintf(buf+strlen(buf), sizeof(buf)-strlen(buf), "\n");
            }
            return String(buf);
        });
        
        // Heap command
        registerCommand("heap", [this](const String& args) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Free Heap: %lu bytes\n", (unsigned long)HAL::getFreeHeap());
            return String(buf);
        });
        
        // Reboot command
        registerCommand("reboot", [this](const String& args) {
            for (auto& [cid, client] : clients) {
                client.println("Rebooting...");
            }
            rebootRequestedAt = HAL::Platform::getMillis();
            rebootPending = true;
            return "";
        });
        
        // Quit command
        registerCommand("quit", [this](const String& args) {
            return "QUIT";  // Special return value to disconnect
        });
    }
    
    bool isIPAllowed(HAL::IPAddress ip) {
        if (config.allowedIPs.empty()) return true;
        
        for (const auto& allowed : config.allowedIPs) {
            if (ip == allowed) return true;
        }
        
        return false;
    }
    
    void sendWelcome(HAL::NetworkClient& client) {
        client.println("\n========================================");
        client.println("  DomoticsCore Remote Console");
        client.println("========================================");

        if (config.requireAuth) {
            client.println("Authentication required. Use: auth <password>\n");
        } else {
            client.println("Type 'help' for available commands\n");

            // Show recent logs only if not requiring auth
            auto recent = getRecentLogs(10);
            if (!recent.empty()) {
                client.println("Recent logs:");
                for (const auto& entry : recent) {
                    client.print(formatLogEntry(entry));
                }
                client.println();
            }
        }

        client.print("> ");  // Show initial prompt
    }
    
    void handleClient(uint32_t clientId, HAL::NetworkClient& client) {
        while (client.available()) {
            char c = client.read();

            // Get or create command buffer for this client
            String& commandBuffer = clientBuffers[clientId];

            // Handle newline (command complete)
            if (c == '\n' || c == '\r') {
                // Skip if buffer is empty (just a newline from log output)
                if (commandBuffer.isEmpty()) {
                    continue;
                }

                String line = commandBuffer;
                commandBuffer = "";  // Clear buffer

                line.trim();

                // Remove any non-printable characters (telnet negotiation)
                String cleaned = "";
                for (size_t i = 0; i < line.length(); i++) {
                    char ch = line.charAt(i);
                    if (ch >= 32 && ch < 127) {  // Only printable ASCII
                        cleaned += ch;
                    }
                }
                line = cleaned;

                if (line.isEmpty()) continue;

                // Debug: log what we received
                DLOG_D(LOG_CONSOLE, "Command received: '%s' (len=%d)", line.c_str(), line.length());

                // Skip if command contains non-alphanumeric at start (telnet noise)
                if (line.length() > 0 && !isalnum(line.charAt(0)) && line.charAt(0) != ' ') {
                    DLOG_D(LOG_CONSOLE, "Skipping command with non-alphanumeric start: 0x%02X", line.charAt(0));
                    continue;
                }

                // Parse command and args
                int spacePos = line.indexOf(' ');
                String cmd = spacePos > 0 ? line.substring(0, spacePos) : line;
                String args = spacePos > 0 ? line.substring(spacePos + 1) : "";

                cmd.trim();
                args.trim();
                cmd.toLowerCase();

                // Handle auth command specially (needs client context)
                if (cmd == "auth") {
                    if (!config.requireAuth) {
                        client.println("Authentication not required.");
                    } else if (config.password == args) {
                        clientAuthenticated[clientId] = true;
                        client.println("Authentication successful!");
                    } else {
                        client.println("Authentication failed.");
                    }
                    client.print("> ");
                    continue;
                }

                // Block commands if not authenticated (allow help + quit for discoverability)
                if (config.requireAuth
                    && (!clientAuthenticated.count(clientId) || !clientAuthenticated[clientId])
                    && cmd != "help" && cmd != "quit") {
                    client.println("Authentication required. Use: auth <password>");
                    client.print("> ");
                    continue;
                }

                // Block commands if allowCommands is false (except help, quit)
                if (!config.allowCommands && cmd != "help" && cmd != "quit") {
                    client.println("Commands are disabled.");
                    client.print("> ");
                    continue;
                }

                // Execute command
                auto it = commands.find(cmd);
                if (it != commands.end()) {
                    String result = it->second(args);

                    if (result == "QUIT") {
                        client.println("Goodbye!");
                        client.stop();
                        return;
                    }

                    if (!result.isEmpty()) {
                        client.print(result);
                    }
                    client.print("> ");  // Show prompt after command
                } else {
                    char buf[128];
                    snprintf(buf, sizeof(buf), "Unknown command: %s (type 'help' for commands)", cmd.c_str());
                    client.println(buf);
                    client.print("> ");  // Show prompt after error
                }
            }
            // Handle backspace
            else if (c == '\b' || c == 127) {
                if (commandBuffer.length() > 0) {
                    commandBuffer.remove(commandBuffer.length() - 1);
                }
            }
            // Handle printable characters ONLY
            else if (c >= 32 && c < 127) {
                commandBuffer += c;
            }
            // Silently ignore all other control characters (telnet negotiation, etc.)
            // This includes: 0-31 (control chars), 127+ (extended ASCII, telnet IAC)
        }
    }
    
    static const char* getColorForLevel(LogLevel level) {
        switch (level) {
            case LOG_LEVEL_ERROR: return "\033[31m";
            case LOG_LEVEL_WARN:  return "\033[33m";
            case LOG_LEVEL_INFO:  return "\033[32m";
            case LOG_LEVEL_DEBUG: return "\033[36m";
            default:              return "";
        }
    }

    String formatLogEntry(const LogEntry& entry) {
        // Header: color(5) + timestamp(12) + level(6) + tag brackets/space(5) = ~28 fixed
        // Dynamic: tag + message — use stack buffer for typical case, fallback for long messages
        size_t needed = 40 + entry.tag.length() + entry.message.length();
        if (needed <= 256) {
            char buf[256];
            if (config.colorOutput) {
                snprintf(buf, sizeof(buf), "%s[%lu][%s][%s] %s\033[0m\n",
                         getColorForLevel(entry.level),
                         (unsigned long)entry.timestamp,
                         logLevelToString(entry.level).c_str(),
                         entry.tag.c_str(), entry.message.c_str());
            } else {
                snprintf(buf, sizeof(buf), "[%lu][%s][%s] %s\n",
                         (unsigned long)entry.timestamp,
                         logLevelToString(entry.level).c_str(),
                         entry.tag.c_str(), entry.message.c_str());
            }
            return String(buf);
        }
        // Fallback for long messages — single String concat
        String formatted;
        if (config.colorOutput) formatted += getColorForLevel(entry.level);
        char hdr[32];
        snprintf(hdr, sizeof(hdr), "[%lu][%s]", (unsigned long)entry.timestamp, logLevelToString(entry.level).c_str());
        formatted += hdr;
        formatted += "[";
        formatted += entry.tag;
        formatted += "] ";
        formatted += entry.message;
        if (config.colorOutput) formatted += "\033[0m";
        formatted += "\n";
        return formatted;
    }
    
    String logLevelToString(LogLevel level) {
        switch (level) {
            case LOG_LEVEL_NONE:  return "NONE";
            case LOG_LEVEL_ERROR: return "E";
            case LOG_LEVEL_WARN:  return "W";
            case LOG_LEVEL_INFO:  return "I";
            case LOG_LEVEL_DEBUG: return "D";
            default: return "?";
        }
    }
    
    void displayConnectionInfo() {
        if (connectionInfoDisplayed) return;
        
        const auto& addresses = getNetworkAddresses();
        for (const auto& entry : addresses) {
            if (entry.second.isEmpty()) continue;
            DLOG_I(LOG_CONSOLE, "Connect via %s: telnet <%s> %d",
                   entry.first.c_str(), entry.second.c_str(), config.port);
            connectionInfoDisplayed = true;
        }
    }
};

} // namespace Components
} // namespace DomoticsCore
