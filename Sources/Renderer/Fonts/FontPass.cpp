#include "Renderer/Fonts/FontPass.hpp"

#include "Camera.hpp"
#include "Font.hpp"
#include "Renderer/Fonts/FontLayout.hpp"
#include "Renderer/Math.hpp"
#include "Renderer/Scenes/Scene.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Renderer::Internal {
namespace {

constexpr float kPi = 3.14159265358979323846f;

void appendQuad(
    std::vector<FontVertex>& out,
    const std::array<FontVertex, 4>& quad)
{
    out.push_back(quad[0]);
    out.push_back(quad[1]);
    out.push_back(quad[2]);
    out.push_back(quad[0]);
    out.push_back(quad[2]);
    out.push_back(quad[3]);
}

std::array<float, 4> glyphUv(std::uint8_t codepoint)
{
    constexpr float cell = 1.0f / 16.0f;
    const float u0 = static_cast<float>(codepoint & 15u) * cell;
    const float v0 = static_cast<float>(codepoint >> 4u) * cell;
    return {u0, v0, u0 + cell, v0 + cell};
}

FontVertex vertex(
    const std::array<float, 4>& clip,
    float u,
    float v,
    float linear_depth,
    bool depth_test,
    const Vec4& color)
{
    FontVertex out;
    out.clip = clip;
    out.uv_depth = {
        u,
        v,
        linear_depth,
        depth_test ? 1.0f : 0.0f,
    };
    out.color = {color.x, color.y, color.z, color.w};
    return out;
}

void appendScreenText(
    const Font::TextComponent& text,
    const FrameOutput& output,
    std::vector<FontLayout::GlyphQuad>& glyphs,
    std::vector<FontVertex>& out)
{
    FontLayout::screen(text.text, text.position, text.scale, glyphs);
    const float width = static_cast<float>(std::max(output.width, 1));
    const float height = static_cast<float>(std::max(output.height, 1));

    for (const FontLayout::GlyphQuad& glyph : glyphs) {
        const std::array<float, 4> uv = glyphUv(glyph.codepoint);
        const float x0 = glyph.x0 * (2.0f / width) - 1.0f;
        const float x1 = glyph.x1 * (2.0f / width) - 1.0f;
        const float y0 = 1.0f - glyph.y0 * (2.0f / height);
        const float y1 = 1.0f - glyph.y1 * (2.0f / height);

        const std::array<FontVertex, 4> quad = {
            vertex({x0, y0, 0.0f, 1.0f}, uv[0], uv[1], 0.0f, false, text.color),
            vertex({x1, y0, 0.0f, 1.0f}, uv[2], uv[1], 0.0f, false, text.color),
            vertex({x1, y1, 0.0f, 1.0f}, uv[2], uv[3], 0.0f, false, text.color),
            vertex({x0, y1, 0.0f, 1.0f}, uv[0], uv[3], 0.0f, false, text.color),
        };
        appendQuad(out, quad);
    }
}

bool worldClip(
    const Math::Mat4& model,
    const Math::Mat4& view,
    const Vec3& local,
    float focal,
    float aspect,
    float near_plane,
    std::array<float, 4>& clip,
    float& linear_depth)
{
    const Vec3 world = Math::transformPoint(model, local);
    const Vec3 camera = Math::transformPoint(view, world);
    linear_depth = -camera.z;
    if (!(linear_depth > near_plane)) return false;

    clip = {
        camera.x * focal / aspect,
        camera.y * focal,
        -camera.z - 2.0f * near_plane,
        -camera.z,
    };
    return true;
}

void appendWorldText(
    const Font::TextComponent& text,
    const Transform& transform,
    const Scenes::Scene::CameraState& camera,
    const FrameOutput& output,
    std::vector<FontLayout::GlyphQuad>& glyphs,
    FontBatches& batches)
{
    if (!camera.valid) return;
    if (text.depth_test && output.depth == DepthSource::None) return;

    FontLayout::world(text.text, text.scale, glyphs);
    if (glyphs.empty()) return;

    const float fov = std::clamp(camera.fov_degrees, 1.0f, 179.0f);
    const float near_plane = std::max(camera.near_plane, 1.0e-4f);
    const float aspect = static_cast<float>(std::max(output.width, 1)) /
        static_cast<float>(std::max(output.height, 1));
    const float focal = 1.0f / std::tan(fov * (kPi / 360.0f));
    const Math::Mat4 model = Math::modelMatrix(transform);

    const Vec3 forward = Math::normalize(Camera::flightDirection(
        camera.transform.rotation.y,
        camera.transform.rotation.x
    ));
    const Vec3 right = Math::normalize(Camera::strafeDirection(camera.transform.rotation.y));
    const Vec3 up = Math::normalize(Math::cross(right, forward));
    const Math::Mat4 view = Math::viewMatrix(
        camera.transform.position,
        forward,
        right,
        up
    );

    std::vector<FontVertex>& out = text.depth_test ? batches.depth : batches.overlay;

    for (const FontLayout::GlyphQuad& glyph : glyphs) {
        const std::array<float, 4> uv = glyphUv(glyph.codepoint);
        const Vec3 local[4] = {
            {glyph.x0, glyph.y0, 0.0f},
            {glyph.x1, glyph.y0, 0.0f},
            {glyph.x1, glyph.y1, 0.0f},
            {glyph.x0, glyph.y1, 0.0f},
        };
        std::array<std::array<float, 4>, 4> clip{};
        float depth[4]{};
        bool visible = true;
        for (std::size_t i = 0u; i < 4u; ++i) {
            if (!worldClip(model, view, local[i], focal, aspect, near_plane, clip[i], depth[i])) {
                visible = false;
                break;
            }
        }
        if (!visible) continue;

        const std::array<FontVertex, 4> quad = {
            vertex(clip[0], uv[0], uv[1], depth[0], text.depth_test, text.color),
            vertex(clip[1], uv[2], uv[1], depth[1], text.depth_test, text.color),
            vertex(clip[2], uv[2], uv[3], depth[2], text.depth_test, text.color),
            vertex(clip[3], uv[0], uv[3], depth[3], text.depth_test, text.color),
        };
        appendQuad(out, quad);
    }
}

} // namespace

