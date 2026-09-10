#ifndef RW_ENGINE_RENDERER_GLOBAL_ILLUMINATION_PHOTON_MAPPING_PHOTON_MAP_HPP
#define RW_ENGINE_RENDERER_GLOBAL_ILLUMINATION_PHOTON_MAPPING_PHOTON_MAP_HPP

#include "Renderer/GlobalIllumination/TraceScene.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace Renderer::GlobalIllumination::PhotonMapping {

struct Settings {
    bool enabled = false;
    std::uint32_t photon_count = 0u;
    std::uint8_t bounces = 0u;
    float radius = 0.0f;
    float ray_epsilon = 0.0f;
};

struct Photon {
    Vec3 position{};
    Vec3 normal {0.0f, 1.0f, 0.0f};
    Vec3 direction{};
    Vec3 power{};
};

class PhotonMap {
public:
    PhotonMap();
    ~PhotonMap();

    PhotonMap(const PhotonMap&) = delete;
    PhotonMap& operator=(const PhotonMap&) = delete;
    PhotonMap(PhotonMap&&) noexcept;
    PhotonMap& operator=(PhotonMap&&) noexcept;

    void rebuild(
        const TraceScene& scene,
        const Scenes::LightState& light,
        const Settings& settings
    );
    Vec3 sample(Vec3 position, Vec3 normal) const;
    void clear();

    bool valid() const;
    std::size_t photonCount() const;
    const Photon *photonData() const;
    float radius() const;

private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
};

} // namespace Renderer::GlobalIllumination::PhotonMapping

#endif
