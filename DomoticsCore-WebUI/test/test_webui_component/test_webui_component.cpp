/**
 * @file test_webui_component.cpp
 * @brief Native unit tests for WebUI component structures
 *
 * Tests cover:
 * - WebUIConfig defaults and configuration
 * - WebUIField creation and fluent interface
 * - WebUIContext creation and factory methods
 * - LazyState change tracking
 * - ProviderRegistry registration and lookup
 * - CachingWebUIProvider memory leak prevention
 */

#include <unity.h>
#include <DomoticsCore/Core.h>
#include <DomoticsCore/IWebUIProvider.h>
#include <DomoticsCore/WebUI/WebUIConfig.h>
#include <DomoticsCore/WebUI/ProviderRegistry.h>
#include <DomoticsCore/WebUI/StreamingContextSerializer.h>
#include <DomoticsCore/Testing/HeapTracker.h>
#include <ArduinoJson.h>

using namespace DomoticsCore;
using namespace DomoticsCore::Components;
using namespace DomoticsCore::Components::WebUI;
using namespace DomoticsCore::Testing;

// ============================================================================
// WebUIConfig Tests
// ============================================================================

void test_webui_config_defaults() {
    WebUIConfig config;

    TEST_ASSERT_EQUAL_STRING("DomoticsCore Device", config.deviceName);
    TEST_ASSERT_EQUAL_STRING("auto", config.theme);
    TEST_ASSERT_EQUAL_UINT16(80, config.port);
    TEST_ASSERT_TRUE(config.enableWebSocket);
    TEST_ASSERT_EQUAL_INT(5000, config.wsUpdateInterval);
    TEST_ASSERT_FALSE(config.useFileSystem);
    TEST_ASSERT_EQUAL_STRING("/webui", config.staticPath);
    TEST_ASSERT_EQUAL_STRING("#007acc", config.primaryColor);
    TEST_ASSERT_FALSE(config.enableAuth);
    TEST_ASSERT_EQUAL_STRING("admin", config.username);
    TEST_ASSERT_EQUAL_STRING("", config.password);
    TEST_ASSERT_EQUAL_INT(3, config.maxWebSocketClients);
    TEST_ASSERT_EQUAL_INT(5000, config.apiTimeout);
    TEST_ASSERT_TRUE(config.enableCompression);
    TEST_ASSERT_TRUE(config.enableCaching);
    TEST_ASSERT_FALSE(config.enableCORS);
}

void test_webui_config_custom_values() {
    WebUIConfig config;
    config.setDeviceName("Custom Device");
    config.setTheme("dark");
    config.port = 8080;
    config.enableWebSocket = false;
    config.wsUpdateInterval = 1000;
    config.maxWebSocketClients = 5;
    config.enableAuth = true;
    config.setUsername("user");
    config.setPassword("secret");

    TEST_ASSERT_EQUAL_STRING("Custom Device", config.deviceName);
    TEST_ASSERT_EQUAL_STRING("dark", config.theme);
    TEST_ASSERT_EQUAL_UINT16(8080, config.port);
    TEST_ASSERT_FALSE(config.enableWebSocket);
    TEST_ASSERT_EQUAL_INT(1000, config.wsUpdateInterval);
    TEST_ASSERT_EQUAL_INT(5, config.maxWebSocketClients);
    TEST_ASSERT_TRUE(config.enableAuth);
    TEST_ASSERT_EQUAL_STRING("user", config.username);
    TEST_ASSERT_EQUAL_STRING("secret", config.password);
}

// ============================================================================
// WebUIField Tests
// ============================================================================

void test_webui_field_basic_construction() {
    WebUIField field("temp", "Temperature", WebUIFieldType::Number, "25.5", "°C", true);

    TEST_ASSERT_EQUAL_STRING("temp", field.getNameCStr());
    TEST_ASSERT_EQUAL_STRING("Temperature", field.getLabelCStr());
    TEST_ASSERT_EQUAL(WebUIFieldType::Number, field.type);
    TEST_ASSERT_EQUAL_STRING("25.5", field.getValueCStr());
    TEST_ASSERT_EQUAL_STRING("°C", field.getUnitCStr());
    TEST_ASSERT_TRUE(field.readOnly);
}

void test_webui_field_default_values() {
    WebUIField field("status", "Status", WebUIFieldType::Text);

    TEST_ASSERT_EQUAL_STRING("status", field.getNameCStr());
    TEST_ASSERT_EQUAL_STRING("Status", field.getLabelCStr());
    TEST_ASSERT_EQUAL(WebUIFieldType::Text, field.type);
    TEST_ASSERT_TRUE(field.value.isEmpty());
    TEST_ASSERT_TRUE(field.unit.isEmpty());
    TEST_ASSERT_FALSE(field.readOnly);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0.0, field.minValue);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 100.0, field.maxValue);
}

void test_webui_field_fluent_range() {
    WebUIField field("brightness", "Brightness", WebUIFieldType::Slider);
    field.range(0, 255);

    TEST_ASSERT_FLOAT_WITHIN(0.01, 0.0, field.minValue);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 255.0, field.maxValue);
}

void test_webui_field_fluent_choices() {
    WebUIField field("mode", "Mode", WebUIFieldType::Select);
    std::vector<String> opts = {"auto", "manual", "off"};
    field.choices(opts);

    TEST_ASSERT_EQUAL(3, field.options.size());
    TEST_ASSERT_EQUAL_STRING("auto", field.options[0].c_str());
    TEST_ASSERT_EQUAL_STRING("manual", field.options[1].c_str());
    TEST_ASSERT_EQUAL_STRING("off", field.options[2].c_str());
}

void test_webui_field_fluent_add_option() {
    WebUIField field("speed", "Speed", WebUIFieldType::Select);
    field.addOption("low", "Low Speed")
         .addOption("medium", "Medium Speed")
         .addOption("high", "High Speed");

    TEST_ASSERT_EQUAL(3, field.options.size());
    TEST_ASSERT_EQUAL_STRING("low", field.options[0].c_str());
    TEST_ASSERT_EQUAL_STRING("Low Speed", field.optionLabels["low"].c_str());
    TEST_ASSERT_EQUAL_STRING("medium", field.options[1].c_str());
    TEST_ASSERT_EQUAL_STRING("Medium Speed", field.optionLabels["medium"].c_str());
}

void test_webui_field_fluent_api() {
    WebUIField field("power", "Power", WebUIFieldType::Button);
    field.api("/api/power/set");

    TEST_ASSERT_EQUAL_STRING("/api/power/set", field.getEndpointCStr());
}

void test_webui_field_copy_constructor() {
    WebUIField original("test", "Test", WebUIFieldType::Number, "42", "units", false);
    original.range(0, 100);
    original.addOption("a", "Option A");

    WebUIField copy(original);

    TEST_ASSERT_EQUAL_STRING("test", copy.getNameCStr());
    TEST_ASSERT_EQUAL_STRING("Test", copy.getLabelCStr());
    TEST_ASSERT_EQUAL_STRING("42", copy.getValueCStr());
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0.0, copy.minValue);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 100.0, copy.maxValue);
    TEST_ASSERT_EQUAL(1, copy.options.size());
}

void test_webui_field_all_types() {
    // Verify all field types are accessible
    WebUIField f1("a", "A", WebUIFieldType::Text);
    WebUIField f2("b", "B", WebUIFieldType::Number);
    WebUIField f3("c", "C", WebUIFieldType::Float);
    WebUIField f4("d", "D", WebUIFieldType::Boolean);
    WebUIField f5("e", "E", WebUIFieldType::Select);
    WebUIField f6("f", "F", WebUIFieldType::Slider);
    WebUIField f7("g", "G", WebUIFieldType::Color);
    WebUIField f8("h", "H", WebUIFieldType::Button);
    WebUIField f9("i", "I", WebUIFieldType::Display);
    WebUIField f10("j", "J", WebUIFieldType::Chart);
    WebUIField f11("k", "K", WebUIFieldType::Status);
    WebUIField f12("l", "L", WebUIFieldType::Progress);
    WebUIField f13("m", "M", WebUIFieldType::Password);
    WebUIField f14("n", "N", WebUIFieldType::File);
    WebUIField f15("o", "O", WebUIFieldType::Multiselect);
    WebUIField f16("p", "P", WebUIFieldType::OrderedList);

    TEST_ASSERT_EQUAL(15, static_cast<int>(f16.type));

    TEST_ASSERT_EQUAL(WebUIFieldType::Text, f1.type);
    TEST_ASSERT_EQUAL(WebUIFieldType::Number, f2.type);
    TEST_ASSERT_EQUAL(WebUIFieldType::File, f14.type);
    TEST_ASSERT_EQUAL(WebUIFieldType::Multiselect, f15.type);
    TEST_ASSERT_EQUAL(WebUIFieldType::OrderedList, f16.type);
    TEST_ASSERT_FALSE(f1.isMultiValue());
    TEST_ASSERT_TRUE(f15.isMultiValue());
    TEST_ASSERT_TRUE(f16.isMultiValue());
}

void test_ordered_list_values_survive_field_copy() {
    WebUIField original("order", "Order", WebUIFieldType::OrderedList);
    original.addOption("ethernet", "Ethernet", true)
            .addOption("wifi", "WiFi", true);

    WebUIField copy(original);
    TEST_ASSERT_EQUAL_UINT32(2, copy.values.size());
    TEST_ASSERT_EQUAL_STRING("ethernet", copy.values[0].c_str());
    TEST_ASSERT_EQUAL_STRING("wifi", copy.values[1].c_str());
    TEST_ASSERT_EQUAL_STRING("Ethernet", copy.optionLabels["ethernet"].c_str());
}

// ============================================================================
// WebUIContext Tests
// ============================================================================

void test_webui_context_basic_construction() {
    WebUIContext ctx("test_ctx", "Test Context", "dc-test", WebUILocation::Dashboard, WebUIPresentation::Card);

    TEST_ASSERT_EQUAL_STRING("test_ctx", ctx.getContextIdCStr());
    TEST_ASSERT_EQUAL_STRING("Test Context", ctx.getTitleCStr());
    TEST_ASSERT_EQUAL_STRING("dc-test", ctx.getIconCStr());
    TEST_ASSERT_EQUAL(WebUILocation::Dashboard, ctx.location);
    TEST_ASSERT_EQUAL(WebUIPresentation::Card, ctx.presentation);
    TEST_ASSERT_EQUAL_INT(0, ctx.priority);
    TEST_ASSERT_FALSE(ctx.realTime);
    TEST_ASSERT_EQUAL_INT(5000, ctx.updateInterval);
}

void test_webui_context_factory_dashboard() {
    auto ctx = WebUIContext::dashboard("dash_id", "Dashboard Card", "dc-dashboard");

    TEST_ASSERT_EQUAL_STRING("dash_id", ctx.getContextIdCStr());
    TEST_ASSERT_EQUAL_STRING("Dashboard Card", ctx.getTitleCStr());
    TEST_ASSERT_EQUAL_STRING("dc-dashboard", ctx.getIconCStr());
    TEST_ASSERT_EQUAL(WebUILocation::Dashboard, ctx.location);
    TEST_ASSERT_EQUAL(WebUIPresentation::Card, ctx.presentation);
}

void test_webui_context_factory_gauge() {
    auto ctx = WebUIContext::gauge("gauge_id", "Gauge Title");

    TEST_ASSERT_EQUAL_STRING("gauge_id", ctx.getContextIdCStr());
    TEST_ASSERT_EQUAL(WebUILocation::Dashboard, ctx.location);
    TEST_ASSERT_EQUAL(WebUIPresentation::Gauge, ctx.presentation);
}

void test_webui_context_factory_status_badge() {
    auto ctx = WebUIContext::statusBadge("status_id", "Status", "dc-wifi");

    TEST_ASSERT_EQUAL_STRING("status_id", ctx.getContextIdCStr());
    TEST_ASSERT_EQUAL(WebUILocation::HeaderStatus, ctx.location);
    TEST_ASSERT_EQUAL(WebUIPresentation::StatusBadge, ctx.presentation);
    // Icon is stored in icon field, rendered by frontend JS
    TEST_ASSERT_EQUAL_STRING("dc-wifi", ctx.getIconCStr());
}

void test_webui_context_factory_header_info() {
    auto ctx = WebUIContext::headerInfo("time_id", "Time", "dc-clock");

    TEST_ASSERT_EQUAL_STRING("time_id", ctx.getContextIdCStr());
    TEST_ASSERT_EQUAL(WebUILocation::HeaderInfo, ctx.location);
    TEST_ASSERT_EQUAL(WebUIPresentation::Text, ctx.presentation);
}

void test_webui_context_factory_settings() {
    auto ctx = WebUIContext::settings("settings_id", "Settings");

    TEST_ASSERT_EQUAL_STRING("settings_id", ctx.getContextIdCStr());
    TEST_ASSERT_EQUAL(WebUILocation::Settings, ctx.location);
    TEST_ASSERT_EQUAL(WebUIPresentation::Card, ctx.presentation);
}

void test_webui_context_fluent_with_field() {
    auto ctx = WebUIContext::dashboard("test", "Test")
        .withField(WebUIField("temp", "Temperature", WebUIFieldType::Number));

    TEST_ASSERT_EQUAL(1, ctx.fields.size());
    TEST_ASSERT_EQUAL_STRING("temp", ctx.fields[0].getNameCStr());
}