bool fontDepthRequired(const Ecs::World& world)
{
    for (const Ecs::Entity entity : world.entities()) {
        const Font::TextComponent* text = world.get<Font::TextComponent>(entity);
        if (!text || text->text.empty()) continue;
        if (text->space == Font::Space::World && text->depth_test) return true;
    }
    return false;
}

void collectFontVertices(
    const Ecs::World& world,
    const FrameOutput& output,
    FontBatches& batches)
{
    batches.depth.clear();
    batches.overlay.clear();

    const Scenes::Scene::CameraState camera = Scenes::Scene::cameraState(world);
    std::vector<FontLayout::GlyphQuad> glyphs;

    for (const Ecs::Entity entity : world.entities()) {
        const Font::TextComponent* text = world.get<Font::TextComponent>(entity);
        if (!text || text->text.empty()) continue;

        if (text->space == Font::Space::Screen) {
            appendScreenText(*text, output, glyphs, batches.overlay);
            continue;
        }

        const Transform* transform = world.get<Transform>(entity);
        if (!transform) continue;
        appendWorldText(*text, *transform, camera, output, glyphs, batches);
    }
}

void renderFonts(const Ecs::World& world, FrameOutput& output)
{
    static bool warned_missing_depth = false;
    if (fontDepthRequired(world) && output.depth == DepthSource::None && !warned_missing_depth) {
        std::fprintf(
            stderr,
            "[Font]: scene backend provides no depth; depth-tested world text is skipped\n"
        );
        warned_missing_depth = true;
    }

    FontBatches batches;
    collectFontVertices(world, output, batches);
    if (batches.depth.empty() && batches.overlay.empty()) return;

    switch (output.api) {
        case GraphicsApi::OpenGL:
            renderFontsOpenGL(batches, output);
            break;
        case GraphicsApi::Metal:
#ifdef __APPLE__
            renderFontsMetal(batches, output);
#endif
            break;
    }
}

void shutdownFonts(GraphicsApi api)
{
    switch (api) {
        case GraphicsApi::OpenGL:
            shutdownFontsOpenGL();
            break;
        case GraphicsApi::Metal:
#ifdef __APPLE__
            shutdownFontsMetal();
#endif
            break;
    }
}

} // namespace Renderer::Internal
