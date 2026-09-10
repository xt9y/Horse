#include "Renderer/Upscale/OpenGL/UpscaleOpenGL.hpp"

#include "Renderer/Systems/OpenGL/Program.hpp"
#include "Renderer/Systems/OpenGL/RenderSurface.hpp"
#include "Renderer/Upscale/OpenGL/UpscaleShaders.hpp"

#include <lwcgl/glmodern.h>
#include <lwcgl/lwcgl.h>

#include <algorithm>
#include <bit>

namespace Renderer::Upscale {
namespace {

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

std::uint64_t settingsSignature(const Quality::ScaledPassSettings& settings, int width, int height)
{
    std::uint64_t hash = 1469598103934665603ull;
    const auto mix = [&hash](std::uint32_t value) {
        hash ^= static_cast<std::uint64_t>(value);
        hash *= 1099511628211ull;
    };
    mix(static_cast<std::uint32_t>(settings.resolution_divisor));
    mix(static_cast<std::uint32_t>(settings.depth_aware_upscale));
    mix(static_cast<std::uint32_t>(settings.temporal_filter));
    mix(std::bit_cast<std::uint32_t>(settings.temporal_weight));
    mix(std::bit_cast<std::uint32_t>(settings.depth_threshold));
    mix(static_cast<std::uint32_t>(width));
    mix(static_cast<std::uint32_t>(height));
    return hash;
}

} // namespace

struct OpenGLPass::Impl {
    Systems::OpenGL::Program program;
    Systems::OpenGL::RenderSurface history;
    std::uint64_t temporal_signature = 0u;
    std::uint64_t settings_signature = 0u;
    bool history_valid = false;
    Statistics stats{};

    struct Uniforms {
        int color = -1;
        int depth = -1;
        int full_depth = -1;
        int effect = -1;
        int history = -1;
        int source_size = -1;
        int effect_size = -1;
        int near_plane = -1;
        int depth_threshold = -1;
        int temporal_weight = -1;
        int depth_aware = -1;
        int has_effect = -1;
        int history_valid = -1;
    } uniforms;