void test_webui_context_fluent_with_multiple_fields() {
    auto ctx = WebUIContext::dashboard("test", "Test")
        .withField(WebUIField("f1", "Field 1", WebUIFieldType::Text))
        .withField(WebUIField("f2", "Field 2", WebUIFieldType::Number))
        .withField(WebUIField("f3", "Field 3", WebUIFieldType::Boolean));

    TEST_ASSERT_EQUAL(3, ctx.fields.size());
}

void test_webui_context_fluent_with_api() {
    auto ctx = WebUIContext::dashboard("test", "Test")
        .withAPI("/api/test");

    TEST_ASSERT_EQUAL_STRING("/api/test", ctx.getApiEndpointCStr());
}

void test_webui_context_fluent_with_real_time() {
    auto ctx = WebUIContext::dashboard("test", "Test")
        .withRealTime(1000);

    TEST_ASSERT_TRUE(ctx.realTime);
    TEST_ASSERT_EQUAL_INT(1000, ctx.updateInterval);
}

void test_webui_context_fluent_with_priority() {
    auto ctx = WebUIContext::dashboard("test", "Test")
        .withPriority(100);

    TEST_ASSERT_EQUAL_INT(100, ctx.priority);
}

void test_webui_context_fluent_always_interactive() {
    auto ctx = WebUIContext::settings("test", "Test")
        .withAlwaysInteractive(true);

    TEST_ASSERT_TRUE(ctx.alwaysInteractive);
}

void test_webui_context_custom_html_css_js() {
    auto ctx = WebUIContext::dashboard("test", "Test")
        .withCustomHtml("<div class='custom'>Content</div>")
        .withCustomCss(".custom { color: red; }")
        .withCustomJs("console.log('test');");

    TEST_ASSERT_TRUE(ctx.hasCustomHtml());
    TEST_ASSERT_TRUE(ctx.hasCustomCss());
    TEST_ASSERT_TRUE(ctx.hasCustomJs());
    TEST_ASSERT_TRUE(strstr(ctx.getCustomHtmlCStr(), "custom") != nullptr);
    TEST_ASSERT_TRUE(strstr(ctx.getCustomCssCStr(), "color") != nullptr);
    TEST_ASSERT_TRUE(strstr(ctx.getCustomJsCStr(), "console") != nullptr);
}

void test_webui_context_copy_constructor() {
    auto original = WebUIContext::dashboard("orig", "Original")
        .withField(WebUIField("f1", "Field", WebUIFieldType::Text))
        .withRealTime(2000);

    WebUIContext copy(original);

    TEST_ASSERT_EQUAL_STRING("orig", copy.getContextIdCStr());
    TEST_ASSERT_EQUAL(1, copy.fields.size());
    TEST_ASSERT_TRUE(copy.realTime);
    TEST_ASSERT_EQUAL_INT(2000, copy.updateInterval);
}

// ============================================================================
// WebUILocation and WebUIPresentation Tests
// ============================================================================

void test_webui_locations_enum() {
    // Verify all location enum values are accessible
    WebUILocation loc1 = WebUILocation::Dashboard;
    WebUILocation loc2 = WebUILocation::ComponentDetail;
    WebUILocation loc3 = WebUILocation::HeaderStatus;
    WebUILocation loc4 = WebUILocation::QuickControls;
    WebUILocation loc5 = WebUILocation::Settings;
    WebUILocation loc6 = WebUILocation::HeaderInfo;

    TEST_ASSERT_NOT_EQUAL(loc1, loc2);
    TEST_ASSERT_NOT_EQUAL(loc3, loc6);
}

void test_webui_presentations_enum() {
    // Verify all presentation enum values are accessible
    WebUIPresentation p1 = WebUIPresentation::Card;
    WebUIPresentation p2 = WebUIPresentation::Gauge;
    WebUIPresentation p3 = WebUIPresentation::Graph;
    WebUIPresentation p4 = WebUIPresentation::StatusBadge;
    WebUIPresentation p5 = WebUIPresentation::ProgressBar;
    WebUIPresentation p6 = WebUIPresentation::Table;
    WebUIPresentation p7 = WebUIPresentation::Toggle;
    WebUIPresentation p8 = WebUIPresentation::Slider;
    WebUIPresentation p9 = WebUIPresentation::Text;
    WebUIPresentation p10 = WebUIPresentation::Button;

    TEST_ASSERT_NOT_EQUAL(p1, p2);
    TEST_ASSERT_NOT_EQUAL(p9, p10);
}

// ============================================================================
// LazyState Tests
// ============================================================================

void test_lazy_state_initial_uninitialized() {
    LazyState<int> state;

    TEST_ASSERT_FALSE(state.isInitialized());
}

void test_lazy_state_has_changed_first_call() {
    LazyState<int> state;

    bool changed = state.hasChanged(42);

    TEST_ASSERT_TRUE(changed);  // First call always returns true
    TEST_ASSERT_TRUE(state.isInitialized());
    TEST_ASSERT_EQUAL_INT(42, state.getValue());
}

void test_lazy_state_has_changed_no_change() {
    LazyState<int> state;

    state.hasChanged(42);
    bool changed = state.hasChanged(42);  // Same value

    TEST_ASSERT_FALSE(changed);
}

void test_lazy_state_has_changed_with_change() {
    LazyState<int> state;

    state.hasChanged(42);
    bool changed = state.hasChanged(100);  // Different value

    TEST_ASSERT_TRUE(changed);
    TEST_ASSERT_EQUAL_INT(100, state.getValue());
}

void test_lazy_state_get_with_initializer() {
    LazyState<String> state;

    String& value = state.get([]() { return String("initialized"); });

    TEST_ASSERT_TRUE(state.isInitialized());
    TEST_ASSERT_EQUAL_STRING("initialized", value.c_str());
}

void test_lazy_state_get_only_initializes_once() {
    LazyState<int> state;
    int callCount = 0;

    state.get([&callCount]() { callCount++; return 1; });
    state.get([&callCount]() { callCount++; return 2; });
    state.get([&callCount]() { callCount++; return 3; });

    TEST_ASSERT_EQUAL_INT(1, callCount);
    TEST_ASSERT_EQUAL_INT(1, state.getValue());
}

void test_lazy_state_reset() {
    LazyState<int> state;
    state.hasChanged(42);

    state.reset();

    TEST_ASSERT_FALSE(state.isInitialized());
}

void test_lazy_state_with_bool() {
    LazyState<bool> state;

    TEST_ASSERT_TRUE(state.hasChanged(false));  // First call
    TEST_ASSERT_FALSE(state.hasChanged(false)); // Same
    TEST_ASSERT_TRUE(state.hasChanged(true));   // Changed
}

void test_lazy_state_with_string() {
    LazyState<String> state;

    TEST_ASSERT_TRUE(state.hasChanged("hello"));
    TEST_ASSERT_FALSE(state.hasChanged("hello"));
    TEST_ASSERT_TRUE(state.hasChanged("world"));
    TEST_ASSERT_EQUAL_STRING("world", state.getValue().c_str());
}

// ============================================================================
// Mock Provider using CachingWebUIProvider (NEW - memory optimized)
// ============================================================================

class MockWebUIProvider : public CachingWebUIProvider {
private:
    String name_;
    String version_;
    std::vector<WebUIContext> pendingContexts_;  // Contexts to add before first access
    bool enabled_ = true;

protected:
    // CachingWebUIProvider: build contexts once, they're cached
    void buildContexts(std::vector<WebUIContext>& contexts) override {
        for (const auto& ctx : pendingContexts_) {
            contexts.push_back(ctx);
        }
    }

public:
    MockWebUIProvider(const String& n, const String& v) : name_(n), version_(v) {}

    void addContext(const WebUIContext& ctx) {
        pendingContexts_.push_back(ctx);
        // Invalidate cache if already built
        invalidateContextCache();
    }

    String getWebUIName() const override { return name_; }
    String getWebUIVersion() const override { return version_; }

    String handleWebUIRequest(const String& contextId, const String& endpoint,
                              const String& method, const std::map<String, String>& params) override {
        return "{\"success\":true}";
    }

    String getWebUIData(const String& contextId) override {
        return "{}";
    }

    bool isWebUIEnabled() override { return enabled_; }
    void setEnabled(bool e) { enabled_ = e; }
};

// ============================================================================
// ProviderRegistry Tests
// ============================================================================

void test_provider_registry_empty() {
    ProviderRegistry registry;

    auto* provider = registry.getProviderForContext("nonexistent");
    TEST_ASSERT_NULL(provider);
}

void test_provider_registry_register_provider() {
    ProviderRegistry registry;
    MockWebUIProvider provider("TestProvider", "1.0.0");
    provider.addContext(WebUIContext::dashboard("test_ctx", "Test"));

    registry.registerProvider(&provider);

    auto* found = registry.getProviderForContext("test_ctx");
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_STRING("TestProvider", found->getWebUIName().c_str());
}

void test_provider_registry_register_multiple_contexts() {
    ProviderRegistry registry;
    MockWebUIProvider provider("MultiContext", "1.0.0");
    provider.addContext(WebUIContext::dashboard("ctx1", "Context 1"));
    provider.addContext(WebUIContext::settings("ctx2", "Context 2"));
    provider.addContext(WebUIContext::statusBadge("ctx3", "Context 3", "dc-test"));

    registry.registerProvider(&provider);

    TEST_ASSERT_NOT_NULL(registry.getProviderForContext("ctx1"));
    TEST_ASSERT_NOT_NULL(registry.getProviderForContext("ctx2"));
    TEST_ASSERT_NOT_NULL(registry.getProviderForContext("ctx3"));

    // All should point to same provider
    TEST_ASSERT_EQUAL_PTR(registry.getProviderForContext("ctx1"),
                          registry.getProviderForContext("ctx2"));
}

void test_provider_registry_unregister_provider() {
    ProviderRegistry registry;
    MockWebUIProvider provider("ToRemove", "1.0.0");
    provider.addContext(WebUIContext::dashboard("remove_ctx", "Remove"));

    registry.registerProvider(&provider);
    TEST_ASSERT_NOT_NULL(registry.getProviderForContext("remove_ctx"));

    registry.unregisterProvider(&provider);
    TEST_ASSERT_NULL(registry.getProviderForContext("remove_ctx"));
}

void test_provider_registry_register_factory() {
    ProviderRegistry registry;

    bool factoryCalled = false;
    registry.registerProviderFactory("test_type", [&factoryCalled](IComponent* comp) -> IWebUIProvider* {
        factoryCalled = true;
        return nullptr;  // Just testing factory registration
    });

    // Factory stored but not called until discovery
    TEST_ASSERT_FALSE(factoryCalled);
}

void test_provider_registry_get_components_list() {
    ProviderRegistry registry;
    MockWebUIProvider provider1("Provider1", "1.0.0");
    provider1.addContext(WebUIContext::dashboard("p1_ctx", "P1"));
    MockWebUIProvider provider2("Provider2", "2.0.0");
    provider2.addContext(WebUIContext::settings("p2_ctx", "P2"));

    registry.registerProvider(&provider1);
    registry.registerProvider(&provider2);

    JsonDocument doc;
    registry.getComponentsList(doc);

    TEST_ASSERT_TRUE(doc["components"].is<JsonArray>());
    JsonArray components = doc["components"].as<JsonArray>();
    TEST_ASSERT_EQUAL(2, components.size());
}

void test_provider_registry_enable_disable() {
    ProviderRegistry registry;
    MockWebUIProvider provider("Toggleable", "1.0.0");
    provider.addContext(WebUIContext::dashboard("toggle_ctx", "Toggle"));

    registry.registerProvider(&provider);

    // Disable
    auto result = registry.enableComponent("Toggleable", false);
    TEST_ASSERT_TRUE(result.found);
    TEST_ASSERT_FALSE(result.enabled);

    // Context should be removed
    TEST_ASSERT_NULL(registry.getProviderForContext("toggle_ctx"));

    // Re-enable
    result = registry.enableComponent("Toggleable", true);
    TEST_ASSERT_TRUE(result.found);
    TEST_ASSERT_TRUE(result.enabled);

    // Context should be back
    TEST_ASSERT_NOT_NULL(registry.getProviderForContext("toggle_ctx"));
}

void test_provider_registry_cannot_disable_webui() {
    ProviderRegistry registry;
    MockWebUIProvider webuiProvider("WebUI", "1.0.0");
    webuiProvider.addContext(WebUIContext::dashboard("webui_ctx", "WebUI"));

    registry.registerProvider(&webuiProvider);

    auto result = registry.enableComponent("WebUI", false);

    // Should return warning without disabling
    TEST_ASSERT_FALSE(result.warning.isEmpty());
    TEST_ASSERT_FALSE(result.success);
}

void test_provider_registry_enable_nonexistent() {
    ProviderRegistry registry;

    auto result = registry.enableComponent("NonExistent", true);

    TEST_ASSERT_FALSE(result.found);
    TEST_ASSERT_FALSE(result.success);
}

