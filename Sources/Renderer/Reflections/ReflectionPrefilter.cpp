#include "Renderer/Internal/ReflectionPrefilter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Renderer::Reflections::Internal {
namespace {

constexpr float Pi = 3.14159265358979323846f;
constexpr float Y1 = 0.48860251190f;

struct Vec3f {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct LinearImage {
    int width = 0;
    int height = 0;
    std::vector<float> rgb;
};

Vec3f add(Vec3f a, Vec3f b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3f multiply(Vec3f value, float scalar)
{
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

float dot(Vec3f a, Vec3f b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3f cross(Vec3f a, Vec3f b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

Vec3f normalize(Vec3f value)
{
    const float length_squared = dot(value, value);
    if (length_squared <= 1.0e-12f) return {0.0f, 1.0f, 0.0f};
    return multiply(value, 1.0f / std::sqrt(length_squared));
}

float srgbToLinear(float value)
{
    value = std::clamp(value, 0.0f, 1.0f);
    return value <= 0.04045f
        ? value / 12.92f
        : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

float linearToSrgb(float value)
{
    value = std::max(value, 0.0f);
    return value <= 0.0031308f
        ? value * 12.92f
        : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}

std::uint8_t toByte(float value)
{
    return static_cast<std::uint8_t>(
        std::clamp(value * 255.0f + 0.5f, 0.0f, 255.0f));
}

LinearImage linearize(const Models::Images::Image& source)
{
    LinearImage result;
    if (source.width <= 0 || source.height <= 0 || source.rgba.empty()) return result;

    result.width = source.width;
    result.height = source.height;
    const std::size_t pixel_count =
        static_cast<std::size_t>(result.width) * static_cast<std::size_t>(result.height);
    result.rgb.resize(pixel_count * 3u, 0.0f);

    for (std::size_t pixel = 0u; pixel < pixel_count; ++pixel) {
        const std::size_t source_offset = pixel * 4u;
        const std::size_t destination_offset = pixel * 3u;
        if (source_offset + 2u >= source.rgba.size()) break;
        result.rgb[destination_offset + 0u] =
            srgbToLinear(static_cast<float>(source.rgba[source_offset + 0u]) / 255.0f);
        result.rgb[destination_offset + 1u] =
            srgbToLinear(static_cast<float>(source.rgba[source_offset + 1u]) / 255.0f);
        result.rgb[destination_offset + 2u] =
            srgbToLinear(static_cast<float>(source.rgba[source_offset + 2u]) / 255.0f);
    }
    return result;
}

std::array<float, 3> sourcePixel(const LinearImage& source, int x, int y)
{
    if (source.width <= 0 || source.height <= 0 || source.rgb.empty()) return {};

    x %= source.width;
    if (x < 0) x += source.width;
    y = std::clamp(y, 0, source.height - 1);
    const std::size_t offset =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(source.width) +
         static_cast<std::size_t>(x)) * 3u;
    if (offset + 2u >= source.rgb.size()) return {};

    return {
        source.rgb[offset + 0u],
        source.rgb[offset + 1u],
        source.rgb[offset + 2u],
    };
}

std::array<float, 3> sampleEquirect(const LinearImage& source, Vec3f direction)
{
    if (source.width <= 0 || source.height <= 0 || source.rgb.empty()) return {};

    direction = normalize(direction);
    const float u = std::atan2(direction.z, direction.x) / (2.0f * Pi) + 0.5f;
    const float v = std::acos(std::clamp(direction.y, -1.0f, 1.0f)) / Pi;
    const float x = u * static_cast<float>(source.width) - 0.5f;
    const float y = v * static_cast<float>(source.height) - 0.5f;
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int x1 = x0 + 1;
    const int y1 = y0 + 1;
    const float tx = x - std::floor(x);
    const float ty = y - std::floor(y);

    const auto p00 = sourcePixel(source, x0, y0);
    const auto p10 = sourcePixel(source, x1, y0);
    const auto p01 = sourcePixel(source, x0, y1);
    const auto p11 = sourcePixel(source, x1, y1);

    std::array<float, 3> result{};
    for (std::size_t channel = 0u; channel < result.size(); ++channel) {
        const float a = p00[channel] + (p10[channel] - p00[channel]) * tx;
        const float b = p01[channel] + (p11[channel] - p01[channel]) * tx;
        result[channel] = a + (b - a) * ty;
    }
    return result;
}

Vec3f directionForPixel(
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t width,
    std::uint32_t height)
{
    const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(width);
    const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(height);
    const float phi = (u - 0.5f) * 2.0f * Pi;
    const float theta = v * Pi;
    const float sin_theta = std::sin(theta);
    return normalize({
        std::cos(phi) * sin_theta,
        std::cos(theta),
        std::sin(phi) * sin_theta,
    });
}

float radicalInverse(std::uint32_t bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return static_cast<float>(bits) * 2.3283064365386963e-10f;
}

std::array<float, 2> hammersley(std::uint32_t index, std::uint32_t count)
{
    return {
        static_cast<float>(index) / static_cast<float>(std::max(count, 1u)),
        radicalInverse(index),
    };
}

Vec3f importanceSampleGgx(std::array<float, 2> xi, Vec3f normal, float roughness)
{
    const float alpha = std::max(roughness * roughness, 1.0e-4f);
    const float alpha_squared = alpha * alpha;
    const float phi = 2.0f * Pi * xi[0];
    const float cos_theta = std::sqrt(
        std::max((1.0f - xi[1]) /
            (1.0f + (alpha_squared - 1.0f) * xi[1]), 0.0f));
    const float sin_theta = std::sqrt(std::max(1.0f - cos_theta * cos_theta, 0.0f));
    const Vec3f tangent_space {
        std::cos(phi) * sin_theta,
        std::sin(phi) * sin_theta,
        cos_theta,
    };

    const Vec3f up = std::abs(normal.y) < 0.999f
        ? Vec3f{0.0f, 1.0f, 0.0f}
        : Vec3f{1.0f, 0.0f, 0.0f};
    const Vec3f tangent = normalize(cross(up, normal));
    const Vec3f bitangent = cross(normal, tangent);
    return normalize(add(
        add(multiply(tangent, tangent_space.x), multiply(bitangent, tangent_space.y)),
        multiply(normal, tangent_space.z)));
}

std::array<float, 3> prefilterDirection(
    const LinearImage& source,
    Vec3f normal,
    float roughness,
    std::uint32_t sample_count)
{
    if (roughness <= 1.0e-5f) return sampleEquirect(source, normal);

    const Vec3f view = normal;
    std::array<float, 3> result{};
    float total_weight = 0.0f;

    for (std::uint32_t sample = 0u; sample < sample_count; ++sample) {
        const Vec3f half_vector = importanceSampleGgx(
            hammersley(sample, sample_count),
            normal,
            roughness);
        const Vec3f light = normalize(add(
            multiply(half_vector, 2.0f * dot(view, half_vector)),
            multiply(view, -1.0f)));
        const float no_l = std::max(dot(normal, light), 0.0f);
        if (no_l <= 0.0f) continue;

        const auto sampled = sampleEquirect(source, light);
        for (std::size_t channel = 0u; channel < result.size(); ++channel)
            result[channel] += sampled[channel] * no_l;
        total_weight += no_l;
    }

    if (total_weight <= 1.0e-8f) return sampleEquirect(source, normal);
    for (float& channel : result) channel /= total_weight;
    return result;
}

PrefilterLevel prefilterLevel(
    const LinearImage& source,
    std::uint32_t width,
    std::uint32_t height,
    float roughness,
    std::uint32_t sample_count)
{
    PrefilterLevel result;
    result.width = std::max(width, 1u);
    result.height = std::max(height, 1u);
    result.rgba.resize(
        static_cast<std::size_t>(result.width) * static_cast<std::size_t>(result.height) * 4u,
        0u);

    for (std::uint32_t y = 0u; y < result.height; ++y) {
        for (std::uint32_t x = 0u; x < result.width; ++x) {
            const auto color = prefilterDirection(
                source,
                directionForPixel(x, y, result.width, result.height),
                roughness,
                sample_count);
            const std::size_t offset =
                (static_cast<std::size_t>(y) * result.width + x) * 4u;
            result.rgba[offset + 0u] = toByte(linearToSrgb(color[0]));
            result.rgba[offset + 1u] = toByte(linearToSrgb(color[1]));
            result.rgba[offset + 2u] = toByte(linearToSrgb(color[2]));
            result.rgba[offset + 3u] = 255u;
        }
    }
    return result;
}

} // namespace

std::uint32_t prefilterSampleCount(Quality quality)
{
    switch (quality) {
        case Quality::Low: return 8u;
        case Quality::Medium: return 16u;
        case Quality::High: return 32u;
        case Quality::Ultra: return 64u;
    }
    return 32u;
}

IrradianceSH irradianceEquirectangular(const Models::Images::Image& source)
{
    IrradianceSH result;
    const LinearImage linear_source = linearize(source);
    if (linear_source.width <= 0 || linear_source.height <= 0 || linear_source.rgb.empty())
        return result;

    const std::uint32_t width = static_cast<std::uint32_t>(linear_source.width);
    const std::uint32_t height = static_cast<std::uint32_t>(linear_source.height);
    const float delta_phi = 2.0f * Pi / static_cast<float>(width);
    const float delta_theta = Pi / static_cast<float>(height);
    float total_weight = 0.0f;

    for (std::uint32_t y = 0u; y < height; ++y) {
        const float theta =
            (static_cast<float>(y) + 0.5f) * delta_theta;
        const float solid_angle_row = std::sin(theta) * delta_theta * delta_phi;

        for (std::uint32_t x = 0u; x < width; ++x) {
            const Vec3f direction = directionForPixel(x, y, width, height);
            const auto color = sourcePixel(
                linear_source,
                static_cast<int>(x),
                static_cast<int>(y));
            total_weight += solid_angle_row;

            for (std::size_t channel = 0u; channel < 3u; ++channel) {
                result.average[channel] += color[channel] * solid_angle_row;
                result.x[channel] += color[channel] * Y1 * direction.x * solid_angle_row;
                result.y[channel] += color[channel] * Y1 * direction.y * solid_angle_row;
                result.z[channel] += color[channel] * Y1 * direction.z * solid_angle_row;
            }
        }
    }

    if (total_weight > 1.0e-8f) {
        const float inverse_weight = 1.0f / total_weight;
        for (float& channel : result.average) channel *= inverse_weight;
    }
    return result;
}

PrefilterChain prefilterEquirectangular(
    const Models::Images::Image& source,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t mip_levels,
    Quality quality)
{
    PrefilterChain result;
    width = std::max(width, 1u);
    height = std::max(height, 1u);
    mip_levels = std::max(mip_levels, 1u);
    result.levels.reserve(mip_levels);

    const bool valid_source =
        source.width > 0 && source.height > 0 && !source.rgba.empty();
    if (!valid_source) {
        for (std::uint32_t level = 0u; level < mip_levels; ++level) {
            const std::uint32_t level_width = std::max(width >> level, 1u);
            const std::uint32_t level_height = std::max(height >> level, 1u);
            PrefilterLevel fallback;
            fallback.width = level_width;
            fallback.height = level_height;
            fallback.rgba.resize(
                static_cast<std::size_t>(level_width) * level_height * 4u,
                0u);
            for (std::size_t alpha = 3u; alpha < fallback.rgba.size(); alpha += 4u)
                fallback.rgba[alpha] = 255u;
            result.levels.push_back(std::move(fallback));
        }
        return result;
    }

    const LinearImage linear_source = linearize(source);
    const std::uint32_t sample_count = prefilterSampleCount(quality);
    for (std::uint32_t level = 0u; level < mip_levels; ++level) {
        const std::uint32_t level_width = std::max(width >> level, 1u);
        const std::uint32_t level_height = std::max(height >> level, 1u);
        const float roughness = mip_levels <= 1u
            ? 0.0f
            : static_cast<float>(level) / static_cast<float>(mip_levels - 1u);
        result.levels.push_back(prefilterLevel(
            linear_source,
            level_width,
            level_height,
            roughness,
            sample_count));
    }

    return result;
}

} // namespace Renderer::Reflections::Internal