    bool ensureProgram()
    {
        if (program.valid()) return true;
        if (lwcglLoadModernGL() != 0 || !GL20.glCreateShader || !GLModern.glActiveTexture)
            return false;
        if (!program.createGraphics(OpenGLShaders::vertex, OpenGLShaders::fragment, "Upscale"))
            return false;

        uniforms.color = program.uniform("uColor");
        uniforms.depth = program.uniform("uDepth");
        uniforms.full_depth = program.uniform("uFullDepth");
        uniforms.effect = program.uniform("uEffect");
        uniforms.history = program.uniform("uHistory");
        uniforms.source_size = program.uniform("uSourceSize");
        uniforms.effect_size = program.uniform("uEffectSize");
        uniforms.near_plane = program.uniform("uNearPlane");
        uniforms.depth_threshold = program.uniform("uDepthThreshold");
        uniforms.temporal_weight = program.uniform("uTemporalWeight");
        uniforms.depth_aware = program.uniform("uDepthAware");
        uniforms.has_effect = program.uniform("uHasEffect");
        uniforms.history_valid = program.uniform("uHistoryValid");

        program.use();
        setInt(uniforms.color, 0);
        setInt(uniforms.depth, 1);
        setInt(uniforms.full_depth, 2);
        setInt(uniforms.effect, 3);
        setInt(uniforms.history, 4);
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

bool OpenGLPass::render(const OpenGLInput& input, const Quality::ScaledPassSettings& requested_settings)
{
    if (!impl_ || input.color_texture == 0u || input.depth_texture == 0u) return false;
    if (!impl_->ensureProgram()) return false;

    const Quality::ScaledPassSettings settings = Quality::sanitized(requested_settings);
    const bool depth_aware = settings.depth_aware_upscale && input.full_depth_texture != 0u;
    const bool has_effect = input.effect_texture != 0u;
    const std::uint64_t settings_signature = settingsSignature(
        settings,
        std::max(input.output_width, 1),
        std::max(input.output_height, 1)
    );
    const bool same_history =
        settings.temporal_filter &&
        impl_->history_valid &&
        impl_->temporal_signature == input.temporal_signature &&
        impl_->settings_signature == settings_signature &&
        impl_->history.width() == std::max(input.output_width, 1) &&
        impl_->history.height() == std::max(input.output_height, 1);

    if (settings.temporal_filter && !impl_->history.ensure(
            std::max(input.output_width, 1),
            std::max(input.output_height, 1),
            Systems::OpenGL::SurfaceColorFormat::Rgba8,
            false))
    {
        return false;
    }

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glViewport(0, 0, std::max(input.output_width, 1), std::max(input.output_height, 1));
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glDepthMask(input.full_depth_texture != 0u ? GL_TRUE : GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    impl_->program.use();

    GLModern.glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, input.color_texture);
    GLModern.glActiveTexture(GL_TEXTURE0 + 1u);
    glBindTexture(GL_TEXTURE_2D, input.depth_texture);
    GLModern.glActiveTexture(GL_TEXTURE0 + 2u);
    glBindTexture(GL_TEXTURE_2D, depth_aware ? input.full_depth_texture : input.depth_texture);
    GLModern.glActiveTexture(GL_TEXTURE0 + 3u);
    glBindTexture(GL_TEXTURE_2D, has_effect ? input.effect_texture : input.color_texture);
    GLModern.glActiveTexture(GL_TEXTURE0 + 4u);
    glBindTexture(GL_TEXTURE_2D, same_history ? impl_->history.colorTexture() : input.color_texture);

    setVec2(impl_->uniforms.source_size,
        static_cast<float>(std::max(input.source_width, 1)),
        static_cast<float>(std::max(input.source_height, 1)));
    setVec2(impl_->uniforms.effect_size,
        static_cast<float>(std::max(input.effect_width, 1)),
        static_cast<float>(std::max(input.effect_height, 1)));
    setFloat(impl_->uniforms.near_plane, std::max(input.near_plane, 1.0e-5f));
    setFloat(impl_->uniforms.depth_threshold, settings.depth_threshold);
    setFloat(impl_->uniforms.temporal_weight, settings.temporal_weight);
    setInt(impl_->uniforms.depth_aware, depth_aware ? 1 : 0);
    setInt(impl_->uniforms.has_effect, has_effect ? 1 : 0);
    setInt(impl_->uniforms.history_valid, same_history ? 1 : 0);

    drawFullscreenTriangle();

    GLModern.glActiveTexture(GL_TEXTURE0 + 4u);
    glBindTexture(GL_TEXTURE_2D, 0u);
    GLModern.glActiveTexture(GL_TEXTURE0 + 3u);
    glBindTexture(GL_TEXTURE_2D, 0u);
    GLModern.glActiveTexture(GL_TEXTURE0 + 2u);
    glBindTexture(GL_TEXTURE_2D, 0u);
    GLModern.glActiveTexture(GL_TEXTURE0 + 1u);
    glBindTexture(GL_TEXTURE_2D, 0u);
    GLModern.glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0u);
    Systems::OpenGL::unbindProgram();

    if (settings.temporal_filter) {
        glBindTexture(GL_TEXTURE_2D, impl_->history.colorTexture());
        glCopyTexSubImage2D(
            GL_TEXTURE_2D,
            0,
            0,
            0,
            0,
            0,
            std::max(input.output_width, 1),
            std::max(input.output_height, 1)
        );
        glBindTexture(GL_TEXTURE_2D, 0u);
    }

    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopAttrib();

    impl_->temporal_signature = input.temporal_signature;
    impl_->settings_signature = settings_signature;
    impl_->history_valid = settings.temporal_filter;
    impl_->stats.active = true;
    impl_->stats.depth_aware = depth_aware;
    impl_->stats.temporal_history = same_history;
    impl_->stats.effect = has_effect;
    impl_->stats.source_width = std::max(input.source_width, 1);
    impl_->stats.source_height = std::max(input.source_height, 1);
    impl_->stats.output_width = std::max(input.output_width, 1);
    impl_->stats.output_height = std::max(input.output_height, 1);
    return true;
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
    impl_->history.clear();
    impl_->history_valid = false;
    impl_->temporal_signature = 0u;
    impl_->settings_signature = 0u;
    impl_->stats = {};
}

Statistics OpenGLPass::statistics() const
{
    return impl_ ? impl_->stats : Statistics{};
}

} // namespace Renderer::Upscale
