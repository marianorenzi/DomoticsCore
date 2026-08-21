#pragma once

#include <DomoticsCore/Platform_HAL.h>
#include <DomoticsCore/WebUI_HAL.h>  // For WEBUI_* constants
#include <vector>
#include <map>
#include <functional>
#include <memory>
#include <ArduinoJson.h>

namespace DomoticsCore {
namespace Components {

/**
 * @brief Helper for lazy state initialization and change tracking
 * 
 * This template class provides timing-independent state tracking for WebUI providers.
 * It handles the common pattern where providers may be created before components
 * are fully initialized.
 * 
 * Usage:
 * @code
 * LazyState<bool> connectedState;
 * 
 * // In hasDataChanged():
 * return connectedState.hasChanged(wifi->isConnected());
 * @endcode
 */
template<typename T>
struct LazyState {
    T value;
    bool initialized = false;
    
    /**
     * @brief Initialize state on first access
     * @param initializer Function that returns the initial value
     * @return Reference to the stored value
     */
    T& get(std::function<T()> initializer) {
        if (!initialized) {
            value = initializer();
            initialized = true;
        }
        return value;
    }
    
    /**
     * @brief Check if value has changed, update internal state
     * @param current Current value to compare against
     * @return true if value changed OR first call (to ensure initial state is sent)
     * 
     * On first call (uninitialized): stores value and returns TRUE (initial state needs to be sent)
     * On subsequent calls: compares with stored value, updates, returns true if changed
     */
    bool hasChanged(const T& current) {
        if (!initialized) {
            value = current;
            initialized = true;
            return true;  // First check returns true to ensure initial state is sent
        }
        bool changed = (current != value);
        value = current;
        return changed;
    }
    
    /**
     * @brief Get current stored value
     * @return Stored value (undefined if not initialized)
     */
    const T& getValue() const {
        return value;
    }
    
    /**
     * @brief Check if state has been initialized
     * @return true if initialized
     */
    bool isInitialized() const {
        return initialized;
    }
    
    /**
     * @brief Reset state to uninitialized
     */
    void reset() {
        initialized = false;
    }
};

/**
 * Enhanced WebUI system with multi-context support
 * Allows components to display data in multiple locations with different presentations
 */

enum class WebUILocation {
    Dashboard,          // Main dashboard overview
    ComponentDetail,    // Detailed component view  
    HeaderStatus,       // Top-right status indicators
    QuickControls,      // Sidebar quick actions
    Settings,           // Settings/configuration area
    HeaderInfo          // Main header info zone (time, uptime, etc.) - NEW: Added at end to preserve existing enum values
};

enum class WebUIPresentation {
    Card,              // Standard card layout
    Gauge,             // Circular gauge/meter
    Graph,             // Time-series chart
    StatusBadge,       // Small status indicator
    ProgressBar,       // Progress/percentage bar
    Table,             // Tabular data
    Toggle,            // On/off switch
    Slider,            // Range control
    Text,              // Simple text display
    Button             // Action button
};

enum class WebUIFieldType {
    Text,              // Text input/display
    Number,            // Number input/display
    Float,             // Float input/display
    Boolean,           // Checkbox/toggle
    Select,            // Dropdown selection
    Slider,            // Range slider
    Color,             // Color picker
    Button,            // Action button
    Display,           // Read-only display
    Chart,             // Chart data (auto-rendered by frontend with history)
    Status,            // Status indicator
    Progress,          // Progress value
    Password,          // Password input
    File,              // File upload input
    Multiselect,       // Multi-line multiple selection
    OrderedList        // Runtime list reordered with drag-and-drop
};

/**
 * Enhanced field definition with context-aware configuration
 * 
 * NOTE: Strings are stored as Arduino String objects to prevent dangling
 * pointer issues when contexts are cached in CachingWebUIProvider.
 * This uses more RAM than const char* but ensures memory safety.
 */
struct WebUIField {
    String name;                    // Field identifier (dynamic storage)
    String label;                   // Display label (dynamic storage)
    WebUIFieldType type;            // Field type
    String value;                   // Default value (dynamic storage)
    std::vector<String> values;     // Default values for multi-value fields
    String unit;                    // Unit of measurement (dynamic storage)
    bool readOnly;                  // Read-only flag
    
