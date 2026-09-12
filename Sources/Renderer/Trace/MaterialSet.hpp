#ifndef HORSE_RENDERER_TRACE_MATERIAL_SET_HPP
#define HORSE_RENDERER_TRACE_MATERIAL_SET_HPP

#include "Models/Models.hpp"
#include "Renderer/Scenes/SceneCache.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Renderer::Trace {

struct alignas(16) GpuAdvancedMaterial {
    std::array<float, 4> emissive_strength {0.0f, 0.0f, 0.0f, 1.0f};
    std::array<float, 4> pbr {0.8f, 0.0f, 1.0f, 1.0f}; // roughness, metallic, AO, normal scale
    std::array<float, 4> specular {1.0f, 1.0f, 1.0f, 1.0f}; // factor, RGB color
    std::array<float, 4> clearcoat_sheen{}; // clearcoat, coat roughness, sheen roughness, transmission
    std::array<float, 4> sheen_thickness{}; // sheen RGB, thickness
    std::array<float, 4> attenuation {1.0f, 1.0f, 1.0f, 0.0f}; // RGB, distance
    std::array<float, 4> diffuse_transmission {1.0f, 1.0f, 1.0f, 0.0f}; // RGB, factor
    std::array<float, 4> anisotropy_iridescence {0.0f, 0.0f, 0.0f, 1.3f}; // strength, rotation, factor, IOR
    std::array<float, 4> iridescence_dispersion_ior {100.0f, 400.0f, 0.0f, 1.5f}; // thickness min/max, dispersion, IOR
    std::array<float, 4> misc {0.5f, 0.0f, 0.0f, 0.0f}; // alpha cutoff, unlit, double-sided, alpha mode
    std::array<std::int32_t, 4> tex0 {-1, -1, -1, -1}; // base, normal, roughness, metallic
    std::array<std::int32_t, 4> tex1 {-1, -1, -1, -1}; // AO, emissive, opacity, clearcoat
    std::array<std::int32_t, 4> tex2 {-1, -1, -1, -1}; // coat roughness, coat normal, sheen color, sheen roughness
    std::array<std::int32_t, 4> tex3 {-1, -1, -1, -1}; // transmission, thickness, specular, specular color
    std::array<std::int32_t, 4> tex4 {-1, -1, -1, -1}; // iridescence, iri thickness, anisotropy, diffuse transmission
    std::array<std::int32_t, 4> tex5 {-1, -1, -1, -1}; // diffuse transmission color, reserved
};

class MaterialSet {
public:
    bool sync(
        const Scenes::SceneCache& scene,
        std::size_t maximum_texture_slots,
        std::string *error = nullptr
    );
    void clear();

    const std::vector<GpuAdvancedMaterial>& materials() const { return materials_; }
    const std::vector<Models::TextureHandle>& textureHandles() const { return texture_handles_; }
    std::size_t baseTextureCount() const { return base_texture_count_; }
    std::uint64_t revision() const { return revision_; }

private:
    std::vector<GpuAdvancedMaterial> materials_;
    std::vector<Models::TextureHandle> texture_handles_;
    std::size_t base_texture_count_ = 0u;
    std::uint64_t source_revision_ = UINT64_MAX;
    std::uint64_t revision_ = 0u;
};

static_assert(sizeof(GpuAdvancedMaterial) == 256u);

} // namespace Renderer::Trace

#endif
