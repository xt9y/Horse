#include "Renderer/HorizonGI/OpenGL/HorizonGIOpenGL.hpp"

#include "Renderer/HorizonGI/OpenGL/HorizonGIShaders.hpp"
#include "Renderer/Systems/OpenGL/Program.hpp"
#include "Renderer/Systems/OpenGL/RenderSurface.hpp"

#include <lwcgl/glmodern.h>
#include <lwcgl/lwcgl.h>

#include <algorithm>
#include <bit>
#include <cstdint>

namespace Renderer::HorizonGI {
namespace {

void hashValue(std::uint64_t& hash, std::uint32_t value)
{
    hash ^= static_cast<std::uint64_t>(value);
    hash *= 1099511628211ull;
}

void hashFloat(std::uint64_t& hash, float value)
{
    hashValue(hash, std::bit_cast<std::uint32_t>(value));
}

std::uint64_t settingsSignature(const Settings& settings, int width, int height)
{
    std::uint64_t hash = 1469598103934665603ull;
    hashValue(hash, static_cast<std::uint32_t>(settings.enabled));
    hashValue(hash, static_cast<std::uint32_t>(settings.pass.resolution_divisor));
    hashValue(hash, static_cast<std::uint32_t>(settings.pass.temporal_filter));
    hashFloat(hash, settings.pass.temporal_weight);
    hashFloat(hash, settings.pass.depth_threshold);
    hashValue(hash, static_cast<std::uint32_t>(settings.directions));
    hashValue(hash, static_cast<std::uint32_t>(settings.steps));
    hashFloat(hash, settings.radius);
    hashFloat(hash, settings.thickness);
    hashFloat(hash, settings.ao_strength);
    hashFloat(hash, settings.indirect_strength);
    hashValue(hash, static_cast<std::uint32_t>(width));
    hashValue(hash, static_cast<std::uint32_t>(height));
    return hash;
}

void setInt(int location, int value)
{
    if (location >= 0) GL20.glUniform1i(location, value);
}

void setFloat(int location, float value)
{
    if (location >= 0) GL20.glUniform1f(location, value);
}

void setVec2(int location, float x, float y)
{
    if (location >= 0) GL20.glUniform2f(location, x, y);
}

void drawFullscreenTriangle()
{
    glBegin(GL_TRIANGLES);
    glTexCoord2f(0.0f, 0.0f);
    glVertex2f(-1.0f, -1.0f);
    glTexCoord2f(2.0f, 0.0f);
    glVertex2f(3.0f, -1.0f);
    glTexCoord2f(0.0f, 2.0f);
    glVertex2f(-1.0f, 3.0f);
    glEnd();
}

} // namespace

struct OpenGLPass::Impl {
    Systems::OpenGL::Program program;
    Systems::OpenGL::RenderSurface surfaces[2];
    int write_index = 0;
    int output_index = -1;
    std::uint64_t temporal_signature = 0u;
    std::uint64_t settings_signature = 0u;
    bool history_valid = false;
    Statistics stats{};

    struct Uniforms {
        int scene_color = -1;
        int scene_depth = -1;
        int history = -1;
        int source_size = -1;
        int near_plane = -1;
        int tan_half_fov = -1;
        int aspect = -1;
        int radius = -1;
        int thickness = -1;
        int ao_strength = -1;
        int indirect_strength = -1;
        int intensity = -1;
        int temporal_weight = -1;
        int directions = -1;
        int steps = -1;
        int indirect_enabled = -1;
        int history_valid = -1;
    } uniforms;

