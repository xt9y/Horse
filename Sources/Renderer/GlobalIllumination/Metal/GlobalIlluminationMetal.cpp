#ifdef __APPLE__

#include "Renderer/GlobalIllumination/Metal/GlobalIlluminationMetal.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace Renderer::Internal {
namespace {

constexpr std::size_t kHeaderVec4Count = 4u;

LWMGLBuffer buffer = nullptr;
std::size_t capacity = 0u;
std::uint64_t uploaded_revision = std::numeric_limits<std::uint64_t>::max();
bool uploaded_valid = false;

bool upload(const GlobalIllumination::Field *field)
{
    const bool valid = field && field->valid();
    const std::uint64_t revision = valid ? field->revision : 0u;
    if (buffer && revision == uploaded_revision && valid == uploaded_valid) return true;

    const std::size_t probe_count = valid ? field->probes.size() : 0u;
    std::vector<float> data((kHeaderVec4Count + probe_count * 4u) * 4u, 0.0f);

    if (valid) {
        data[0u] = field->minimum.x;
        data[1u] = field->minimum.y;
        data[2u] = field->minimum.z;
        data[3u] = 1.0f;

        data[4u] = field->maximum.x;
        data[5u] = field->maximum.y;
        data[6u] = field->maximum.z;
        data[7u] = std::max(field->intensity, 0.0f);

        data[8u] = static_cast<float>(field->size_x);
        data[9u] = static_cast<float>(field->size_y);
        data[10u] = static_cast<float>(field->size_z);
        data[11u] = static_cast<float>(probe_count);

        for (std::size_t probe = 0u; probe < probe_count; ++probe) {
            for (std::size_t coefficient = 0u; coefficient < 4u; ++coefficient) {
                const Vec3 value = field->probes[probe].sh[coefficient];
                const std::size_t offset = (kHeaderVec4Count + probe * 4u + coefficient) * 4u;
                data[offset + 0u] = value.x;
                data[offset + 1u] = value.y;
                data[offset + 2u] = value.z;
                data[offset + 3u] = 0.0f;
            }
        }
    }

    const std::size_t bytes = data.size() * sizeof(float);
    if (!buffer || capacity < bytes) {
        if (buffer) Metal.destroyBuffer(buffer);
        const LWMGLBufferDesc desc = {bytes, LWMGL_STORAGE_SHARED};
        buffer = Metal.createBuffer(&desc, data.data());
        if (!buffer) {
            capacity = 0u;
            return false;
        }
        capacity = bytes;
    } else if (Metal.uploadBuffer(buffer, 0u, data.data(), bytes) != 0) {
        return false;
    }

    uploaded_revision = revision;
    uploaded_valid = valid;
    return true;
}

} // namespace

bool bindGlobalIlluminationMetal(
    LWMGLCommand command,
    const GlobalIllumination::Field *field,
    std::uint32_t buffer_index)
{
    if (!command || !Metal.isCreated()) return false;
    if (!upload(field)) return false;
    return Metal.setBuffer(command, buffer, 0u, buffer_index) == 0;
}

void shutdownGlobalIlluminationMetal()
{
    if (buffer) Metal.destroyBuffer(buffer);
    buffer = nullptr;
    capacity = 0u;
    uploaded_revision = std::numeric_limits<std::uint64_t>::max();
    uploaded_valid = false;
}

} // namespace Renderer::Internal

#endif
