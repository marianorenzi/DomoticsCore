/**
 * @file test_streaming_serializer.cpp
 * @brief Unit tests for StreamingContextSerializer JSON output validation
 *
 * Runs on native platform to verify:
 * 1. Streaming serializer produces valid JSON
 * 2. CachingWebUIProvider caches contexts correctly
 * 3. Large customHtml/customCss/customJs strings serialize correctly
 * 4. Multiple contexts serialize without corruption
 */

#include <unity.h>
#include <DomoticsCore/Platform_HAL.h>
#include <DomoticsCore/IWebUIProvider.h>
#include <DomoticsCore/WebUI/StreamingContextSerializer.h>
#include <ArduinoJson.h>
#include <cstring>
#include <vector>
#include <string>

using namespace DomoticsCore::Components;
using namespace DomoticsCore::Components::WebUI;

// Helper to serialize a context to a std::string using the streaming serializer
// Uses std::string for native platform compatibility
std::string serializeContextToStdString(const WebUIContext& ctx) {
    StreamingContextSerializer serializer;
    serializer.begin(ctx);

    std::string result;
    uint8_t buffer[256];

    while (!serializer.isComplete()) {
        size_t written = serializer.write(buffer, sizeof(buffer));
        if (written > 0) {
            result.append(reinterpret_cast<const char*>(buffer), written);
        }
    }

    return result;
}

