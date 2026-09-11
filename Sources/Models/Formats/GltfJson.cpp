#include "Models/Formats/GltfJson.hpp"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <sstream>

namespace Models::Formats::GltfJson {
namespace {

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

class Parser {
public:
    explicit Parser(std::string_view input)
        : begin_(input.data()), current_(input.data()), end_(input.data() + input.size()) {}

    bool run(Value *out, std::string *error)
    {
        if (!out) return fail(error, "glTF JSON output is null");
        whitespace();
        if (!value(out, error)) return false;
        whitespace();
        if (current_ != end_) return parseFail(error, "unexpected trailing data");
        return true;
    }

private:
    const char *begin_ = nullptr;
    const char *current_ = nullptr;
    const char *end_ = nullptr;

    bool parseFail(std::string *error, const std::string& message) const
    {
        return fail(
            error,
            "glTF JSON error at byte " +
                std::to_string(static_cast<std::size_t>(current_ - begin_)) + ": " + message
        );
    }

    void whitespace()
    {
        while (current_ != end_) {
            const char c = *current_;
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
            ++current_;
        }
    }

    bool consume(char expected)
    {
        if (current_ == end_ || *current_ != expected) return false;
        ++current_;
        return true;
    }

    static int hexDigit(char c)
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + c - 'a';
        if (c >= 'A' && c <= 'F') return 10 + c - 'A';
        return -1;
    }

    bool unicode(std::uint32_t *out, std::string *error)
    {
        if (!out || static_cast<std::size_t>(end_ - current_) < 4u)
            return parseFail(error, "truncated unicode escape");
        std::uint32_t result = 0u;
        for (int i = 0; i < 4; ++i) {
            const int digit = hexDigit(*current_++);
            if (digit < 0) return parseFail(error, "invalid unicode escape");
            result = (result << 4u) | static_cast<std::uint32_t>(digit);
        }
        *out = result;
        return true;
    }

