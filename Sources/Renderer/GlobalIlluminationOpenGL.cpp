#include "Renderer/GlobalIlluminationOpenGL.hpp"

#include <lwcgl/glmodern.h>
#include <lwcgl/lwcgl.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Renderer::Internal {
namespace {

constexpr GLuint kGiBinding = 5u;

GLuint buffer = 0u;
std::uint64_t uploaded_revision = 0u;
GLuint cached_program = 0u;
GLint enabled_location = -1;
GLint minimum_location = -1;
GLint maximum_location = -1;
GLint dimensions_location = -1;
GLint intensity_location = -1;

void cacheLocations(GLuint program)
{
    if (cached_program == program) return;
    cached_program = program;
    enabled_location = GL20.glGetUniformLocation(program, "uGiEnabled");
    minimum_location = GL20.glGetUniformLocation(program, "uGiMinimum");
    maximum_location = GL20.glGetUniformLocation(program, "uGiMaximum");
    dimensions_location = GL20.glGetUniformLocation(program, "uGiDimensions");
    intensity_location = GL20.glGetUniformLocation(program, "uGiIntensity");
}

bool upload(const GlobalIllumination::Field& field)
{
    if (buffer == 0u) {
        GL15.glGenBuffers(1, &buffer);
        if (buffer == 0u) return false;
    }

    std::vector<float> coefficients;
    coefficients.resize(field.probes.size() * 16u, 0.0f);
    for (std::size_t probe = 0u; probe < field.probes.size(); ++probe) {
        for (std::size_t coefficient = 0u; coefficient < 4u; ++coefficient) {
            const Vec3 value = field.probes[probe].sh[coefficient];
            const std::size_t offset = probe * 16u + coefficient * 4u;
            coefficients[offset + 0u] = value.x;
            coefficients[offset + 1u] = value.y;
            coefficients[offset + 2u] = value.z;
        }
    }

    GL15.glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
    GL15.glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        static_cast<LWCGLsizeiptr>(coefficients.size() * sizeof(float)),
        coefficients.data(),
        GL_DYNAMIC_DRAW
    );
    GL15.glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0u);
    uploaded_revision = field.revision;
    return true;
}

} // namespace

void bindGlobalIlluminationOpenGL(
    unsigned int program_value,
    const GlobalIllumination::Field *field)
{
    const GLuint program = static_cast<GLuint>(program_value);
    if (program == 0u || !GL20.glGetUniformLocation) return;
    cacheLocations(program);

    const bool valid = field && field->valid();
    if (enabled_location >= 0) GL20.glUniform1i(enabled_location, valid ? 1 : 0);
    if (!valid) return;

    if (field->revision != uploaded_revision && !upload(*field)) {
        if (enabled_location >= 0) GL20.glUniform1i(enabled_location, 0);
        return;
    }

    if (minimum_location >= 0) {
        GL20.glUniform3f(
            minimum_location,
            field->minimum.x,
            field->minimum.y,
            field->minimum.z
        );
    }
    if (maximum_location >= 0) {
        GL20.glUniform3f(
            maximum_location,
            field->maximum.x,
            field->maximum.y,
            field->maximum.z
        );
    }
    if (dimensions_location >= 0) {
        GL20.glUniform3f(
            dimensions_location,
            static_cast<float>(field->size_x),
            static_cast<float>(field->size_y),
            static_cast<float>(field->size_z)
        );
    }
    if (intensity_location >= 0) {
        GL20.glUniform1f(intensity_location, std::max(field->intensity, 0.0f));
    }

    GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kGiBinding, buffer);
}

void shutdownGlobalIlluminationOpenGL()
{
    if (buffer != 0u && GL15.glDeleteBuffers) GL15.glDeleteBuffers(1, &buffer);
    buffer = 0u;
    uploaded_revision = 0u;
    cached_program = 0u;
    enabled_location = -1;
    minimum_location = -1;
    maximum_location = -1;
    dimensions_location = -1;
    intensity_location = -1;
}

} // namespace Renderer::Internal
