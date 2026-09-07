#include "Models/Formats/FbxDocument.hpp"

#include "Models/Formats/FbxAscii.hpp"
#include "Models/Formats/FbxBinary.hpp"

#include <fstream>
#include <limits>
#include <type_traits>
#include <vector>

namespace Models::FbxDocument {
namespace {

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

} // namespace

std::int64_t Property::asInt64(std::int64_t fallback) const
{
    return std::visit(
        [fallback](const auto& item) -> std::int64_t {
            using T = std::decay_t<decltype(item)>;
            if constexpr (
                std::is_same_v<T, std::int16_t> ||
                std::is_same_v<T, std::int32_t> ||
                std::is_same_v<T, std::int64_t>)
            {
                return static_cast<std::int64_t>(item);
            } else if constexpr (std::is_same_v<T, bool>) {
                return item ? 1 : 0;
            } else if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
                return static_cast<std::int64_t>(item);
            } else {
                return fallback;
            }
        },
        value
    );
}

double Property::asDouble(double fallback) const
{
    return std::visit(
        [fallback](const auto& item) -> double {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_arithmetic_v<T>) return static_cast<double>(item);
            return fallback;
        },
        value
    );
}

std::string Property::asString(std::string fallback) const
{
    if (const auto *item = std::get_if<std::string>(&value)) return *item;
    return fallback;
}

const Bytes *Property::asBytes() const
{
    return std::get_if<Bytes>(&value);
}

const std::vector<double> *Property::asDoubleArray() const
{
    return std::get_if<std::vector<double>>(&value);
}

const std::vector<float> *Property::asFloatArray() const
{
    return std::get_if<std::vector<float>>(&value);
}

const std::vector<std::int32_t> *Property::asInt32Array() const
{
    return std::get_if<std::vector<std::int32_t>>(&value);
}

const std::vector<std::int64_t> *Property::asInt64Array() const
{
    return std::get_if<std::vector<std::int64_t>>(&value);
}

const Node *Node::child(const std::string& child_name) const
{
    for (const Node& candidate : children) {
        if (candidate.name == child_name) return &candidate;
    }
    return nullptr;
}

std::vector<const Node *> Node::childrenNamed(const std::string& child_name) const
{
    std::vector<const Node *> result;
    for (const Node& candidate : children) {
        if (candidate.name == child_name) result.push_back(&candidate);
    }
    return result;
}

std::vector<double> Node::numericArray() const
{
    if (properties.empty()) return {};
    if (const auto *array = properties[0].asDoubleArray()) return *array;
    if (const auto *array = properties[0].asFloatArray()) return std::vector<double>(array->begin(), array->end());
    if (const auto *array = properties[0].asInt32Array()) return std::vector<double>(array->begin(), array->end());
    if (const auto *array = properties[0].asInt64Array()) return std::vector<double>(array->begin(), array->end());

    std::vector<double> result;
    result.reserve(properties.size());
    for (const Property& property : properties) result.push_back(property.asDouble());
    return result;
}

bool parseMemory(
    const std::uint8_t *data,
    std::size_t size,
    RawDocument *out,
    std::string *error)
{
    if (error) error->clear();
    if (!data || !out) return fail(error, "invalid FBX input");
    if (FbxBinary::matches(data, size)) return FbxBinary::parse(data, size, out, error);
    return FbxAscii::parse(data, size, out, error);
}

bool parseFile(const std::string& path, RawDocument *out, std::string *error)
{
    if (error) error->clear();
    if (!out) return fail(error, "null FBX document output");
    std::ifstream file(path, std::ios::binary);
    if (!file) return fail(error, "failed to open FBX file: " + path);
    file.seekg(0, std::ios::end);
    const std::streamoff length = file.tellg();
    if (length < 0) return fail(error, "failed to size FBX file: " + path);
    if (static_cast<std::uint64_t>(length) > std::numeric_limits<std::size_t>::max()) {
        return fail(error, "FBX file is too large: " + path);
    }
    file.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    if (!bytes.empty()) file.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file && !bytes.empty()) return fail(error, "failed to read FBX file: " + path);
    return parseMemory(bytes.data(), bytes.size(), out, error);
}

} // namespace Models::FbxDocument