    static void utf8(std::string *out, std::uint32_t codepoint)
    {
        if (!out) return;
        if (codepoint <= 0x7fu) {
            out->push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7ffu) {
            out->push_back(static_cast<char>(0xc0u | (codepoint >> 6u)));
            out->push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
        } else if (codepoint <= 0xffffu) {
            out->push_back(static_cast<char>(0xe0u | (codepoint >> 12u)));
            out->push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3fu)));
            out->push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
        } else {
            out->push_back(static_cast<char>(0xf0u | (codepoint >> 18u)));
            out->push_back(static_cast<char>(0x80u | ((codepoint >> 12u) & 0x3fu)));
            out->push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3fu)));
            out->push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
        }
    }

    bool string(std::string *out, std::string *error)
    {
        if (!out || !consume('"')) return parseFail(error, "expected string");
        out->clear();
        while (current_ != end_) {
            const unsigned char c = static_cast<unsigned char>(*current_++);
            if (c == '"') return true;
            if (c < 0x20u) return parseFail(error, "control character in string");
            if (c != '\\') {
                out->push_back(static_cast<char>(c));
                continue;
            }
            if (current_ == end_) return parseFail(error, "truncated string escape");
            switch (*current_++) {
                case '"': out->push_back('"'); break;
                case '\\': out->push_back('\\'); break;
                case '/': out->push_back('/'); break;
                case 'b': out->push_back('\b'); break;
                case 'f': out->push_back('\f'); break;
                case 'n': out->push_back('\n'); break;
                case 'r': out->push_back('\r'); break;
                case 't': out->push_back('\t'); break;
                case 'u': {
                    std::uint32_t first = 0u;
                    if (!unicode(&first, error)) return false;
                    if (first >= 0xd800u && first <= 0xdbffu) {
                        if (static_cast<std::size_t>(end_ - current_) < 2u || current_[0] != '\\' || current_[1] != 'u')
                            return parseFail(error, "missing low surrogate");
                        current_ += 2;
                        std::uint32_t second = 0u;
                        if (!unicode(&second, error)) return false;
                        if (second < 0xdc00u || second > 0xdfffu)
                            return parseFail(error, "invalid low surrogate");
                        first = 0x10000u + ((first - 0xd800u) << 10u) + (second - 0xdc00u);
                    } else if (first >= 0xdc00u && first <= 0xdfffu) {
                        return parseFail(error, "unexpected low surrogate");
                    }
                    utf8(out, first);
                    break;
                }
                default: return parseFail(error, "invalid string escape");
            }
        }
        return parseFail(error, "unterminated string");
    }

    bool literal(std::string_view token, Type type, bool boolean, Value *out, std::string *error)
    {
        if (static_cast<std::size_t>(end_ - current_) < token.size() ||
            std::string_view(current_, token.size()) != token)
            return parseFail(error, "invalid literal");
        current_ += token.size();
        out->type = type;
        out->boolean = boolean;
        return true;
    }

    bool number(Value *out, std::string *error)
    {
        const char *start = current_;
        if (current_ != end_ && *current_ == '-') ++current_;
        if (current_ == end_) return parseFail(error, "truncated number");
        if (*current_ == '0') {
            ++current_;
        } else {
            if (*current_ < '1' || *current_ > '9') return parseFail(error, "invalid number");
            while (current_ != end_ && *current_ >= '0' && *current_ <= '9') ++current_;
        }
        if (current_ != end_ && *current_ == '.') {
            ++current_;
            if (current_ == end_ || *current_ < '0' || *current_ > '9') return parseFail(error, "invalid fraction");
            while (current_ != end_ && *current_ >= '0' && *current_ <= '9') ++current_;
        }
        if (current_ != end_ && (*current_ == 'e' || *current_ == 'E')) {
            ++current_;
            if (current_ != end_ && (*current_ == '+' || *current_ == '-')) ++current_;
            if (current_ == end_ || *current_ < '0' || *current_ > '9') return parseFail(error, "invalid exponent");
            while (current_ != end_ && *current_ >= '0' && *current_ <= '9') ++current_;
        }

        const std::string token(start, current_);
        char *parsed_end = nullptr;
        errno = 0;
        const double parsed = std::strtod(token.c_str(), &parsed_end);
        if (errno == ERANGE || !parsed_end || *parsed_end != '\0' || !std::isfinite(parsed))
            return parseFail(error, "number out of range");
        out->type = Type::Number;
        out->number = parsed;
        return true;
    }

    bool array(Value *out, std::string *error)
    {
        if (!consume('[')) return parseFail(error, "expected array");
        out->type = Type::Array;
        out->array.clear();
        whitespace();
        if (consume(']')) return true;
        for (;;) {
            Value item;
            if (!value(&item, error)) return false;
            out->array.push_back(std::move(item));
            whitespace();
            if (consume(']')) return true;
            if (!consume(',')) return parseFail(error, "expected ',' in array");
            whitespace();
        }
    }

    bool object(Value *out, std::string *error)
    {
        if (!consume('{')) return parseFail(error, "expected object");
        out->type = Type::Object;
        out->object.clear();
        whitespace();
        if (consume('}')) return true;
        for (;;) {
            std::string key;
            if (!string(&key, error)) return false;
            whitespace();
            if (!consume(':')) return parseFail(error, "expected ':' in object");
            whitespace();
            Value member;
            if (!value(&member, error)) return false;
            out->object.insert_or_assign(std::move(key), std::move(member));
            whitespace();
            if (consume('}')) return true;
            if (!consume(',')) return parseFail(error, "expected ',' in object");
            whitespace();
        }
    }

    bool value(Value *out, std::string *error)
    {
        if (!out) return parseFail(error, "null value output");
        whitespace();
        if (current_ == end_) return parseFail(error, "unexpected end of input");
        switch (*current_) {
            case '{': return object(out, error);
            case '[': return array(out, error);
            case '"':
                out->type = Type::String;
                return string(&out->string, error);
            case 't': return literal("true", Type::Boolean, true, out, error);
            case 'f': return literal("false", Type::Boolean, false, out, error);
            case 'n': return literal("null", Type::Null, false, out, error);
            default: return number(out, error);
        }
    }
};