    // Hybrid static pointers — when non-null, these take priority over String members
    const char* namePtr = nullptr;
    const char* labelPtr = nullptr;
    const char* valuePtr = nullptr;
    const char* unitPtr = nullptr;
    const char* endpointPtr = nullptr;

    // Constraints and options
    float minValue = 0;
    float maxValue = 100;
    std::vector<String> options;    // Available option values
    std::map<String, String> optionLabels;  // Option value -> label mapping
    String endpoint;                // API endpoint for updates (dynamic storage)

    // Context-specific configuration
    // Use pointer to avoid large JsonDocument allocation on stack/heap for every field
    // Only allocated when configure() is called - most fields don't need custom config
    std::unique_ptr<JsonDocument> config;  // Custom field configuration (optional)

    // Constructor with const char* — stores pointers directly, no String allocation
    WebUIField(const char* n, const char* l, WebUIFieldType t,
               const char* v = "", const char* u = "", bool ro = false)
        : type(t), readOnly(ro),
          namePtr(n), labelPtr(l), valuePtr(v), unitPtr(u) {}
    
    // Constructor with String — stores in String members, pointers remain null
    WebUIField(const String& n, const String& l, WebUIFieldType t,
               const String& v = "", const String& u = "", bool ro = false)
        : name(n), label(l), type(t), value(v), unit(u), readOnly(ro) {}

    // Copy constructor - preserves hybrid state
    WebUIField(const WebUIField& other)
        : name(other.name), label(other.label), type(other.type), value(other.value), values(other.values),
          unit(other.unit), readOnly(other.readOnly),
          namePtr(other.namePtr), labelPtr(other.labelPtr),
          valuePtr(other.valuePtr), unitPtr(other.unitPtr),
          endpointPtr(other.endpointPtr),
          minValue(other.minValue), maxValue(other.maxValue),
          options(other.options), optionLabels(other.optionLabels),
          endpoint(other.endpoint) {
        if (other.config) {
            config = std::make_unique<JsonDocument>(*other.config);
        }
    }

    // Copy assignment - preserves hybrid state
    WebUIField& operator=(const WebUIField& other) {
        if (this != &other) {
            name = other.name;
            label = other.label;
            type = other.type;
            value = other.value;
            values = other.values;
            unit = other.unit;
            readOnly = other.readOnly;
            namePtr = other.namePtr;
            labelPtr = other.labelPtr;
            valuePtr = other.valuePtr;
            unitPtr = other.unitPtr;
            endpointPtr = other.endpointPtr;
            minValue = other.minValue;
            maxValue = other.maxValue;
            options = other.options;
            optionLabels = other.optionLabels;
            endpoint = other.endpoint;
            if (other.config) {
                config = std::make_unique<JsonDocument>(*other.config);
            } else {
                config.reset();
            }
        }
        return *this;
    }

    // Move constructor and assignment are defaulted
    WebUIField(WebUIField&&) = default;
    WebUIField& operator=(WebUIField&&) = default;

    // Hybrid accessors — return Ptr if non-null, else String::c_str()
    const char* getNameCStr() const { return namePtr ? namePtr : name.c_str(); }
    const char* getLabelCStr() const { return labelPtr ? labelPtr : label.c_str(); }
    const char* getValueCStr() const { return valuePtr ? valuePtr : value.c_str(); }
    const char* getUnitCStr() const { return unitPtr ? unitPtr : unit.c_str(); }
    const char* getEndpointCStr() const { return endpointPtr ? endpointPtr : endpoint.c_str(); }
    bool isMultiValue() const {
        return type == WebUIFieldType::Multiselect || type == WebUIFieldType::OrderedList;
    }