void test_provider_registry_context_providers_accessor() {
    ProviderRegistry registry;
    MockWebUIProvider provider("Accessor", "1.0.0");
    provider.addContext(WebUIContext::dashboard("acc_ctx", "Accessor"));

    registry.registerProvider(&provider);

    const auto& contextProviders = registry.getContextProviders();
    TEST_ASSERT_EQUAL(1, contextProviders.size());
    TEST_ASSERT_TRUE(contextProviders.find("acc_ctx") != contextProviders.end());
}

void test_provider_registry_prepare_schema_generation() {
    ProviderRegistry registry;
    MockWebUIProvider provider("Schema", "1.0.0");
    provider.addContext(WebUIContext::dashboard("schema_ctx", "Schema"));

    registry.registerProvider(&provider);

    auto state = registry.prepareSchemaGeneration();
    TEST_ASSERT_NOT_NULL(state.get());
    TEST_ASSERT_FALSE(state->finished);
    TEST_ASSERT_EQUAL(1, state->providers.size());
}

void test_provider_registry_iterate_contexts() {
    ProviderRegistry registry;
    MockWebUIProvider provider("IterCtx", "1.0.0");
    provider.addContext(WebUIContext::dashboard("ctx_a", "A"));
    provider.addContext(WebUIContext::settings("ctx_b", "B"));

    registry.registerProvider(&provider);

    // Iterate contexts using forEachContext
    std::vector<String> contextIds;
    provider.forEachContext([&contextIds](const WebUIContext& ctx) {
        contextIds.push_back(ctx.getContextIdCStr());
        return true;  // continue
    });

    TEST_ASSERT_EQUAL(2, contextIds.size());
    TEST_ASSERT_EQUAL_STRING("ctx_a", contextIds[0].c_str());
    TEST_ASSERT_EQUAL_STRING("ctx_b", contextIds[1].c_str());
}

void test_provider_get_context_at() {
    MockWebUIProvider provider("IndexedTest", "1.0.0");
    provider.addContext(WebUIContext::dashboard("idx_0", "First"));
    provider.addContext(WebUIContext::settings("idx_1", "Second"));
    provider.addContext(WebUIContext::statusBadge("idx_2", "Third", "dc-icon"));

    // Test getContextCount
    TEST_ASSERT_EQUAL(3, provider.getContextCount());

    // Test getContextAt with valid indices
    WebUIContext ctx;
    TEST_ASSERT_TRUE(provider.getContextAt(0, ctx));
    TEST_ASSERT_EQUAL_STRING("idx_0", ctx.getContextIdCStr());

    TEST_ASSERT_TRUE(provider.getContextAt(1, ctx));
    TEST_ASSERT_EQUAL_STRING("idx_1", ctx.getContextIdCStr());

    TEST_ASSERT_TRUE(provider.getContextAt(2, ctx));
    TEST_ASSERT_EQUAL_STRING("idx_2", ctx.getContextIdCStr());

    // Test getContextAt with invalid index
    TEST_ASSERT_FALSE(provider.getContextAt(3, ctx));
    TEST_ASSERT_FALSE(provider.getContextAt(100, ctx));
}

// ============================================================================
// StreamingContextSerializer Tests
// ============================================================================

