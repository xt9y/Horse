#include "Models/Formats/FbxAscii.hpp"

#include <charconv>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace Models::FbxAscii {
namespace {

constexpr std::size_t kMaximumDepth = 256u;
constexpr std::size_t kMaximumNodes = 1000000u;
constexpr std::size_t kMaximumArrayElements = 100000000u;

bool fail(std::string *error, const std::string& message)
{
    if (error && error->empty()) *error = message;
    return false;
}

struct Token {
    enum class Type {
        Identifier,
        String,
        Integer,
        Real,
        Colon,
        Comma,
        LBrace,
        RBrace,
        Star,
        Newline,
        End,
    };

    Type type = Type::End;
    std::string text;
};

class Lexer {
public:
    Lexer(const std::uint8_t *data, std::size_t size)
        : text_(reinterpret_cast<const char *>(data), size)
    {
    }

    bool next(Token *token, std::string *error)
    {
        if (!token) return fail(error, "null FBX ASCII token output");
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\r') {
                ++pos_;
                continue;
            }
            if (c == ';') {
                while (pos_ < text_.size() && text_[pos_] != '\n') ++pos_;
                continue;
            }
            break;
        }

        if (pos_ >= text_.size()) {
            *token = {Token::Type::End, {}};
            return true;
        }

        const char c = text_[pos_++];
        switch (c) {
            case '\n': *token = {Token::Type::Newline, {}}; return true;
            case ':': *token = {Token::Type::Colon, {}}; return true;
            case ',': *token = {Token::Type::Comma, {}}; return true;
            case '{': *token = {Token::Type::LBrace, {}}; return true;
            case '}': *token = {Token::Type::RBrace, {}}; return true;
            case '*': *token = {Token::Type::Star, {}}; return true;
            case '"': return readString(token, error);
            default: break;
        }

        if (c == '+' || c == '-' || c == '.' || std::isdigit(static_cast<unsigned char>(c))) {
            std::string value(1u, c);
            while (pos_ < text_.size()) {
                const char next = text_[pos_];
                if (!std::isdigit(static_cast<unsigned char>(next)) && next != '.' && next != 'e' && next != 'E' && next != '+' && next != '-') break;
                value.push_back(next);
                ++pos_;
            }
            const bool real = value.find_first_of(".eE") != std::string::npos;
            *token = {real ? Token::Type::Real : Token::Type::Integer, std::move(value)};
            return true;
        }

        std::string value(1u, c);
        while (pos_ < text_.size()) {
            const char next = text_[pos_];
            if (next == ':' || next == ',' || next == '{' || next == '}' || next == '*' || next == '\n' || std::isspace(static_cast<unsigned char>(next))) break;
            value.push_back(next);
            ++pos_;
        }
        *token = {Token::Type::Identifier, std::move(value)};
        return true;
    }

private:
    bool readString(Token *token, std::string *error)
    {
        std::string value;
        while (pos_ < text_.size()) {
            const char c = text_[pos_++];
            if (c == '"') {
                *token = {Token::Type::String, std::move(value)};
                return true;
            }
            if (c != '\\') {
                value.push_back(c);
                continue;
            }
            if (pos_ >= text_.size()) return fail(error, "unterminated FBX ASCII escape");
            const char escaped = text_[pos_++];
            switch (escaped) {
                case '\\': value.push_back('\\'); break;
                case '"': value.push_back('"'); break;
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                default: value.push_back(escaped); break;
            }
        }
        return fail(error, "unterminated FBX ASCII string");
    }

    std::string text_;
    std::size_t pos_ = 0u;
};

bool parseInteger(const std::string& text, std::int64_t *value)
{
    if (!value || text.empty()) return false;
    const char *begin = text.data();
    const char *end = begin + text.size();
    const auto result = std::from_chars(begin, end, *value, 10);
    return result.ec == std::errc{} && result.ptr == end;
}