    // Fluent interface
    WebUIField& range(float min, float max) { minValue = min; maxValue = max; return *this; }
    WebUIField& choices(const std::vector<String>& opts, bool addVals = false) {
        options = opts;
        if (addVals) setValues(opts);
        return *this;
    }
    WebUIField& setValues(const std::vector<String>& vals) {
        values = vals;
        return *this;
    }
    WebUIField& addOption(const String& val, const String& lbl, bool addVal = false) {
        options.push_back(val);
        optionLabels[val] = lbl;
        if (addVal) addValue(val);
        return *this;
    }
    WebUIField& addValue(const String& val) {
        values.push_back(val);
        return *this;
    }
    WebUIField& api(const char* ep) { endpointPtr = ep; endpoint = ""; return *this; }
    WebUIField& configure(const String& key, const JsonVariant& val) {
        if (!config) {
            config = std::make_unique<JsonDocument>();
        }
        (*config)[key] = val;
        return *this;
    }
};

/**
 * WebUI context - defines how component data appears in specific UI location
 * 
 * MEMORY OPTIMIZATION (ESP8266):
 * Static strings (contextId, title, icon, apiEndpoint) are stored as const char*
 * pointers which can point to PROGMEM data, saving ~100+ bytes per context.
 */
struct WebUIContext {
    String contextId;               // Unique context identifier (dynamic storage)
    String title;                   // Context title (dynamic storage)
    String icon;                    // Icon class/name (dynamic storage)
    WebUILocation location;         // Where to display
    WebUIPresentation presentation; // How to display
    int priority = 0;               // Display order (higher = first)
    
    // Hybrid static pointers for identity fields — when non-null, take priority over String
    const char* contextIdPtr = nullptr;
    const char* titlePtr = nullptr;
    const char* iconPtr = nullptr;
    const char* apiEndpointPtr = nullptr;

    // Component-provided custom UI elements (usually empty - only allocated when used)
    // Hybrid storage: const char* for static/Flash content, String for dynamic content
    String customHtml;              // Dynamic custom HTML (heap-allocated)
    String customCss;               // Dynamic custom CSS
    String customJs;                // Dynamic custom JS
    const char* customHtmlPtr = nullptr;  // Static/Flash custom HTML (no heap)
    const char* customCssPtr = nullptr;   // Static/Flash custom CSS
    const char* customJsPtr = nullptr;    // Static/Flash custom JS
    
    std::vector<WebUIField> fields; // Context fields
    String apiEndpoint;             // API endpoint for this context (dynamic storage)
    bool realTime = false;          // Enable real-time updates
    int updateInterval = 5000;      // Update interval in ms
    bool alwaysInteractive = false; // If true, controls are always enabled (bypassing Settings lock)

    // Context-specific configuration
    // Use pointer to avoid large JsonDocument allocation on stack/heap for every context
    // Only allocated when configure() is called - most contexts don't need custom config
    std::unique_ptr<JsonDocument> contextConfig;  // Custom presentation config (optional)

    // Constructors
    WebUIContext() = default;

    // Constructor with const char* — stores pointers directly, no String allocation
    WebUIContext(const char* id, const char* t, const char* ic,
                 WebUILocation loc, WebUIPresentation pres = WebUIPresentation::Card)
        : location(loc), presentation(pres),
          contextIdPtr(id), titlePtr(t), iconPtr(ic) {}

    // Constructor with String (for backward compatibility)
    WebUIContext(const String& id, const String& t, const String& ic,
                 WebUILocation loc, WebUIPresentation pres = WebUIPresentation::Card)
        : contextId(id), title(t), icon(ic), location(loc), presentation(pres) {}

    // Copy constructor - preserves hybrid state for all Ptr/String pairs
    WebUIContext(const WebUIContext& other)
        : contextId(other.contextId), title(other.title), icon(other.icon),
          location(other.location), presentation(other.presentation), priority(other.priority),
          contextIdPtr(other.contextIdPtr), titlePtr(other.titlePtr),
          iconPtr(other.iconPtr), apiEndpointPtr(other.apiEndpointPtr),
          customHtml(other.customHtml), customCss(other.customCss), customJs(other.customJs),
          customHtmlPtr(other.customHtmlPtr), customCssPtr(other.customCssPtr), customJsPtr(other.customJsPtr),
          fields(other.fields), apiEndpoint(other.apiEndpoint), realTime(other.realTime),
          updateInterval(other.updateInterval), alwaysInteractive(other.alwaysInteractive) {
        if (other.contextConfig) {
            contextConfig = std::make_unique<JsonDocument>(*other.contextConfig);
        }
    }

