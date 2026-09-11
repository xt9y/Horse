#include "Models/Formats/Gltf.hpp"

#include "Models/Core/Texture.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Models::Formats::Gltf {
namespace {

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

struct Json {
    enum class Type : std::uint8_t { Null, Boolean, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string text;
    std::vector<Json> array;
    std::unordered_map<std::string, Json> object;

    const Json *get(std::string_view key) const
    {
        if (type != Type::Object) return nullptr;
        const auto found = object.find(std::string(key));
        return found == object.end() ? nullptr : &found->second;
    }
};

class JsonParser {
public:
    JsonParser(const char *data, std::size_t size) : begin_(data), current_(data), end_(data + size) {}

    bool parse(Json *out, std::string *error)
    {
        if (!out) return fail(error, "glTF JSON output is null");
        skipWhitespace();
        if (!parseValue(out, error)) return false;
        skipWhitespace();
        if (current_ != end_) return parseFail(error, "unexpected trailing JSON data");
        return true;
    }

private:
    const char *begin_ = nullptr;
    const char *current_ = nullptr;
    const char *end_ = nullptr;

    bool parseFail(std::string *error, const std::string& message) const
    {
        const std::size_t offset = static_cast<std::size_t>(current_ - begin_);
        return fail(error, "glTF JSON error at byte " + std::to_string(offset) + ": " + message);
    }

    void skipWhitespace()
    {
        while (current_ != end_ && (*current_ == ' ' || *current_ == '\t' || *current_ == '\r' || *current_ == '\n')) ++current_;
    }

    bool consume(char value)
    {
        if (current_ == end_ || *current_ != value) return false;
        ++current_;
        return true;
    }

    static int hex(char value)
    {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return 10 + value - 'a';
        if (value >= 'A' && value <= 'F') return 10 + value - 'A';
        return -1;
    }

    bool unicodeEscape(std::uint32_t *out, std::string *error)
    {
        if (!out || static_cast<std::size_t>(end_ - current_) < 4u) return parseFail(error, "truncated unicode escape");
        std::uint32_t value = 0u;
        for (int index = 0; index < 4; ++index) {
            const int digit = hex(*current_++);
            if (digit < 0) return parseFail(error, "invalid unicode escape");
            value = (value << 4u) | static_cast<std::uint32_t>(digit);
        }
        *out = value;
        return true;
    }

    static void appendUtf8(std::string& out, std::uint32_t codepoint)
    {
        if (codepoint <= 0x7fu) {
            out.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7ffu) {
            out.push_back(static_cast<char>(0xc0u | (codepoint >> 6u)));
            out.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
        } else if (codepoint <= 0xffffu) {
            out.push_back(static_cast<char>(0xe0u | (codepoint >> 12u)));
            out.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3fu)));
            out.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
        } else {
            out.push_back(static_cast<char>(0xf0u | (codepoint >> 18u)));
            out.push_back(static_cast<char>(0x80u | ((codepoint >> 12u) & 0x3fu)));
            out.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3fu)));
            out.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
        }
    }

    bool parseString(std::string *out, std::string *error)
    {
        if (!out || !consume('"')) return parseFail(error, "expected string");
        out->clear();
        while (current_ != end_) {
            const unsigned char value = static_cast<unsigned char>(*current_++);
            if (value == '"') return true;
            if (value < 0x20u) return parseFail(error, "control character in string");
            if (value != '\\') {
                out->push_back(static_cast<char>(value));
                continue;
            }
            if (current_ == end_) return parseFail(error, "truncated string escape");
            const char escape = *current_++;
            switch (escape) {
                case '"': out->push_back('"'); break;
                case '\\': out->push_back('\\'); break;
                case '/': out->push_back('/'); break;
                case 'b': out->push_back('\b'); break;
                case 'f': out->push_back('\f'); break;
                case 'n': out->push_back('\n'); break;
                case 'r': out->push_back('\r'); break;
                case 't': out->push_back('\t'); break;
                case 'u': {
                    std::uint32_t codepoint = 0u;
                    if (!unicodeEscape(&codepoint, error)) return false;
                    if (codepoint >= 0xd800u && codepoint <= 0xdbffu) {
                        if (static_cast<std::size_t>(end_ - current_) < 2u || current_[0] != '\\' || current_[1] != 'u')
                            return parseFail(error, "missing low unicode surrogate");
                        current_ += 2;
                        std::uint32_t low = 0u;
                        if (!unicodeEscape(&low, error)) return false;
                        if (low < 0xdc00u || low > 0xdfffu) return parseFail(error, "invalid low unicode surrogate");
                        codepoint = 0x10000u + ((codepoint - 0xd800u) << 10u) + (low - 0xdc00u);
                    } else if (codepoint >= 0xdc00u && codepoint <= 0xdfffu) {
                        return parseFail(error, "unexpected low unicode surrogate");
                    }
                    appendUtf8(*out, codepoint);
                    break;
                }
                default: return parseFail(error, "invalid string escape");
            }
        }
        return parseFail(error, "unterminated string");
    }

    bool parseNumber(Json *out, std::string *error)
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
            if (current_ == end_ || *current_ < '0' || *current_ > '9') return parseFail(error, "invalid number fraction");
            while (current_ != end_ && *current_ >= '0' && *current_ <= '9') ++current_;
        }
        if (current_ != end_ && (*current_ == 'e' || *current_ == 'E')) {
            ++current_;
            if (current_ != end_ && (*current_ == '+' || *current_ == '-')) ++current_;
            if (current_ == end_ || *current_ < '0' || *current_ > '9') return parseFail(error, "invalid number exponent");
            while (current_ != end_ && *current_ >= '0' && *current_ <= '9') ++current_;
        }

        const std::string token(start, current_);
        char *parsed_end = nullptr;
        errno = 0;
        const double number = std::strtod(token.c_str(), &parsed_end);
        if (errno == ERANGE || !parsed_end || *parsed_end != '\0' || !std::isfinite(number))
            return parseFail(error, "number is out of range");
        out->type = Json::Type::Number;
        out->number = number;
        return true;
    }

    bool parseArray(Json *out, std::string *error)
    {
        if (!consume('[')) return parseFail(error, "expected array");
        out->type = Json::Type::Array;
        out->array.clear();
        skipWhitespace();
        if (consume(']')) return true;
        for (;;) {
            Json value;
            if (!parseValue(&value, error)) return false;
            out->array.push_back(std::move(value));
            skipWhitespace();
            if (consume(']')) return true;
            if (!consume(',')) return parseFail(error, "expected ',' in array");
            skipWhitespace();
        }
    }

    bool parseObject(Json *out, std::string *error)
    {
        if (!consume('{')) return parseFail(error, "expected object");
        out->type = Json::Type::Object;
        out->object.clear();
        skipWhitespace();
        if (consume('}')) return true;
        for (;;) {
            std::string key;
            if (!parseString(&key, error)) return false;
            skipWhitespace();
            if (!consume(':')) return parseFail(error, "expected ':' in object");
            skipWhitespace();
            Json value;
            if (!parseValue(&value, error)) return false;
            out->object.insert_or_assign(std::move(key), std::move(value));
            skipWhitespace();
            if (consume('}')) return true;
            if (!consume(',')) return parseFail(error, "expected ',' in object");
            skipWhitespace();
        }
    }

    bool parseLiteral(std::string_view literal, Json::Type type, bool boolean, Json *out, std::string *error)
    {
        if (static_cast<std::size_t>(end_ - current_) < literal.size() ||
            std::string_view(current_, literal.size()) != literal)
            return parseFail(error, "invalid JSON literal");
        current_ += literal.size();
        out->type = type;
        out->boolean = boolean;
        return true;
    }

    bool parseValue(Json *out, std::string *error)
    {
        skipWhitespace();
        if (current_ == end_) return parseFail(error, "unexpected end of JSON");
        if (*current_ == '{') return parseObject(out, error);
        if (*current_ == '[') return parseArray(out, error);
        if (*current_ == '"') {
            out->type = Json::Type::String;
            return parseString(&out->text, error);
        }
        if (*current_ == 't') return parseLiteral("true", Json::Type::Boolean, true, out, error);
        if (*current_ == 'f') return parseLiteral("false", Json::Type::Boolean, false, out, error);
        if (*current_ == 'n') return parseLiteral("null", Json::Type::Null, false, out, error);
        return parseNumber(out, error);
    }
};

