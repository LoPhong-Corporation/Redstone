#include "core/Json.hpp"

#include <cwchar>
#include <cwctype>

namespace core::json
{
namespace
{

// Recursive-descent parser operating on a std::wstring. Kept as a small
// local class rather than free functions so parsing state (position) does
// not have to be threaded through every helper by hand.
class Parser
{
public:
    explicit Parser(const std::wstring& text)
        : text_(text)
    {
    }

    std::optional<JsonValue> ParseDocument()
    {
        SkipWhitespace();
        auto value = ParseValue();
        if (!value)
        {
            return std::nullopt;
        }
        SkipWhitespace();
        // Trailing garbage after the top-level value is treated as an error
        // so subtly-corrupted config files are not silently accepted.
        if (pos_ != text_.size())
        {
            return std::nullopt;
        }
        return value;
    }

private:
    const std::wstring& text_;
    size_t pos_ = 0;
    bool failed_ = false;

    [[nodiscard]] bool AtEnd() const { return pos_ >= text_.size(); }
    [[nodiscard]] wchar_t Peek() const { return AtEnd() ? L'\0' : text_[pos_]; }
    wchar_t Advance() { return AtEnd() ? L'\0' : text_[pos_++]; }

    void SkipWhitespace()
    {
        while (!AtEnd() && std::iswspace(Peek()))
        {
            ++pos_;
        }
    }

    bool Consume(wchar_t expected)
    {
        if (Peek() != expected)
        {
            failed_ = true;
            return false;
        }
        ++pos_;
        return true;
    }

    bool ConsumeLiteral(const wchar_t* literal)
    {
        const size_t length = std::wcslen(literal);
        if (text_.compare(pos_, length, literal) != 0)
        {
            return false;
        }
        pos_ += length;
        return true;
    }

    std::optional<JsonValue> ParseValue()
    {
        SkipWhitespace();
        if (AtEnd())
        {
            failed_ = true;
            return std::nullopt;
        }

        switch (Peek())
        {
            case L'{': return ParseObject();
            case L'[': return ParseArray();
            case L'"': return ParseString();
            case L't':
            case L'f': return ParseBool();
            case L'n': return ParseNull();
            default: return ParseNumber();
        }
    }

    std::optional<JsonValue> ParseObject()
    {
        Consume(L'{');
        JsonValue result = JsonValue::MakeObject();
        SkipWhitespace();
        if (Peek() == L'}')
        {
            ++pos_;
            return result;
        }

        while (true)
        {
            SkipWhitespace();
            auto key = ParseString();
            if (!key || failed_)
            {
                return std::nullopt;
            }
            SkipWhitespace();
            if (!Consume(L':'))
            {
                return std::nullopt;
            }
            auto value = ParseValue();
            if (!value)
            {
                return std::nullopt;
            }
            result.objectValue.emplace_back(key->stringValue, std::move(*value));

            SkipWhitespace();
            if (Peek() == L',')
            {
                ++pos_;
                continue;
            }
            if (Peek() == L'}')
            {
                ++pos_;
                break;
            }
            failed_ = true;
            return std::nullopt;
        }
        return result;
    }

    std::optional<JsonValue> ParseArray()
    {
        Consume(L'[');
        JsonValue result = JsonValue::MakeArray();
        SkipWhitespace();
        if (Peek() == L']')
        {
            ++pos_;
            return result;
        }

        while (true)
        {
            auto value = ParseValue();
            if (!value)
            {
                return std::nullopt;
            }
            result.arrayValue.push_back(std::move(*value));

            SkipWhitespace();
            if (Peek() == L',')
            {
                ++pos_;
                continue;
            }
            if (Peek() == L']')
            {
                ++pos_;
                break;
            }
            failed_ = true;
            return std::nullopt;
        }
        return result;
    }

    std::optional<JsonValue> ParseString()
    {
        if (!Consume(L'"'))
        {
            return std::nullopt;
        }
        std::wstring value;
        while (true)
        {
            if (AtEnd())
            {
                failed_ = true;
                return std::nullopt;
            }
            wchar_t c = Advance();
            if (c == L'"')
            {
                break;
            }
            if (c == L'\\')
            {
                wchar_t escaped = Advance();
                switch (escaped)
                {
                    case L'"': value += L'"'; break;
                    case L'\\': value += L'\\'; break;
                    case L'/': value += L'/'; break;
                    case L'n': value += L'\n'; break;
                    case L't': value += L'\t'; break;
                    case L'r': value += L'\r'; break;
                    default: value += escaped; break;
                }
                continue;
            }
            value += c;
        }
        return JsonValue::MakeString(value);
    }

