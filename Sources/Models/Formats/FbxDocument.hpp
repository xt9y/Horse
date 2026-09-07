#ifndef RW_ENGINE_MODELS_FORMATS_FBX_DOCUMENT_HPP
#define RW_ENGINE_MODELS_FORMATS_FBX_DOCUMENT_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace Models::FbxDocument {

using Bytes = std::vector<std::uint8_t>;
using Value = std::variant<
    std::int16_t,
    bool,
    std::int32_t,
    float,
    double,
    std::int64_t,
    std::string,
    Bytes,
    std::vector<float>,
    std::vector<double>,
    std::vector<std::int32_t>,
    std::vector<std::int64_t>
>;

struct Property {
    char type = 0;
    Value value = std::int32_t{0};

    std::int64_t asInt64(std::int64_t fallback = 0) const;
    double asDouble(double fallback = 0.0) const;
    std::string asString(std::string fallback = {}) const;
    const Bytes *asBytes() const;
    const std::vector<double> *asDoubleArray() const;
    const std::vector<float> *asFloatArray() const;
    const std::vector<std::int32_t> *asInt32Array() const;
    const std::vector<std::int64_t> *asInt64Array() const;
};

struct Node {
    std::string name;
    std::vector<Property> properties;
    std::vector<Node> children;

    const Node *child(const std::string& child_name) const;
    std::vector<const Node *> childrenNamed(const std::string& child_name) const;
    std::vector<double> numericArray() const;
};

struct RawDocument {
    std::uint32_t version = 0u;
    bool binary = false;
    Node root;
};

} // namespace Models::FbxDocument

#endif