bool parseReal(const std::string& text, double *value)
{
    if (!value || text.empty()) return false;

    std::size_t pos = 0u;
    bool negative = false;
    if (text[pos] == '+' || text[pos] == '-') {
        negative = text[pos] == '-';
        if (++pos == text.size()) return false;
    }

    long double significand = 0.0L;
    int fractional_digits = 0;
    bool saw_digit = false;

    while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
        saw_digit = true;
        significand = significand * 10.0L + static_cast<long double>(text[pos] - '0');
        ++pos;
    }

    if (pos < text.size() && text[pos] == '.') {
        ++pos;
        while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
            saw_digit = true;
            significand = significand * 10.0L + static_cast<long double>(text[pos] - '0');
            ++fractional_digits;
            ++pos;
        }
    }
    if (!saw_digit) return false;

    int exponent = -fractional_digits;
    if (pos < text.size() && (text[pos] == 'e' || text[pos] == 'E')) {
        ++pos;
        if (pos == text.size()) return false;
        bool exponent_negative = false;
        if (text[pos] == '+' || text[pos] == '-') {
            exponent_negative = text[pos] == '-';
            if (++pos == text.size()) return false;
        }

        int parsed_exponent = 0;
        bool saw_exponent_digit = false;
        while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
            saw_exponent_digit = true;
            if (parsed_exponent < 100000) parsed_exponent = parsed_exponent * 10 + (text[pos] - '0');
            ++pos;
        }
        if (!saw_exponent_digit) return false;
        exponent += exponent_negative ? -parsed_exponent : parsed_exponent;
    }

    if (pos != text.size()) return false;

    long double parsed = significand;
    if (exponent != 0) parsed *= std::pow(10.0L, static_cast<long double>(exponent));
    if (negative) parsed = -parsed;

    const double converted = static_cast<double>(parsed);
    if (!std::isfinite(converted)) return false;
    if (converted == 0.0 && significand != 0.0L) return false;
    *value = converted;
    return true;
}

class Parser {
public:
    Parser(const std::uint8_t *data, std::size_t size, std::string *error)
        : lexer_(data, size), error_(error)
    {
    }

    bool parse(FbxDocument::RawDocument *out)
    {
        if (!out) return fail(error_, "null FBX ASCII document output");
        *out = {};
        out->binary = false;
        out->version = 7400u;
        if (!advance()) return false;
        skipNewlines();
        while (current_.type != Token::Type::End) {
            FbxDocument::Node node;
            if (!parseNode(&node, 0u)) return false;
            out->root.children.push_back(std::move(node));
            skipNewlines();
        }

        const FbxDocument::Node *header = out->root.child("FBXHeaderExtension");
        const FbxDocument::Node *version = header ? header->child("FBXVersion") : out->root.child("FBXVersion");
        if (version && !version->properties.empty()) {
            const std::int64_t parsed = version->properties[0].asInt64(7400);
            if (parsed > 0 && parsed <= std::numeric_limits<std::uint32_t>::max()) {
                out->version = static_cast<std::uint32_t>(parsed);
            }
        }
        return true;
    }

private:
    bool advance()
    {
        return lexer_.next(&current_, error_);
    }

    void skipNewlines()
    {
        while (current_.type == Token::Type::Newline) {
            if (!advance()) return;
        }
    }

    bool parseNode(FbxDocument::Node *out, std::size_t depth)
    {
        if (!out) return fail(error_, "null FBX ASCII node output");
        if (depth > kMaximumDepth) return fail(error_, "FBX ASCII nesting exceeds safety limit");
        if (++node_count_ > kMaximumNodes) return fail(error_, "FBX ASCII node count exceeds safety limit");

        if (current_.type != Token::Type::Identifier && current_.type != Token::Type::String) {
            return fail(error_, "expected FBX ASCII node name");
        }
        out->name = current_.text;
        if (!advance()) return false;
        if (current_.type != Token::Type::Colon) return fail(error_, "expected ':' after FBX ASCII node name");
        if (!advance()) return false;

        while (current_.type != Token::Type::LBrace && current_.type != Token::Type::Newline && current_.type != Token::Type::End && current_.type != Token::Type::RBrace) {
            if (current_.type == Token::Type::Comma) {
                if (!advance()) return false;
                continue;
            }
            FbxDocument::Property property;
            if (!parseProperty(&property)) return false;
            out->properties.push_back(std::move(property));
        }

        if (current_.type == Token::Type::LBrace) {
            if (!advance()) return false;
            skipNewlines();
            while (current_.type != Token::Type::RBrace) {
                if (current_.type == Token::Type::End) return fail(error_, "unterminated FBX ASCII node block");
                FbxDocument::Node child;
                if (!parseNode(&child, depth + 1u)) return false;
                out->children.push_back(std::move(child));
                skipNewlines();
            }
            if (!advance()) return false;
        }

        if (current_.type == Token::Type::Newline) return advance();
        return true;
    }