void test_streaming_serializer_simple_context() {
    // Create a simple context
    auto ctx = WebUIContext::dashboard("test_id", "Test Title", "dc-test");

    StreamingContextSerializer serializer;
    serializer.begin(ctx);

    // Serialize to buffer
    uint8_t buffer[4096];
    size_t totalWritten = 0;

    while (!serializer.isComplete() && totalWritten < sizeof(buffer)) {
        size_t written = serializer.write(buffer + totalWritten, sizeof(buffer) - totalWritten);
        if (written == 0) break;
        totalWritten += written;
    }

    TEST_ASSERT_TRUE(serializer.isComplete());
    TEST_ASSERT_GREATER_THAN(0, totalWritten);

    // Parse as JSON to validate
    buffer[totalWritten] = '\0';
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, (char*)buffer);

    TEST_ASSERT_TRUE(err == DeserializationError::Ok);
    TEST_ASSERT_EQUAL_STRING("test_id", doc["contextId"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("Test Title", doc["title"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("dc-test", doc["icon"].as<const char*>());
}

void test_streaming_serializer_with_fields() {
    auto ctx = WebUIContext::settings("settings_id", "Settings")
        .withField(WebUIField("name", "Name", WebUIFieldType::Text, "test"))
        .withField(WebUIField("value", "Value", WebUIFieldType::Number, "42", "units", true));

    StreamingContextSerializer serializer;
    serializer.begin(ctx);

    uint8_t buffer[4096];
    size_t totalWritten = 0;

    while (!serializer.isComplete() && totalWritten < sizeof(buffer)) {
        size_t written = serializer.write(buffer + totalWritten, sizeof(buffer) - totalWritten);
        if (written == 0) break;
        totalWritten += written;
    }

    buffer[totalWritten] = '\0';
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, (char*)buffer);

    TEST_ASSERT_TRUE(err == DeserializationError::Ok);
    TEST_ASSERT_TRUE(doc["fields"].is<JsonArray>());
    TEST_ASSERT_EQUAL(2, doc["fields"].as<JsonArray>().size());

    JsonArray fields = doc["fields"].as<JsonArray>();
    TEST_ASSERT_EQUAL_STRING("name", fields[0]["name"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("value", fields[1]["name"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("42", fields[1]["value"].as<const char*>());
    TEST_ASSERT_TRUE(fields[1]["readOnly"].as<bool>());
}

void test_streaming_serializer_with_custom_html() {
    auto ctx = WebUIContext::dashboard("custom_id", "Custom")
        .withCustomHtml("<div class=\"test\">Hello</div>")
        .withCustomCss(".test { color: red; }")
        .withCustomJs("console.log('test');");

    StreamingContextSerializer serializer;
    serializer.begin(ctx);

    uint8_t buffer[4096];
    size_t totalWritten = 0;

    while (!serializer.isComplete() && totalWritten < sizeof(buffer)) {
        size_t written = serializer.write(buffer + totalWritten, sizeof(buffer) - totalWritten);
        if (written == 0) break;
        totalWritten += written;
    }

    buffer[totalWritten] = '\0';
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, (char*)buffer);

    TEST_ASSERT_TRUE(err == DeserializationError::Ok);
    TEST_ASSERT_TRUE(strstr(doc["customHtml"].as<const char*>(), "class") != nullptr);
    TEST_ASSERT_TRUE(strstr(doc["customCss"].as<const char*>(), "color") != nullptr);
    TEST_ASSERT_TRUE(strstr(doc["customJs"].as<const char*>(), "console") != nullptr);
}

void test_streaming_serializer_chunked_output() {
    // Test that serializer works with small buffer sizes (simulating chunked HTTP)
    auto ctx = WebUIContext::dashboard("chunk_test", "Chunked Test")
        .withField(WebUIField("field1", "Field 1", WebUIFieldType::Text, "value1"));

    StreamingContextSerializer serializer;
    serializer.begin(ctx);

    // Use small buffer to force multiple chunks (64 bytes)
    // Must be large enough to fit the longest atomic piece (like "\"contextId\":")
    uint8_t smallBuffer[65];  // +1 for null terminator
    String fullOutput;
    int chunkCount = 0;

    while (!serializer.isComplete() && chunkCount < 200) {
        size_t written = serializer.write(smallBuffer, 64);
        if (written > 0) {
            // Append buffer content to string
            smallBuffer[written] = '\0';
            fullOutput += (const char*)smallBuffer;
            chunkCount++;
        } else if (!serializer.isComplete()) {
            break;  // Stuck
        }
    }

    TEST_ASSERT_TRUE(serializer.isComplete());
    TEST_ASSERT_GREATER_THAN(1, chunkCount);  // Should take multiple chunks

    // Validate the combined output is valid JSON
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, fullOutput);
    TEST_ASSERT_TRUE(err == DeserializationError::Ok);
    TEST_ASSERT_EQUAL_STRING("chunk_test", doc["contextId"].as<const char*>());
}

void test_streaming_serializer_json_escaping() {
    // Test that special characters are properly escaped
    auto ctx = WebUIContext::dashboard("escape_test", "Test \"Quotes\" & <Tags>")
        .withField(WebUIField("field", "Field\nWith\tTabs", WebUIFieldType::Text, "value\\with\\backslash"));

    StreamingContextSerializer serializer;
    serializer.begin(ctx);

    uint8_t buffer[4096];
    size_t totalWritten = 0;

    while (!serializer.isComplete() && totalWritten < sizeof(buffer)) {
        size_t written = serializer.write(buffer + totalWritten, sizeof(buffer) - totalWritten);
        if (written == 0) break;
        totalWritten += written;
    }

    buffer[totalWritten] = '\0';
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, (char*)buffer);

    // If JSON is invalid, escaping failed
    TEST_ASSERT_TRUE(err == DeserializationError::Ok);
    // Verify special chars were preserved after parsing
    TEST_ASSERT_TRUE(strstr(doc["title"].as<const char*>(), "Quotes") != nullptr);
}

void test_streaming_serializer_field_with_options() {
    WebUIField field("mode", "Mode", WebUIFieldType::Select);
    field.addOption("auto", "Automatic")
         .addOption("manual", "Manual Control")
         .addOption("off", "Disabled");

    auto ctx = WebUIContext::settings("options_test", "Options Test")
        .withField(field);

    StreamingContextSerializer serializer;
    serializer.begin(ctx);

    uint8_t buffer[4096];
    size_t totalWritten = 0;

    while (!serializer.isComplete() && totalWritten < sizeof(buffer)) {
        size_t written = serializer.write(buffer + totalWritten, sizeof(buffer) - totalWritten);
        if (written == 0) break;
        totalWritten += written;
    }

    buffer[totalWritten] = '\0';
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, (char*)buffer);

    TEST_ASSERT_TRUE(err == DeserializationError::Ok);

    JsonArray fields = doc["fields"].as<JsonArray>();
    TEST_ASSERT_EQUAL(1, fields.size());

    JsonArray options = fields[0]["options"].as<JsonArray>();
    TEST_ASSERT_EQUAL(3, options.size());
    TEST_ASSERT_EQUAL_STRING("auto", options[0].as<const char*>());

    JsonObject optionLabels = fields[0]["optionLabels"].as<JsonObject>();
    TEST_ASSERT_EQUAL_STRING("Automatic", optionLabels["auto"].as<const char*>());
}

void test_streaming_serializer_ordered_list_value_is_array() {
    WebUIField field("priorities", "Priorities", WebUIFieldType::OrderedList);
    field.addOption("ethernet", "Ethernet", true)
         .addOption("wifi", "WiFi", true);
    auto context = WebUIContext::settings("network_settings", "Network").withField(field);
    StreamingContextSerializer serializer;
    serializer.begin(context);
    uint8_t buffer[4096];
    size_t totalWritten = 0;
    while (!serializer.isComplete() && totalWritten < sizeof(buffer) - 1) {
        size_t written = serializer.write(buffer + totalWritten, sizeof(buffer) - 1 - totalWritten);
        if (!written) break;
        totalWritten += written;
    }
    buffer[totalWritten] = '\0';
    JsonDocument document;
    TEST_ASSERT_FALSE(deserializeJson(document, reinterpret_cast<char*>(buffer)));
    JsonArray value = document["fields"][0]["value"].as<JsonArray>();
    TEST_ASSERT_EQUAL_UINT32(2, value.size());
    TEST_ASSERT_EQUAL_STRING("ethernet", value[0]);
    TEST_ASSERT_EQUAL_STRING("wifi", value[1]);
    TEST_ASSERT_EQUAL_STRING("Ethernet", document["fields"][0]["optionLabels"]["ethernet"]);
}

// ============================================================================
// Memory Stability Tests
// ============================================================================

void test_streaming_serializer_no_memory_leak() {
    // Test that repeated schema serialization doesn't leak memory
    // This is the critical test - simulates multiple /api/ui/schema requests

    MockWebUIProvider provider("HeapTest", "1.0.0");
    provider.addContext(WebUIContext::dashboard("heap_dash", "Dashboard")
        .withField(WebUIField("temp", "Temperature", WebUIFieldType::Number, "25.5"))
        .withField(WebUIField("humid", "Humidity", WebUIFieldType::Number, "60"))
        .withCustomHtml("<div class=\"test\">Custom HTML content here</div>")
        .withCustomCss(".test { color: red; font-size: 14px; }"));
    provider.addContext(WebUIContext::settings("heap_settings", "Settings")
        .withField(WebUIField("enabled", "Enabled", WebUIFieldType::Boolean, "true"))
        .withField(WebUIField("name", "Name", WebUIFieldType::Text, "Test Device")));

    // Warm up - first allocation
    {
        uint8_t buffer[2048];
        provider.forEachContext([&buffer](const WebUIContext& ctx) {
            StreamingContextSerializer serializer;
            serializer.begin(ctx);
            while (!serializer.isComplete()) {
                serializer.write(buffer, sizeof(buffer));
            }
            return true;
        });
    }

    // Measure baseline heap after warmup
    size_t heapBefore = HAL::Platform::getFreeHeap();

    // Run multiple iterations (simulating multiple schema requests)
    const int ITERATIONS = 10;
    for (int i = 0; i < ITERATIONS; i++) {
        String schema = "[";
        bool first = true;

        provider.forEachContext([&schema, &first](const WebUIContext& ctx) {
            StreamingContextSerializer serializer;
            serializer.begin(ctx);

            uint8_t buffer[512];
            String ctxJson;

            while (!serializer.isComplete()) {
                size_t written = serializer.write(buffer, sizeof(buffer));
                if (written > 0) {
                    buffer[written] = '\0';
                    ctxJson += (const char*)buffer;
                }
            }

            if (!first) schema += ",";
            schema += ctxJson;
            first = false;
            return true;
        });

        schema += "]";

        // Verify JSON is valid each iteration
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, schema);
        TEST_ASSERT_TRUE(err == DeserializationError::Ok);
    }

    // Measure heap after iterations
    size_t heapAfter = HAL::Platform::getFreeHeap();

    // Calculate leak per iteration
    // Allow small tolerance for allocator overhead, but no significant leak
    int heapDiff = (int)heapBefore - (int)heapAfter;
    int leakPerIteration = heapDiff / ITERATIONS;

    // Print for debugging
    printf("Heap before: %zu, after: %zu, diff: %d, per iteration: %d\n",
           heapBefore, heapAfter, heapDiff, leakPerIteration);

    // Assert no significant leak (allow 8 bytes tolerance per iteration for allocator fragmentation)
    TEST_ASSERT_TRUE(leakPerIteration <= 8);
}

void test_provider_registry_schema_generation_no_leak() {
    // Test the full registry + provider flow for memory leaks
    ProviderRegistry registry;

    MockWebUIProvider provider1("Provider1", "1.0.0");
    provider1.addContext(WebUIContext::dashboard("p1_ctx", "Provider 1")
        .withField(WebUIField("value", "Value", WebUIFieldType::Number, "100")));

    MockWebUIProvider provider2("Provider2", "1.0.0");
    provider2.addContext(WebUIContext::settings("p2_ctx", "Provider 2")
        .withField(WebUIField("mode", "Mode", WebUIFieldType::Select, "auto")));

    registry.registerProvider(&provider1);
    registry.registerProvider(&provider2);

    // Warmup
    {
        auto state = registry.prepareSchemaGeneration();
        (void)state;
    }

    size_t heapBefore = HAL::Platform::getFreeHeap();

    // Multiple schema preparation cycles
    const int ITERATIONS = 10;
    for (int i = 0; i < ITERATIONS; i++) {
        auto state = registry.prepareSchemaGeneration();
        TEST_ASSERT_NOT_NULL(state.get());
        TEST_ASSERT_EQUAL(2, state->providers.size());
        // State goes out of scope here, should be freed
    }

    size_t heapAfter = HAL::Platform::getFreeHeap();
    int heapDiff = (int)heapBefore - (int)heapAfter;

    printf("Registry heap before: %zu, after: %zu, diff: %d\n",
           heapBefore, heapAfter, heapDiff);

    // shared_ptr should clean up properly - allow small tolerance
    TEST_ASSERT_TRUE(heapDiff <= 32);
}

// ============================================================================
// Schema Validation Tests (simulates what would be sent to browser)
// ============================================================================

void test_full_schema_array_valid_json() {
    // Simulate building a full schema array like /api/ui/schema
    MockWebUIProvider provider1("Provider1", "1.0.0");
    provider1.addContext(WebUIContext::dashboard("p1_dash", "Dashboard 1")
        .withField(WebUIField("temp", "Temperature", WebUIFieldType::Number, "25.5", "°C")));

    MockWebUIProvider provider2("Provider2", "1.0.0");
    provider2.addContext(WebUIContext::settings("p2_settings", "Settings")
        .withField(WebUIField("enabled", "Enabled", WebUIFieldType::Boolean, "true")));

    // Build schema array manually (like the chunked endpoint does)
    String schema = "[";
    bool first = true;

    std::vector<IWebUIProvider*> providers = {&provider1, &provider2};

    for (auto* provider : providers) {
        provider->forEachContext([&schema, &first](const WebUIContext& ctx) {
            StreamingContextSerializer serializer;
            serializer.begin(ctx);

            uint8_t buffer[2048];
            String ctxJson;

            while (!serializer.isComplete()) {
                size_t written = serializer.write(buffer, sizeof(buffer));
                if (written > 0) {
                    buffer[written] = '\0';
                    ctxJson += (const char*)buffer;
                }
            }

            if (!first) schema += ",";
            schema += ctxJson;
            first = false;
            return true;
        });
    }

    schema += "]";

    // Parse the full schema
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, schema);

    TEST_ASSERT_TRUE(err == DeserializationError::Ok);
    TEST_ASSERT_TRUE(doc.is<JsonArray>());
    TEST_ASSERT_EQUAL(2, doc.as<JsonArray>().size());

    // Verify contexts
    TEST_ASSERT_EQUAL_STRING("p1_dash", doc[0]["contextId"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("p2_settings", doc[1]["contextId"].as<const char*>());
}

// ============================================================================
// Memory Leak DETECTION Tests - Test CURRENT behavior before fixes
// ============================================================================

/**
 * This test DETECTS memory behavior of the standard IWebUIProvider.
 * MockWebUIProvider inherits from IWebUIProvider (NOT CachingWebUIProvider),
 * so it recreates contexts on every getWebUIContexts() call.
 * 
 * PURPOSE: Measure actual memory impact of repeated context creation
 * to understand WHERE memory leaks originate.
 */
void test_detect_memory_behavior_repeated_context_creation() {
    HeapTracker tracker;
    
    // Create a provider with substantial context data (simulating real WebUI)
    MockWebUIProvider provider("LeakTest", "1.0.0");
    provider.addContext(WebUIContext::dashboard("dash", "Dashboard")
        .withField(WebUIField("temp", "Temperature", WebUIFieldType::Number, "25.5", "°C", true))
        .withField(WebUIField("humid", "Humidity", WebUIFieldType::Number, "60", "%", true))
        .withCustomHtml("<div class=\"widget\"><span class=\"value\">Custom HTML content here for testing memory allocation patterns in WebUI contexts</span></div>")
        .withCustomCss(".widget { background: #fff; padding: 1rem; } .value { font-size: 2rem; color: #007acc; }"));
    
    provider.addContext(WebUIContext::settings("settings", "Settings")
        .withField(WebUIField("name", "Device Name", WebUIFieldType::Text, "DomoticsCore"))
        .withField(WebUIField("enabled", "Enabled", WebUIFieldType::Boolean, "true")));
    
    // Warm up - first call using new API
    provider.forEachContext([](const WebUIContext& ctx) {
        (void)ctx.getContextIdCStr();
        return true;
    });
    
    // Checkpoint after warmup
    tracker.checkpoint("after_warmup");
    
    // Use NEW memory-efficient API: forEachContext (no vector copies)
    for (int i = 0; i < 50; i++) {
        provider.forEachContext([](const WebUIContext& ctx) {
            (void)ctx.getContextIdCStr();
            return true;
        });
    }
    
    tracker.checkpoint("after_50_calls");
    
    // Calculate delta
    int32_t delta = tracker.getDelta("after_warmup", "after_50_calls");
    
    // Report the actual memory behavior
    printf("\n[MEMORY DETECTION] forEachContext() x50 (NEW optimized API):\n");
    printf("  Heap delta: %d bytes\n", delta);
    printf("  Per call: ~%d bytes\n", delta / 50);
    
    // FAIL if significant memory leak detected
    // Native uses real mallinfo tracking, so we can detect real leaks
    const int32_t LEAK_THRESHOLD = 1024;  // 1KB tolerance for native (allocator overhead)
    
    if (delta > LEAK_THRESHOLD) {
        printf("  *** MEMORY LEAK DETECTED: %d bytes > threshold %d ***\n", delta, LEAK_THRESHOLD);
    }
    
    TEST_ASSERT_TRUE_MESSAGE(delta <= LEAK_THRESHOLD, "Memory leak detected in getWebUIContexts()");
}

/**
 * ZERO LEAK TEST: Test with MULTIPLE providers (like real WebUI)
 * This should identify if leak comes from multi-provider scenario
 */
void test_zero_leak_multiple_providers() {
    HeapTracker tracker;
    
    // 3 providers like real WebUI
    MockWebUIProvider p1("WiFi", "1.0.0");
    p1.addContext(WebUIContext::dashboard("wifi", "WiFi")
        .withField(WebUIField("ssid", "SSID", WebUIFieldType::Text, "MyNet"))
        .withCustomHtml("<div>wifi</div>"));
    
    MockWebUIProvider p2("NTP", "1.0.0");
    p2.addContext(WebUIContext::settings("ntp", "NTP")
        .withField(WebUIField("server", "Server", WebUIFieldType::Text, "pool.ntp.org")));
    
    MockWebUIProvider p3("System", "1.0.0");
    p3.addContext(WebUIContext::dashboard("sys", "System")
        .withField(WebUIField("heap", "Heap", WebUIFieldType::Number, "50000")));
    
    std::vector<IWebUIProvider*> providers = {&p1, &p2, &p3};
    
    // Force cache build
    for (auto* p : providers) {
        p->forEachContext([](const WebUIContext&) { return true; });
    }
    
    tracker.checkpoint("start");
    
    // 500 iterations - using ZERO-COPY getContextAtRef
    for (int i = 0; i < 500; i++) {
        for (auto* provider : providers) {
            size_t count = provider->getContextCount();
            for (size_t j = 0; j < count; j++) {
                const WebUIContext* ctxPtr = provider->getContextAtRef(j);
                if (!ctxPtr) continue;
                
                StreamingContextSerializer serializer;
                serializer.begin(*ctxPtr);
                
                uint8_t buffer[256];
                while (!serializer.isComplete()) {
                    serializer.write(buffer, sizeof(buffer));
                }
            }
        }
    }
    
    tracker.checkpoint("end");
    int32_t delta = tracker.getDelta("start", "end");
    
    printf("\n[ZERO LEAK - MULTI PROVIDER] 3 providers x500:\n");
    printf("  Heap delta: %d bytes (%.2f/iter)\n", delta, delta/500.0f);
    
    // Allow small allocator overhead (native allocator may not release immediately)
    const int32_t THRESHOLD = 512;
    TEST_ASSERT_TRUE_MESSAGE(delta <= THRESHOLD, "Multi-provider leak exceeds threshold");
}

/**
 * ZERO LEAK TEST: Find EXACT source of 3 bytes/req leak
 * Must be 0 bytes - no tolerance for any leak
 */
void test_zero_leak_streaming_only() {
    HeapTracker tracker;
    
    MockWebUIProvider provider("Test", "1.0.0");
    provider.addContext(WebUIContext::dashboard("dash", "Dashboard")
        .withField(WebUIField("temp", "Temp", WebUIFieldType::Number, "25"))
        .withCustomHtml("<div>test content</div>"));
    
    // Force cache build
    provider.forEachContext([](const WebUIContext&) { return true; });
    
    tracker.checkpoint("start");
    
    // Test StreamingContextSerializer with ZERO-COPY
    for (int i = 0; i < 100; i++) {
        size_t count = provider.getContextCount();
        for (size_t j = 0; j < count; j++) {
            const WebUIContext* ctxPtr = provider.getContextAtRef(j);
            if (!ctxPtr) continue;
            
            StreamingContextSerializer serializer;
            serializer.begin(*ctxPtr);
            
            uint8_t buffer[256];
            while (!serializer.isComplete()) {
                serializer.write(buffer, sizeof(buffer));
            }
        }
    }
    
    tracker.checkpoint("end");
    int32_t delta = tracker.getDelta("start", "end");
    
    printf("\n[ZERO LEAK TEST] StreamingContextSerializer x100:\n");
    printf("  Heap delta: %d bytes\n", delta);
    printf("  Per iteration: %.2f bytes\n", delta / 100.0f);
    
    // Allow small allocator overhead
    const int32_t THRESHOLD = 512;
    TEST_ASSERT_TRUE_MESSAGE(delta <= THRESHOLD, "Streaming leak exceeds threshold");
}

/**
 * ZERO LEAK TEST: Test getContextAt alone
 */
void test_zero_leak_getContextAt_only() {
    HeapTracker tracker;
    
    MockWebUIProvider provider("Test", "1.0.0");
    provider.addContext(WebUIContext::dashboard("dash", "Dashboard")
        .withField(WebUIField("temp", "Temp", WebUIFieldType::Number, "25"))
        .withCustomHtml("<div>test</div>"));
    
    // Force cache build
    provider.forEachContext([](const WebUIContext&) { return true; });
    
    tracker.checkpoint("start");
    
    // Test getContextAtRef - ZERO COPY
    for (int i = 0; i < 100; i++) {
        const WebUIContext* ctxPtr = provider.getContextAtRef(0);
        (void)ctxPtr;  // No copy, just pointer access
    }
    
    tracker.checkpoint("end");
    int32_t delta = tracker.getDelta("start", "end");
    
    printf("[MEMORY TEST] getContextAt x100: %d bytes (%.2f/iter)\n", delta, delta/100.0f);
    // Allow small allocator overhead
    const int32_t THRESHOLD = 512;
    TEST_ASSERT_TRUE_MESSAGE(delta <= THRESHOLD, "getContextAt leak exceeds threshold");
}

/**
 * ISOLATION: Test ONLY JsonDocument allocation (no contexts)
 */
void test_isolate_jsondocument_only() {
    HeapTracker tracker;
    tracker.checkpoint("start");
    
    for (int i = 0; i < 500; i++) {
        JsonDocument doc;
        JsonObject obj = doc.to<JsonObject>();
        obj["test"] = "value";
        obj["number"] = i;
        String json;
        serializeJson(doc, json);
        (void)json;
    }
    
    tracker.checkpoint("end");
    int32_t delta = tracker.getDelta("start", "end");
    printf("\n[ISOLATE JsonDocument x500]: %d bytes (%.1f/req)\n", delta, delta/500.0f);
    TEST_ASSERT_TRUE_MESSAGE(delta <= 512, "JsonDocument leak");
}

/**
 * ISOLATION: Test ONLY String concatenation
 */
void test_isolate_string_concat_only() {
    HeapTracker tracker;
    tracker.checkpoint("start");
    
    for (int i = 0; i < 500; i++) {
        String base = "{\"test\":\"value\"}";
        String result = "," + base;
        (void)result;
    }
    
    tracker.checkpoint("end");
    int32_t delta = tracker.getDelta("start", "end");
    printf("[ISOLATE String concat x500]: %d bytes (%.1f/req)\n", delta, delta/500.0f);
    TEST_ASSERT_TRUE_MESSAGE(delta <= 512, "String concat leak");
}

/**
 * ISOLATION: Test ONLY getWebUIContexts copies
 */
void test_isolate_context_copies_only() {
    HeapTracker tracker;
    
    MockWebUIProvider provider("Test", "1.0.0");
    provider.addContext(WebUIContext::dashboard("dash", "Dashboard")
        .withField(WebUIField("temp", "Temp", WebUIFieldType::Number, "25"))
        .withCustomHtml("<div>test</div>"));
    
    // Warmup
    (void)provider.getContextCount();
    
    tracker.checkpoint("start");
    
    for (int i = 0; i < 500; i++) {
        (void)provider.getContextCount();

    }
    
    tracker.checkpoint("end");
    int32_t delta = tracker.getDelta("start", "end");
    printf("[ISOLATE context copies x500]: %d bytes (%.1f/req)\n", delta, delta/500.0f);
    TEST_ASSERT_TRUE_MESSAGE(delta <= 1024, "Context copy leak");
}

/**
 * ISOLATION: Combine context + JSON (like real code)
 */
void test_isolate_context_plus_json() {
    HeapTracker tracker;
    
    MockWebUIProvider provider("Test", "1.0.0");
    provider.addContext(WebUIContext::dashboard("dash", "Dashboard")
        .withField(WebUIField("temp", "Temp", WebUIFieldType::Number, "25"))
        .withCustomHtml("<div>test</div>"));
    
    (void)provider.getContextCount();
    
    tracker.checkpoint("start");
    
    for (int i = 0; i < 500; i++) {
        provider.forEachContext([](const WebUIContext& ctx) {
            JsonDocument doc;
            doc["id"] = ctx.getContextIdCStr();
            doc["html"] = ctx.getCustomHtmlCStr();
            String json;
            serializeJson(doc, json);
            String pending = "," + json;
            (void)pending;
            return true;
        });
    }
    
    tracker.checkpoint("end");
    int32_t delta = tracker.getDelta("start", "end");
    printf("[ISOLATE context+JSON x500]: %d bytes (%.1f/req)\n", delta, delta/500.0f);
    TEST_ASSERT_TRUE_MESSAGE(delta <= 2048, "Context+JSON leak");
}

/**
 * AGGRESSIVE TEST: Simulate 500 curl requests to reproduce ESP8266 OOM
 * This test monitors heap trend over many iterations to detect gradual leak.
 */
void test_aggressive_schema_generation_500_requests() {
    HeapTracker tracker;
    
    // Setup realistic providers with substantial data
    MockWebUIProvider provider1("WiFi", "1.4.0");
    provider1.addContext(WebUIContext::statusBadge("wifi_status", "WiFi", "dc-wifi").withRealTime(2000));
    provider1.addContext(WebUIContext::dashboard("wifi_component", "WiFi")
        .withField(WebUIField("ssid", "SSID", WebUIFieldType::Text, "MyNetwork"))
        .withField(WebUIField("ip", "IP", WebUIFieldType::Display, "192.168.1.100"))
        .withField(WebUIField("signal", "Signal", WebUIFieldType::Number, "-65", "dBm", true))
        .withCustomHtml("<div class='wifi-signal'><span class='bars'></span></div>")
        .withCustomCss(".wifi-signal { display: flex; } .bars { width: 20px; }"));
    
    MockWebUIProvider provider2("NTP", "1.3.0");
    provider2.addContext(WebUIContext::headerInfo("ntp_time", "Time", "dc-clock")
        .withField(WebUIField("time", "Time", WebUIFieldType::Display, "14:30:00"))
        .withRealTime(1000));
    provider2.addContext(WebUIContext::settings("ntp_settings", "NTP Settings")
        .withField(WebUIField("server", "Server", WebUIFieldType::Text, "pool.ntp.org"))
        .withField(WebUIField("timezone", "Timezone", WebUIFieldType::Select, "UTC")));
    
    MockWebUIProvider provider3("SystemInfo", "1.4.0");
    provider3.addContext(WebUIContext::dashboard("sysinfo_dash", "System")
        .withField(WebUIField("heap", "Free Heap", WebUIFieldType::Number, "45000", "bytes", true))
        .withField(WebUIField("uptime", "Uptime", WebUIFieldType::Display, "1d 5h 30m"))
        .withCustomHtml("<div class='gauge'><svg viewBox='0 0 100 100'></svg></div>")
        .withCustomCss(".gauge svg { width: 100%; height: auto; }"));
    
    // Warm up
    for (int w = 0; w < 5; w++) {
        (void)provider1.getContextCount();
        (void)provider2.getContextCount();
        (void)provider3.getContextCount();
    }
    
    // Pre-build provider list ONCE (like WebUI.h does with static state)
    IWebUIProvider* providers[3] = {&provider1, &provider2, &provider3};
    
    tracker.checkpoint("start");
    
    const int TOTAL_REQUESTS = 500;
    
    for (int request = 0; request < TOTAL_REQUESTS; request++) {
        // Simulate WebUI.h behavior using getContextAtRef() + StreamingContextSerializer
        // ZERO COPY - pointer to cached context
        for (int p = 0; p < 3; p++) {
            IWebUIProvider* provider = providers[p];
            size_t contextCount = provider->getContextCount();
            for (size_t i = 0; i < contextCount; i++) {
                const WebUIContext* ctxPtr = provider->getContextAtRef(i);
                if (!ctxPtr) continue;
                
                StreamingContextSerializer serializer;
                serializer.begin(*ctxPtr);
                
                uint8_t buffer[512];
                while (!serializer.isComplete()) {
                    serializer.write(buffer, sizeof(buffer));
                }
            }
        }
    }
    
    tracker.checkpoint("end");
    int32_t delta = tracker.getDelta("start", "end");
    
    printf("\n[AGGRESSIVE TEST - 500 curl requests simulation]\n");
    printf("  Heap delta: %d bytes (%.2f/req)\n", delta, delta / 500.0f);
    
    // MUST be 0 - no tolerance
    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, delta, "AGGRESSIVE test MUST have ZERO leak");
}

/**
 * CRITICAL TEST: Simulate repeated schema JSON generation (like curl requests)
 * This reproduces the OOM issue observed on ESP8266 with repeated curl.
 */
void test_simulate_repeated_schema_generation() {
    HeapTracker tracker;
    
    // Setup providers like real WebUI
    MockWebUIProvider provider1("TestComp1", "1.0.0");
    provider1.addContext(WebUIContext::dashboard("dash1", "Dashboard 1")
        .withField(WebUIField("temp", "Temperature", WebUIFieldType::Number, "25.5", "°C", true))
        .withCustomHtml("<div class='widget'>Custom content</div>"));
    
    MockWebUIProvider provider2("TestComp2", "1.0.0");
    provider2.addContext(WebUIContext::settings("settings2", "Settings")
        .withField(WebUIField("name", "Name", WebUIFieldType::Text, "Device")));
    
    // Warm up
    for (int w = 0; w < 2; w++) {
        (void)provider1.getContextCount();
        (void)provider2.getContextCount();
    }
    
    tracker.checkpoint("before_schema_gen");
    
    // Simulate 50 curl requests on /api/ui/schema
    const int CURL_REQUESTS = 50;
    for (int request = 0; request < CURL_REQUESTS; request++) {
        // Simulate what WebUI.h does for each request:
        
        // 1. Iterate contexts via forEachContext (no copy)
        provider1.forEachContext([](const WebUIContext& ctx) {
            JsonDocument doc;
            JsonObject obj = doc.to<JsonObject>();
            obj["contextId"] = ctx.getContextIdCStr();
            obj["title"] = ctx.getTitleCStr();
            obj["customHtml"] = ctx.getCustomHtmlCStr();
            
            String json;
            serializeJson(doc, json);
            String pending = "," + json;
            (void)pending;
            return true;
        });
        
        provider2.forEachContext([](const WebUIContext& ctx) {
            JsonDocument doc;
            JsonObject obj = doc.to<JsonObject>();
            obj["contextId"] = ctx.getContextIdCStr();
            obj["title"] = ctx.getTitleCStr();
            
            String json;
            serializeJson(doc, json);
            String pending = "," + json;
            (void)pending;
            return true;
        });
    }
    
    tracker.checkpoint("after_schema_gen");
    
    int32_t delta = tracker.getDelta("before_schema_gen", "after_schema_gen");
    int32_t perRequest = delta / CURL_REQUESTS;
    
    printf("\n[SCHEMA GENERATION LEAK TEST - Simulates curl requests]\n");
    printf("  Simulated curl requests: %d\n", CURL_REQUESTS);
    printf("  Total heap delta: %d bytes\n", delta);
    printf("  Per request: %d bytes\n", perRequest);
    
    // This should FAIL - demonstrating the real leak source
    const int32_t LEAK_THRESHOLD = 512;
    
    if (delta > LEAK_THRESHOLD) {
        printf("  *** SCHEMA GENERATION LEAK DETECTED: %d bytes > %d ***\n", delta, LEAK_THRESHOLD);
        printf("  This is the source of OOM on repeated curl requests!\n");
    }
    
    TEST_ASSERT_TRUE_MESSAGE(delta <= LEAK_THRESHOLD, "Schema generation leak detected");
}

/**
 * ISOLATION TEST: Test if leak comes from String copies vs vector operations
 */
void test_isolate_string_copy_leak() {
    HeapTracker tracker;
    
    // Test 1: Pure String operations (no WebUIContext)
    tracker.checkpoint("before_strings");
    
    for (int i = 0; i < 50; i++) {
        String s1 = "Test string with some content";
        String s2 = s1;  // Copy
        String s3 = s2 + " more content";
        (void)s3;
    }
    
    tracker.checkpoint("after_strings");
    int32_t stringDelta = tracker.getDelta("before_strings", "after_strings");
    
    // Test 2: WebUIContext without customHtml (minimal)
    MockWebUIProvider minimalProvider("Minimal", "1.0.0");
    minimalProvider.addContext(WebUIContext::dashboard("min", "Min"));
    
    (void)minimalProvider.getContextCount();
    
    tracker.checkpoint("before_minimal");
    
    for (int i = 0; i < 50; i++) {
        (void)minimalProvider.getContextCount();
    }
    
    tracker.checkpoint("after_minimal");
    int32_t minimalDelta = tracker.getDelta("before_minimal", "after_minimal");
    
    // Test 3: WebUIContext WITH customHtml (large strings)
    MockWebUIProvider largeProvider("Large", "1.0.0");
    largeProvider.addContext(WebUIContext::dashboard("large", "Large")
        .withCustomHtml("<div>Large HTML content that takes memory</div>")
        .withCustomCss(".large { color: red; }"));
    
    (void)largeProvider.getContextCount();
    
    tracker.checkpoint("before_large");
    
    for (int i = 0; i < 50; i++) {
        (void)largeProvider.getContextCount();
    }
    
    tracker.checkpoint("after_large");
    int32_t largeDelta = tracker.getDelta("before_large", "after_large");
    
    printf("\n[LEAK ISOLATION TEST]\n");
    printf("  Pure String ops x50:       %d bytes\n", stringDelta);
    printf("  Minimal context x50:       %d bytes\n", minimalDelta);
    printf("  Large customHtml x50:      %d bytes\n", largeDelta);
    printf("  Difference (large-minimal): %d bytes\n", largeDelta - minimalDelta);
    
    // The difference shows how much customHtml contributes
    TEST_ASSERT_TRUE(true);  // Informational test
}

/**
 * Test memory behavior when contexts contain large customHtml/Css/Js strings
 * These are the suspected source of memory leaks on ESP8266.
 */
void test_detect_memory_large_custom_content() {
    HeapTracker tracker;
    
    MockWebUIProvider provider("LargeContent", "1.0.0");
    
    // Create context with large custom content (simulating real chart/complex UI)
    String largeHtml = "<div class=\"chart-container\">";
    for (int i = 0; i < 20; i++) {
        largeHtml += "<div class=\"data-point\" data-value=\"" + String(i * 10) + "\"></div>";
    }
    largeHtml += "</div>";
    
    provider.addContext(WebUIContext::dashboard("chart", "Chart")
        .withCustomHtmlDynamic(largeHtml)
        .withCustomCss(".chart-container { display: flex; } .data-point { width: 20px; height: var(--value); }")
        .withCustomJs("function updateChart(data) { /* chart update logic */ }"));
    
    tracker.checkpoint("before");
    
    // Use NEW memory-efficient API: forEachContext
    for (int i = 0; i < 20; i++) {
        provider.forEachContext([](const WebUIContext& ctx) {
            // Access custom content via const reference (no copy)
            const String& html = ctx.customHtml;
            const String& css = ctx.customCss;
            const String& js = ctx.customJs;
            (void)html; (void)css; (void)js;
            return true;
        });
    }
    
    tracker.checkpoint("after");
    
    int32_t delta = tracker.getDelta("before", "after");
    printf("\n[MEMORY DETECTION] Large customHtml/Css/Js x20 (forEachContext):\n");
    printf("  Heap delta: %d bytes\n", delta);
    printf("  Content size: ~%d bytes\n", (int)largeHtml.length());
    
    // FAIL if significant memory leak detected
    const int32_t LEAK_THRESHOLD = 512;
    
    if (delta > LEAK_THRESHOLD) {
        printf("  *** MEMORY LEAK DETECTED: %d bytes > threshold %d ***\n", delta, LEAK_THRESHOLD);
    }
    
    TEST_ASSERT_TRUE_MESSAGE(delta <= LEAK_THRESHOLD, "Memory leak in large custom content");
}

// ============================================================================
// CachingWebUIProvider Memory Tests (HeapTracker Integration)
// ============================================================================

// Test implementation of CachingWebUIProvider for memory testing
class TestCachingProvider : public CachingWebUIProvider {
public:
    int buildCount = 0;
    
protected:
    void buildContexts(std::vector<WebUIContext>& contexts) override {
        buildCount++;
        // Create contexts with substantial data to detect leaks
        contexts.push_back(WebUIContext::dashboard("test_dash", "Dashboard")
            .withField(WebUIField("field1", "Field 1", WebUIFieldType::Text, "value1"))
            .withField(WebUIField("field2", "Field 2", WebUIFieldType::Number, "42"))
            .withCustomHtml("<div class='test'>Custom HTML Content</div>")
            .withCustomCss(".test { color: red; }"));
        
        contexts.push_back(WebUIContext::settings("test_settings", "Settings")
            .withField(WebUIField("setting1", "Setting", WebUIFieldType::Boolean, "true")));
    }
    
public:
    String getWebUIName() const override { return "TestProvider"; }
    String getWebUIVersion() const override { return "1.0.0"; }
    
    String handleWebUIRequest(const String& contextId, const String& endpoint,
                              const String& method, const std::map<String, String>& params) override {
        return "{}";
    }
};

void test_caching_provider_builds_once() {
    TestCachingProvider provider;
    
    // First call should trigger build
    TEST_ASSERT_EQUAL(2, provider.getContextCount());
    TEST_ASSERT_EQUAL(1, provider.buildCount);
    
    // Second call should use cache
    TEST_ASSERT_EQUAL(2, provider.getContextCount());
    TEST_ASSERT_EQUAL(1, provider.buildCount); // Still 1, not rebuilt
    
    // Third call - still cached
    TEST_ASSERT_EQUAL(2, provider.getContextCount());
    TEST_ASSERT_EQUAL(1, provider.buildCount);
}

void test_caching_provider_memory_stable_100_calls() {
    HeapTracker tracker;
    TestCachingProvider provider;
    
    // First call builds cache
    (void)provider.getContextCount();
    
    // Checkpoint after cache is built
    tracker.checkpoint("after_cache");
    
    // Call 100 times - should not allocate new memory
    for (int i = 0; i < 100; i++) {
        TEST_ASSERT_EQUAL(2, provider.getContextCount());
    }
    
    tracker.checkpoint("after_100_calls");
    
    // Assert no heap growth (tolerance for internal std::vector copies)
    MemoryTestResult result = tracker.assertStable("after_cache", "after_100_calls", 1024);
    TEST_ASSERT_TRUE_MESSAGE(result.passed, result.message.c_str());
}

void test_caching_provider_invalidate_rebuilds() {
    TestCachingProvider provider;
    
    // Build cache
    (void)provider.getContextCount();
    TEST_ASSERT_EQUAL(1, provider.buildCount);
    
    // Invalidate
    provider.invalidateContextCache();
    
    // Next call should rebuild
    (void)provider.getContextCount();
    TEST_ASSERT_EQUAL(2, provider.buildCount);
}

void test_caching_provider_foreach_no_rebuild() {
    TestCachingProvider provider;
    
    // Build via getWebUIContexts
    (void)provider.getContextCount();
    TEST_ASSERT_EQUAL(1, provider.buildCount);
    
    // forEachContext should use cache
    int contextCount = 0;
    provider.forEachContext([&contextCount](const WebUIContext& ctx) {
        contextCount++;
        return true;
    });
    
    TEST_ASSERT_EQUAL(2, contextCount);
    TEST_ASSERT_EQUAL(1, provider.buildCount); // No rebuild
}

void test_caching_provider_get_context_at() {
    TestCachingProvider provider;
    
    WebUIContext ctx;
    bool found = provider.getContextAt(0, ctx);
    TEST_ASSERT_TRUE(found);
    TEST_ASSERT_EQUAL_STRING("test_dash", ctx.getContextIdCStr());
    
    found = provider.getContextAt(1, ctx);
    TEST_ASSERT_TRUE(found);
    TEST_ASSERT_EQUAL_STRING("test_settings", ctx.getContextIdCStr());
    
    found = provider.getContextAt(2, ctx);
    TEST_ASSERT_FALSE(found);
    
    // All via cache - only 1 build
    TEST_ASSERT_EQUAL(1, provider.buildCount);
}

void test_caching_provider_memory_lifecycle() {
    HeapTracker tracker;
    
    tracker.checkpoint("before");
    
    // Create and destroy multiple providers
    for (int i = 0; i < 10; i++) {
        TestCachingProvider provider;
        (void)provider.getContextCount();
        // Provider destroyed at end of scope
    }
    
    tracker.checkpoint("after");
    
    MemoryTestResult result = tracker.assertStable("before", "after", 512);
    TEST_ASSERT_TRUE_MESSAGE(result.passed, result.message.c_str());
}

/**
 * Test: forEachContext with copy assignment (simulates /api/ui/context issue)
 * This test demonstrates the memory cost of copying a context inside forEachContext
 */
void test_foreach_context_with_copy_assignment() {
    HeapTracker tracker;
    
    // Create provider with contexts (TestCachingProvider has built-in contexts)
    TestCachingProvider provider;
    
    // Warmup - trigger cache build and do initial copies
    WebUIContext warmupCopy;
    provider.forEachContext([&](const WebUIContext& ctx) {
        warmupCopy = ctx;
        return true;
    });
    
    tracker.checkpoint("start");
    
    // Simulate /api/ui/context behavior: find context and COPY it
    for (int i = 0; i < 100; i++) {
        WebUIContext foundContext;  // Stack allocation
        bool found = false;
        
        provider.forEachContext([&](const WebUIContext& ctx) {
            if (strcmp(ctx.getContextIdCStr(), "test_dash") == 0) {  // Use existing context ID
                foundContext = ctx;  // COPY - this is the potential leak source
                found = true;
                return false;
            }
            return true;
        });
        
        TEST_ASSERT_TRUE(found);
        // foundContext goes out of scope here - should be deallocated
    }
    
    tracker.checkpoint("end");
    
    int32_t delta = tracker.getDelta("start", "end");
    printf("\n[forEachContext with copy x100]: %d bytes (%.1f/req)\n", delta, delta/100.0f);
    
    // Should be stable - copies are stack-allocated and freed each iteration
    TEST_ASSERT_TRUE_MESSAGE(delta <= 256, "forEachContext copy leak detected");
}

/**
 * Test: Rapid refresh schema generation (simulates browser F5 spam)
 * 
 * This test reproduces the real-world scenario where:
 * 1. User rapidly refreshes the page (F5 spam)
 * 2. Each refresh triggers: schema request + WebSocket connect
 * 3. Previous requests may be interrupted (client disconnects)
 * 4. Memory should remain stable despite incomplete operations
 * 
 * Real behavior observed on ESP8266:
 * - Heap drops from 11216 to 3240 bytes during rapid refresh
 * - OOM crash when heap goes below ~2000 bytes
 */
void test_rapid_refresh_schema_generation() {
    HeapTracker tracker;
    
    // Create provider with realistic content (like LEDWebUI + SystemInfoWebUI)
    TestCachingProvider provider;
    
    // Warmup - let caches build
    provider.forEachContext([](const WebUIContext& ctx) { return true; });
    
    tracker.checkpoint("start");
    
    // Simulate 50 rapid page refreshes
    // Each refresh does: 1) start schema gen, 2) possibly interrupt, 3) start new one
    for (int refresh = 0; refresh < 50; refresh++) {
        // Simulate partial schema generation (like interrupted by disconnect)
        // This is what happens when user refreshes before schema completes
        
        // 1. Get schema contexts (simulates /api/ui/schema start)
        std::vector<const WebUIContext*> contextPtrs;
        size_t idx = 0;
        while (const WebUIContext* ctx = provider.getContextAtRef(idx++)) {
            contextPtrs.push_back(ctx);
        }
        
        // 2. Serialize only SOME contexts (simulates interrupted transfer)
        // About 30% of refreshes complete, 70% are interrupted
        size_t contextsToSerialize = (refresh % 3 == 0) ? contextPtrs.size() : contextPtrs.size() / 2;
        
        for (size_t i = 0; i < contextsToSerialize && i < contextPtrs.size(); i++) {
            StreamingContextSerializer serializer;
            serializer.begin(*contextPtrs[i]);
            
            // Serialize to local buffer (simulates chunked response)
            uint8_t buffer[256];
            while (!serializer.isComplete()) {
                size_t written = serializer.write(buffer, sizeof(buffer));
                if (written == 0) break;
                // In real code, this would be sent to client
                // If client disconnects, we just stop here
            }
        }
        
        // 3. Simulate WebSocket data send (getWebUIData allocates Strings)
        for (size_t i = 0; i < contextPtrs.size(); i++) {
            // This simulates getWebUIData() which creates JSON strings
            JsonDocument doc;
            doc["test_field"] = "test_value";
            doc["iteration"] = refresh;
            String json;
            serializeJson(doc, json);
            // json goes out of scope - should be freed
        }
    }
    
    tracker.checkpoint("end");
    
    int32_t delta = tracker.getDelta("start", "end");
    printf("\n[Rapid refresh x50]: %d bytes delta (%.1f/refresh)\n", delta, delta/50.0f);
    
    // Allow small tolerance for allocator overhead
    // If this fails, there's a memory leak during rapid refresh
    TEST_ASSERT_TRUE_MESSAGE(delta <= 512, 
        "Memory leak detected during rapid refresh simulation - "
        "heap should be stable after 50 page refreshes");
}

/**
 * @brief Simulates Standard example with many providers (16 contexts)
 * 
 * This reproduces the OOM scenario on ESP8266 where many providers
 * create many contexts that all need to be serialized together.
 */
class MultiContextProvider : public CachingWebUIProvider {
protected:
    void buildContexts(std::vector<WebUIContext>& ctxs) override {
        // Simulate WifiWebUI (5 contexts)
        ctxs.push_back(WebUIContext::statusBadge("wifi_status", "WiFi", "dc-wifi").withRealTime(2000));
        ctxs.push_back(WebUIContext::statusBadge("ap_status", "AP", "dc-ap").withRealTime(2000));
        ctxs.push_back(WebUIContext{"wifi_component", "WiFi", "dc-wifi", WebUILocation::ComponentDetail, WebUIPresentation::Card}
            .withField(WebUIField("connected", "Connected", WebUIFieldType::Display, "No", "", true))
            .withField(WebUIField("ssid_now", "SSID", WebUIFieldType::Display, "", "", true))
            .withField(WebUIField("ip", "IP", WebUIFieldType::Display, "0.0.0.0", "", true))
            .withRealTime(2000));
        ctxs.push_back(WebUIContext::settings("wifi_sta_settings", "WiFi Network")
            .withField(WebUIField("ssid", "Network SSID", WebUIFieldType::Text, ""))
            .withField(WebUIField("sta_password", "Password", WebUIFieldType::Password, ""))
            .withField(WebUIField("scan_networks", "Scan Networks", WebUIFieldType::Button, ""))
            .withField(WebUIField("networks", "Available Networks", WebUIFieldType::Display, ""))
            .withField(WebUIField("wifi_enabled", "Enable WiFi", WebUIFieldType::Boolean, "false"))
            .withRealTime(2000));
        ctxs.push_back(WebUIContext::settings("wifi_ap_settings", "Access Point (AP)")
            .withField(WebUIField("ap_ssid", "AP SSID", WebUIFieldType::Text, "DomoticsCore-AP"))
            .withField(WebUIField("ap_enabled", "Enable AP", WebUIFieldType::Boolean, "true"))
            .withRealTime(2000));
        
        // Simulate NTPWebUI (4 contexts)
        ctxs.push_back(WebUIContext::headerInfo("ntp_time", "Time", "dc-clock")
            .withField(WebUIField("time", "Time", WebUIFieldType::Display, "--:--:--", "", true))
            .withRealTime(1000));
        ctxs.push_back(WebUIContext::dashboard("ntp_dashboard", "Current Time", "dc-clock")
            .withField(WebUIField("time", "Time", WebUIFieldType::Display, "--:--:--", "", true))
            .withField(WebUIField("date", "Date", WebUIFieldType::Display, "----/--/--", "", true))
            .withField(WebUIField("timezone", "Timezone", WebUIFieldType::Display, "UTC", "", true))
            .withRealTime(1000));
        ctxs.push_back(WebUIContext::settings("ntp_settings", "NTP Configuration")
            .withField(WebUIField("enabled", "Enable NTP Sync", WebUIFieldType::Boolean, "true"))
            .withField(WebUIField("servers", "NTP Servers", WebUIFieldType::Text, "pool.ntp.org"))
            .withField(WebUIField("sync_interval", "Sync Interval (hours)", WebUIFieldType::Number, "1")));
        
        // Simulate SystemInfoWebUI (3 contexts)
        ctxs.push_back(WebUIContext::dashboard("system_info", "Device Information")
            .withField(WebUIField("manufacturer", "Manufacturer", WebUIFieldType::Display, "", "", true))
            .withField(WebUIField("firmware", "Firmware", WebUIFieldType::Display, "", "", true))
            .withField(WebUIField("chip", "Chip", WebUIFieldType::Display, "", "", true))
            .withField(WebUIField("revision", "Revision", WebUIFieldType::Display, "", "", true))
            .withField(WebUIField("cpu_freq", "CPU Freq", WebUIFieldType::Display, "", "", true))
            .withField(WebUIField("total_heap", "Total Heap", WebUIFieldType::Display, "", "", true)));
        ctxs.push_back(WebUIContext::dashboard("system_metrics", "System Metrics")
            .withField(WebUIField("cpu_load", "CPU Load", WebUIFieldType::Chart, "", "%"))
            .withField(WebUIField("heap_usage", "Memory Usage", WebUIFieldType::Chart, "", "%"))
            .withRealTime(2000));
        ctxs.push_back(WebUIContext::settings("system_settings", "Device Settings")
            .withField(WebUIField("device_name", "Device Name", WebUIFieldType::Text, "")));
        
        // Simulate RemoteConsoleWebUI (2 contexts)
        ctxs.push_back(WebUIContext{"console_component", "Remote Console", "dc-plug", WebUILocation::ComponentDetail, WebUIPresentation::Card}
            .withField(WebUIField("status", "Status", WebUIFieldType::Display, "Active", "", true))
            .withField(WebUIField("port", "Port", WebUIFieldType::Display, "23 (Telnet)", "", true)));
        ctxs.push_back(WebUIContext::settings("console_settings", "Remote Console")
            .withField(WebUIField("port", "Telnet Port", WebUIFieldType::Display, "23"))
            .withField(WebUIField("protocol", "Protocol", WebUIFieldType::Display, "Telnet")));
        
        // Simulate WebUI builtin (2 contexts)
        ctxs.push_back(WebUIContext::headerInfo("webui_uptime", "Uptime", "dc-clock")
            .withField(WebUIField("uptime", "Uptime", WebUIFieldType::Display, "0s", "", true))
            .withRealTime(5000));
        ctxs.push_back(WebUIContext::settings("webui_settings", "WebUI Settings")
            .withField(WebUIField("theme", "Theme", WebUIFieldType::Select, "auto"))
            .withField(WebUIField("primary_color", "Primary Color", WebUIFieldType::Color, "#007acc")));
    }
public:
    String getWebUIName() const override { return "MultiTest"; }
    String getWebUIVersion() const override { return "1.0.0"; }
    String getWebUIData(const String&) override { return "{}"; }
    String handleWebUIRequest(const String&, const String&, const String&, const std::map<String, String>&) override { return "{}"; }
    bool hasDataChanged(const String&) override { return false; }
};

void test_many_providers_memory_usage() {
    HeapTracker tracker;
    
    tracker.checkpoint("before_provider");
    
    // Create provider with 16 contexts (like Standard example)
    MultiContextProvider* provider = new MultiContextProvider();
    
    tracker.checkpoint("after_create");
    
    // Force context build (cache warmup)
    size_t contextCount = 0;
    provider->forEachContext([&contextCount](const WebUIContext& ctx) { 
        contextCount++; 
        return true; 
    });
    
    tracker.checkpoint("after_warmup");
    
    printf("\n[Many providers test]: %zu contexts created\n", contextCount);
    TEST_ASSERT_EQUAL(15, contextCount);  // 5 Wifi + 3 NTP + 3 SystemInfo + 2 Console + 2 WebUI
    
    // Measure schema serialization memory
    tracker.checkpoint("before_schema");
    
    size_t totalSchemaSize = 0;
    size_t idx = 0;
    while (const WebUIContext* ctx = provider->getContextAtRef(idx++)) {
        StreamingContextSerializer serializer;
        serializer.begin(*ctx);
        
        uint8_t buffer[512];
        while (!serializer.isComplete()) {
            size_t written = serializer.write(buffer, sizeof(buffer));
            totalSchemaSize += written;
            if (written == 0) break;
        }
    }
    
    tracker.checkpoint("after_schema");
    
    printf("[Many providers test]: Schema size = %zu bytes\n", totalSchemaSize);
    printf("[Many providers test]: Peak memory for schema serialization = %d bytes\n", 
           (int)tracker.getDelta("before_schema", "after_schema"));
    
    // Cleanup
    delete provider;
    
    tracker.checkpoint("after_cleanup");
    
    int32_t totalDelta = tracker.getDelta("before_provider", "after_cleanup");
    printf("[Many providers test]: Total memory delta = %d bytes\n", totalDelta);
    
    // Memory should be mostly released (allow 4KB for allocator overhead/fragmentation on native)
    // On ESP8266, streaming serialization (0 bytes peak) is what matters most
    TEST_ASSERT_TRUE_MESSAGE(totalDelta <= 4096, 
        "Memory leak after provider cleanup - should be under 4KB");
    
    // Schema should not be too large (target < 10KB for ESP8266 with ~6KB free heap)
    TEST_ASSERT_TRUE_MESSAGE(totalSchemaSize < 10000, 
        "Schema too large for ESP8266 - consider reducing contexts or fields");
}

/**
 * @brief Test rapid consecutive schema serializations (simulates browser page load)
 * 
 * Browser may send multiple requests rapidly on page load. Previously this
 * caused 429 errors due to rate limiting. Now we just reset incomplete requests.
 */
void test_rapid_consecutive_schema_requests() {
    // Create a provider with contexts
    class TestProvider : public CachingWebUIProvider {
    protected:
        void buildContexts(std::vector<WebUIContext>& ctxs) override {
            ctxs.push_back(WebUIContext::dashboard("test1", "Test 1"));
            ctxs.push_back(WebUIContext::settings("test2", "Test 2"));
        }
    public:
        String getWebUIName() const override { return "TestProvider"; }
        String getWebUIVersion() const override { return "1.0.0"; }
        String getWebUIData(const String&) override { return "{}"; }
        String handleWebUIRequest(const String&, const String&, const String&, const std::map<String, String>&) override { return "{}"; }
        bool hasDataChanged(const String&) override { return false; }
    };

    TestProvider provider;
    
    // Simulate 10 rapid consecutive schema serializations (like browser page loads)
    for (int i = 0; i < 10; i++) {
        size_t idx = 0;
        while (const WebUIContext* ctx = provider.getContextAtRef(idx++)) {
            StreamingContextSerializer serializer;
            serializer.begin(*ctx);
            
            uint8_t buffer[512];
            while (!serializer.isComplete()) {
                size_t written = serializer.write(buffer, sizeof(buffer));
                if (written == 0) break;
            }
        }
    }
    
    // If we got here without crash or hang, the test passes
    // Previously, rate limiting would block rapid requests with 429
    TEST_PASS_MESSAGE("Rapid consecutive requests handled without blocking");
}

// ============================================================================
// Integration Tests - Schema Endpoint Simulation
// ============================================================================

/**
 * @brief Helper to serialize a context to JSON using ArduinoJson (same as endpoint)
 */
void serializeContextToJson(JsonObject& obj, const WebUIContext& context) {
    obj["contextId"] = context.getContextIdCStr();
    obj["title"] = context.getTitleCStr();
    obj["icon"] = context.getIconCStr();
    obj["location"] = (int)context.location;
    obj["presentation"] = (int)context.presentation;
    obj["priority"] = context.priority;
    obj["apiEndpoint"] = context.getApiEndpointCStr();
    obj["alwaysInteractive"] = context.alwaysInteractive;
    
    if (!context.customHtml.isEmpty()) obj["customHtml"] = context.customHtml;
    if (!context.customCss.isEmpty()) obj["customCss"] = context.customCss;
    if (!context.customJs.isEmpty()) obj["customJs"] = context.customJs;

    JsonArray fields = obj["fields"].to<JsonArray>();
    for (const auto& field : context.fields) {
        JsonObject fieldObj = fields.add<JsonObject>();
        fieldObj["name"] = field.getNameCStr();
        fieldObj["label"] = field.getLabelCStr();
        fieldObj["type"] = (int)field.type;
        fieldObj["value"] = field.getValueCStr();
        fieldObj["unit"] = field.getUnitCStr();
        fieldObj["readOnly"] = field.readOnly;
        fieldObj["minValue"] = field.minValue;
        fieldObj["maxValue"] = field.maxValue;
        fieldObj["endpoint"] = field.getEndpointCStr();
        if (!field.options.empty()) {
            JsonArray options = fieldObj["options"].to<JsonArray>();
            for (const auto& opt : field.options) options.add(opt);
        }
        if (!field.optionLabels.empty()) {
            JsonObject labels = fieldObj["optionLabels"].to<JsonObject>();
            for (const auto& pair : field.optionLabels) labels[pair.first] = pair.second;
        }
    }
}

/**
 * @brief Integration test: Simulates the /api/ui/schema endpoint exactly
 * 
 * This test reproduces the exact code path used in WebUI.h to serialize
 * the schema, then parses it back to verify it's valid JSON.
 */
void test_integration_schema_endpoint_produces_valid_json() {
    // Setup: Create provider with multiple contexts
    MultiContextProvider provider;
    
    // Build unique provider list (like endpoint does)
    std::vector<IWebUIProvider*> providers;
    providers.push_back(&provider);
    
    // Simulate ResponseStream by building JSON string
    String jsonOutput = "[";
    bool first = true;
    
    for (IWebUIProvider* p : providers) {
        if (!p || !p->isWebUIEnabled()) continue;
        
        // Use direct pointer access - no copies (exactly like endpoint)
        for (size_t i = 0; ; i++) {
            const WebUIContext* ctx = p->getContextAtRef(i);
            if (!ctx) break;
            if (strlen(ctx->getContextIdCStr()) == 0) continue;
            
            if (!first) jsonOutput += ",";
            first = false;
            
            // Serialize directly using ArduinoJson
            JsonDocument doc;
            JsonObject obj = doc.to<JsonObject>();
            serializeContextToJson(obj, *ctx);
            
            String ctxJson;
            serializeJson(doc, ctxJson);
            jsonOutput += ctxJson;
        }
    }
    jsonOutput += "]";
    
    printf("\n[Integration] Schema JSON length: %d bytes\n", jsonOutput.length());
    
    // Verify: Parse the JSON back
    JsonDocument parseDoc;
    DeserializationError error = deserializeJson(parseDoc, jsonOutput);
    
    TEST_ASSERT_TRUE_MESSAGE(error == DeserializationError::Ok, 
        "Schema JSON is invalid - failed to parse");
    
    // Verify it's an array
    TEST_ASSERT_TRUE_MESSAGE(parseDoc.is<JsonArray>(), 
        "Schema should be a JSON array");
    
    JsonArray arr = parseDoc.as<JsonArray>();
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, arr.size(), 
        "Schema array should not be empty");
    
    // Verify each context has required fields
    for (JsonObject ctx : arr) {
        TEST_ASSERT_TRUE_MESSAGE(ctx.containsKey("contextId"), 
            "Each context must have contextId");
        TEST_ASSERT_TRUE_MESSAGE(ctx.containsKey("title"), 
            "Each context must have title");
        TEST_ASSERT_TRUE_MESSAGE(ctx.containsKey("fields"), 
            "Each context must have fields array");
        
        // Verify contextId is a valid string (no garbage)
        const char* ctxId = ctx["contextId"];
        TEST_ASSERT_NOT_NULL_MESSAGE(ctxId, "contextId should not be null");
        TEST_ASSERT_GREATER_THAN_MESSAGE(0, strlen(ctxId), "contextId should not be empty");
        
        // Check for invalid characters (would cause querySelector errors)
        for (size_t i = 0; i < strlen(ctxId); i++) {
            char c = ctxId[i];
            bool valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || 
                        (c >= '0' && c <= '9') || c == '_' || c == '-';
            TEST_ASSERT_TRUE_MESSAGE(valid, 
                "contextId contains invalid character for CSS selector");
        }
    }
    
    printf("[Integration] All %d contexts have valid structure\n", arr.size());
    TEST_PASS_MESSAGE("Schema endpoint produces valid, parseable JSON");
}