    std::optional<JsonValue> ParseBool()
    {
        if (ConsumeLiteral(L"true"))
        {
            return JsonValue::MakeBool(true);
        }
        if (ConsumeLiteral(L"false"))
        {
            return JsonValue::MakeBool(false);
        }
        failed_ = true;
        return std::nullopt;
    }

    std::optional<JsonValue> ParseNull()
    {
        if (ConsumeLiteral(L"null"))
        {
            return JsonValue{};
        }
        failed_ = true;
        return std::nullopt;
    }

    std::optional<JsonValue> ParseNumber()
    {
        const size_t start = pos_;
        if (Peek() == L'-')
        {
            ++pos_;
        }
        while (!AtEnd() && std::iswdigit(Peek()))
        {
            ++pos_;
        }
        if (Peek() == L'.')
        {
            ++pos_;
            while (!AtEnd() && std::iswdigit(Peek()))
            {
                ++pos_;
            }
        }
        if (Peek() == L'e' || Peek() == L'E')
        {
            ++pos_;
            if (Peek() == L'+' || Peek() == L'-')
            {
                ++pos_;
            }
            while (!AtEnd() && std::iswdigit(Peek()))
            {
                ++pos_;
            }
        }
        if (pos_ == start)
        {
            failed_ = true;
            return std::nullopt;
        }
        try
        {
            double value = std::stod(text_.substr(start, pos_ - start));
            return JsonValue::MakeNumber(value);
        }
        catch (const std::exception&)
        {
            failed_ = true;
            return std::nullopt;
        }
    }
};

void SerializeInternal(const JsonValue& value, std::wstring& out, int indent)
{
    const std::wstring pad(static_cast<size_t>(indent) * 2, L' ');
    const std::wstring childPad(static_cast<size_t>(indent + 1) * 2, L' ');

    switch (value.type)
    {
        case JsonValue::Type::Null:
            out += L"null";
            break;
        case JsonValue::Type::Bool:
            out += value.boolValue ? L"true" : L"false";
            break;
        case JsonValue::Type::Number:
        {
            // Whole numbers (the common case for our config: ports, MB
            // values) are printed without a trailing ".0" for readability.
            if (value.numberValue == static_cast<long long>(value.numberValue))
            {
                out += std::to_wstring(static_cast<long long>(value.numberValue));
            }
            else
            {
                out += std::to_wstring(value.numberValue);
            }
            break;
        }
        case JsonValue::Type::String:
        {
            out += L'"';
            for (wchar_t c : value.stringValue)
            {
                switch (c)
                {
                    case L'"': out += L"\\\""; break;
                    case L'\\': out += L"\\\\"; break;
                    case L'\n': out += L"\\n"; break;
                    case L'\t': out += L"\\t"; break;
                    case L'\r': out += L"\\r"; break;
                    default: out += c; break;
                }
            }
            out += L'"';
            break;
        }
        case JsonValue::Type::Array:
        {
            if (value.arrayValue.empty())
            {
                out += L"[]";
                break;
            }
            out += L"[\n";
            for (size_t i = 0; i < value.arrayValue.size(); ++i)
            {
                out += childPad;
                SerializeInternal(value.arrayValue[i], out, indent + 1);
                if (i + 1 != value.arrayValue.size())
                {
                    out += L',';
                }
                out += L'\n';
            }
            out += pad + L"]";
            break;
        }
        case JsonValue::Type::Object:
        {
            if (value.objectValue.empty())
            {
                out += L"{}";
                break;
            }
            out += L"{\n";
            for (size_t i = 0; i < value.objectValue.size(); ++i)
            {
                const auto& [key, child] = value.objectValue[i];
                out += childPad + L'"' + key + L"\": ";
                SerializeInternal(child, out, indent + 1);
                if (i + 1 != value.objectValue.size())
                {
                    out += L',';
                }
                out += L'\n';
            }
            out += pad + L"}";
            break;
        }
    }
}

} // namespace

std::optional<JsonValue> Parse(const std::wstring& text)
{
    Parser parser(text);
    return parser.ParseDocument();
}

std::wstring Serialize(const JsonValue& value)
{
    std::wstring out;
    SerializeInternal(value, out, 0);
    return out;
}

} // namespace core::json