    bool parseProperty(FbxDocument::Property *out)
    {
        if (!out) return fail(error_, "null FBX ASCII property output");
        if (current_.type == Token::Type::Star) return parseArray(out);
        if (current_.type == Token::Type::String || current_.type == Token::Type::Identifier) {
            *out = FbxDocument::Property::string(current_.text);
            return advance();
        }
        if (current_.type == Token::Type::Integer) {
            std::int64_t value = 0;
            if (!parseInteger(current_.text, &value)) return fail(error_, "invalid FBX ASCII integer");
            *out = FbxDocument::Property::integer(value);
            return advance();
        }
        if (current_.type == Token::Type::Real) {
            double value = 0.0;
            if (!parseReal(current_.text, &value)) return fail(error_, "invalid FBX ASCII real");
            *out = FbxDocument::Property::real(value);
            return advance();
        }
        return fail(error_, "unsupported FBX ASCII property token");
    }

    bool parseArray(FbxDocument::Property *out)
    {
        if (!advance()) return false;
        if (current_.type != Token::Type::Integer) return fail(error_, "expected FBX ASCII array size");
        std::int64_t declared_count = 0;
        if (!parseInteger(current_.text, &declared_count) || declared_count < 0 || static_cast<std::uint64_t>(declared_count) > kMaximumArrayElements) {
            return fail(error_, "invalid FBX ASCII array size");
        }
        if (!advance()) return false;
        if (current_.type != Token::Type::LBrace) return fail(error_, "expected '{' after FBX ASCII array size");
        if (!advance()) return false;
        skipNewlines();

        if (current_.type != Token::Type::Identifier || current_.text != "a") return fail(error_, "expected FBX ASCII array payload");
        if (!advance()) return false;
        if (current_.type != Token::Type::Colon) return fail(error_, "expected ':' before FBX ASCII array payload");
        if (!advance()) return false;

        std::vector<std::int64_t> integers;
        std::vector<double> reals;
        integers.reserve(static_cast<std::size_t>(declared_count));
        reals.reserve(static_cast<std::size_t>(declared_count));
        bool has_real = false;

        while (current_.type != Token::Type::RBrace) {
            if (current_.type == Token::Type::End) return fail(error_, "unterminated FBX ASCII array");
            if (current_.type == Token::Type::Newline || current_.type == Token::Type::Comma) {
                if (!advance()) return false;
                continue;
            }
            if (current_.type == Token::Type::Integer) {
                std::int64_t value = 0;
                if (!parseInteger(current_.text, &value)) return fail(error_, "invalid FBX ASCII integer array value");
                integers.push_back(value);
                reals.push_back(static_cast<double>(value));
            } else if (current_.type == Token::Type::Real) {
                double value = 0.0;
                if (!parseReal(current_.text, &value)) return fail(error_, "invalid FBX ASCII real array value");
                integers.push_back(static_cast<std::int64_t>(value));
                reals.push_back(value);
                has_real = true;
            } else {
                return fail(error_, "invalid FBX ASCII array value");
            }
            if (integers.size() > static_cast<std::size_t>(declared_count)) return fail(error_, "FBX ASCII array exceeds declared size");
            if (!advance()) return false;
        }

        if (integers.size() != static_cast<std::size_t>(declared_count)) return fail(error_, "FBX ASCII array size mismatch");
        if (!advance()) return false;

        if (has_real) {
            *out = FbxDocument::Property::realArray(std::move(reals));
        } else {
            *out = FbxDocument::Property::integerArray(std::move(integers));
        }
        return true;
    }

    Lexer lexer_;
    Token current_;
    std::string *error_ = nullptr;
    std::size_t node_count_ = 0u;
};

} // namespace

bool parse(const std::uint8_t *data, std::size_t size, FbxDocument::RawDocument *out, std::string *error)
{
    if (!data || size == 0u || !out) return fail(error, "invalid FBX ASCII input");
    if (error) error->clear();
    return Parser(data, size, error).parse(out);
}

} // namespace Models::FbxAscii
