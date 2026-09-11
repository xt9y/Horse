#ifndef HORSE_MODELS_FORMATS_GLTF_JSON_HPP
#define HORSE_MODELS_FORMATS_GLTF_JSON_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Models::Formats::GltfJson {

enum class Type : std::uint8_t {
    Null,
    Boolean,
    Number,
    String,
    Array,
    Object,
};

struct Value {
    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<Value> array;
    std::unordered_map<std::string, Value> object;

    const Value *get(std::string_view key) const;
    bool is(Type expected) const { return type == expected; }
};

bool parse(std::string_view text, Value *out, std::string *error = nullptr);
std::string stringify(const Value& value);

int integer(const Value *value, int fallback = -1);
std::uint32_t unsignedInteger(const Value *value, std::uint32_t fallback = UINT32_MAX);
std::size_t sizeValue(const Value *value, std::size_t fallback = 0u);
float floatValue(const Value *value, float fallback = 0.0f);
bool boolValue(const Value *value, bool fallback = false);
std::string stringValue(const Value *value, std::string fallback = {});

} // namespace Models::Formats::GltfJson

#endif
