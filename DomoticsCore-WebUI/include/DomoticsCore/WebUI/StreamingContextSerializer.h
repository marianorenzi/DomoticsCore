#pragma once

/**
 * @file StreamingContextSerializer.h
 * @brief Memory-efficient streaming JSON serializer for WebUIContext
 *
 * Writes JSON directly to a buffer without intermediate String allocation.
 * Critical for ESP8266 where contexts with large customHtml/customCss/customJs
 * would exceed available heap if fully buffered.
 *
 * Design: State machine that can pause/resume serialization at any point,
 * writing only what fits in the current buffer chunk.
 */

#include <DomoticsCore/IWebUIProvider.h>
#include <DomoticsCore/Platform_HAL.h>

namespace DomoticsCore {
namespace Components {
namespace WebUI {

/**
 * @class StreamingContextSerializer
 * @brief Streams a WebUIContext to JSON without full memory allocation
 *
 * Usage:
 * @code
 * StreamingContextSerializer serializer;
 * serializer.begin(context);
 *
 * while (!serializer.isComplete()) {
 *     size_t written = serializer.write(buffer, bufferSize);
 *     sendChunk(buffer, written);
 * }
 * @endcode
 */
class StreamingContextSerializer {
public:
    enum class State {
        NotStarted,
        // Object opening
        OpenBrace,
        // Simple properties
        ContextId, ContextIdValue, ContextIdComma,
        Title, TitleValue, TitleComma,
        Icon, IconValue, IconComma,
        Location, LocationValue, LocationComma,
        Presentation, PresentationValue, PresentationComma,
        Priority, PriorityValue, PriorityComma,
        ApiEndpoint, ApiEndpointValue, ApiEndpointComma,
        AlwaysInteractive, AlwaysInteractiveValue,
        // Optional large strings
        CustomHtmlCheck, CustomHtmlKey, CustomHtmlValue, CustomHtmlComma,
        CustomCssCheck, CustomCssKey, CustomCssValue, CustomCssComma,
        CustomJsCheck, CustomJsKey, CustomJsValue, CustomJsComma,
        // Fields array
        FieldsKey, FieldsArrayOpen,
        FieldObject,  // Serializing current field
        FieldComma,
        FieldsArrayClose,
        // Object close
        CloseBrace,
        Complete
    };

private:
    const WebUIContext* ctx = nullptr;
    State state = State::NotStarted;

    // For string streaming
    size_t stringOffset = 0;  // Position within current string being written
    const String* currentString = nullptr;  // Pointer to string being streamed

    // For literal streaming (supports buffers smaller than literal strings)
    const char* currentLiteral = nullptr;
    size_t literalOffset = 0;

    // For fields iteration
    size_t fieldIndex = 0;

    // Field serialization sub-state
    enum class FieldState {
        OpenBrace,
        Name, NameValue, NameComma,
        Label, LabelValue, LabelComma,
        Type, TypeValue, TypeComma,
        Value, ValueValue, ValuesArrayOpen, ValuesArrayValue, ValuesArrayComma, ValuesArrayClose, ValueComma,
        Unit, UnitValue, UnitComma,
        ReadOnly, ReadOnlyValue, ReadOnlyComma,
        MinValue, MinValueValue, MinValueComma,
        MaxValue, MaxValueValue, MaxValueComma,
        Endpoint, EndpointValue,
        OptionsCheck, OptionsKey, OptionsArrayOpen, OptionValue, OptionComma, OptionsArrayClose, OptionsComma,
        OptionLabelsCheck, OptionLabelsKey, OptionLabelsOpen,
        OptionLabelKey, OptionLabelColon, OptionLabelValue, OptionLabelComma,
        OptionLabelsClose,
        CloseBrace,
        Complete
    };
    FieldState fieldState = FieldState::OpenBrace;
    size_t arrayIndex = 0;

    // Temporary number buffer for integer conversions
    char numBuf[16];
    
