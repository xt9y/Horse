#include "Models/Formats/GltfRequiredExtensions.hpp"

#include "Models/Formats/GltfData.hpp"
#include "Models/Formats/GltfDraco.hpp"
#include "Models/Formats/GltfJson.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace Models::Formats::GltfRequiredExtensions {
namespace {

using GltfJson::Type;
using GltfJson::Value;

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

Value *member(Value *value, std::string_view name)
{
    if (!value || !value->is(Type::Object)) return nullptr;
    const auto found = value->object.find(std::string(name));
    return found == value->object.end() ? nullptr : &found->second;
}

Value string(std::string value)
{
    Value result;
    result.type = Type::String;
    result.string = std::move(value);
    return result;
}

Value number(double value)
{
    Value result;
    result.type = Type::Number;
    result.number = value;
    return result;
}

void set(Value *value, std::string name, Value field)
{
    if (value && value->is(Type::Object))
        value->object.insert_or_assign(std::move(name), std::move(field));
}

bool implementedRequired(std::string_view name)
{
    return name == "KHR_gaussian_splatting";
}

bool needsMaterialization(const Value& root)
{
    const Value *required = root.get("extensionsRequired");
    if (!required || !required->is(Type::Array)) return false;
    for (const Value& extension : required->array)
        if (extension.is(Type::String) && implementedRequired(extension.string)) return true;
    return false;
}

std::string base64(const std::vector<std::uint8_t>& bytes)
{
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    if (bytes.size() <= (std::numeric_limits<std::size_t>::max() - 2u) / 3u)
        output.reserve(((bytes.size() + 2u) / 3u) * 4u);
    for (std::size_t index = 0u; index < bytes.size(); index += 3u) {
        const std::uint32_t a = bytes[index];
        const std::uint32_t b = index + 1u < bytes.size() ? bytes[index + 1u] : 0u;
        const std::uint32_t c = index + 2u < bytes.size() ? bytes[index + 2u] : 0u;
        const std::uint32_t packed = (a << 16u) | (b << 8u) | c;
        output.push_back(alphabet[(packed >> 18u) & 63u]);
        output.push_back(alphabet[(packed >> 12u) & 63u]);
        output.push_back(index + 1u < bytes.size() ? alphabet[(packed >> 6u) & 63u] : '=');
        output.push_back(index + 2u < bytes.size() ? alphabet[packed & 63u] : '=');
    }
    return output;
}

std::string dataUri(const std::vector<std::uint8_t>& bytes)
{
    return "data:application/octet-stream;base64," + base64(bytes);
}

bool embedBuffers(Value *root, const GltfData::Context& context, std::string *error)
{
    Value *buffers = member(root, "buffers");
    if (!buffers) return true;
    if (!buffers->is(Type::Array) || buffers->array.size() != context.buffers.size())
        return fail(error, "invalid glTF buffers while materializing required extension");
    for (std::size_t index = 0u; index < buffers->array.size(); ++index) {
        Value& buffer = buffers->array[index];
        if (!buffer.is(Type::Object)) return fail(error, "invalid glTF buffer object");
        set(&buffer, "uri", string(dataUri(context.buffers[index])));
        set(&buffer, "byteLength", number(static_cast<double>(context.buffers[index].size())));
    }
    return true;
}

bool rewriteImages(Value *root, const std::filesystem::path& directory, std::string *error)
{
    Value *images = member(root, "images");
    if (!images) return true;
    if (!images->is(Type::Array)) return fail(error, "glTF images must be an array");
    for (Value& image : images->array) {
        Value *uri = member(&image, "uri");
        if (!uri) continue;
        if (!uri->is(Type::String)) return fail(error, "glTF image URI must be a string");
        if (uri->string.starts_with("data:")) continue;
        std::string decoded;
        if (!GltfData::decodeUriPath(uri->string, &decoded, error)) return false;
        std::filesystem::path source(decoded);
        if (source.is_relative()) source = (directory / source).lexically_normal();
        uri->string = source.generic_string();
    }
    return true;
}

void filterRequired(Value *root)
{
    Value *required = member(root, "extensionsRequired");
    if (!required || !required->is(Type::Array)) return;
    std::erase_if(required->array, [](const Value& extension) {
        return extension.is(Type::String) && implementedRequired(extension.string);
    });
}

class TemporaryFile {
public:
    explicit TemporaryFile(std::filesystem::path path) : path_(std::move(path)) {}
    ~TemporaryFile()
    {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

bool writeTemporary(
    const std::string& source,
    const std::string& json,
    std::filesystem::path *path,
    std::string *error)
{
    if (!path) return false;
    std::error_code ec;
    std::filesystem::path directory = std::filesystem::temp_directory_path(ec);
    if (ec) directory = std::filesystem::path(source).parent_path();
    static std::atomic<std::uint64_t> sequence {0u};
    const std::uint64_t id = sequence.fetch_add(1u, std::memory_order_relaxed);
    const std::size_t hash = std::hash<std::string>{}(source);
    *path = directory / ("horse-gltf-required-" + std::to_string(hash) + "-" + std::to_string(id) + ".gltf");
    std::ofstream file(*path, std::ios::binary | std::ios::trunc);
    if (!file) return fail(error, "failed to create temporary glTF for required extension");
    file.write(json.data(), static_cast<std::streamsize>(json.size()));
    if (!file) return fail(error, "failed to write temporary glTF for required extension");
    return true;
}

} // namespace

bool load(const std::string& path, Document *output, std::string *error)
{
    if (error) error->clear();
    if (!output) return fail(error, "null glTF output");

    std::string json;
    std::vector<std::uint8_t> binary;
    if (!GltfData::parseContainer(path, &json, &binary, error)) return false;

    Value root;
    if (!GltfJson::parse(json, &root, error)) return false;
    if (!root.is(Type::Object)) return fail(error, "glTF root must be an object");
    if (!needsMaterialization(root)) return GltfDraco::load(path, output, error);

    GltfData::Context context;
    context.path = path;
    context.directory = std::filesystem::path(path).parent_path();
    context.root = &root;
    if (!GltfData::loadBuffers(&context, binary, error)) return false;
    if (!embedBuffers(&root, context, error) || !rewriteImages(&root, context.directory, error))
        return false;
    filterRequired(&root);

    const std::string materialized = GltfJson::stringify(root);
    std::filesystem::path temporary_path;
    if (!writeTemporary(path, materialized, &temporary_path, error)) return false;
    TemporaryFile temporary(std::move(temporary_path));
    return GltfDraco::load(temporary.path().string(), output, error);
}

} // namespace Models::Formats::GltfRequiredExtensions
