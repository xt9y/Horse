#ifndef RW_ENGINE_RENDERER_GLOBAL_ILLUMINATION_DEBUG_HPP
#define RW_ENGINE_RENDERER_GLOBAL_ILLUMINATION_DEBUG_HPP

#include "Renderer/GlobalIllumination/GlobalIllumination.hpp"
#include "Renderer/GlobalIllumination/PhotonMapping/PhotonMap.hpp"
#include "Renderer/GlobalIllumination/TraceScene.hpp"

#include <cstddef>
#include <cstdint>

namespace Renderer::GlobalIllumination::Debug {

struct Statistics {
    std::size_t photons = 0u;
    std::size_t probes = 0u;
    std::size_t triangles = 0u;
    std::size_t materials = 0u;
    std::size_t bvh_nodes = 0u;
    std::uint32_t bvh_depth = 0u;
    std::uint32_t requested_photons = 0u;
    std::uint8_t bounce = 0u;
    std::uint8_t bounces = 0u;
    float photon_radius = 0.0f;
    float progress = 0.0f;
    double scene_build_ms = 0.0;
    double photon_build_ms = 0.0;
    bool calculating = false;
};

Statistics statistics();
const TraceScene& traceScene();
const PhotonMapping::PhotonMap& photonMap();
const Field *field();

} // namespace Renderer::GlobalIllumination::Debug

#endif