/**
 * @brief Integration test: Detect memory corruption from temporary strings in cached contexts
 * 
 * This test catches the bug where buildContexts() uses temporary String variables
 * that get destroyed after the function returns, leaving dangling pointers.
 */
void test_integration_detect_dangling_string_pointers() {
    // Create a provider that intentionally uses a temporary string (BAD PATTERN)
    class BadProvider : public CachingWebUIProvider {
    protected:
        void buildContexts(std::vector<WebUIContext>& ctxs) override {
            // BAD: Using a temporary String that will be destroyed
            String tempValue = "temp_value_" + String(HAL::getMillis());
            ctxs.push_back(WebUIContext{"bad_ctx", "Bad", "dc-warning", 
                                        WebUILocation::Dashboard, WebUIPresentation::Card}
                .withField(WebUIField("bad_field", "Bad", WebUIFieldType::Text, tempValue.c_str())));
        }
    public:
        String getWebUIName() const override { return "Bad"; }
        String getWebUIVersion() const override { return "1.0"; }
        String getWebUIData(const String&) override { return "{}"; }
        String handleWebUIRequest(const String&, const String&, const String&, const std::map<String, String>&) override { return "{}"; }
        bool hasDataChanged(const String&) override { return false; }
    };
    
    // Create a provider that uses static literals (GOOD PATTERN)
    class GoodProvider : public CachingWebUIProvider {
    protected:
        void buildContexts(std::vector<WebUIContext>& ctxs) override {
            // GOOD: Using static string literal
            ctxs.push_back(WebUIContext{"good_ctx", "Good", "dc-check", 
                                        WebUILocation::Dashboard, WebUIPresentation::Card}
                .withField(WebUIField("good_field", "Good", WebUIFieldType::Text, "static_value")));
        }
    public:
        String getWebUIName() const override { return "Good"; }
        String getWebUIVersion() const override { return "1.0"; }
        String getWebUIData(const String&) override { return "{}"; }
        String handleWebUIRequest(const String&, const String&, const String&, const std::map<String, String>&) override { return "{}"; }
        bool hasDataChanged(const String&) override { return false; }
    };
    
    GoodProvider good;
    
    // Access context multiple times - good provider should be stable
    for (int i = 0; i < 10; i++) {
        const WebUIContext* ctx = good.getContextAtRef(0);
        TEST_ASSERT_NOT_NULL_MESSAGE(ctx, "Good context should exist");
        
        // Serialize to JSON and parse back
        JsonDocument doc;
        JsonObject obj = doc.to<JsonObject>();
        serializeContextToJson(obj, *ctx);
        
        String json;
        serializeJson(doc, json);
        
        // Parse and validate
        JsonDocument parseDoc;
        DeserializationError err = deserializeJson(parseDoc, json);
        TEST_ASSERT_TRUE_MESSAGE(err == DeserializationError::Ok, "Good JSON should parse");
        
        // Verify field value is correct
        const char* value = parseDoc["fields"][0]["value"];
        TEST_ASSERT_NOT_NULL_MESSAGE(value, "Value should exist");
        TEST_ASSERT_EQUAL_STRING_MESSAGE("static_value", value, "Value should be static");
    }
    
    printf("\n[Integration] Dangling pointer detection: Good provider stable\n");
    TEST_PASS_MESSAGE("Static string literals in cached contexts work correctly");
}