    bool ensureProgram()
    {
        if (program.valid()) return true;
        if (lwcglLoadModernGL() != 0 || !GL20.glCreateShader || !GLModern.glActiveTexture)
            return false;
        if (!program.createGraphics(OpenGLShaders::vertex, OpenGLShaders::fragment, "HorizonGI"))
            return false;

        uniforms.scene_color = program.uniform("uSceneColor");
        uniforms.scene_depth = program.uniform("uSceneDepth");
        uniforms.history = program.uniform("uHistory");
        uniforms.source_size = program.uniform("uSourceSize");
        uniforms.near_plane = program.uniform("uNearPlane");
        uniforms.tan_half_fov = program.uniform("uTanHalfFov");
        uniforms.aspect = program.uniform("uAspect");
        uniforms.radius = program.uniform("uRadius");
        uniforms.thickness = program.uniform("uThickness");
        uniforms.ao_strength = program.uniform("uAoStrength");
        uniforms.indirect_strength = program.uniform("uIndirectStrength");
        uniforms.intensity = program.uniform("uIntensity");
        uniforms.temporal_weight = program.uniform("uTemporalWeight");
        uniforms.directions = program.uniform("uDirections");
        uniforms.steps = program.uniform("uSteps");
        uniforms.indirect_enabled = program.uniform("uIndirectEnabled");
        uniforms.history_valid = program.uniform("uHistoryValid");

        program.use();
        setInt(uniforms.scene_color, 0);
        setInt(uniforms.scene_depth, 1);
        setInt(uniforms.history, 2);
        Systems::OpenGL::unbindProgram();
        return true;
    }
};

OpenGLPass::OpenGLPass() : impl_(new Impl) {}

OpenGLPass::~OpenGLPass()
{
    shutdown();
    delete impl_;
    impl_ = nullptr;
}

unsigned int OpenGLPass::render(const OpenGLInput& input, const Settings& requested_settings)
{
    if (!impl_ || input.color_texture == 0u || input.depth_texture == 0u) return 0u;
    if (!impl_->ensureProgram()) return 0u;

    const Settings settings = sanitized(requested_settings);
    const Quality::Extent extent = Quality::scaledExtent(
        input.output_width,
        input.output_height,
        settings.pass
    );
    const int target = impl_->write_index;
    if (!impl_->surfaces[target].ensure(
            extent.width,
            extent.height,
            Systems::OpenGL::SurfaceColorFormat::Rgba16Float,
            false))
    {
        return 0u;
    }

    const std::uint64_t settings_signature = settingsSignature(settings, extent.width, extent.height);
    const bool same_history =
        impl_->history_valid &&
        settings.pass.temporal_filter &&
        impl_->output_index >= 0 &&
        impl_->temporal_signature == input.temporal_signature &&
        impl_->settings_signature == settings_signature &&
        impl_->surfaces[impl_->output_index].width() == extent.width &&
        impl_->surfaces[impl_->output_index].height() == extent.height;

    if (!impl_->surfaces[target].bind()) return 0u;
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    impl_->program.use();

    GLModern.glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, input.color_texture);
    GLModern.glActiveTexture(GL_TEXTURE0 + 1u);
    glBindTexture(GL_TEXTURE_2D, input.depth_texture);
    GLModern.glActiveTexture(GL_TEXTURE0 + 2u);
    glBindTexture(
        GL_TEXTURE_2D,
        same_history ? impl_->surfaces[impl_->output_index].colorTexture() : input.color_texture
    );

    setVec2(impl_->uniforms.source_size,
        static_cast<float>(std::max(input.source_width, 1)),
        static_cast<float>(std::max(input.source_height, 1)));
    setFloat(impl_->uniforms.near_plane, std::max(input.near_plane, 1.0e-5f));
    setFloat(impl_->uniforms.tan_half_fov, std::max(input.tan_half_fov, 1.0e-5f));
    setFloat(impl_->uniforms.aspect, std::max(input.aspect, 1.0e-5f));
    setFloat(impl_->uniforms.radius, settings.radius);
    setFloat(impl_->uniforms.thickness, settings.thickness);
    setFloat(impl_->uniforms.ao_strength, settings.ao_strength);
    setFloat(impl_->uniforms.indirect_strength, settings.indirect_strength);
    setFloat(impl_->uniforms.intensity, std::max(input.intensity, 0.0f));
    setFloat(impl_->uniforms.temporal_weight, settings.pass.temporal_weight);
    setInt(impl_->uniforms.directions, settings.directions);
    setInt(impl_->uniforms.steps, settings.steps);
    setInt(impl_->uniforms.indirect_enabled, settings.enabled ? 1 : 0);
    setInt(impl_->uniforms.history_valid, same_history ? 1 : 0);

    drawFullscreenTriangle();

    GLModern.glActiveTexture(GL_TEXTURE0 + 2u);
    glBindTexture(GL_TEXTURE_2D, 0u);
    GLModern.glActiveTexture(GL_TEXTURE0 + 1u);
    glBindTexture(GL_TEXTURE_2D, 0u);
    GLModern.glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0u);
    Systems::OpenGL::unbindProgram();

    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopAttrib();
    Systems::OpenGL::RenderSurface::unbind();
    glViewport(0, 0, std::max(input.output_width, 1), std::max(input.output_height, 1));

    impl_->output_index = target;
    impl_->write_index = 1 - target;
    impl_->temporal_signature = input.temporal_signature;
    impl_->settings_signature = settings_signature;
    impl_->history_valid = settings.pass.temporal_filter;
    impl_->stats.active = true;
    impl_->stats.indirect = settings.enabled;
    impl_->stats.temporal_history = same_history;
    impl_->stats.width = extent.width;
    impl_->stats.height = extent.height;
    impl_->stats.directions = settings.directions;
    impl_->stats.steps = settings.steps;
    return impl_->surfaces[target].colorTexture();
}

void OpenGLPass::resetHistory()
{
    if (!impl_) return;
    impl_->history_valid = false;
    impl_->temporal_signature = 0u;
    impl_->settings_signature = 0u;
    impl_->stats.temporal_history = false;
}

void OpenGLPass::shutdown()
{
    if (!impl_) return;
    impl_->program.destroy();
    impl_->surfaces[0].clear();
    impl_->surfaces[1].clear();
    impl_->write_index = 0;
    impl_->output_index = -1;
    impl_->history_valid = false;
    impl_->temporal_signature = 0u;
    impl_->settings_signature = 0u;
    impl_->stats = {};
}

Statistics OpenGLPass::statistics() const
{
    return impl_ ? impl_->stats : Statistics{};
}

} // namespace Renderer::HorizonGI