    // Copy assignment - preserves hybrid state
    WebUIContext& operator=(const WebUIContext& other) {
        if (this != &other) {
            contextId = other.contextId;
            title = other.title;
            icon = other.icon;
            location = other.location;
            presentation = other.presentation;
            priority = other.priority;
            contextIdPtr = other.contextIdPtr;
            titlePtr = other.titlePtr;
            iconPtr = other.iconPtr;
            apiEndpointPtr = other.apiEndpointPtr;
            customHtml = other.customHtml;
            customCss = other.customCss;
            customJs = other.customJs;
            customHtmlPtr = other.customHtmlPtr;
            customCssPtr = other.customCssPtr;
            customJsPtr = other.customJsPtr;
            fields = other.fields;
            apiEndpoint = other.apiEndpoint;
            realTime = other.realTime;
            updateInterval = other.updateInterval;
            alwaysInteractive = other.alwaysInteractive;
            if (other.contextConfig) {
                contextConfig = std::make_unique<JsonDocument>(*other.contextConfig);
            } else {
                contextConfig.reset();
            }
        }
        return *this;
    }

    // Move constructor and assignment are defaulted (unique_ptr handles move correctly)
    WebUIContext(WebUIContext&&) = default;
    WebUIContext& operator=(WebUIContext&&) = default;
    
    // Fluent interface
    WebUIContext& withField(const WebUIField& field) { 
        fields.push_back(field); 
        return *this; 
    }
    
    WebUIContext& withAPI(const char* ep) { 
        apiEndpointPtr = ep;
        apiEndpoint = "";
        return *this; 
    }
    WebUIContext& withAPI(const String& ep) { 
        apiEndpoint = ep;
        apiEndpointPtr = nullptr;
        return *this; 
    }
    
    WebUIContext& withRealTime(int interval = 5000) { 
        realTime = true; 
        updateInterval = interval; 
        return *this; 
    }

    WebUIContext& withAlwaysInteractive(bool interactive = true) {
        alwaysInteractive = interactive;
        return *this;
    }
    
    WebUIContext& withPriority(int p) {
        priority = p;
        return *this;
    }

    WebUIContext& configure(const String& key, const JsonVariant& value) {
        if (!contextConfig) {
            contextConfig = std::make_unique<JsonDocument>();
        }
        (*contextConfig)[key] = value;
        return *this;
    }
    
    // Static content (const char* / Flash / PROGMEM) — no heap allocation
    WebUIContext& withCustomHtml(const char* html) {
        customHtmlPtr = html;
        customHtml = String();
        return *this;
    }
    WebUIContext& withCustomCss(const char* css) {
        customCssPtr = css;
        customCss = String();
        return *this;
    }
    WebUIContext& withCustomJs(const char* js) {
        customJsPtr = js;
        customJs = String();
        return *this;
    }

    // Dynamic content (String / runtime-generated) — heap-allocated
    WebUIContext& withCustomHtmlDynamic(const String& html) {
        customHtml = html;
        customHtmlPtr = nullptr;
        return *this;
    }
    WebUIContext& withCustomCssDynamic(const String& css) {
        customCss = css;
        customCssPtr = nullptr;
        return *this;
    }
    WebUIContext& withCustomJsDynamic(const String& js) {
        customJs = js;
        customJsPtr = nullptr;
        return *this;
    }

    // Hybrid accessors for identity fields — return Ptr if non-null, else String::c_str()
    const char* getContextIdCStr() const { return contextIdPtr ? contextIdPtr : contextId.c_str(); }
    const char* getTitleCStr() const { return titlePtr ? titlePtr : title.c_str(); }
    const char* getIconCStr() const { return iconPtr ? iconPtr : icon.c_str(); }
    const char* getApiEndpointCStr() const { return apiEndpointPtr ? apiEndpointPtr : apiEndpoint.c_str(); }

    // Hybrid accessors for custom content — return Ptr if set, else String
    bool hasCustomHtml() const { return customHtmlPtr != nullptr || customHtml.length() > 0; }
    bool hasCustomCss() const { return customCssPtr != nullptr || customCss.length() > 0; }
    bool hasCustomJs() const { return customJsPtr != nullptr || customJs.length() > 0; }

    const char* getCustomHtmlCStr() const { return customHtmlPtr ? customHtmlPtr : customHtml.c_str(); }
    const char* getCustomCssCStr() const { return customCssPtr ? customCssPtr : customCss.c_str(); }
    const char* getCustomJsCStr() const { return customJsPtr ? customJsPtr : customJs.c_str(); }

    size_t getCustomHtmlLen() const { return customHtmlPtr ? strlen(customHtmlPtr) : customHtml.length(); }
    size_t getCustomCssLen() const { return customCssPtr ? strlen(customCssPtr) : customCss.length(); }
    size_t getCustomJsLen() const { return customJsPtr ? strlen(customJsPtr) : customJs.length(); }
    
