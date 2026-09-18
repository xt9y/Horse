#ifndef HORSE_RENDERER_INTERNAL_SHADOW_CACHE_HPP
#define HORSE_RENDERER_INTERNAL_SHADOW_CACHE_HPP

#include <bit>
#include <cstdint>

namespace Renderer::RasterizerSDLGPU::ShadowCache {

struct Key {
    std::uint64_t geometry_revision = 0u;
    std::uint64_t light_signature = 0u;
    std::uint64_t camera_signature = 0u;
    std::uint32_t view = 0u;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t resolution = 0u;
    std::uint32_t cascades = 0u;
    float distance = 0.0f;
    float near_plane = 0.0f;
    bool camera_dependent = false;
};

constexpr void append(std::uint64_t& hash, std::uint64_t value)
{
    hash ^= value;
    hash *= 1099511628211ull;
}

constexpr void appendFloat(std::uint64_t& hash, float value)
{
    append(hash, std::bit_cast<std::uint32_t>(value));
}

constexpr std::uint64_t signature(const Key& key)
{
    std::uint64_t hash = 1469598103934665603ull;
    append(hash, key.geometry_revision);
    append(hash, key.light_signature);
    append(hash, key.view);
    append(hash, key.resolution);
    appendFloat(hash, key.distance);
    appendFloat(hash, key.near_plane);

    if (key.camera_dependent) {
        append(hash, key.camera_signature);
        append(hash, key.width);
        append(hash, key.height);
        append(hash, key.cascades);
    }
    return hash;
}

} // namespace Renderer::RasterizerSDLGPU::ShadowCache

#endif
