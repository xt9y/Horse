#include "Models/Formats/FbxDocument.hpp"

#include <type_traits>

namespace Models::FbxDocument {

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

} // namespace Models::FbxDocument
