#ifndef RW_ENGINE_RENDERER_GLOBAL_ILLUMINATION_HPP
#define RW_ENGINE_RENDERER_GLOBAL_ILLUMINATION_HPP

#include "Ecs/Ecs.hpp"
#include "Renderer/Components.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Renderer::GlobalIllumination {

struct Probe {
    std::array<Vec3, 4> sh{};
};

struct Field {
    Vec3 minimum{};
    Vec3 maximum{};
    std::uint32_t size_x = 0u;
    std::uint32_t size_y = 0u;
    std::uint32_t size_z = 0u;
    float intensity = 0.0f;
    std::uint64_t revision = 0u;
    std::vector<Probe> probes;

    std::size_t probeCount() const
    {
        return static_cast<std::size_t>(size_x) *
            static_cast<std::size_t>(size_y) *
            static_cast<std::size_t>(size_z);
    }

    bool valid() const
    {
        return size_x > 1u && size_y > 1u && size_z > 1u &&
            probes.size() == probeCount();
    }
};

void setRaysPerProbe(std::size_t value);
void setProbeBudgetPerFrame(std::size_t value);
void setProbeDimensionRange(std::uint32_t minimum, std::uint32_t maximum);
void setBoundsMargin(float scale, float minimum);
void setRayEpsilon(float value);
void setMaximumBounces(std::uint8_t value);
void setMaximumPhotonCount(std::uint32_t value);
void setPaused(bool value);
bool paused();

const Field *update(const Ecs::World& world);
Vec3 sample(const Field *field, Vec3 position, Vec3 normal);
void reset();

} // namespace Renderer::GlobalIllumination

#endif