    // Factory methods — const char* overloads store pointers directly (zero heap)
    static WebUIContext dashboard(const char* id, const char* title, const char* icon = "fas fa-tachometer-alt") {
        return WebUIContext(id, title, icon, WebUILocation::Dashboard, WebUIPresentation::Card);
    }
    static WebUIContext gauge(const char* id, const char* title, const char* icon = "fas fa-gauge") {
        return WebUIContext(id, title, icon, WebUILocation::Dashboard, WebUIPresentation::Gauge);
    }
    static WebUIContext statusBadge(const char* id, const char* title, const char* icon = "dc-info") {
        return WebUIContext(id, title, icon, WebUILocation::HeaderStatus, WebUIPresentation::StatusBadge);
    }
    static WebUIContext headerInfo(const char* id, const char* label, const char* icon = "dc-info") {
        return WebUIContext(id, label, icon, WebUILocation::HeaderInfo, WebUIPresentation::Text);
    }
    static WebUIContext graph(const char* id, const char* title, const char* icon = "dc-chart") {
        return WebUIContext(id, title, icon, WebUILocation::ComponentDetail, WebUIPresentation::Graph);
    }
    static WebUIContext quickControl(const char* id, const char* title, const char* icon = "dc-settings") {
        return WebUIContext(id, title, icon, WebUILocation::QuickControls, WebUIPresentation::Toggle);
    }
    static WebUIContext settings(const char* id, const char* title, const char* icon = "dc-cog") {
        return WebUIContext(id, title, icon, WebUILocation::Settings, WebUIPresentation::Card);
    }

    // Factory methods — String overloads for backward compatibility
    static WebUIContext dashboard(const String& id, const String& title, const String& icon = "fas fa-tachometer-alt") {
        return WebUIContext(id, title, icon, WebUILocation::Dashboard, WebUIPresentation::Card);
    }
    static WebUIContext gauge(const String& id, const String& title, const String& icon = "fas fa-gauge") {
        return WebUIContext(id, title, icon, WebUILocation::Dashboard, WebUIPresentation::Gauge);
    }
    static WebUIContext statusBadge(const String& id, const String& title, const String& icon = "dc-info") {
        return WebUIContext(id, title, icon, WebUILocation::HeaderStatus, WebUIPresentation::StatusBadge);
    }
    static WebUIContext headerInfo(const String& id, const String& label, const String& icon = "dc-info") {
        return WebUIContext(id, label, icon, WebUILocation::HeaderInfo, WebUIPresentation::Text);
    }
    static WebUIContext graph(const String& id, const String& title, const String& icon = "dc-chart") {
        return WebUIContext(id, title, icon, WebUILocation::ComponentDetail, WebUIPresentation::Graph);
    }
    static WebUIContext quickControl(const String& id, const String& title, const String& icon = "dc-settings") {
        return WebUIContext(id, title, icon, WebUILocation::QuickControls, WebUIPresentation::Toggle);
    }
    static WebUIContext settings(const String& id, const String& title, const String& icon = "dc-cog") {
        return WebUIContext(id, title, icon, WebUILocation::Settings, WebUIPresentation::Card);
    }
};

/**
 * WebUI Provider interface
 * Components implement this to provide multi-context UI integration
 */
class IWebUIProvider {
public:
    virtual ~IWebUIProvider() = default;

    /**
     * Iterate over contexts without copying (memory optimization)
     * @param callback Called for each context; return false to stop iteration
     */
    virtual void forEachContext(std::function<bool(const WebUIContext&)> callback) = 0;

    /**
     * Get context count
     */
    virtual size_t getContextCount() = 0;

    /**
     * Get context by index for streaming serialization
     * @param index Context index (0-based)
     * @param outContext Output - will be filled with context data
     * @return true if context exists at index, false otherwise
     */
    virtual bool getContextAt(size_t index, WebUIContext& outContext) = 0;
    
    /**
     * Get const reference to context at index - NO COPY (for memory efficiency)
     * Returns nullptr if index out of range.
     * IMPORTANT: Caller must ensure provider outlives usage of returned pointer.
     */
    virtual const WebUIContext* getContextAtRef(size_t index) {
        // Default implementation: not supported without caching
        return nullptr;
    }

