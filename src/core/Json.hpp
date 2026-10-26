#pragma once

// A small, self-contained JSON reader/writer.
//
// Why hand-rolled instead of a library: the project philosophy (spec
// section 49) is "native, lightweight, minimal dependencies" and we only
// ever need to round-trip our own ServerConfig array, so a full
// general-purpose JSON library would be overkill. This parser is a
// straightforward recursive-descent implementation; it is deliberately
// simple rather than fully RFC 8259 compliant (e.g. it does not support
// \uXXXX escapes), which is enough for the ASCII-range config values this
// application writes.

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace core::json
{

class JsonValue
{
public:
    enum class Type
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    Type type = Type::Null;
    bool boolValue = false;
    double numberValue = 0.0;
    std::wstring stringValue;
    std::vector<JsonValue> arrayValue;
    std::vector<std::pair<std::wstring, JsonValue>> objectValue;

    static JsonValue MakeObject() { JsonValue v; v.type = Type::Object; return v; }
    static JsonValue MakeArray() { JsonValue v; v.type = Type::Array; return v; }
    static JsonValue MakeString(std::wstring s) { JsonValue v; v.type = Type::String; v.stringValue = std::move(s); return v; }
    static JsonValue MakeNumber(double n) { JsonValue v; v.type = Type::Number; v.numberValue = n; return v; }
    static JsonValue MakeBool(bool b) { JsonValue v; v.type = Type::Bool; v.boolValue = b; return v; }

    void Set(const std::wstring& key, JsonValue value)
    {
        for (auto& [existingKey, existingValue] : objectValue)
        {
            if (existingKey == key)
            {
                existingValue = std::move(value);
                return;
            }
        }
        objectValue.emplace_back(key, std::move(value));
    }

    [[nodiscard]] const JsonValue* Find(const std::wstring& key) const
    {
        for (const auto& [existingKey, existingValue] : objectValue)
        {
            if (existingKey == key)
            {
                return &existingValue;
            }
        }
        return nullptr;
    }

    [[nodiscard]] std::wstring AsString(const std::wstring& fallback = L"") const
    {
        return type == Type::String ? stringValue : fallback;
    }

    [[nodiscard]] double AsNumber(double fallback = 0.0) const
    {
        return type == Type::Number ? numberValue : fallback;
    }

    [[nodiscard]] bool AsBool(bool fallback = false) const
    {
        return type == Type::Bool ? boolValue : fallback;
    }
};

// Parses a JSON document. Returns std::nullopt on malformed input; the
// caller decides how to react (ConfigManager falls back to an empty list).
std::optional<JsonValue> Parse(const std::wstring& text);

// Serializes a JSON value with simple 2-space indentation.
std::wstring Serialize(const JsonValue& value);

} // namespace core::json
