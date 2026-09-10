#include "Renderer/GlobalIllumination/OpenGL/GlobalIlluminationOpenGL.hpp"

#include <lwcgl/glmodern.h>
#include <lwcgl/lwcgl.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace Renderer::Internal {
namespace {

constexpr GLuint kGiBinding = 5u;
constexpr std::size_t kHeaderVec4Count = 4u;

GLuint buffer = 0u;
std::uint64_t uploaded_revision = std::numeric_limits<std::uint64_t>::max();
bool uploaded_valid = false;

bool available()
{
    return GL15.glGenBuffers && GL15.glBindBuffer && GL15.glBufferData && GL30.glBindBufferBase;
}

bool upload(const GlobalIllumination::Field *field)
{
    if (!available()) return false;
    if (buffer == 0u) {
        GL15.glGenBuffers(1, &buffer);
        if (buffer == 0u) return false;
    }

    const bool valid = field && field->valid();
    const std::uint64_t revision = valid ? field->revision : 0u;
    if (revision == uploaded_revision && valid == uploaded_valid) return true;

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

    GL15.glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
    GL15.glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        static_cast<LWCGLsizeiptr>(data.size() * sizeof(float)),
        data.data(),
        GL_DYNAMIC_DRAW
    );
    GL15.glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0u);

    uploaded_revision = revision;
    uploaded_valid = valid;
    return true;
}

} // namespace

void bindGlobalIlluminationOpenGL(const GlobalIllumination::Field *field)
{
    if (!upload(field)) return;
    GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kGiBinding, buffer);
}

void shutdownGlobalIlluminationOpenGL()
{
    if (buffer != 0u && GL15.glDeleteBuffers) GL15.glDeleteBuffers(1, &buffer);
    buffer = 0u;
    uploaded_revision = std::numeric_limits<std::uint64_t>::max();
    uploaded_valid = false;
}

} // namespace Renderer::Internal