bool number(const Json *value, double *out)
{
    if (!value || value->type != Json::Type::Number || !out) return false;
    *out = value->number;
    return true;
}

int integer(const Json *value, int fallback = -1)
{
    if (!value || value->type != Json::Type::Number) return fallback;
    if (value->number < static_cast<double>(std::numeric_limits<int>::min()) ||
        value->number > static_cast<double>(std::numeric_limits<int>::max()))
        return fallback;
    return static_cast<int>(value->number);
}

std::size_t sizeValue(const Json *value, std::size_t fallback = 0u)
{
    if (!value || value->type != Json::Type::Number || value->number < 0.0 ||
        value->number > static_cast<double>(std::numeric_limits<std::size_t>::max()))
        return fallback;
    return static_cast<std::size_t>(value->number);
}

float floatValue(const Json *value, float fallback)
{
    if (!value || value->type != Json::Type::Number || !std::isfinite(value->number)) return fallback;
    if (value->number < -static_cast<double>(std::numeric_limits<float>::max()) ||
        value->number > static_cast<double>(std::numeric_limits<float>::max()))
        return fallback;
    return static_cast<float>(value->number);
}

bool boolValue(const Json *value, bool fallback)
{
    return value && value->type == Json::Type::Boolean ? value->boolean : fallback;
}

std::string stringValue(const Json *value, std::string fallback = {})
{
    return value && value->type == Json::Type::String ? value->text : std::move(fallback);
}

bool readFile(const std::filesystem::path& path, std::vector<std::uint8_t> *out, std::string *error)
{
    if (!out) return fail(error, "glTF file output is null");
    std::ifstream input(path, std::ios::binary);
    if (!input) return fail(error, "failed to open glTF resource: " + path.string());
    input.seekg(0, std::ios::end);
    const std::streamoff length = input.tellg();
    if (length < 0 || static_cast<std::uint64_t>(length) > std::numeric_limits<std::size_t>::max())
        return fail(error, "failed to size glTF resource: " + path.string());
    input.seekg(0, std::ios::beg);
    out->resize(static_cast<std::size_t>(length));
    if (!out->empty()) input.read(reinterpret_cast<char *>(out->data()), static_cast<std::streamsize>(out->size()));
    if (!input && !out->empty()) return fail(error, "failed to read glTF resource: " + path.string());
    return true;
}

std::uint32_t readU32(const std::uint8_t *data)
{
    return static_cast<std::uint32_t>(data[0]) |
        (static_cast<std::uint32_t>(data[1]) << 8u) |
        (static_cast<std::uint32_t>(data[2]) << 16u) |
        (static_cast<std::uint32_t>(data[3]) << 24u);
}

bool parseContainer(
    const std::string& path,
    std::string *json_text,
    std::vector<std::uint8_t> *binary_chunk,
    std::string *error)
{
    if (!json_text || !binary_chunk) return fail(error, "glTF container output is null");
    std::vector<std::uint8_t> bytes;
    if (!readFile(path, &bytes, error)) return false;
    binary_chunk->clear();

    const bool glb = bytes.size() >= 4u && readU32(bytes.data()) == 0x46546c67u;
    if (!glb) {
        std::size_t offset = 0u;
        if (bytes.size() >= 3u && bytes[0] == 0xefu && bytes[1] == 0xbbu && bytes[2] == 0xbfu) offset = 3u;
        json_text->assign(reinterpret_cast<const char *>(bytes.data() + offset), bytes.size() - offset);
        return true;
    }

    if (bytes.size() < 12u) return fail(error, "truncated GLB header: " + path);
    const std::uint32_t version = readU32(bytes.data() + 4u);
    const std::uint32_t declared_length = readU32(bytes.data() + 8u);
    if (version != 2u) return fail(error, "unsupported GLB version: " + std::to_string(version));
    if (declared_length > bytes.size() || declared_length < 12u) return fail(error, "invalid GLB length: " + path);

    bool have_json = false;
    std::size_t offset = 12u;
    while (offset + 8u <= declared_length) {
        const std::uint32_t chunk_length = readU32(bytes.data() + offset);
        const std::uint32_t chunk_type = readU32(bytes.data() + offset + 4u);
        offset += 8u;
        if (chunk_length > declared_length - offset) return fail(error, "GLB chunk exceeds container length: " + path);
        if (chunk_type == 0x4e4f534au && !have_json) {
            json_text->assign(reinterpret_cast<const char *>(bytes.data() + offset), chunk_length);
            while (!json_text->empty() && (json_text->back() == '\0' || json_text->back() == ' ' ||
                json_text->back() == '\t' || json_text->back() == '\r' || json_text->back() == '\n'))
                json_text->pop_back();
            have_json = true;
        } else if (chunk_type == 0x004e4942u && binary_chunk->empty()) {
            binary_chunk->assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                bytes.begin() + static_cast<std::ptrdiff_t>(offset + chunk_length));
        }
        offset += chunk_length;
    }
    if (!have_json) return fail(error, "GLB has no JSON chunk: " + path);
    return true;
}

int base64Digit(unsigned char value)
{
    if (value >= 'A' && value <= 'Z') return value - 'A';
    if (value >= 'a' && value <= 'z') return 26 + value - 'a';
    if (value >= '0' && value <= '9') return 52 + value - '0';
    if (value == '+') return 62;
    if (value == '/') return 63;
    return -1;
}

bool decodeBase64(std::string_view input, std::vector<std::uint8_t> *out, std::string *error)
{
    if (!out) return fail(error, "base64 output is null");
    out->clear();
    std::uint32_t accumulator = 0u;
    int bits = 0;
    bool padding = false;
    for (const unsigned char value : input) {
        if (value == ' ' || value == '\t' || value == '\r' || value == '\n') continue;
        if (value == '=') {
            padding = true;
            continue;
        }
        if (padding) return fail(error, "invalid base64 padding in glTF data URI");
        const int digit = base64Digit(value);
        if (digit < 0) return fail(error, "invalid base64 character in glTF data URI");
        accumulator = (accumulator << 6u) | static_cast<std::uint32_t>(digit);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out->push_back(static_cast<std::uint8_t>((accumulator >> static_cast<unsigned int>(bits)) & 0xffu));
        }
    }
    return true;
}