    // Memory metrics (T034)
    size_t totalBytesWritten_ = 0;
    size_t chunkCount_ = 0;

public:
    StreamingContextSerializer() = default;
    
    /**
     * @brief Get total bytes written during serialization
     * @return Total bytes written across all write() calls
     */
    size_t getTotalBytesWritten() const { return totalBytesWritten_; }
    
    /**
     * @brief Get number of chunks written
     * @return Number of write() calls that produced output
     */
    size_t getChunkCount() const { return chunkCount_; }

    /**
     * @brief Start serializing a context
     * @param context Reference to context (must remain valid during serialization)
     */
    void begin(const WebUIContext& context) {
        ctx = &context;
        state = State::OpenBrace;
        stringOffset = 0;
        currentString = nullptr;
        currentLiteral = nullptr;
        literalOffset = 0;
        fieldIndex = 0;
        fieldState = FieldState::OpenBrace;
        arrayIndex = 0;
        totalBytesWritten_ = 0;
        chunkCount_ = 0;
    }

    /**
     * @brief Check if serialization is complete
     */
    bool isComplete() const {
        return state == State::Complete;
    }

    /**
     * @brief Write as much as possible to buffer
     * @param buffer Output buffer
     * @param maxLen Maximum bytes to write
     * @return Number of bytes written
     */
    size_t write(uint8_t* buffer, size_t maxLen) {
        if (!ctx || state == State::Complete || maxLen == 0) return 0;

        size_t written = 0;

        while (written < maxLen && state != State::Complete) {
            size_t remaining = maxLen - written;
            size_t n = 0;

            switch (state) {
                case State::OpenBrace:
                    n = writeLiteral(buffer + written, remaining, "{");
                    if (isLiteralComplete()) state = State::ContextId;
                    break;

                case State::ContextId:
                    n = writeLiteral(buffer + written, remaining, "\"contextId\":");
                    if (isLiteralComplete()) state = State::ContextIdValue;
                    break;

                case State::ContextIdValue:
                    n = writeJsonString(buffer + written, remaining, ctx->getContextIdCStr());
                    if (stringOffset == 0) state = State::ContextIdComma;
                    break;

                case State::ContextIdComma:
                    n = writeLiteral(buffer + written, remaining, ",");
                    if (isLiteralComplete()) state = State::Title;
                    break;

                case State::Title:
                    n = writeLiteral(buffer + written, remaining, "\"title\":");
                    if (isLiteralComplete()) state = State::TitleValue;
                    break;

                case State::TitleValue:
                    n = writeJsonString(buffer + written, remaining, ctx->getTitleCStr());
                    if (stringOffset == 0) state = State::TitleComma;
                    break;

                case State::TitleComma:
                    n = writeLiteral(buffer + written, remaining, ",");
                    if (isLiteralComplete()) state = State::Icon;
                    break;

                case State::Icon:
                    n = writeLiteral(buffer + written, remaining, "\"icon\":");
                    if (isLiteralComplete()) state = State::IconValue;
                    break;

                case State::IconValue:
                    n = writeJsonString(buffer + written, remaining, ctx->getIconCStr());
                    if (stringOffset == 0) state = State::IconComma;
                    break;

                case State::IconComma:
                    n = writeLiteral(buffer + written, remaining, ",");
                    if (isLiteralComplete()) state = State::Location;
                    break;

                case State::Location:
                    n = writeLiteral(buffer + written, remaining, "\"location\":");
                    if (isLiteralComplete()) state = State::LocationValue;
                    break;

                case State::LocationValue:
                    snprintf(numBuf, sizeof(numBuf), "%d", (int)ctx->location);
                    n = writeLiteral(buffer + written, remaining, numBuf);
                    if (isLiteralComplete()) state = State::LocationComma;
                    break;

                case State::LocationComma:
                    n = writeLiteral(buffer + written, remaining, ",");
                    if (isLiteralComplete()) state = State::Presentation;
                    break;

                case State::Presentation:
                    n = writeLiteral(buffer + written, remaining, "\"presentation\":");
                    if (isLiteralComplete()) state = State::PresentationValue;
                    break;

                case State::PresentationValue:
                    snprintf(numBuf, sizeof(numBuf), "%d", (int)ctx->presentation);
                    n = writeLiteral(buffer + written, remaining, numBuf);
                    if (isLiteralComplete()) state = State::PresentationComma;
                    break;

                case State::PresentationComma:
                    n = writeLiteral(buffer + written, remaining, ",");
                    if (isLiteralComplete()) state = State::Priority;
                    break;

                case State::Priority:
                    n = writeLiteral(buffer + written, remaining, "\"priority\":");
                    if (isLiteralComplete()) state = State::PriorityValue;
                    break;

                case State::PriorityValue:
                    snprintf(numBuf, sizeof(numBuf), "%d", ctx->priority);
                    n = writeLiteral(buffer + written, remaining, numBuf);
                    if (isLiteralComplete()) state = State::PriorityComma;
                    break;

                case State::PriorityComma:
                    n = writeLiteral(buffer + written, remaining, ",");
                    if (isLiteralComplete()) state = State::ApiEndpoint;
                    break;

                case State::ApiEndpoint:
                    n = writeLiteral(buffer + written, remaining, "\"apiEndpoint\":");
                    if (isLiteralComplete()) state = State::ApiEndpointValue;
                    break;

                case State::ApiEndpointValue:
                    n = writeJsonString(buffer + written, remaining, ctx->getApiEndpointCStr());
                    if (stringOffset == 0) state = State::ApiEndpointComma;
                    break;

                case State::ApiEndpointComma:
                    n = writeLiteral(buffer + written, remaining, ",");
                    if (isLiteralComplete()) state = State::AlwaysInteractive;
                    break;

                case State::AlwaysInteractive:
                    n = writeLiteral(buffer + written, remaining, "\"alwaysInteractive\":");
                    if (isLiteralComplete()) state = State::AlwaysInteractiveValue;
                    break;

                case State::AlwaysInteractiveValue:
                    n = writeLiteral(buffer + written, remaining, ctx->alwaysInteractive ? "true" : "false");
                    if (isLiteralComplete()) state = State::CustomHtmlCheck;
                    break;

                // Optional customHtml (hybrid: Ptr or String)
                case State::CustomHtmlCheck:
                    if (ctx->hasCustomHtml()) {
                        state = State::CustomHtmlKey;
                    } else {
                        state = State::CustomCssCheck;
                    }
                    break;

                case State::CustomHtmlKey:
                    n = writeLiteral(buffer + written, remaining, ",\"customHtml\":");
                    if (isLiteralComplete()) state = State::CustomHtmlValue;
                    break;

                case State::CustomHtmlValue:
                    n = writeJsonString(buffer + written, remaining, ctx->getCustomHtmlCStr());
                    if (stringOffset == 0) state = State::CustomCssCheck;
                    break;

                // Optional customCss (hybrid: Ptr or String)
                case State::CustomCssCheck:
                    if (ctx->hasCustomCss()) {
                        state = State::CustomCssKey;
                    } else {
                        state = State::CustomJsCheck;
                    }
                    break;

                case State::CustomCssKey:
                    n = writeLiteral(buffer + written, remaining, ",\"customCss\":");
                    if (isLiteralComplete()) state = State::CustomCssValue;
                    break;

                case State::CustomCssValue:
                    n = writeJsonString(buffer + written, remaining, ctx->getCustomCssCStr());
                    if (stringOffset == 0) state = State::CustomJsCheck;
                    break;

                // Optional customJs (hybrid: Ptr or String)
                case State::CustomJsCheck:
                    if (ctx->hasCustomJs()) {
                        state = State::CustomJsKey;
                    } else {
                        state = State::FieldsKey;
                    }
                    break;

                case State::CustomJsKey:
                    n = writeLiteral(buffer + written, remaining, ",\"customJs\":");
                    if (isLiteralComplete()) state = State::CustomJsValue;
                    break;

                case State::CustomJsValue:
                    n = writeJsonString(buffer + written, remaining, ctx->getCustomJsCStr());
                    if (stringOffset == 0) state = State::FieldsKey;
                    break;

                // Fields array
                case State::FieldsKey:
                    n = writeLiteral(buffer + written, remaining, ",\"fields\":");
                    if (isLiteralComplete()) state = State::FieldsArrayOpen;
                    break;

                case State::FieldsArrayOpen:
                    n = writeLiteral(buffer + written, remaining, "[");
                    if (isLiteralComplete()) {
                        fieldIndex = 0;
                        if (ctx->fields.empty()) {
                            state = State::FieldsArrayClose;
                        } else {
                            state = State::FieldObject;
                            fieldState = FieldState::OpenBrace;
                        }
                    }
                    break;

                case State::FieldObject:
                    n = writeField(buffer + written, remaining);
                    if (fieldState == FieldState::Complete) {
                        fieldIndex++;
                        if (fieldIndex < ctx->fields.size()) {
                            state = State::FieldComma;
                        } else {
                            state = State::FieldsArrayClose;
                        }
                    }
                    break;

                case State::FieldComma:
                    n = writeLiteral(buffer + written, remaining, ",");
                    if (isLiteralComplete()) {
                        state = State::FieldObject;
                        fieldState = FieldState::OpenBrace;
                        arrayIndex = 0;
                    }
                    break;

                case State::FieldsArrayClose:
                    n = writeLiteral(buffer + written, remaining, "]");
                    if (isLiteralComplete()) state = State::CloseBrace;
                    break;

                case State::CloseBrace:
                    n = writeLiteral(buffer + written, remaining, "}");
                    if (isLiteralComplete()) state = State::Complete;
                    break;

                default:
                    break;
            }

            written += n;

            // If we couldn't write anything and state didn't change, buffer is full - break to avoid infinite loop
            if (n == 0) {
                // These states are "check" states that don't write anything
                if (state == State::CustomHtmlCheck ||
                    state == State::CustomCssCheck ||
                    state == State::CustomJsCheck) {
                    continue;  // These states transition without writing
                }
                // Otherwise we're stuck, exit
                break;
            }
        }

        // Track metrics
        if (written > 0) {
            totalBytesWritten_ += written;
            chunkCount_++;
        }
        return written;
    }

private:
    /**
     * @brief Write a literal string to buffer, supporting partial writes
     * @return Bytes written (may be partial if buffer is smaller than remaining literal)
     *
     * Uses currentLiteral and literalOffset to track position for streaming.
     * Returns true via pointer parameter when literal is complete.
     */
    size_t writeLiteral(uint8_t* buffer, size_t maxLen, const char* str) {
        // Start new literal or continue existing one
        if (currentLiteral != str) {
            currentLiteral = str;
            literalOffset = 0;
        }

        size_t totalLen = strlen(str);
        size_t remaining = totalLen - literalOffset;

        if (remaining == 0) {
            currentLiteral = nullptr;
            literalOffset = 0;
            return 0;
        }

        size_t toWrite = (remaining < maxLen) ? remaining : maxLen;
        memcpy(buffer, str + literalOffset, toWrite);
        literalOffset += toWrite;

        // Check if complete
        if (literalOffset >= totalLen) {
            currentLiteral = nullptr;
            literalOffset = 0;
        }

        return toWrite;
    }