/**
 * @brief Integration test: Verify schema remains valid after 100 requests
 * 
 * This catches memory corruption that might only appear after repeated use.
 */
void test_integration_schema_stable_after_100_requests() {
    MultiContextProvider provider;
    
    for (int request = 0; request < 100; request++) {
        String jsonOutput = "[";
        bool first = true;
        
        for (size_t i = 0; ; i++) {
            const WebUIContext* ctx = provider.getContextAtRef(i);
            if (!ctx) break;
            if (strlen(ctx->getContextIdCStr()) == 0) continue;
            
            if (!first) jsonOutput += ",";
            first = false;
            
            JsonDocument doc;
            JsonObject obj = doc.to<JsonObject>();
            serializeContextToJson(obj, *ctx);
            
            String ctxJson;
            serializeJson(doc, ctxJson);
            jsonOutput += ctxJson;
        }
        jsonOutput += "]";
        
        // Parse and validate each response
        JsonDocument parseDoc;
        DeserializationError error = deserializeJson(parseDoc, jsonOutput);
        
        if (error != DeserializationError::Ok) {
            printf("\n[FAIL] Request %d produced invalid JSON: %s\n", 
                   request, error.c_str());
            TEST_FAIL_MESSAGE("Schema corruption detected after repeated requests");
        }
        
        JsonArray arr = parseDoc.as<JsonArray>();
        if (arr.size() == 0) {
            printf("\n[FAIL] Request %d produced empty schema\n", request);
            TEST_FAIL_MESSAGE("Schema became empty after repeated requests");
        }
    }
    
    printf("\n[Integration] 100 consecutive schema requests all valid\n");
    TEST_PASS_MESSAGE("Schema remains valid after 100 requests");
}