bool decodePercent(std::string_view input, std::vector<std::uint8_t> *out, std::string *error)
{
    if (!out) return fail(error, "URI output is null");
    out->clear();
    out->reserve(input.size());
    auto hex = [](char value) -> int {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return 10 + value - 'a';
        if (value >= 'A' && value <= 'F') return 10 + value - 'A';
        return -1;
    };
    for (std::size_t index = 0u; index < input.size(); ++index) {
        if (input[index] != '%') {
            out->push_back(static_cast<std::uint8_t>(input[index]));
            continue;
        }
        if (index + 2u >= input.size()) return fail(error, "truncated percent escape in glTF URI");
        const int high = hex(input[index + 1u]);
        const int low = hex(input[index + 2u]);
        if (high < 0 || low < 0) return fail(error, "invalid percent escape in glTF URI");
        out->push_back(static_cast<std::uint8_t>((high << 4) | low));
        index += 2u;
    }
    return true;
}

bool decodeDataUri(std::string_view uri, std::vector<std::uint8_t> *out, std::string *error)
{
    if (!uri.starts_with("data:")) return false;
    const std::size_t comma = uri.find(',');
    if (comma == std::string_view::npos) return fail(error, "invalid glTF data URI");
    const std::string_view metadata = uri.substr(5u, comma - 5u);
    const std::string_view payload = uri.substr(comma + 1u);
    const bool base64 = metadata.size() >= 7u && metadata.find(";base64") != std::string_view::npos;
    return base64 ? decodeBase64(payload, out, error) : decodePercent(payload, out, error);
}