    /**
     * @brief Check if current literal is complete (or no literal in progress)
     */
    bool isLiteralComplete() const {
        return currentLiteral == nullptr;
    }

    /**
     * @brief Write a JSON-escaped string, streaming across multiple calls
     * @return Bytes written
     *
     * Uses stringOffset to track position within string for resumable writing.
     * Handles JSON escaping for special characters.
     */
    size_t writeJsonString(uint8_t* buffer, size_t maxLen, const String& str) {
        size_t written = 0;

        // Opening quote
        if (stringOffset == 0) {
            if (maxLen < 1) return 0;
            buffer[written++] = '"';
            stringOffset = 1;  // Mark that we've written opening quote
        }

        // String content with escaping
        // stringOffset-1 is the position in the actual string
        size_t strPos = stringOffset - 1;

        while (written < maxLen && strPos < str.length()) {
            char c = str[strPos];
            const char* escaped = nullptr;

            // Check if character needs escaping
            switch (c) {
                case '"':  escaped = "\\\""; break;
                case '\\': escaped = "\\\\"; break;
                case '\n': escaped = "\\n"; break;
                case '\r': escaped = "\\r"; break;
                case '\t': escaped = "\\t"; break;
                default:
                    if (c < 0x20) {
                        // Control character - write as \u00XX
                        if (written + 6 > maxLen) {
                            stringOffset = strPos + 1;
                            return written;
                        }
                        static const char hex[] = "0123456789abcdef";
                        buffer[written++] = '\\';
                        buffer[written++] = 'u';
                        buffer[written++] = '0';
                        buffer[written++] = '0';
                        buffer[written++] = hex[(c >> 4) & 0xF];
                        buffer[written++] = hex[c & 0xF];
                        strPos++;
                        continue;
                    }
                    break;
            }

            if (escaped) {
                size_t escLen = strlen(escaped);
                if (written + escLen > maxLen) {
                    stringOffset = strPos + 1;
                    return written;
                }
                memcpy(buffer + written, escaped, escLen);
                written += escLen;
            } else {
                buffer[written++] = c;
            }
            strPos++;
        }

        // Closing quote
        if (strPos >= str.length()) {
            if (written < maxLen) {
                buffer[written++] = '"';
                stringOffset = 0;  // Reset for next string
                return written;
            }
        }

        stringOffset = strPos + 1;
        return written;
    }