// Test basic context serialization
void test_basic_context_serialization(void) {
    WebUIContext ctx("test_id", "Test Title", "test-icon",
                     WebUILocation::Dashboard, WebUIPresentation::Card);
    ctx.withField(WebUIField("field1", "Field One", WebUIFieldType::Text, "value1"));
    ctx.priority = 10;
    ctx.apiEndpoint = "/api/test";

    std::string json = serializeContextToStdString(ctx);

    // Validate JSON can be parsed
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, json);

    TEST_ASSERT_EQUAL_MESSAGE(DeserializationError::Ok, error.code(),
                              "JSON should be valid");

    // Validate content
    TEST_ASSERT_EQUAL_STRING("test_id", doc["contextId"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("Test Title", doc["title"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("test-icon", doc["icon"].as<const char*>());
    TEST_ASSERT_EQUAL(0, doc["location"].as<int>()); // Dashboard = 0
    TEST_ASSERT_EQUAL(0, doc["presentation"].as<int>()); // Card = 0
    TEST_ASSERT_EQUAL(10, doc["priority"].as<int>());
    TEST_ASSERT_EQUAL_STRING("/api/test", doc["apiEndpoint"].as<const char*>());

    // Validate fields array
    JsonArray fields = doc["fields"].as<JsonArray>();
    TEST_ASSERT_EQUAL(1, fields.size());
    TEST_ASSERT_EQUAL_STRING("field1", fields[0]["name"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("Field One", fields[0]["label"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("value1", fields[0]["value"].as<const char*>());
}

// Test context with special characters that need escaping
void test_json_escaping(void) {
    WebUIContext ctx("escape_test", "Title with \"quotes\"", "icon",
                     WebUILocation::Dashboard, WebUIPresentation::Card);
    ctx.withField(WebUIField("field1", "Label\nwith\nnewlines", WebUIFieldType::Text,
                             "value\\with\\backslash"));

    std::string json = serializeContextToStdString(ctx);

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, json);

    TEST_ASSERT_EQUAL_MESSAGE(DeserializationError::Ok, error.code(),
                              "JSON with escaped characters should be valid");

    TEST_ASSERT_EQUAL_STRING("Title with \"quotes\"", doc["title"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("Label\nwith\nnewlines",
                             doc["fields"][0]["label"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("value\\with\\backslash",
                             doc["fields"][0]["value"].as<const char*>());
}

// Test context with large customHtml/customCss/customJs
void test_large_custom_content(void) {
    WebUIContext ctx("custom_test", "Custom Test", "icon",
                     WebUILocation::Settings, WebUIPresentation::Card);

    // Simulate large HTML content like in LEDWebUI
    const char* largeHtml = R"(
        <div class="card-header">
            <h3 class="card-title">LED Control</h3>
        </div>
        <div class="card-content led-dashboard">
            <div class="led-bulb-container">
                <svg class="led-bulb" viewBox="0 0 1024 1024">
                    <use href="#bulb-twotone"/>
                </svg>
            </div>
        </div>
    )";

    const char* largeCss = R"(
        .led-dashboard .led-bulb-container {
            display: flex;
            justify-content: center;
            margin-bottom: 1rem;
        }
        .led-dashboard .led-bulb {
            width: 64px;
            height: 64px;
            transition: all 0.3s ease;
        }
    )";

    const char* largeJs = R"(
        function updateLEDBulb() {
            const bulb = document.querySelector('.led-dashboard .led-bulb');
            const toggle = document.querySelector('#state_toggle');
            if (bulb && toggle) {
                bulb.classList.toggle('on', toggle.checked);
            }
        }
    )";

    ctx.withCustomHtml(largeHtml);
    ctx.withCustomCss(largeCss);
    ctx.withCustomJs(largeJs);
    ctx.withField(WebUIField("state", "State", WebUIFieldType::Boolean, "false"));

    std::string json = serializeContextToStdString(ctx);

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, json);

    TEST_ASSERT_EQUAL_MESSAGE(DeserializationError::Ok, error.code(),
                              "JSON with large custom content should be valid");

    // Verify custom content is present using std::string find
    std::string customHtml = doc["customHtml"].as<std::string>();
    std::string customCss = doc["customCss"].as<std::string>();
    std::string customJs = doc["customJs"].as<std::string>();
    TEST_ASSERT_TRUE(customHtml.find("LED Control") != std::string::npos);
    TEST_ASSERT_TRUE(customCss.find("led-bulb-container") != std::string::npos);
    TEST_ASSERT_TRUE(customJs.find("updateLEDBulb") != std::string::npos);
}

// Test multiple fields with options
void test_field_with_options(void) {
    WebUIContext ctx("select_test", "Select Test", "icon",
                     WebUILocation::Settings, WebUIPresentation::Card);

    WebUIField selectField("effect", "Effect", WebUIFieldType::Select, "Solid");
    selectField.choices({"Solid", "Blink", "Fade", "Pulse"});
    ctx.withField(selectField);

    std::string json = serializeContextToStdString(ctx);

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, json);

    TEST_ASSERT_EQUAL_MESSAGE(DeserializationError::Ok, error.code(),
                              "JSON with select field should be valid");

    JsonArray options = doc["fields"][0]["options"].as<JsonArray>();
    TEST_ASSERT_EQUAL(4, options.size());
    TEST_ASSERT_EQUAL_STRING("Solid", options[0].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("Blink", options[1].as<const char*>());
}

// Regression: an option-label key/value pair may span any HTTP response chunk.
void test_option_labels_across_chunk_boundaries(void) {
    WebUIContext ctx("option_labels", "Option Labels", "icon",
                     WebUILocation::Settings, WebUIPresentation::Card);
    WebUIField field("mode", "Mode", WebUIFieldType::Select, "option_b");
    field.addOption("option_a", "First Option")
         .addOption("option_b", "Second Option")
         .addOption("option_c", "Third Option");
    ctx.withField(field);

    for (size_t chunkSize = 2; chunkSize <= 32; ++chunkSize) {
        StreamingContextSerializer serializer;
        serializer.begin(ctx);
        std::string json;
        std::vector<uint8_t> buffer(chunkSize);
        size_t iterations = 0;

        while (!serializer.isComplete() && iterations++ < 4096) {
            const size_t written = serializer.write(buffer.data(), buffer.size());
            json.append(reinterpret_cast<const char*>(buffer.data()), written);
        }

        TEST_ASSERT_TRUE_MESSAGE(serializer.isComplete(),
                                 "Serializer must complete for every chunk size");
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, json);
        TEST_ASSERT_EQUAL_MESSAGE(DeserializationError::Ok,
                                  error.code(),
                                  "Failed to deserialize JSON after streaming option labels across chunk boundaries");
        TEST_ASSERT_EQUAL_STRING("First Option",
                                 doc["fields"][0]["optionLabels"]["option_a"].as<const char*>());
        TEST_ASSERT_EQUAL_STRING("Second Option",
                                 doc["fields"][0]["optionLabels"]["option_b"].as<const char*>());
        TEST_ASSERT_EQUAL_STRING("Third Option",
                                 doc["fields"][0]["optionLabels"]["option_c"].as<const char*>());
    }
}

void test_multiselect_value_is_json_array(void) {
    WebUIField field("networks", "Networks", WebUIFieldType::Multiselect);
    field.choices({"office,5g", "guest"}, true);
    WebUIContext ctx = WebUIContext::settings("network_test", "Network Test")
        .withField(field);

    std::string json = serializeContextToStdString(ctx);
    JsonDocument doc;
    TEST_ASSERT_EQUAL(DeserializationError::Ok, deserializeJson(doc, json).code());

    JsonArray values = doc["fields"][0]["value"].as<JsonArray>();
    TEST_ASSERT_EQUAL(2, values.size());
    TEST_ASSERT_EQUAL_STRING("office,5g", values[0].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("guest", values[1].as<const char*>());
}

void test_multi_values_across_chunk_boundaries(void) {
    WebUIField field("priorities", "Priorities", WebUIFieldType::OrderedList);
    field.addOption("ethernet-long", "Ethernet", true)
         .addOption("wifi\"quoted", "WiFi", true)
         .addOption("cellular\\backup", "Cellular", true);
    WebUIContext ctx = WebUIContext::settings("network_test", "Network Test")
        .withField(field);

    for (size_t chunkSize = 2; chunkSize <= 32; ++chunkSize) {
        StreamingContextSerializer serializer;
        serializer.begin(ctx);
        std::string json;
        std::vector<uint8_t> buffer(chunkSize);
        size_t iterations = 0;

        while (!serializer.isComplete() && iterations++ < 4096) {
            size_t written = serializer.write(buffer.data(), buffer.size());
            json.append(reinterpret_cast<const char*>(buffer.data()), written);
        }

        TEST_ASSERT_TRUE_MESSAGE(serializer.isComplete(),
                                 "Multi-value serialization must complete for every chunk size");
        JsonDocument doc;
        TEST_ASSERT_EQUAL_MESSAGE(DeserializationError::Ok,
                                  deserializeJson(doc, json).code(),
                                  "Multi-value JSON must remain valid across chunk boundaries");
        JsonArray values = doc["fields"][0]["value"].as<JsonArray>();
        TEST_ASSERT_EQUAL_UINT32(3, values.size());
        TEST_ASSERT_EQUAL_STRING("ethernet-long", values[0].as<const char*>());
        TEST_ASSERT_EQUAL_STRING("wifi\"quoted", values[1].as<const char*>());
        TEST_ASSERT_EQUAL_STRING("cellular\\backup", values[2].as<const char*>());
    }
}

// Test CachingWebUIProvider returns same cached contexts
class TestCachingProvider : public CachingWebUIProvider {
protected:
    void buildContexts(std::vector<WebUIContext>& contexts) override {
        buildCount++;
        contexts.push_back(WebUIContext::dashboard("test_dash", "Test Dashboard")
            .withField(WebUIField("field1", "Field 1", WebUIFieldType::Text, "value1")));
    }

public:
    String getWebUIName() const override { return "Test"; }
    String getWebUIVersion() const override { return "1.0.0"; }
    String handleWebUIRequest(const String&, const String&, const String&,
                              const std::map<String, String>&) override {
        return "{}";
    }

    int buildCount = 0;
};

void test_caching_provider_caches_contexts(void) {
    TestCachingProvider provider;

    // First call should build
    TEST_ASSERT_EQUAL(1, provider.getContextCount());
    TEST_ASSERT_EQUAL(1, provider.buildCount);

    // Second call should use cache
    TEST_ASSERT_EQUAL(1, provider.getContextCount());
    TEST_ASSERT_EQUAL_MESSAGE(1, provider.buildCount,
                              "buildContexts should only be called once");

    // Third call with getContextAt
    WebUIContext ctx;
    bool found = provider.getContextAt(0, ctx);
    TEST_ASSERT_TRUE(found);
    TEST_ASSERT_EQUAL_MESSAGE(1, provider.buildCount,
                              "getContextAt should use cache");
    TEST_ASSERT_EQUAL_STRING("test_dash", ctx.getContextIdCStr());

    // After invalidation, buildContexts should be called again
    provider.invalidateContextCache();
    TEST_ASSERT_EQUAL(1, provider.getContextCount());
    TEST_ASSERT_EQUAL_MESSAGE(2, provider.buildCount,
                              "After invalidation, buildContexts should be called again");
}

// Test serializing array of contexts (simulates schema endpoint)
void test_serialize_multiple_contexts(void) {
    std::vector<WebUIContext> contexts;

    contexts.push_back(WebUIContext::statusBadge("status1", "Status 1", "icon1")
        .withField(WebUIField("state", "State", WebUIFieldType::Status, "ON")));

    contexts.push_back(WebUIContext::dashboard("dash1", "Dashboard")
        .withField(WebUIField("value", "Value", WebUIFieldType::Number, "42")));

    contexts.push_back(WebUIContext::settings("settings1", "Settings")
        .withField(WebUIField("enabled", "Enabled", WebUIFieldType::Boolean, "true"))
        .withField(WebUIField("name", "Name", WebUIFieldType::Text, "Test")));

    // Simulate schema endpoint: serialize as JSON array
    std::string json = "[";
    bool first = true;
    for (const auto& ctx : contexts) {
        if (!first) json += ",";
        first = false;
        json += serializeContextToStdString(ctx);
    }
    json += "]";

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, json);

    TEST_ASSERT_EQUAL_MESSAGE(DeserializationError::Ok, error.code(),
                              "JSON array of contexts should be valid");

    JsonArray arr = doc.as<JsonArray>();
    TEST_ASSERT_EQUAL(3, arr.size());
    TEST_ASSERT_EQUAL_STRING("status1", arr[0]["contextId"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("dash1", arr[1]["contextId"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("settings1", arr[2]["contextId"].as<const char*>());
}

// Test chunked writing (simulates small buffer conditions)
void test_chunked_serialization(void) {
    WebUIContext ctx("chunked_test", "Chunked Test", "icon",
                     WebUILocation::Dashboard, WebUIPresentation::Card);
    ctx.withField(WebUIField("field1", "Field One", WebUIFieldType::Text, "value1"));
    ctx.withField(WebUIField("field2", "Field Two", WebUIFieldType::Number, "42"));
    ctx.withCustomHtml("<div>Custom HTML Content</div>");

    // Serialize with tiny buffer (8 bytes) to test chunking
    StreamingContextSerializer serializer;
    serializer.begin(ctx);

    std::string result;
    uint8_t buffer[8];  // Very small buffer
    int iterations = 0;
    const int maxIterations = 1000;  // Prevent infinite loop

    while (!serializer.isComplete() && iterations < maxIterations) {
        size_t written = serializer.write(buffer, sizeof(buffer));
        if (written > 0) {
            result.append(reinterpret_cast<const char*>(buffer), written);
        }
        iterations++;
    }

    TEST_ASSERT_TRUE_MESSAGE(serializer.isComplete(),
                             "Serializer should complete");
    TEST_ASSERT_TRUE_MESSAGE(iterations < maxIterations,
                             "Should not need excessive iterations");

    // Validate the chunked output is valid JSON
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, result);

    TEST_ASSERT_EQUAL_MESSAGE(DeserializationError::Ok, error.code(),
                              "Chunked JSON should be valid");

    TEST_ASSERT_EQUAL_STRING("chunked_test", doc["contextId"].as<const char*>());
    TEST_ASSERT_EQUAL(2, doc["fields"].as<JsonArray>().size());
}

// T076: Verify Ptr vs String storage produces identical JSON
void test_hybrid_ptr_vs_string_identical_json(void) {
    // Context built with const char* (Ptr path)
    WebUIContext ptrCtx("hybrid_test", "Hybrid Title", "dc-test",
                        WebUILocation::Dashboard, WebUIPresentation::Card);
    ptrCtx.withField(WebUIField("sensor", "Sensor Value", WebUIFieldType::Number, "42", "°C"));
    ptrCtx.withAPI("/api/hybrid");

    // Context built with String (String path)
    WebUIContext strCtx(String("hybrid_test"), String("Hybrid Title"), String("dc-test"),
                        WebUILocation::Dashboard, WebUIPresentation::Card);
    strCtx.withField(WebUIField(String("sensor"), String("Sensor Value"), WebUIFieldType::Number, String("42"), String("°C")));
    strCtx.withAPI(String("/api/hybrid"));

    std::string ptrJson = serializeContextToStdString(ptrCtx);
    std::string strJson = serializeContextToStdString(strCtx);

    TEST_ASSERT_EQUAL_STRING_MESSAGE(strJson.c_str(), ptrJson.c_str(),
        "Ptr-based and String-based contexts must produce identical JSON");

    // Also verify both are valid JSON
    JsonDocument doc;
    TEST_ASSERT_EQUAL(DeserializationError::Ok, deserializeJson(doc, ptrJson).code());
    TEST_ASSERT_EQUAL_STRING("hybrid_test", doc["contextId"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("/api/hybrid", doc["apiEndpoint"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("sensor", doc["fields"][0]["name"].as<const char*>());
}

void setUp(void) {
    // Setup before each test
}

void tearDown(void) {
    // Cleanup after each test
}

int main(int argc, char **argv) {
    UNITY_BEGIN();

    RUN_TEST(test_basic_context_serialization);
    RUN_TEST(test_json_escaping);
    RUN_TEST(test_large_custom_content);
    RUN_TEST(test_field_with_options);
    RUN_TEST(test_option_labels_across_chunk_boundaries);
    RUN_TEST(test_multiselect_value_is_json_array);
    RUN_TEST(test_multi_values_across_chunk_boundaries);
    RUN_TEST(test_caching_provider_caches_contexts);
    RUN_TEST(test_serialize_multiple_contexts);
    RUN_TEST(test_chunked_serialization);
    RUN_TEST(test_hybrid_ptr_vs_string_identical_json);

    return UNITY_END();
}