bool decodedUriPath(std::string_view uri, std::string *out, std::string *error)
{
    if (!out) return fail(error, "URI path output is null");
    const std::size_t colon = uri.find(':');
    const std::size_t slash = uri.find('/');
    if (colon != std::string_view::npos && (slash == std::string_view::npos || colon < slash))
        return fail(error, "unsupported external glTF URI scheme: " + std::string(uri.substr(0u, colon)));
    std::vector<std::uint8_t> bytes;
    if (!decodePercent(uri, &bytes, error)) return false;
    out->assign(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    return true;
}

using Mat4 = std::array<float, 16>;

Mat4 identity()
{
    return {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
}

Mat4 multiply(const Mat4& a, const Mat4& b)
{
    Mat4 result{};
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k)
                result[static_cast<std::size_t>(column * 4 + row)] +=
                    a[static_cast<std::size_t>(k * 4 + row)] * b[static_cast<std::size_t>(column * 4 + k)];
    return result;
}

Mat4 translation(float x, float y, float z)
{
    Mat4 result = identity();
    result[12] = x; result[13] = y; result[14] = z;
    return result;
}

Mat4 scaling(float x, float y, float z)
{
    Mat4 result{};
    result[0] = x; result[5] = y; result[10] = z; result[15] = 1.0f;
    return result;
}

Mat4 quaternion(float x, float y, float z, float w)
{
    const float length_squared = x*x + y*y + z*z + w*w;
    if (length_squared <= 1.0e-20f) return identity();
    const float inverse = 1.0f / std::sqrt(length_squared);
    x *= inverse; y *= inverse; z *= inverse; w *= inverse;
    const float xx=x*x, yy=y*y, zz=z*z, xy=x*y, xz=x*z, yz=y*z, wx=w*x, wy=w*y, wz=w*z;
    return {
        1.0f-2.0f*(yy+zz), 2.0f*(xy+wz), 2.0f*(xz-wy), 0.0f,
        2.0f*(xy-wz), 1.0f-2.0f*(xx+zz), 2.0f*(yz+wx), 0.0f,
        2.0f*(xz+wy), 2.0f*(yz-wx), 1.0f-2.0f*(xx+yy), 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
}

Vec3 transformPoint(const Mat4& matrix, Vec3 value)
{
    return {
        matrix[0]*value.x + matrix[4]*value.y + matrix[8]*value.z + matrix[12],
        matrix[1]*value.x + matrix[5]*value.y + matrix[9]*value.z + matrix[13],
        matrix[2]*value.x + matrix[6]*value.y + matrix[10]*value.z + matrix[14],
    };
}

float determinant3(const Mat4& matrix)
{
    return matrix[0]*(matrix[5]*matrix[10]-matrix[9]*matrix[6]) -
        matrix[4]*(matrix[1]*matrix[10]-matrix[9]*matrix[2]) +
        matrix[8]*(matrix[1]*matrix[6]-matrix[5]*matrix[2]);
}

Vec3 transformNormal(const Mat4& matrix, Vec3 normal)
{
    const float a00=matrix[0], a01=matrix[4], a02=matrix[8];
    const float a10=matrix[1], a11=matrix[5], a12=matrix[9];
    const float a20=matrix[2], a21=matrix[6], a22=matrix[10];
    const float determinant = a00*(a11*a22-a12*a21)-a01*(a10*a22-a12*a20)+a02*(a10*a21-a11*a20);
    if (std::abs(determinant) <= 1.0e-12f) return normal;
    const float inverse = 1.0f / determinant;
    const float i00=(a11*a22-a12*a21)*inverse, i01=(a02*a21-a01*a22)*inverse, i02=(a01*a12-a02*a11)*inverse;
    const float i10=(a12*a20-a10*a22)*inverse, i11=(a00*a22-a02*a20)*inverse, i12=(a02*a10-a00*a12)*inverse;
    const float i20=(a10*a21-a11*a20)*inverse, i21=(a01*a20-a00*a21)*inverse, i22=(a00*a11-a01*a10)*inverse;
    Vec3 result {
        i00*normal.x + i10*normal.y + i20*normal.z,
        i01*normal.x + i11*normal.y + i21*normal.z,
        i02*normal.x + i12*normal.y + i22*normal.z,
    };
    const float length_squared = result.x*result.x + result.y*result.y + result.z*result.z;
    if (length_squared <= 1.0e-20f) return {0.0f, 1.0f, 0.0f};
    const float inv_length = 1.0f / std::sqrt(length_squared);
    result.x *= inv_length; result.y *= inv_length; result.z *= inv_length;
    return result;
}

Mat4 nodeMatrix(const Json& node)
{
    if (const Json *matrix = node.get("matrix"); matrix && matrix->type == Json::Type::Array && matrix->array.size() == 16u) {
        Mat4 result{};
        bool valid = true;
        for (std::size_t index = 0u; index < 16u; ++index) {
            if (matrix->array[index].type != Json::Type::Number) { valid = false; break; }
            result[index] = floatValue(&matrix->array[index], index % 5u == 0u ? 1.0f : 0.0f);
        }
        if (valid) return result;
    }

    Vec3 t{};
    Vec3 s{1.0f,1.0f,1.0f};
    std::array<float,4> r{0.0f,0.0f,0.0f,1.0f};
    if (const Json *value=node.get("translation"); value && value->type==Json::Type::Array && value->array.size()>=3u) {
        t={floatValue(&value->array[0],0),floatValue(&value->array[1],0),floatValue(&value->array[2],0)};
    }
    if (const Json *value=node.get("scale"); value && value->type==Json::Type::Array && value->array.size()>=3u) {
        s={floatValue(&value->array[0],1),floatValue(&value->array[1],1),floatValue(&value->array[2],1)};
    }
    if (const Json *value=node.get("rotation"); value && value->type==Json::Type::Array && value->array.size()>=4u) {
        for (std::size_t index=0;index<4u;++index) r[index]=floatValue(&value->array[index], index==3u?1.0f:0.0f);
    }
    return multiply(multiply(translation(t.x,t.y,t.z), quaternion(r[0],r[1],r[2],r[3])), scaling(s.x,s.y,s.z));
}

struct BufferView {
    int buffer = -1;
    std::size_t offset = 0u;
    std::size_t length = 0u;
    std::size_t stride = 0u;
};

struct Accessor {
    int view = -1;
    std::size_t offset = 0u;
    std::size_t count = 0u;
    int component_type = 0;
    std::string type;
    bool normalized = false;

    std::size_t sparse_count = 0u;
    int sparse_indices_view = -1;
    std::size_t sparse_indices_offset = 0u;
    int sparse_indices_component = 0;
    int sparse_values_view = -1;
    std::size_t sparse_values_offset = 0u;
};

std::size_t componentSize(int type)
{
    switch (type) {
        case 5120: case 5121: return 1u;
        case 5122: case 5123: return 2u;
        case 5125: case 5126: return 4u;
        default: return 0u;
    }
}

std::size_t componentCount(std::string_view type)
{
    if (type == "SCALAR") return 1u;
    if (type == "VEC2") return 2u;
    if (type == "VEC3") return 3u;
    if (type == "VEC4") return 4u;
    if (type == "MAT2") return 4u;
    if (type == "MAT3") return 9u;
    if (type == "MAT4") return 16u;
    return 0u;
}

struct Context {
    std::string path;
    std::filesystem::path directory;
    const Json *root = nullptr;
    std::vector<std::vector<std::uint8_t>> buffers;
    std::vector<BufferView> views;
    std::vector<Accessor> accessors;
    std::vector<TextureHandle> image_cache;
    std::vector<MaterialData> materials;
};

bool rangePointer(const Context& context, int view_index, std::size_t relative_offset, std::size_t bytes,
    const std::uint8_t **out, std::string *error)
{
    if (!out || view_index < 0 || static_cast<std::size_t>(view_index) >= context.views.size())
        return fail(error, "glTF references an invalid bufferView");
    const BufferView& view = context.views[static_cast<std::size_t>(view_index)];
    if (view.buffer < 0 || static_cast<std::size_t>(view.buffer) >= context.buffers.size())
        return fail(error, "glTF bufferView references an invalid buffer");
    if (relative_offset > view.length || bytes > view.length - relative_offset)
        return fail(error, "glTF accessor exceeds bufferView bounds");
    const std::vector<std::uint8_t>& buffer = context.buffers[static_cast<std::size_t>(view.buffer)];
    if (view.offset > buffer.size() || relative_offset > buffer.size() - view.offset ||
        bytes > buffer.size() - view.offset - relative_offset)
        return fail(error, "glTF bufferView exceeds buffer bounds");
    *out = buffer.data() + view.offset + relative_offset;
    return true;
}

template <typename T>
T readScalar(const std::uint8_t *data)
{
    T value{};
    std::memcpy(&value, data, sizeof(T));
    return value;
}

float componentFloat(const std::uint8_t *data, int type, bool normalized)
{
    switch (type) {
        case 5120: {
            const std::int8_t value=readScalar<std::int8_t>(data);
            return normalized ? std::max(static_cast<float>(value)/127.0f,-1.0f) : static_cast<float>(value);
        }
        case 5121: {
            const std::uint8_t value=readScalar<std::uint8_t>(data);
            return normalized ? static_cast<float>(value)/255.0f : static_cast<float>(value);
        }
        case 5122: {
            const std::int16_t value=readScalar<std::int16_t>(data);
            return normalized ? std::max(static_cast<float>(value)/32767.0f,-1.0f) : static_cast<float>(value);
        }
        case 5123: {
            const std::uint16_t value=readScalar<std::uint16_t>(data);
            return normalized ? static_cast<float>(value)/65535.0f : static_cast<float>(value);
        }
        case 5125: {
            const std::uint32_t value=readScalar<std::uint32_t>(data);
            return normalized ? static_cast<float>(static_cast<double>(value)/4294967295.0) : static_cast<float>(value);
        }
        case 5126: return readScalar<float>(data);
        default: return 0.0f;
    }
}

bool unsignedComponent(const std::uint8_t *data, int type, std::uint32_t *out)
{
    if (!out) return false;
    switch (type) {
        case 5121: *out=readScalar<std::uint8_t>(data); return true;
        case 5123: *out=readScalar<std::uint16_t>(data); return true;
        case 5125: *out=readScalar<std::uint32_t>(data); return true;
        default: return false;
    }
}

bool parseViews(Context *context, std::string *error)
{
    if (!context || !context->root) return false;
    const Json *values = context->root->get("bufferViews");
    if (!values) return true;
    if (values->type != Json::Type::Array) return fail(error, "glTF bufferViews must be an array");
    context->views.reserve(values->array.size());
    for (const Json& value : values->array) {
        if (value.type != Json::Type::Object) return fail(error, "invalid glTF bufferView");
        BufferView view;
        view.buffer = integer(value.get("buffer"));
        view.offset = sizeValue(value.get("byteOffset"));
        view.length = sizeValue(value.get("byteLength"));
        view.stride = sizeValue(value.get("byteStride"));
        if (view.buffer < 0 || static_cast<std::size_t>(view.buffer) >= context->buffers.size() || view.length == 0u)
            return fail(error, "invalid glTF bufferView range");
        const std::vector<std::uint8_t>& buffer=context->buffers[static_cast<std::size_t>(view.buffer)];
        if (view.offset > buffer.size() || view.length > buffer.size()-view.offset)
            return fail(error, "glTF bufferView exceeds buffer bounds");
        context->views.push_back(view);
    }
    return true;
}

bool parseAccessors(Context *context, std::string *error)
{
    if (!context || !context->root) return false;
    const Json *values=context->root->get("accessors");
    if (!values) return true;
    if (values->type!=Json::Type::Array) return fail(error,"glTF accessors must be an array");
    context->accessors.reserve(values->array.size());
    for (const Json& value:values->array) {
        if (value.type!=Json::Type::Object) return fail(error,"invalid glTF accessor");
        Accessor accessor;
        accessor.view=integer(value.get("bufferView"));
        accessor.offset=sizeValue(value.get("byteOffset"));
        accessor.count=sizeValue(value.get("count"));
        accessor.component_type=integer(value.get("componentType"),0);
        accessor.type=stringValue(value.get("type"));
        accessor.normalized=boolValue(value.get("normalized"),false);
        if (accessor.count==0u || componentSize(accessor.component_type)==0u || componentCount(accessor.type)==0u)
            return fail(error,"invalid glTF accessor layout");
        if (accessor.view >= 0 && static_cast<std::size_t>(accessor.view)>=context->views.size())
            return fail(error,"glTF accessor references invalid bufferView");

        if (const Json *sparse=value.get("sparse"); sparse && sparse->type==Json::Type::Object) {
            accessor.sparse_count=sizeValue(sparse->get("count"));
            const Json *indices=sparse->get("indices");
            const Json *values_object=sparse->get("values");
            if (!indices || indices->type!=Json::Type::Object || !values_object || values_object->type!=Json::Type::Object ||
                accessor.sparse_count>accessor.count)
                return fail(error,"invalid sparse glTF accessor");
            accessor.sparse_indices_view=integer(indices->get("bufferView"));
            accessor.sparse_indices_offset=sizeValue(indices->get("byteOffset"));
            accessor.sparse_indices_component=integer(indices->get("componentType"),0);
            accessor.sparse_values_view=integer(values_object->get("bufferView"));
            accessor.sparse_values_offset=sizeValue(values_object->get("byteOffset"));
            if ((accessor.sparse_indices_component!=5121 && accessor.sparse_indices_component!=5123 && accessor.sparse_indices_component!=5125) ||
                accessor.sparse_indices_view<0 || accessor.sparse_values_view<0 ||
                static_cast<std::size_t>(accessor.sparse_indices_view)>=context->views.size() ||
                static_cast<std::size_t>(accessor.sparse_values_view)>=context->views.size())
                return fail(error,"invalid sparse glTF accessor buffers");
        }
        context->accessors.push_back(std::move(accessor));
    }
    return true;
}

bool decodeFloats(const Context& context, int accessor_index, std::size_t expected_components,
    std::vector<float> *out, std::string *error)
{
    if (!out || accessor_index<0 || static_cast<std::size_t>(accessor_index)>=context.accessors.size())
        return fail(error,"glTF references invalid accessor");
    const Accessor& accessor=context.accessors[static_cast<std::size_t>(accessor_index)];
    const std::size_t components=componentCount(accessor.type);
    const std::size_t component_size=componentSize(accessor.component_type);
    if (components!=expected_components) return fail(error,"glTF accessor has unexpected element type");
    if (accessor.count>std::numeric_limits<std::size_t>::max()/components)
        return fail(error,"glTF accessor is too large");
    out->assign(accessor.count*components,0.0f);
    const std::size_t element_size=components*component_size;

    if (accessor.view>=0) {
        const BufferView& view=context.views[static_cast<std::size_t>(accessor.view)];
        const std::size_t stride=view.stride==0u?element_size:view.stride;
        if (stride<element_size) return fail(error,"glTF accessor byteStride is smaller than element size");
        if (accessor.count>0u && (accessor.count-1u)>std::numeric_limits<std::size_t>::max()/stride)
            return fail(error,"glTF accessor stride overflows");
        const std::size_t required=accessor.count==0u?0u:(accessor.count-1u)*stride+element_size;
        const std::uint8_t *base=nullptr;
        if (!rangePointer(context,accessor.view,accessor.offset,required,&base,error)) return false;
        for (std::size_t element=0u;element<accessor.count;++element)
            for (std::size_t component=0u;component<components;++component)
                (*out)[element*components+component]=componentFloat(base+element*stride+component*component_size,
                    accessor.component_type,accessor.normalized);
    }

    if (accessor.sparse_count>0u) {
        const std::size_t index_size=componentSize(accessor.sparse_indices_component);
        const std::uint8_t *indices=nullptr;
        const std::uint8_t *values=nullptr;
        if (!rangePointer(context,accessor.sparse_indices_view,accessor.sparse_indices_offset,
            accessor.sparse_count*index_size,&indices,error) ||
            !rangePointer(context,accessor.sparse_values_view,accessor.sparse_values_offset,
            accessor.sparse_count*element_size,&values,error)) return false;
        for (std::size_t sparse=0u;sparse<accessor.sparse_count;++sparse) {
            std::uint32_t destination=0u;
            if (!unsignedComponent(indices+sparse*index_size,accessor.sparse_indices_component,&destination) || destination>=accessor.count)
                return fail(error,"invalid sparse glTF accessor index");
            for (std::size_t component=0u;component<components;++component)
                (*out)[static_cast<std::size_t>(destination)*components+component]=componentFloat(
                    values+sparse*element_size+component*component_size,accessor.component_type,accessor.normalized);
        }
    }
    return true;
}

bool decodeIndices(const Context& context, int accessor_index, std::vector<std::uint32_t> *out, std::string *error)
{
    if (!out || accessor_index<0 || static_cast<std::size_t>(accessor_index)>=context.accessors.size())
        return fail(error,"glTF references invalid index accessor");
    const Accessor& accessor=context.accessors[static_cast<std::size_t>(accessor_index)];
    if (accessor.type!="SCALAR" || (accessor.component_type!=5121 && accessor.component_type!=5123 && accessor.component_type!=5125))
        return fail(error,"glTF index accessor must use unsigned scalar components");
    const std::size_t size=componentSize(accessor.component_type);
    out->assign(accessor.count,0u);
    if (accessor.view>=0) {
        const BufferView& view=context.views[static_cast<std::size_t>(accessor.view)];
        const std::size_t stride=view.stride==0u?size:view.stride;
        if (stride<size) return fail(error,"glTF index stride is smaller than component size");
        const std::size_t required=accessor.count==0u?0u:(accessor.count-1u)*stride+size;
        const std::uint8_t *base=nullptr;
        if (!rangePointer(context,accessor.view,accessor.offset,required,&base,error)) return false;
        for (std::size_t index=0u;index<accessor.count;++index)
            if (!unsignedComponent(base+index*stride,accessor.component_type,&(*out)[index])) return false;
    }
    if (accessor.sparse_count>0u) {
        const std::size_t sparse_index_size=componentSize(accessor.sparse_indices_component);
        const std::uint8_t *indices=nullptr,*values=nullptr;
        if (!rangePointer(context,accessor.sparse_indices_view,accessor.sparse_indices_offset,
            accessor.sparse_count*sparse_index_size,&indices,error) ||
            !rangePointer(context,accessor.sparse_values_view,accessor.sparse_values_offset,
            accessor.sparse_count*size,&values,error)) return false;
        for (std::size_t sparse=0u;sparse<accessor.sparse_count;++sparse) {
            std::uint32_t destination=0u,value=0u;
            if (!unsignedComponent(indices+sparse*sparse_index_size,accessor.sparse_indices_component,&destination) || destination>=accessor.count ||
                !unsignedComponent(values+sparse*size,accessor.component_type,&value))
                return fail(error,"invalid sparse glTF index accessor");
            (*out)[destination]=value;
        }
    }
    return true;
}

bool loadBuffers(Context *context, const std::vector<std::uint8_t>& binary_chunk, std::string *error)
{
    if (!context || !context->root) return false;
    const Json *values=context->root->get("buffers");
    if (!values || values->type!=Json::Type::Array) return fail(error,"glTF has no buffers array");
    context->buffers.reserve(values->array.size());
    bool used_binary=false;
    for (std::size_t index=0u;index<values->array.size();++index) {
        const Json& value=values->array[index];
        if (value.type!=Json::Type::Object) return fail(error,"invalid glTF buffer");
        const std::size_t expected=sizeValue(value.get("byteLength"));
        std::vector<std::uint8_t> bytes;
        const Json *uri=value.get("uri");
        if (!uri) {
            if (used_binary || binary_chunk.empty()) return fail(error,"glTF buffer has no URI or GLB binary chunk");
            bytes=binary_chunk;
            used_binary=true;
        } else if (uri->type==Json::Type::String && uri->text.starts_with("data:")) {
            if (!decodeDataUri(uri->text,&bytes,error)) return false;
        } else if (uri->type==Json::Type::String) {
            std::string decoded;
            if (!decodedUriPath(uri->text,&decoded,error)) return false;
            if (!readFile(context->directory/std::filesystem::path(decoded),&bytes,error)) return false;
        } else {
            return fail(error,"invalid glTF buffer URI");
        }
        if (bytes.size()<expected) return fail(error,"glTF buffer is shorter than declared byteLength");
        context->buffers.push_back(std::move(bytes));
    }
    return true;
}

TextureHandle deriveChannel(TextureHandle source, int channel, const char *label, std::string *error)
{
    const TextureAsset *asset=texture(source);
    if (!asset || channel<0 || channel>3) return INVALID_TEXTURE;
    Images::Image image=asset->image;
    for (std::size_t offset=0u;offset+3u<image.rgba.size();offset+=4u) {
        const std::uint8_t value=image.rgba[offset+static_cast<std::size_t>(channel)];
        image.rgba[offset]=value; image.rgba[offset+1u]=value; image.rgba[offset+2u]=value; image.rgba[offset+3u]=255u;
    }
    image.meaningful_alpha=false;
    return registerTextureImage(asset->path+"\n@gltf-channel:"+label,std::move(image),error);
}

TextureHandle opaqueTexture(TextureHandle source, std::string *error)
{
    const TextureAsset *asset=texture(source);
    if (!asset) return INVALID_TEXTURE;
    if (!asset->image.meaningful_alpha) return source;
    Images::Image image=asset->image;
    for (std::size_t offset=3u;offset<image.rgba.size();offset+=4u) image.rgba[offset]=255u;
    image.meaningful_alpha=false;
    return registerTextureImage(asset->path+"\n@gltf-alpha:opaque",std::move(image),error);
}

bool imageTexture(Context *context, int image_index, TextureHandle *out, std::string *error)
{
    if (!context || !out || !context->root || image_index<0) return fail(error,"invalid glTF image index");
    const Json *images=context->root->get("images");
    if (!images || images->type!=Json::Type::Array || static_cast<std::size_t>(image_index)>=images->array.size())
        return fail(error,"glTF texture references invalid image");
    if (context->image_cache.empty()) context->image_cache.assign(images->array.size(),INVALID_TEXTURE);
    TextureHandle& cached=context->image_cache[static_cast<std::size_t>(image_index)];
    if (cached!=INVALID_TEXTURE) { *out=cached; return true; }

    const Json& image=images->array[static_cast<std::size_t>(image_index)];
    if (image.type!=Json::Type::Object) return fail(error,"invalid glTF image");
    const std::string cache_key=context->path+"#image:"+std::to_string(image_index);
    const Json *uri=image.get("uri");
    if (uri && uri->type==Json::Type::String) {
        if (uri->text.starts_with("data:")) {
            std::vector<std::uint8_t> bytes;
            if (!decodeDataUri(uri->text,&bytes,error)) return false;
            cached=loadTextureMemory(cache_key,bytes.data(),bytes.size(),error);
        } else {
            std::string decoded;
            if (!decodedUriPath(uri->text,&decoded,error)) return false;
            cached=loadTexture((context->directory/std::filesystem::path(decoded)).lexically_normal().string(),error);
        }
    } else {
        const int view_index=integer(image.get("bufferView"));
        if (view_index<0 || static_cast<std::size_t>(view_index)>=context->views.size())
            return fail(error,"glTF embedded image has invalid bufferView");
        const BufferView& view=context->views[static_cast<std::size_t>(view_index)];
        const std::uint8_t *data=nullptr;
        if (!rangePointer(*context,view_index,0u,view.length,&data,error)) return false;
        cached=loadTextureMemory(cache_key,data,view.length,error);
    }
    if (cached==INVALID_TEXTURE) return false;
    *out=cached;
    return true;
}

bool textureIndex(Context *context, int texture_index, TextureHandle *out, std::string *error)
{
    if (!context || !out || !context->root || texture_index<0) return fail(error,"invalid glTF texture index");
    const Json *textures=context->root->get("textures");
    if (!textures || textures->type!=Json::Type::Array || static_cast<std::size_t>(texture_index)>=textures->array.size())
        return fail(error,"glTF material references invalid texture");
    const Json& texture_value=textures->array[static_cast<std::size_t>(texture_index)];
    if (texture_value.type!=Json::Type::Object) return fail(error,"invalid glTF texture");
    int source=integer(texture_value.get("source"));
    if (source<0) {
        if (const Json *extensions=texture_value.get("extensions"); extensions && extensions->type==Json::Type::Object) {
            if (const Json *basis=extensions->get("KHR_texture_basisu"); basis && basis->type==Json::Type::Object)
                source=integer(basis->get("source"));
        }
    }
    if (source<0) return fail(error,"glTF texture has no supported image source");
    return imageTexture(context,source,out,error);
}

bool textureInfo(Context *context, const Json *info, TextureHandle *out, std::string *error)
{
    if (!info) { *out=INVALID_TEXTURE; return true; }
    if (info->type!=Json::Type::Object) return fail(error,"invalid glTF texture info");
    const int texcoord=integer(info->get("texCoord"),0);
    if (texcoord!=0) return fail(error,"Horse glTF importer currently supports TEXCOORD_0 only");
    const int index=integer(info->get("index"));
    if (index<0) return fail(error,"glTF texture info has invalid index");
    return textureIndex(context,index,out,error);
}

void texturePath(MaterialData *material, TextureHandle handle, std::string *target)
{
    (void)material;
    if (!target || handle==INVALID_TEXTURE) return;
    if (const TextureAsset *asset=texture(handle)) *target=asset->path;
}

bool parseMaterials(Context *context, std::string *error)
{
    if (!context || !context->root) return false;
    const Json *materials=context->root->get("materials");
    if (!materials) return true;
    if (materials->type!=Json::Type::Array) return fail(error,"glTF materials must be an array");
    context->materials.reserve(materials->array.size());
    for (std::size_t material_index=0u;material_index<materials->array.size();++material_index) {
        const Json& source=materials->array[material_index];
        if (source.type!=Json::Type::Object) return fail(error,"invalid glTF material");
        MaterialData material;
        material.name=stringValue(source.get("name"),"gltf_material_"+std::to_string(material_index));
        material.color={1,1,1}; material.opacity=1.0f; material.roughness=1.0f; material.metallic=1.0f;
        material.ambient_occlusion=1.0f; material.emissive_color={0,0,0}; material.emissive_strength=1.0f;
        material.alpha_cutoff=floatValue(source.get("alphaCutoff"),0.5f);
        material.double_sided=boolValue(source.get("doubleSided"),false);
        const std::string alpha_mode=stringValue(source.get("alphaMode"),"OPAQUE");
        material.alpha_mode=alpha_mode=="MASK"?AlphaMode::Mask:(alpha_mode=="BLEND"?AlphaMode::Blend:AlphaMode::Opaque);

        float base_alpha=1.0f;
        if (const Json *pbr=source.get("pbrMetallicRoughness"); pbr && pbr->type==Json::Type::Object) {
            if (const Json *factor=pbr->get("baseColorFactor"); factor && factor->type==Json::Type::Array && factor->array.size()>=4u) {
                material.color={floatValue(&factor->array[0],1),floatValue(&factor->array[1],1),floatValue(&factor->array[2],1)};
                base_alpha=floatValue(&factor->array[3],1);
            }
            material.metallic=std::clamp(floatValue(pbr->get("metallicFactor"),1.0f),0.0f,1.0f);
            material.roughness=std::clamp(floatValue(pbr->get("roughnessFactor"),1.0f),0.0f,1.0f);
            TextureHandle base=INVALID_TEXTURE;
            if (!textureInfo(context,pbr->get("baseColorTexture"),&base,error)) return false;
            if (base!=INVALID_TEXTURE && material.alpha_mode==AlphaMode::Opaque) {
                base=opaqueTexture(base,error);
                if (base==INVALID_TEXTURE) return false;
            }
            material.diffuse_texture=base;
            texturePath(&material,base,&material.texture_path);

            TextureHandle packed=INVALID_TEXTURE;
            if (!textureInfo(context,pbr->get("metallicRoughnessTexture"),&packed,error)) return false;
            if (packed!=INVALID_TEXTURE) {
                material.roughness_texture=deriveChannel(packed,1,"roughness",error);
                material.metallic_texture=deriveChannel(packed,2,"metallic",error);
                if (material.roughness_texture==INVALID_TEXTURE || material.metallic_texture==INVALID_TEXTURE) return false;
                texturePath(&material,material.roughness_texture,&material.roughness_texture_path);
                texturePath(&material,material.metallic_texture,&material.metallic_texture_path);
            }
        }
        material.opacity=material.alpha_mode==AlphaMode::Opaque?1.0f:std::clamp(base_alpha,0.0f,1.0f);

        TextureHandle normal=INVALID_TEXTURE;
        if (!textureInfo(context,source.get("normalTexture"),&normal,error)) return false;
        material.normal_texture=normal; texturePath(&material,normal,&material.normal_texture_path);

        TextureHandle ao=INVALID_TEXTURE;
        if (!textureInfo(context,source.get("occlusionTexture"),&ao,error)) return false;
        material.ambient_occlusion_texture=ao; texturePath(&material,ao,&material.ambient_occlusion_texture_path);
        if (const Json *occlusion=source.get("occlusionTexture"); occlusion && occlusion->type==Json::Type::Object)
            material.ambient_occlusion=std::clamp(floatValue(occlusion->get("strength"),1.0f),0.0f,1.0f);

        if (const Json *factor=source.get("emissiveFactor"); factor && factor->type==Json::Type::Array && factor->array.size()>=3u)
            material.emissive_color={floatValue(&factor->array[0],0),floatValue(&factor->array[1],0),floatValue(&factor->array[2],0)};
        TextureHandle emissive=INVALID_TEXTURE;
        if (!textureInfo(context,source.get("emissiveTexture"),&emissive,error)) return false;
        material.emissive_texture=emissive; texturePath(&material,emissive,&material.emissive_texture_path);

        if (const Json *extensions=source.get("extensions"); extensions && extensions->type==Json::Type::Object) {
            if (const Json *emissive_strength=extensions->get("KHR_materials_emissive_strength"); emissive_strength && emissive_strength->type==Json::Type::Object)
                material.emissive_strength=std::max(floatValue(emissive_strength->get("emissiveStrength"),1.0f),0.0f);
            if (const Json *ior=extensions->get("KHR_materials_ior"); ior && ior->type==Json::Type::Object)
                material.ior=std::max(floatValue(ior->get("ior"),1.5f),1.0f);
            if (const Json *clearcoat=extensions->get("KHR_materials_clearcoat"); clearcoat && clearcoat->type==Json::Type::Object) {
                material.clearcoat=std::clamp(floatValue(clearcoat->get("clearcoatFactor"),0.0f),0.0f,1.0f);
                material.clearcoat_roughness=std::clamp(floatValue(clearcoat->get("clearcoatRoughnessFactor"),0.0f),0.0f,1.0f);
            }
        }
        context->materials.push_back(std::move(material));
    }
    return true;
}

void computeBounds(MeshData *mesh)
{
    if (!mesh || mesh->vertices.empty()) return;
    mesh->bounds.minimum=mesh->vertices.front().position;
    mesh->bounds.maximum=mesh->vertices.front().position;
    for (const Vertex& vertex:mesh->vertices) {
        mesh->bounds.minimum.x=std::min(mesh->bounds.minimum.x,vertex.position.x);
        mesh->bounds.minimum.y=std::min(mesh->bounds.minimum.y,vertex.position.y);
        mesh->bounds.minimum.z=std::min(mesh->bounds.minimum.z,vertex.position.z);
        mesh->bounds.maximum.x=std::max(mesh->bounds.maximum.x,vertex.position.x);
        mesh->bounds.maximum.y=std::max(mesh->bounds.maximum.y,vertex.position.y);
        mesh->bounds.maximum.z=std::max(mesh->bounds.maximum.z,vertex.position.z);
    }
}

void generateNormals(MeshData *mesh)
{
    if (!mesh) return;
    for (Vertex& vertex:mesh->vertices) vertex.normal={0,0,0};
    for (std::size_t index=0u;index+2u<mesh->indices.size();index+=3u) {
        const std::uint32_t ia=mesh->indices[index],ib=mesh->indices[index+1u],ic=mesh->indices[index+2u];
        if (ia>=mesh->vertices.size() || ib>=mesh->vertices.size() || ic>=mesh->vertices.size()) continue;
        const Vec3 a=mesh->vertices[ia].position,b=mesh->vertices[ib].position,c=mesh->vertices[ic].position;
        const Vec3 ab{b.x-a.x,b.y-a.y,b.z-a.z},ac{c.x-a.x,c.y-a.y,c.z-a.z};
        const Vec3 face{ab.y*ac.z-ab.z*ac.y,ab.z*ac.x-ab.x*ac.z,ab.x*ac.y-ab.y*ac.x};
        for (const std::uint32_t vertex_index:{ia,ib,ic}) {
            Vertex& vertex=mesh->vertices[vertex_index];
            vertex.normal.x+=face.x; vertex.normal.y+=face.y; vertex.normal.z+=face.z;
        }
    }
    for (Vertex& vertex:mesh->vertices) {
        const float length_squared=vertex.normal.x*vertex.normal.x+vertex.normal.y*vertex.normal.y+vertex.normal.z*vertex.normal.z;
        if (length_squared<=1.0e-20f) { vertex.normal={0,1,0}; continue; }
        const float inverse=1.0f/std::sqrt(length_squared);
        vertex.normal.x*=inverse; vertex.normal.y*=inverse; vertex.normal.z*=inverse;
    }
}

bool buildPrimitive(Context& context, const Json& primitive, const Mat4& transform, Part *out, std::string *error)
{
    if (!out || primitive.type!=Json::Type::Object) return false;
    if (integer(primitive.get("mode"),4)!=4) return false;
    const Json *attributes=primitive.get("attributes");
    if (!attributes || attributes->type!=Json::Type::Object) return fail(error,"glTF triangle primitive has no attributes");
    const int position_accessor=integer(attributes->get("POSITION"));
    if (position_accessor<0) return fail(error,"glTF triangle primitive has no POSITION accessor");
    std::vector<float> positions;
    if (!decodeFloats(context,position_accessor,3u,&positions,error)) return false;
    const std::size_t vertex_count=positions.size()/3u;
    if (vertex_count==0u) return false;

    std::vector<float> normals;
    const int normal_accessor=integer(attributes->get("NORMAL"));
    if (normal_accessor>=0) {
        if (!decodeFloats(context,normal_accessor,3u,&normals,error)) return false;
        if (normals.size()/3u!=vertex_count) return fail(error,"glTF NORMAL count does not match POSITION count");
    }
    std::vector<float> uvs;
    const int uv_accessor=integer(attributes->get("TEXCOORD_0"));
    if (uv_accessor>=0) {
        if (!decodeFloats(context,uv_accessor,2u,&uvs,error)) return false;
        if (uvs.size()/2u!=vertex_count) return fail(error,"glTF TEXCOORD_0 count does not match POSITION count");
    }

    MeshData mesh;
    mesh.vertices.resize(vertex_count);
    for (std::size_t index=0u;index<vertex_count;++index) {
        Vertex& vertex=mesh.vertices[index];
        vertex.position=transformPoint(transform,{positions[index*3u],positions[index*3u+1u],positions[index*3u+2u]});
        if (!normals.empty()) vertex.normal=transformNormal(transform,{normals[index*3u],normals[index*3u+1u],normals[index*3u+2u]});
        if (!uvs.empty()) vertex.uv={uvs[index*2u],uvs[index*2u+1u]};
    }

    const int indices_accessor=integer(primitive.get("indices"));
    if (indices_accessor>=0) {
        if (!decodeIndices(context,indices_accessor,&mesh.indices,error)) return false;
    } else {
        if (vertex_count>static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
            return fail(error,"glTF primitive has too many vertices");
        mesh.indices.resize(vertex_count);
        for (std::size_t index=0u;index<vertex_count;++index) mesh.indices[index]=static_cast<std::uint32_t>(index);
    }
    if (mesh.indices.size()%3u!=0u) return fail(error,"glTF TRIANGLES index count is not divisible by three");
    for (const std::uint32_t index:mesh.indices)
        if (index>=mesh.vertices.size()) return fail(error,"glTF primitive index exceeds vertex count");
    if (determinant3(transform)<0.0f)
        for (std::size_t index=0u;index+2u<mesh.indices.size();index+=3u) std::swap(mesh.indices[index+1u],mesh.indices[index+2u]);
    if (normals.empty()) generateNormals(&mesh);
    computeBounds(&mesh);

    const int material_index=integer(primitive.get("material"));
    MaterialData material;
    material.roughness=1.0f; material.metallic=1.0f;
    if (material_index>=0) {
        if (static_cast<std::size_t>(material_index)>=context.materials.size()) return fail(error,"glTF primitive references invalid material");
        material=context.materials[static_cast<std::size_t>(material_index)];
    }
    out->mesh=std::move(mesh);
    out->material=std::move(material);
    return true;
}

bool appendMesh(Context& context, int mesh_index, const Mat4& transform, Document *output, std::string *error)
{
    const Json *meshes=context.root->get("meshes");
    if (!meshes || meshes->type!=Json::Type::Array || mesh_index<0 || static_cast<std::size_t>(mesh_index)>=meshes->array.size())
        return fail(error,"glTF node references invalid mesh");
    const Json& mesh=meshes->array[static_cast<std::size_t>(mesh_index)];
    const Json *primitives=mesh.get("primitives");
    if (!primitives || primitives->type!=Json::Type::Array) return fail(error,"glTF mesh has no primitives");
    for (const Json& primitive:primitives->array) {
        if (primitive.type!=Json::Type::Object) continue;
        if (integer(primitive.get("mode"),4)!=4) continue;
        Part part;
        if (!buildPrimitive(context,primitive,transform,&part,error)) return false;
        if (!part.mesh.indices.empty()) output->parts.push_back(std::move(part));
    }
    return true;
}

bool visitNode(Context& context, int node_index, const Mat4& parent, std::vector<std::uint8_t>& visiting,
    Document *output, std::string *error)
{
    const Json *nodes=context.root->get("nodes");
    if (!nodes || nodes->type!=Json::Type::Array || node_index<0 || static_cast<std::size_t>(node_index)>=nodes->array.size())
        return fail(error,"glTF scene references invalid node");
    const std::size_t index=static_cast<std::size_t>(node_index);
    if (visiting[index]!=0u) return fail(error,"cycle detected in glTF node hierarchy");
    visiting[index]=1u;
    const Json& node=nodes->array[index];
    if (node.type!=Json::Type::Object) return fail(error,"invalid glTF node");
    const Mat4 world=multiply(parent,nodeMatrix(node));
    const int mesh_index=integer(node.get("mesh"));
    if (mesh_index>=0 && !appendMesh(context,mesh_index,world,output,error)) return false;
    if (const Json *children=node.get("children"); children) {
        if (children->type!=Json::Type::Array) return fail(error,"glTF node children must be an array");
        for (const Json& child:children->array) {
            const int child_index=integer(&child);
            if (child_index<0 || !visitNode(context,child_index,world,visiting,output,error)) return false;
        }
    }
    visiting[index]=0u;
    return true;
}

bool buildScene(Context& context, Document *output, std::string *error)
{
    const Json *nodes=context.root->get("nodes");
    if (!nodes || nodes->type!=Json::Type::Array) return fail(error,"glTF has no nodes array");
    std::vector<int> roots;
    const Json *scenes=context.root->get("scenes");
    if (scenes && scenes->type==Json::Type::Array && !scenes->array.empty()) {
        int scene_index=integer(context.root->get("scene"),0);
        if (scene_index<0 || static_cast<std::size_t>(scene_index)>=scenes->array.size()) return fail(error,"glTF default scene index is invalid");
        const Json& scene=scenes->array[static_cast<std::size_t>(scene_index)];
        const Json *scene_nodes=scene.get("nodes");
        if (scene_nodes && scene_nodes->type==Json::Type::Array)
            for (const Json& value:scene_nodes->array) {
                const int index=integer(&value);
                if (index>=0) roots.push_back(index);
            }
    } else {
        std::vector<std::uint8_t> is_child(nodes->array.size(),0u);
        for (const Json& node:nodes->array) {
            if (node.type!=Json::Type::Object) continue;
            if (const Json *children=node.get("children"); children && children->type==Json::Type::Array)
                for (const Json& child:children->array) {
                    const int index=integer(&child);
                    if (index>=0 && static_cast<std::size_t>(index)<is_child.size()) is_child[static_cast<std::size_t>(index)]=1u;
                }
        }
        for (std::size_t index=0u;index<is_child.size();++index) if (is_child[index]==0u) roots.push_back(static_cast<int>(index));
    }
    std::vector<std::uint8_t> visiting(nodes->array.size(),0u);
    for (const int root:roots) if (!visitNode(context,root,identity(),visiting,output,error)) return false;
    return true;
}

} // namespace

bool load(const std::string& path, Formats::Document *output, std::string *error)
{
    if (error) error->clear();
    if (!output) return fail(error,"null glTF output");
    output->parts.clear(); output->skeleton={}; output->animations.clear(); output->has_skeleton=false;

    std::string json_text;
    std::vector<std::uint8_t> binary_chunk;
    if (!parseContainer(path,&json_text,&binary_chunk,error)) return false;
    Json root;
    JsonParser parser(json_text.data(),json_text.size());
    if (!parser.parse(&root,error)) return false;
    if (root.type!=Json::Type::Object) return fail(error,"glTF root must be an object");
    const Json *asset=root.get("asset");
    const std::string version=asset && asset->type==Json::Type::Object?stringValue(asset->get("version")):std::string{};
    if (version.empty() || version[0]!='2') return fail(error,"Horse supports glTF 2.x assets only");

    Context context;
    context.path=path;
    context.directory=std::filesystem::path(path).parent_path();
    context.root=&root;
    if (!loadBuffers(&context,binary_chunk,error) || !parseViews(&context,error) || !parseAccessors(&context,error) ||
        !parseMaterials(&context,error) || !buildScene(context,output,error)) return false;
    if (output->parts.empty()) return fail(error,"glTF contains no supported triangle primitives: "+path);
    return true;
}

} // namespace Models::Formats::Gltf
