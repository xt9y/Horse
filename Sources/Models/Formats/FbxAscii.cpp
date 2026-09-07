#include "Models/Formats/FbxAscii.hpp"

#include <charconv>
#include <cctype>
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
    } type = Type::End;
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
        if (!token) return fail(error, "invalid FBX ASCII token output");
        for (;;) {
            while (pos_ < text_.size()) {
                const char c = text_[pos_];
                if (c == ' ' || c == '\t' || c == '\r') {
                    ++pos_;
                    continue;
                }
                break;
            }
            if (pos_ >= text_.size()) {
                *token = {Token::Type::End,{}};
                return true;
            }
            if (text_[pos_] == ';') {
                while (pos_ < text_.size() && text_[pos_] != '\n') ++pos_;
                if (pos_ < text_.size()) ++pos_;
                *token = {Token::Type::Newline,"\n"};
                return true;
            }
            break;
        }

        const char c = text_[pos_++];
        if (c == '\n') { *token = {Token::Type::Newline,"\n"}; return true; }
        if (c == ':') { *token = {Token::Type::Colon,":"}; return true; }
        if (c == ',') { *token = {Token::Type::Comma,","}; return true; }
        if (c == '{') { *token = {Token::Type::LBrace,"{"}; return true; }
        if (c == '}') { *token = {Token::Type::RBrace,"}"}; return true; }
        if (c == '*') { *token = {Token::Type::Star,"*"}; return true; }
        if (c == '"') return readString(token, error);

        std::string value(1, c);
        while (pos_ < text_.size()) {
            const char next = text_[pos_];
            if (
                next == ' ' || next == '\t' || next == '\r' || next == '\n' ||
                next == ':' || next == ',' || next == '{' || next == '}' || next == '*' || next == ';')
            {
                break;
            }
            value.push_back(next);
            ++pos_;
        }

        const bool maybe_number =
            std::isdigit(static_cast<unsigned char>(value[0])) ||
            value[0] == '+' || value[0] == '-' || value[0] == '.';
        if (!maybe_number) {
            *token = {Token::Type::Identifier,std::move(value)};
            return true;
        }
        const bool real = value.find_first_of(".eE") != std::string::npos;
        *token = {real ? Token::Type::Real : Token::Type::Integer,std::move(value)};
        return true;
    }