    /**
     * @brief Write a JSON-escaped C-string (const char*), null-safe
     * @return Bytes written
     */
    size_t writeJsonString(uint8_t* buffer, size_t maxLen, const char* str) {
        // Normalize null and empty inputs, then let the resumable logic below
        // write the quotes across chunk boundaries when necessary.
        if (str == nullptr) {
            str = "";
        }
        
        size_t written = 0;
        size_t strLen = strlen(str);

        // Opening quote
        if (stringOffset == 0) {
            if (maxLen < 1) return 0;
            buffer[written++] = '"';
            stringOffset = 1;  // Mark that we've written opening quote
        }

        // String content with escaping
        size_t strPos = stringOffset - 1;

        while (written < maxLen && strPos < strLen) {
            char c = str[strPos];
            const char* escaped = nullptr;

            switch (c) {
                case '"':  escaped = "\\\""; break;
                case '\\': escaped = "\\\\"; break;
                case '\n': escaped = "\\n"; break;
                case '\r': escaped = "\\r"; break;
                case '\t': escaped = "\\t"; break;
                default:
                    if (c < 0x20) {
                        if (written + 6 > maxLen) {
                            stringOffset = strPos + 1;
                            return written;
                        }
                        static const char hex[] = "0123456789abcdef";
                        buffer[written++] = '\\';
                        buffer[written++] = 'u';
                        buffer[written++] = '0';
                        buffer[written++] = '0';
                        buffer[written++] = hex[(c >> 4) & 0xF];
                        buffer[written++] = hex[c & 0xF];
                        strPos++;
                        continue;
                    }
                    break;
            }

            if (escaped) {
                size_t escLen = strlen(escaped);
                if (written + escLen > maxLen) {
                    stringOffset = strPos + 1;
                    return written;
                }
                memcpy(buffer + written, escaped, escLen);
                written += escLen;
            } else {
                buffer[written++] = c;
            }
            strPos++;
        }

        // Closing quote
        if (strPos >= strLen) {
            if (written < maxLen) {
                buffer[written++] = '"';
                stringOffset = 0;  // Reset for next string
                return written;
            }
        }

        stringOffset = strPos + 1;
        return written;
    }

