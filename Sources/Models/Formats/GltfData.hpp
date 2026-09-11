#ifndef HORSE_MODELS_FORMATS_GLTF_DATA_HPP
#define HORSE_MODELS_FORMATS_GLTF_DATA_HPP

#include "Models/Formats/GltfJson.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Models::Formats::GltfData {

struct BufferView {
    int buffer = -1;
    std::size_t offset = 0u;
    std::size_t length = 0u;
    std::size_t stride = 0u;
    int target = 0;
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

struct Context {
    std::string path;
    std::filesystem::path directory;
    const GltfJson::Value *root = nullptr;
    std::vector<std::vector<std::uint8_t>> buffers;
    std::vector<BufferView> views;
    std::vector<Accessor> accessors;
};

bool readFile(const std::filesystem::path& path, std::vector<std::uint8_t> *out, std::string *error = nullptr);
bool parseContainer(
    const std::string& path,
    std::string *json,
    std::vector<std::uint8_t> *binary,
    std::string *error = nullptr
);
bool decodeDataUri(std::string_view uri, std::vector<std::uint8_t> *out, std::string *error = nullptr);
bool decodeUriPath(std::string_view uri, std::string *out, std::string *error = nullptr);

bool loadBuffers(Context *context, const std::vector<std::uint8_t>& binary, std::string *error = nullptr);
bool loadViews(Context *context, std::string *error = nullptr);
bool loadAccessors(Context *context, std::string *error = nullptr);

std::size_t componentSize(int component_type);
std::size_t componentCount(std::string_view type);
bool decodeFloats(
    const Context& context,
    int accessor,
    std::vector<float> *out,
    std::size_t expected_components = 0u,
    std::string *error = nullptr
);
bool decodeUnsigned(
    const Context& context,
    int accessor,
    std::vector<std::uint32_t> *out,
    std::size_t expected_components = 0u,
    std::string *error = nullptr
);

} // namespace Models::Formats::GltfData

#endif