private:
    bool readString(Token *token, std::string *error)
    {
        std::string value;
        while (pos_ < text_.size()) {
            const char c = text_[pos_++];
            if (c == '"') {
                *token = {Token::Type::String,std::move(value)};
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
    const char *begin = text.data();
    const char *end = begin + text.size();
    const auto result = std::from_chars(begin, end, *value, std::chars_format::general);
    return result.ec == std::errc{} && result.ptr == end;
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
    bool advance() { return lexer_.next(&current_, error_); }

    void skipNewlines()
    {
        while (current_.type == Token::Type::Newline) {
            if (!advance()) break;
        }
    }

    bool parseNode(FbxDocument::Node *node, std::size_t depth)
    {
        if (!node) return fail(error_, "null FBX ASCII node output");
        if (depth > kMaximumDepth) return fail(error_, "FBX ASCII nesting exceeds limit");
        if (node_count_ >= kMaximumNodes) return fail(error_, "FBX ASCII node count exceeds limit");
        if (current_.type != Token::Type::Identifier) return fail(error_, "expected FBX ASCII node name");
        node->name = current_.text;
        if (!advance()) return false;
        if (current_.type != Token::Type::Colon) return fail(error_, "expected ':' after FBX ASCII node name");
        if (!advance()) return false;

        if (current_.type == Token::Type::Star) {
            if (!parseArray(node)) return false;
            ++node_count_;
            return true;
        }

        while (
            current_.type != Token::Type::LBrace &&
            current_.type != Token::Type::RBrace &&
            current_.type != Token::Type::Newline &&
            current_.type != Token::Type::End)
        {
            if (current_.type == Token::Type::Comma) {
                if (!advance()) return false;
                continue;
            }
            FbxDocument::Property property;
            if (!parseProperty(&property)) return false;
            node->properties.push_back(std::move(property));
        }

        if (current_.type == Token::Type::LBrace) {
            if (!advance()) return false;
            skipNewlines();
            while (current_.type != Token::Type::RBrace) {
                if (current_.type == Token::Type::End) return fail(error_, "unterminated FBX ASCII node");
                FbxDocument::Node child;
                if (!parseNode(&child, depth + 1u)) return false;
                node->children.push_back(std::move(child));
                skipNewlines();
            }
            if (!advance()) return false;
        }
        if (current_.type == Token::Type::Newline) skipNewlines();
        ++node_count_;
        return true;
    }

    bool parseProperty(FbxDocument::Property *property)
    {
        if (!property) return fail(error_, "invalid FBX ASCII property output");
        if (current_.type == Token::Type::String || current_.type == Token::Type::Identifier) {
            property->type = 'S';
            property->value = current_.text;
            return advance();
        }
        if (current_.type == Token::Type::Integer) {
            std::int64_t value = 0;
            if (!parseInteger(current_.text, &value)) return fail(error_, "invalid FBX ASCII integer");
            property->type = 'L';
            property->value = value;
            return advance();
        }
        if (current_.type == Token::Type::Real) {
            double value = 0.0;
            if (!parseReal(current_.text, &value)) return fail(error_, "invalid FBX ASCII real");
            property->type = 'D';
            property->value = value;
            return advance();
        }
        return fail(error_, "invalid FBX ASCII property");
    }

    bool parseArray(FbxDocument::Node *node)
    {
        if (!advance()) return false;
        if (current_.type != Token::Type::Integer) return fail(error_, "expected FBX ASCII array count");
        std::int64_t declared = 0;
        if (!parseInteger(current_.text, &declared) || declared < 0 || static_cast<std::uint64_t>(declared) > kMaximumArrayElements) {
            return fail(error_, "invalid FBX ASCII array count");
        }
        if (!advance()) return false;
        if (current_.type != Token::Type::LBrace) return fail(error_, "expected '{' after FBX ASCII array count");
        if (!advance()) return false;
        skipNewlines();
        if (current_.type == Token::Type::Identifier && current_.text == "a") {
            if (!advance()) return false;
            if (current_.type != Token::Type::Colon) return fail(error_, "expected ':' after FBX ASCII array marker");
            if (!advance()) return false;
        }

        std::vector<double> values;
        values.reserve(static_cast<std::size_t>(declared));
        while (current_.type != Token::Type::RBrace) {
            if (current_.type == Token::Type::End) return fail(error_, "unterminated FBX ASCII array");
            if (current_.type == Token::Type::Comma || current_.type == Token::Type::Newline) {
                if (!advance()) return false;
                continue;
            }
            double value = 0.0;
            if (current_.type == Token::Type::Integer) {
                std::int64_t integer = 0;
                if (!parseInteger(current_.text, &integer)) return fail(error_, "invalid integer in FBX ASCII array");
                value = static_cast<double>(integer);
            } else if (current_.type == Token::Type::Real) {
                if (!parseReal(current_.text, &value)) return fail(error_, "invalid real in FBX ASCII array");
            } else {
                return fail(error_, "expected number in FBX ASCII array");
            }
            values.push_back(value);
            if (values.size() > static_cast<std::size_t>(declared)) return fail(error_, "FBX ASCII array exceeds declared count");
            if (!advance()) return false;
        }
        if (values.size() != static_cast<std::size_t>(declared)) return fail(error_, "FBX ASCII array count mismatch");
        if (!advance()) return false;
        if (current_.type == Token::Type::Newline) skipNewlines();
        node->properties.push_back(FbxDocument::Property{'d', std::move(values)});
        return true;
    }

    Lexer lexer_;
    Token current_;
    std::string *error_ = nullptr;
    std::size_t node_count_ = 0u;
};

} // namespace

bool parse(
    const std::uint8_t *data,
    std::size_t size,
    FbxDocument::RawDocument *out,
    std::string *error)
{
    if (error) error->clear();
    if (!data || !out) return fail(error, "invalid ASCII FBX input");
    Parser parser(data, size, error);
    return parser.parse(out);
}

} // namespace Models::FbxAscii