    /**
     * @brief Write current field object
     * @return Bytes written
     */
    size_t writeField(uint8_t* buffer, size_t maxLen) {
        if (fieldIndex >= ctx->fields.size()) {
            fieldState = FieldState::Complete;
            return 0;
        }

        const WebUIField& field = ctx->fields[fieldIndex];
        size_t written = 0;

        while (written < maxLen && fieldState != FieldState::Complete) {
            size_t remaining = maxLen - written;
            size_t n = 0;

            switch (fieldState) {
                case FieldState::OpenBrace:
                    n = writeLiteral(buffer + written, remaining, "{");
                    if (isLiteralComplete()) fieldState = FieldState::Name;
                    break;

                case FieldState::Name:
                    n = writeLiteral(buffer + written, remaining, "\"name\":");
                    if (isLiteralComplete()) fieldState = FieldState::NameValue;
                    break;

                case FieldState::NameValue:
                    n = writeJsonString(buffer + written, remaining, field.getNameCStr());
                    if (stringOffset == 0) fieldState = FieldState::NameComma;
                    break;

                case FieldState::NameComma:
                    n = writeLiteral(buffer + written, remaining, ",");
                    if (isLiteralComplete()) fieldState = FieldState::Label;
                    break;

                case FieldState::Label:
                    n = writeLiteral(buffer + written, remaining, "\"label\":");
                    if (isLiteralComplete()) fieldState = FieldState::LabelValue;
                    break;

                case FieldState::LabelValue:
                    n = writeJsonString(buffer + written, remaining, field.getLabelCStr());
                    if (stringOffset == 0) fieldState = FieldState::LabelComma;
                    break;

                case FieldState::LabelComma:
                    n = writeLiteral(buffer + written, remaining, ",");
                    if (isLiteralComplete()) fieldState = FieldState::Type;
                    break;

                case FieldState::Type:
                    n = writeLiteral(buffer + written, remaining, "\"type\":");
                    if (isLiteralComplete()) fieldState = FieldState::TypeValue;
                    break;

                case FieldState::TypeValue:
                    snprintf(numBuf, sizeof(numBuf), "%d", (int)field.type);
                    n = writeLiteral(buffer + written, remaining, numBuf);
                    if (isLiteralComplete()) fieldState = FieldState::TypeComma;
                    break;

                case FieldState::TypeComma:
                    n = writeLiteral(buffer + written, remaining, ",");
                    if (isLiteralComplete()) fieldState = FieldState::Value;
                    break;

                case FieldState::Value:
                    n = writeLiteral(buffer + written, remaining, "\"value\":");
                    if (isLiteralComplete()) {
                        arrayIndex = 0;
                        fieldState = field.isMultiValue()
                            ? FieldState::ValuesArrayOpen
                            : FieldState::ValueValue;
                    }
                    break;

                case FieldState::ValueValue:
                    n = writeJsonString(buffer + written, remaining, field.getValueCStr());
                    if (stringOffset == 0) fieldState = FieldState::ValueComma;
                    break;

                case FieldState::ValuesArrayOpen:
                    n = writeLiteral(buffer + written, remaining, "[");
                    if (isLiteralComplete()) fieldState = FieldState::ValuesArrayValue;
                    break;

                case FieldState::ValuesArrayValue:
                    if (arrayIndex < field.values.size()) {
                        n = writeJsonString(buffer + written, remaining, field.values[arrayIndex]);
                        if (stringOffset == 0) {
                            arrayIndex++;
                            fieldState = arrayIndex < field.values.size()
                                ? FieldState::ValuesArrayComma
                                : FieldState::ValuesArrayClose;
                        }
                    } else {
                        fieldState = FieldState::ValuesArrayClose;
                    }
                    break;

                case FieldState::ValuesArrayComma:
                    n = writeLiteral(buffer + written, remaining, ",");
                    if (isLiteralComplete()) fieldState = FieldState::ValuesArrayValue;
                    break;

                case FieldState::ValuesArrayClose:
                    n = writeLiteral(buffer + written, remaining, "]");
                    if (isLiteralComplete()) fieldState = FieldState::ValueComma;
                    break;

                case FieldState::ValueComma:
                    n = writeLiteral(buffer + written, remaining, ",");
                    if (isLiteralComplete()) fieldState = FieldState::Unit;
                    break;

                case FieldState::Unit:
                    n = writeLiteral(buffer + written, remaining, "\"unit\":");
                    if (isLiteralComplete()) fieldState = FieldState::UnitValue;
                    break;

                case FieldState::UnitValue:
                    n = writeJsonString(buffer + written, remaining, field.getUnitCStr());
                    if (stringOffset == 0) fieldState = FieldState::UnitComma;
                    break;

                case FieldState::UnitComma:
                    n = writeLiteral(buffer + written, remaining, ",");
                    if (isLiteralComplete()) fieldState = FieldState::ReadOnly;
                    break;

                case FieldState::ReadOnly:
                    n = writeLiteral(buffer + written, remaining, "\"readOnly\":");
                    if (isLiteralComplete()) fieldState = FieldState::ReadOnlyValue;
                    break;

                case FieldState::ReadOnlyValue:
                    n = writeLiteral(buffer + written, remaining, field.readOnly ? "true" : "false");
                    if (isLiteralComplete()) fieldState = FieldState::ReadOnlyComma;
                    break;

                case FieldState::ReadOnlyComma:
                    n = writeLiteral(buffer + written, remaining, ",");
                    if (isLiteralComplete()) fieldState = FieldState::MinValue;
                    break;

                case FieldState::MinValue:
                    n = writeLiteral(buffer + written, remaining, "\"minValue\":");
                    if (isLiteralComplete()) fieldState = FieldState::MinValueValue;
                    break;

                case FieldState::MinValueValue:
                    snprintf(numBuf, sizeof(numBuf), "%.2f", field.minValue);
                    n = writeLiteral(buffer + written, remaining, numBuf);
                    if (isLiteralComplete()) fieldState = FieldState::MinValueComma;
                    break;

                case FieldState::MinValueComma:
                    n = writeLiteral(buffer + written, remaining, ",");
                    if (isLiteralComplete()) fieldState = FieldState::MaxValue;
                    break;

                case FieldState::MaxValue:
                    n = writeLiteral(buffer + written, remaining, "\"maxValue\":");
                    if (isLiteralComplete()) fieldState = FieldState::MaxValueValue;
                    break;

                case FieldState::MaxValueValue:
                    snprintf(numBuf, sizeof(numBuf), "%.2f", field.maxValue);
                    n = writeLiteral(buffer + written, remaining, numBuf);
                    if (isLiteralComplete()) fieldState = FieldState::MaxValueComma;
                    break;

                case FieldState::MaxValueComma:
                    n = writeLiteral(buffer + written, remaining, ",");
                    if (isLiteralComplete()) fieldState = FieldState::Endpoint;
                    break;

                case FieldState::Endpoint:
                    n = writeLiteral(buffer + written, remaining, "\"endpoint\":");
                    if (isLiteralComplete()) fieldState = FieldState::EndpointValue;
                    break;

                case FieldState::EndpointValue:
                    n = writeJsonString(buffer + written, remaining, field.getEndpointCStr());
                    if (stringOffset == 0) fieldState = FieldState::OptionsCheck;
                    break;

                // Options array (optional)
                case FieldState::OptionsCheck:
                    if (!field.options.empty()) {
                        fieldState = FieldState::OptionsKey;
                        arrayIndex = 0;
                    } else {
                        fieldState = FieldState::OptionLabelsCheck;
                    }
                    break;

                case FieldState::OptionsKey:
                    n = writeLiteral(buffer + written, remaining, ",\"options\":");
                    if (isLiteralComplete()) fieldState = FieldState::OptionsArrayOpen;
                    break;

                case FieldState::OptionsArrayOpen:
                    n = writeLiteral(buffer + written, remaining, "[");
                    if (isLiteralComplete()) fieldState = FieldState::OptionValue;
                    break;

                case FieldState::OptionValue:
                    if (arrayIndex < field.options.size()) {
                        n = writeJsonString(buffer + written, remaining, field.options[arrayIndex]);
                        if (stringOffset == 0) {
                            arrayIndex++;
                            if (arrayIndex < field.options.size()) {
                                fieldState = FieldState::OptionComma;
                            } else {
                                fieldState = FieldState::OptionsArrayClose;
                            }
                        }
                    } else {
                        fieldState = FieldState::OptionsArrayClose;
                    }
                    break;

                case FieldState::OptionComma:
                    n = writeLiteral(buffer + written, remaining, ",");
                    if (isLiteralComplete()) fieldState = FieldState::OptionValue;
                    break;

                case FieldState::OptionsArrayClose:
                    n = writeLiteral(buffer + written, remaining, "]");
                    if (isLiteralComplete()) fieldState = FieldState::OptionLabelsCheck;
                    break;

                // Option labels object (optional)
                case FieldState::OptionLabelsCheck:
                    if (!field.optionLabels.empty()) {
                        fieldState = FieldState::OptionLabelsKey;
                        arrayIndex = 0;
                    } else {
                        fieldState = FieldState::CloseBrace;
                    }
                    break;

                case FieldState::OptionLabelsKey:
                    n = writeLiteral(buffer + written, remaining, ",\"optionLabels\":");
                    if (isLiteralComplete()) fieldState = FieldState::OptionLabelsOpen;
                    break;

                case FieldState::OptionLabelsOpen:
                    n = writeLiteral(buffer + written, remaining, "{");
                    if (isLiteralComplete()) fieldState = FieldState::OptionLabelKey;
                    break;

                case FieldState::OptionLabelKey: {
                    auto it = field.optionLabels.begin();
                    std::advance(it, arrayIndex);
                    if (it != field.optionLabels.end()) {
                        n = writeJsonString(buffer + written, remaining, it->first);
                        if (stringOffset == 0) {
                            fieldState = FieldState::OptionLabelColon;
                        }
                    } else {
                        fieldState = FieldState::OptionLabelsClose;
                    }
                    break;
                }

                case FieldState::OptionLabelColon:
                    n = writeLiteral(buffer + written, remaining, ":");
                    if (isLiteralComplete()) fieldState = FieldState::OptionLabelValue;
                    break;

                case FieldState::OptionLabelValue: {
                    auto it = field.optionLabels.begin();
                    std::advance(it, arrayIndex);
                    if (it != field.optionLabels.end()) {
                        n = writeJsonString(buffer + written, remaining, it->second);
                        if (stringOffset == 0) {
                            arrayIndex++;
                            fieldState = arrayIndex < field.optionLabels.size()
                                ? FieldState::OptionLabelComma
                                : FieldState::OptionLabelsClose;
                        }
                    } else {
                        fieldState = FieldState::OptionLabelsClose;
                    }
                    break;
                }

                case FieldState::OptionLabelComma:
                    n = writeLiteral(buffer + written, remaining, ",");
                    if (isLiteralComplete()) fieldState = FieldState::OptionLabelKey;
                    break;

                case FieldState::OptionLabelsClose:
                    n = writeLiteral(buffer + written, remaining, "}");
                    if (isLiteralComplete()) fieldState = FieldState::CloseBrace;
                    break;

                case FieldState::CloseBrace:
                    n = writeLiteral(buffer + written, remaining, "}");
                    if (isLiteralComplete()) fieldState = FieldState::Complete;
                    break;

                default:
                    break;
            }

            written += n;

            // If we couldn't write anything, check if we should break
            if (n == 0) {
                // These states are "check" states that transition without writing
                if (fieldState == FieldState::OptionsCheck ||
                    fieldState == FieldState::OptionLabelsCheck) {
                    continue;
                }
                // Otherwise we're stuck, exit
                break;
            }
        }

        return written;
    }
};

} // namespace WebUI
} // namespace Components
} // namespace DomoticsCore
