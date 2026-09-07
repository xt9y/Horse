#include "Models/Images/Image.hpp"

#include "Models/Images/Tga.hpp"

#include <stb_image.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <limits>
#include <string>
#include <utility>

namespace Models::Images {
namespace {

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

bool finishDecode(
    unsigned char *pixels,
    int width,
    int height,
    Image *image,
    std::string *error)
{
    if (!pixels) {
        const char *reason = stbi_failure_reason();
        return fail(error, reason ? reason : "image decode failed");
    }

    if (width <= 0 || height <= 0) {
        stbi_image_free(pixels);
        return fail(error, "decoded image has invalid dimensions");
    }

    const std::uint64_t pixel_count =
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    if (pixel_count > std::numeric_limits<std::size_t>::max() / 4u) {
        stbi_image_free(pixels);
        return fail(error, "decoded image dimensions overflow output buffer");
    }

    Image decoded;
    decoded.width = width;
    decoded.height = height;
    decoded.rgba.assign(
        pixels,
        pixels + static_cast<std::size_t>(pixel_count) * 4u
    );

    for (std::size_t offset = 3u; offset < decoded.rgba.size(); offset += 4u) {
        if (decoded.rgba[offset] != 255u) {
            decoded.meaningful_alpha = true;
            break;
        }
    }

    stbi_image_free(pixels);
    *image = std::move(decoded);
    return true;
}

std::string lowerExtension(const std::string& path)
{
    std::string extension = std::filesystem::path(path).extension().string();
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); }
    );
    return extension;
}

} // namespace

bool load(const std::string& path, Image *image, std::string *error)
{
    if (error) error->clear();
    if (!image) return fail(error, "null image output");
    if (path.empty()) return fail(error, "empty image path");

    if (lowerExtension(path) == ".tga") {
        std::string tga_error;
        if (Tga::load(path, image, &tga_error)) return true;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char *pixels = stbi_load(
        path.c_str(),
        &width,
        &height,
        &channels,
        STBI_rgb_alpha
    );
    (void)channels;

    if (!pixels) {
        const char *reason = stbi_failure_reason();
        return fail(
            error,
            "cannot decode image: " + path +
            (reason ? std::string(": ") + reason : std::string{})
        );
    }

    return finishDecode(pixels, width, height, image, error);
}

bool loadMemory(
    const void *data,
    std::size_t size,
    Image *image,
    std::string *error)
{
    if (error) error->clear();
    if (!image) return fail(error, "null image output");
    if (!data || size == 0u) return fail(error, "empty embedded image");
    if (size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return fail(error, "embedded image is too large to decode");
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char *pixels = stbi_load_from_memory(
        static_cast<const stbi_uc *>(data),
        static_cast<int>(size),
        &width,
        &height,
        &channels,
        STBI_rgb_alpha
    );
    (void)channels;

    return finishDecode(pixels, width, height, image, error);
}

} // namespace Models::Images
