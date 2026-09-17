#include "Models/Formats/GltfDependencies.hpp"

#include "Models/Formats/GltfData.hpp"
#include "Models/Formats/GltfJson.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace Models::Formats::GltfDependencies {
namespace {

using GltfJson::Type;
using GltfJson::Value;

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

void appendUnique(std::vector<std::string> *dependencies, std::filesystem::path path)
{
    if (!dependencies) return;
    std::error_code ec;
    const std::filesystem::path absolute = std::filesystem::absolute(path, ec);
    const std::string normalized = (ec ? path : absolute).lexically_normal().string();
    if (std::find(dependencies->begin(), dependencies->end(), normalized) == dependencies->end())
        dependencies->push_back(normalized);
}

} // namespace

bool apply(const std::string& path, Document *document, std::string *error)
{
    if (error) error->clear();
    if (!document) return fail(error, "null glTF dependency document");

    std::string json;
    std::vector<std::uint8_t> binary;
    if (!GltfData::parseContainer(path, &json, &binary, error)) return false;

    Value root;
    if (!GltfJson::parse(json, &root, error)) return false;
    if (!root.is(Type::Object)) return fail(error, "glTF root must be an object");

    const Value *buffers = root.get("buffers");
    if (!buffers) return true;
    if (!buffers->is(Type::Array)) return fail(error, "glTF buffers must be an array");

    const std::filesystem::path directory = std::filesystem::path(path).parent_path();
    for (const Value& buffer : buffers->array) {
        if (!buffer.is(Type::Object)) return fail(error, "invalid glTF buffer object");
        const Value *uri = buffer.get("uri");
        if (!uri) continue;
        if (!uri->is(Type::String)) return fail(error, "glTF buffer URI must be a string");
        if (uri->string.starts_with("data:")) continue;

        std::string decoded;
        if (!GltfData::decodeUriPath(uri->string, &decoded, error)) return false;
        std::filesystem::path dependency(decoded);
        if (dependency.is_relative()) dependency = directory / dependency;
        appendUnique(&document->dependencies, std::move(dependency));
    }

    return true;
}

} // namespace Models::Formats::GltfDependencies