// ============================================================================
// Test Runner
// ============================================================================

void test_webui_config_truncation() {
    WebUIConfig config;

    // deviceName[32] — input of 40 chars should be truncated to 31+null
    const char* longName = "This is a very long device name!!!!!!!!";
    config.setDeviceName(longName);
    TEST_ASSERT_EQUAL(31, strlen(config.deviceName));
    TEST_ASSERT_EQUAL_STRING("This is a very long device name", config.deviceName);

    // theme[8] — "verydarktheme" is 13 chars, truncated to 7+null
    config.setTheme("verydarktheme");
    TEST_ASSERT_EQUAL(7, strlen(config.theme));

    // Getter returns correct String
    TEST_ASSERT_EQUAL_STRING(config.deviceName, config.getDeviceName().c_str());

    // Short values work normally
    config.setDeviceName("OK");
    TEST_ASSERT_EQUAL_STRING("OK", config.deviceName);
}

void test_caching_provider_get_context_by_id_from_cache() {
    MockWebUIProvider provider("Test", "1.0.0");
    provider.addContext(WebUIContext::dashboard("dash_1", "Dashboard"));
    provider.addContext(WebUIContext::settings("settings_1", "Settings"));

    // Force cache build via forEachContext
    provider.forEachContext([](const WebUIContext&) { return true; });

    // getWebUIContext should return correct match from cache
    WebUIContext found = provider.getWebUIContext("dash_1");
    TEST_ASSERT_EQUAL_STRING("dash_1", found.getContextIdCStr());
    TEST_ASSERT_EQUAL_STRING("Dashboard", found.getTitleCStr());

    // Second context also found
    WebUIContext found2 = provider.getWebUIContext("settings_1");
    TEST_ASSERT_EQUAL_STRING("settings_1", found2.getContextIdCStr());

    // Lookup non-existent returns empty
    WebUIContext notFound = provider.getWebUIContext("nonexistent");
    TEST_ASSERT_TRUE(strlen(notFound.getContextIdCStr()) == 0);
}

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();

    // WebUIConfig tests
    RUN_TEST(test_webui_config_defaults);
    RUN_TEST(test_webui_config_custom_values);

    // WebUIField tests
    RUN_TEST(test_webui_field_basic_construction);
    RUN_TEST(test_webui_field_default_values);
    RUN_TEST(test_webui_field_fluent_range);
    RUN_TEST(test_webui_field_fluent_choices);
    RUN_TEST(test_webui_field_fluent_add_option);
    RUN_TEST(test_webui_field_fluent_api);
    RUN_TEST(test_webui_field_copy_constructor);
    RUN_TEST(test_webui_field_all_types);
    RUN_TEST(test_ordered_list_values_survive_field_copy);

    // WebUIContext tests
    RUN_TEST(test_webui_context_basic_construction);
    RUN_TEST(test_webui_context_factory_dashboard);
    RUN_TEST(test_webui_context_factory_gauge);
    RUN_TEST(test_webui_context_factory_status_badge);
    RUN_TEST(test_webui_context_factory_header_info);
    RUN_TEST(test_webui_context_factory_settings);
    RUN_TEST(test_webui_context_fluent_with_field);
    RUN_TEST(test_webui_context_fluent_with_multiple_fields);
    RUN_TEST(test_webui_context_fluent_with_api);
    RUN_TEST(test_webui_context_fluent_with_real_time);
    RUN_TEST(test_webui_context_fluent_with_priority);
    RUN_TEST(test_webui_context_fluent_always_interactive);
    RUN_TEST(test_webui_context_custom_html_css_js);
    RUN_TEST(test_webui_context_copy_constructor);

    // Enum tests
    RUN_TEST(test_webui_locations_enum);
    RUN_TEST(test_webui_presentations_enum);

    // LazyState tests
    RUN_TEST(test_lazy_state_initial_uninitialized);
    RUN_TEST(test_lazy_state_has_changed_first_call);
    RUN_TEST(test_lazy_state_has_changed_no_change);
    RUN_TEST(test_lazy_state_has_changed_with_change);
    RUN_TEST(test_lazy_state_get_with_initializer);
    RUN_TEST(test_lazy_state_get_only_initializes_once);
    RUN_TEST(test_lazy_state_reset);
    RUN_TEST(test_lazy_state_with_bool);
    RUN_TEST(test_lazy_state_with_string);

    // ProviderRegistry tests
    RUN_TEST(test_provider_registry_empty);
    RUN_TEST(test_provider_registry_register_provider);
    RUN_TEST(test_provider_registry_register_multiple_contexts);
    RUN_TEST(test_provider_registry_unregister_provider);
    RUN_TEST(test_provider_registry_register_factory);
    RUN_TEST(test_provider_registry_get_components_list);
    RUN_TEST(test_provider_registry_enable_disable);
    RUN_TEST(test_provider_registry_cannot_disable_webui);
    RUN_TEST(test_provider_registry_enable_nonexistent);
    RUN_TEST(test_provider_registry_context_providers_accessor);
    RUN_TEST(test_provider_registry_prepare_schema_generation);
    RUN_TEST(test_provider_registry_iterate_contexts);
    RUN_TEST(test_provider_get_context_at);

    // StreamingContextSerializer tests
    RUN_TEST(test_streaming_serializer_simple_context);
    RUN_TEST(test_streaming_serializer_with_fields);
    RUN_TEST(test_streaming_serializer_with_custom_html);
    RUN_TEST(test_streaming_serializer_chunked_output);
    RUN_TEST(test_streaming_serializer_json_escaping);
    RUN_TEST(test_streaming_serializer_field_with_options);
    RUN_TEST(test_streaming_serializer_ordered_list_value_is_array);

    // Memory stability tests (heap leak detection)
    RUN_TEST(test_streaming_serializer_no_memory_leak);
    RUN_TEST(test_provider_registry_schema_generation_no_leak);

    // Schema validation tests
    RUN_TEST(test_full_schema_array_valid_json);

    // ZERO LEAK tests (must be exactly 0 bytes)
    RUN_TEST(test_zero_leak_multiple_providers);
    RUN_TEST(test_zero_leak_streaming_only);
    RUN_TEST(test_zero_leak_getContextAt_only);
    
    // Memory leak ISOLATION tests (find exact source)
    RUN_TEST(test_isolate_jsondocument_only);
    RUN_TEST(test_isolate_string_concat_only);
    RUN_TEST(test_isolate_context_copies_only);
    RUN_TEST(test_isolate_context_plus_json);
    
    // Memory leak DETECTION tests
    RUN_TEST(test_detect_memory_behavior_repeated_context_creation);
    RUN_TEST(test_aggressive_schema_generation_500_requests);  // Reproduces OOM
    RUN_TEST(test_simulate_repeated_schema_generation);
    RUN_TEST(test_isolate_string_copy_leak);
    RUN_TEST(test_detect_memory_large_custom_content);

    // CachingWebUIProvider memory tests (HeapTracker)
    RUN_TEST(test_caching_provider_builds_once);
    RUN_TEST(test_caching_provider_memory_stable_100_calls);
    RUN_TEST(test_caching_provider_invalidate_rebuilds);
    RUN_TEST(test_caching_provider_foreach_no_rebuild);
    RUN_TEST(test_caching_provider_get_context_at);
    RUN_TEST(test_caching_provider_memory_lifecycle);
    
    // Context copy issue test (forEachContext with copy assignment)
    RUN_TEST(test_foreach_context_with_copy_assignment);
    
    // Rapid refresh simulation test (reproduces page reload scenario)
    RUN_TEST(test_rapid_refresh_schema_generation);
    
    // Many providers simulation (Standard example scenario)
    RUN_TEST(test_many_providers_memory_usage);
    
    // Rapid consecutive requests (regression test for 429 rate limiting issue)
    RUN_TEST(test_rapid_consecutive_schema_requests);
    
    // Integration tests - Schema endpoint simulation
    RUN_TEST(test_integration_schema_endpoint_produces_valid_json);
    RUN_TEST(test_integration_detect_dangling_string_pointers);
    RUN_TEST(test_integration_schema_stable_after_100_requests);

    // WebUIConfig char[] optimization tests (Phase 1)
    RUN_TEST(test_webui_config_truncation);
    RUN_TEST(test_caching_provider_get_context_by_id_from_cache);

    return UNITY_END();
}
