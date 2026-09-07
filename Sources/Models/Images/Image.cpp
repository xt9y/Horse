#include "Models/Images/Image.hpp"

#include "Models/Images/Jpeg.hpp"
#include "Models/Images/Png.hpp"
#include "Models/Images/Tga.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace Models::Images {
namespace {

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

std::string lowerExtension(const std::string& path)
{
    std::string extension = std::filesystem::path(path).extension().string();
    std::transform(
        extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); }
    );
    return extension;
}

bool decodeMemory(
    const std::uint8_t *data,
    std::size_t size,
    const std::string& hint,
    Image *image,
    std::string *error)
{
    if (!data || size == 0u) return fail(error, "empty image data");
    if (Png::matches(data, size)) return Png::decode(data, size, image, error);
    if (Jpeg::matches(data, size)) return Jpeg::decode(data, size, image, error);
    if (Models::Tga::matches(data, size) || lowerExtension(hint) == ".tga") {
        return Models::Tga::decode(data, size, image, error);
    }
    return fail(error, "unsupported or invalid image data" + (hint.empty() ? std::string{} : ": " + hint));
}

} // namespace

bool load(const std::string& path, Image *image, std::string *error)
{
    if (error) error->clear();
    if (!image) return fail(error, "null image output");
    if (path.empty()) return fail(error, "empty image path");

    std::ifstream input(path, std::ios::binary);
    if (!input) return fail(error, "failed to open image: " + path);
    input.seekg(0, std::ios::end);
    const std::streamoff length = input.tellg();
    if (length < 0) return fail(error, "failed to size image: " + path);
    if (static_cast<std::uint64_t>(length) > std::numeric_limits<std::size_t>::max()) {
        return fail(error, "image file is too large: " + path);
    }
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    if (!bytes.empty()) input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input && !bytes.empty()) return fail(error, "failed to read image: " + path);
    return decodeMemory(bytes.data(), bytes.size(), path, image, error);
}

bool loadMemory(
    const void *data,
    std::size_t size,
    Image *image,
    std::string *error)
{
    if (error) error->clear();
    if (!image) return fail(error, "null image output");
    return decodeMemory(static_cast<const std::uint8_t *>(data), size, {}, image, error);
}

} // namespace Models::Images