void appendEscaped(std::string *out, const std::string& value)
{
    if (!out) return;
    out->push_back('"');
    static constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char c : value) {
        switch (c) {
            case '"': out->append("\\\""); break;
            case '\\': out->append("\\\\"); break;
            case '\b': out->append("\\b"); break;
            case '\f': out->append("\\f"); break;
            case '\n': out->append("\\n"); break;
            case '\r': out->append("\\r"); break;
            case '\t': out->append("\\t"); break;
            default:
                if (c < 0x20u) {
                    out->append("\\u00");
                    out->push_back(hex[(c >> 4u) & 0x0fu]);
                    out->push_back(hex[c & 0x0fu]);
                } else {
                    out->push_back(static_cast<char>(c));
                }
                break;
        }
    }
    out->push_back('"');
}

void appendJson(std::string *out, const Value& value)
{
    if (!out) return;
    switch (value.type) {
        case Type::Null:
            out->append("null");
            break;
        case Type::Boolean:
            out->append(value.boolean ? "true" : "false");
            break;
        case Type::Number: {
            std::ostringstream stream;
            stream << std::setprecision(17) << value.number;
            out->append(stream.str());
            break;
        }
        case Type::String:
            appendEscaped(out, value.string);
            break;
        case Type::Array:
            out->push_back('[');
            for (std::size_t i = 0u; i < value.array.size(); ++i) {
                if (i != 0u) out->push_back(',');
                appendJson(out, value.array[i]);
            }
            out->push_back(']');
            break;
        case Type::Object: {
            out->push_back('{');
            bool first = true;
            for (const auto& [key, member] : value.object) {
                if (!first) out->push_back(',');
                first = false;
                appendEscaped(out, key);
                out->push_back(':');
                appendJson(out, member);
            }
            out->push_back('}');
            break;
        }
    }
}

bool wholeNumber(const Value *value)
{
    return value && value->type == Type::Number && std::isfinite(value->number) &&
        std::floor(value->number) == value->number;
}

} // namespace

const Value *Value::get(std::string_view key) const
{
    if (type != Type::Object) return nullptr;
    const auto found = object.find(std::string(key));
    return found == object.end() ? nullptr : &found->second;
}

bool parse(std::string_view text, Value *out, std::string *error)
{
    if (error) error->clear();
    return Parser(text).run(out, error);
}

std::string stringify(const Value& value)
{
    std::string result;
    appendJson(&result, value);
    return result;
}

int integer(const Value *value, int fallback)
{
    if (!wholeNumber(value) || value->number < static_cast<double>(std::numeric_limits<int>::min()) ||
        value->number > static_cast<double>(std::numeric_limits<int>::max()))
        return fallback;
    return static_cast<int>(value->number);
}

std::uint32_t unsignedInteger(const Value *value, std::uint32_t fallback)
{
    if (!wholeNumber(value) || value->number < 0.0 ||
        value->number > static_cast<double>(std::numeric_limits<std::uint32_t>::max()))
        return fallback;
    return static_cast<std::uint32_t>(value->number);
}

std::size_t sizeValue(const Value *value, std::size_t fallback)
{
    if (!wholeNumber(value) || value->number < 0.0 ||
        value->number > static_cast<double>(std::numeric_limits<std::size_t>::max()))
        return fallback;
    return static_cast<std::size_t>(value->number);
}

float floatValue(const Value *value, float fallback)
{
    if (!value || value->type != Type::Number || !std::isfinite(value->number) ||
        value->number < -static_cast<double>(std::numeric_limits<float>::max()) ||
        value->number > static_cast<double>(std::numeric_limits<float>::max()))
        return fallback;
    return static_cast<float>(value->number);
}

bool boolValue(const Value *value, bool fallback)
{
    return value && value->type == Type::Boolean ? value->boolean : fallback;
}

std::string stringValue(const Value *value, std::string fallback)
{
    return value && value->type == Type::String ? value->string : std::move(fallback);
}

} // namespace Models::Formats::GltfJson