    /**
     * Handle WebUI API requests for specific context
     * @param contextId The context identifier
     * @param endpoint The API endpoint being called
     * @param method HTTP method (GET, POST, PUT, DELETE)
     * @param params Request parameters
     * @return JSON response string
     */
    virtual String handleWebUIRequest(const String& contextId, const String& endpoint, 
                                    const String& method, const std::map<String, String>& params) = 0;
    
    /**
     * Get real-time data for specific context
     * @param contextId The context identifier
     * @return JSON string with current context data
     */
    virtual String getWebUIData(const String& contextId) { return "{}"; }
    
    /**
     * Check if context data has changed since last query (for delta updates)
     * @param contextId The context identifier
     * @return true if data has changed and should be sent to clients
     * @note Default implementation always returns true (always send updates)
     *       Override to optimize bandwidth by only sending when data changes
     */
    virtual bool hasDataChanged(const String& contextId) { return true; }

    // Methods to get component metadata directly for the UI
    virtual String getWebUIName() const = 0;
    virtual String getWebUIVersion() const = 0;
    
    /**
     * Get specific WebUI context by ID
     * @param contextId The context identifier
     * @return WebUIContext object for the specified ID
     */
    virtual WebUIContext getWebUIContext(const String& contextId) = 0;
    
    /**
     * Check if component should be visible in WebUI
     * @return true if component should appear in WebUI
     */
    virtual bool isWebUIEnabled() { return true; }

};

/**
 * Base class for WebUI providers that caches contexts to prevent memory leaks.
 *
 * On ESP8266, each call to getWebUIContexts() that creates new WebUIContext
 * objects with String members causes heap fragmentation. This base class
 * caches contexts on first access and returns from cache subsequently.
 *
 * Usage:
 * @code
 * class MyWebUI : public CachingWebUIProvider {
 * protected:
 *     void buildContexts(std::vector<WebUIContext>& contexts) override {
 *         contexts.push_back(WebUIContext::dashboard("my_dash", "Dashboard")
 *             .withField(...));
 *     }
 * public:
 *     String getWebUIName() const override { return "MyComponent"; }
 *     // ... other required methods
 * };
 * @endcode
 */
class CachingWebUIProvider : public IWebUIProvider {
protected:
    mutable std::vector<WebUIContext> cachedContexts_;
    mutable bool contextsCached_ = false;

    /**
     * Build contexts - called once, results are cached.
     * Subclasses implement this instead of getWebUIContexts().
     * @param contexts Vector to populate with contexts
     */
    virtual void buildContexts(std::vector<WebUIContext>& contexts) = 0;

    /**
     * Ensure contexts are cached
     */
    void ensureContextsCached() const {
        if (!contextsCached_) {
            // Cast away const for lazy initialization
            auto* self = const_cast<CachingWebUIProvider*>(this);
            self->buildContexts(self->cachedContexts_);
            self->contextsCached_ = true;
        }
    }

public:
    /**
     * Invalidate cached contexts (call when config changes)
     */
    void invalidateContextCache() {
        cachedContexts_.clear();
        cachedContexts_.shrink_to_fit();
        contextsCached_ = false;
    }

    // IWebUIProvider implementation with caching

    void forEachContext(std::function<bool(const WebUIContext&)> callback) override {
        ensureContextsCached();
        for (const auto& ctx : cachedContexts_) {
            if (!callback(ctx)) break;
        }
    }

    size_t getContextCount() override {
        ensureContextsCached();
        return cachedContexts_.size();
    }

    bool getContextAt(size_t index, WebUIContext& outContext) override {
        ensureContextsCached();
        if (index < cachedContexts_.size()) {
            outContext = cachedContexts_[index];
            return true;
        }
        return false;
    }
    
    /**
     * Get const reference to cached context - NO COPY
     */
    const WebUIContext* getContextAtRef(size_t index) override {
        ensureContextsCached();
        if (index < cachedContexts_.size()) {
            return &cachedContexts_[index];
        }
        return nullptr;
    }

    WebUIContext getWebUIContext(const String& contextId) override {
        ensureContextsCached();
        for (const auto& ctx : cachedContexts_) {
            if (strcmp(ctx.getContextIdCStr(), contextId.c_str()) == 0) {
                return ctx;
            }
        }
        return WebUIContext();
    }
};

} // namespace Components
} // namespace DomoticsCore
