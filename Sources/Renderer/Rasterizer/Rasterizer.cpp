#include "Renderer/Rasterizer/Rasterizer.hpp"

#include "Camera.hpp"
#include "Models/Core/Texture.hpp"
#include "Models/Models.hpp"
#include "Renderer/FontPass.hpp"
#include "Renderer/GlobalIllumination.hpp"
#include "Renderer/Math.hpp"
#include "Renderer/Scene.hpp"

#include <lwcgl/lwcgl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Renderer {
namespace {

constexpr float kPi = 3.14159265358979323846f;

Vec3 add(Vec3 a, Vec3 b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 multiply(Vec3 a, Vec3 b)
{
    return {a.x * b.x, a.y * b.y, a.z * b.z};
}

Vec3 multiply(Vec3 value, float scalar)
{
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

Vec3 subtract(Vec3 a, Vec3 b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

float dot(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 normalize(Vec3 value)
{
    const float magnitude = std::sqrt(std::max(dot(value, value), 0.0f));
    if (magnitude <= 1.0e-12f) return {0.0f, 1.0f, 0.0f};
    return multiply(value, 1.0f / magnitude);
}

Vec3 directIrradiance(Vec3 position, Vec3 normal, const Scene::LightState& light)
{
    if (!light.valid || light.light.intensity <= 0.0f) return {};

    Vec3 direction{};
    float attenuation = 1.0f;
    if (light.light.type == LightType::Directional) {
        direction = normalize(light.transform.position);
        if (dot(light.transform.position, light.transform.position) <= 1.0e-12f) {
            direction = normalize(Vec3{-0.35f, 0.8f, 0.45f});
        }
    } else {
        const Vec3 to_light = subtract(light.transform.position, position);
        const float distance_squared = std::max(dot(to_light, to_light), 1.0e-4f);
        direction = normalize(to_light);
        attenuation = 1.0f / distance_squared;
    }

    const float cosine = std::max(dot(normalize(normal), direction), 0.0f);
    return multiply(
        light.light.color,
        std::max(light.light.intensity, 0.0f) * attenuation * cosine
    );
}

void applyInfinitePerspective(float fov_degrees, float aspect, float near_plane)
{
    const float safe_fov = std::clamp(fov_degrees, 1.0f, 179.0f);
    const float safe_aspect = aspect > 1.0e-6f ? aspect : 1.0f;
    const float safe_near = std::max(near_plane, 1.0e-4f);
    const float focal = 1.0f / std::tan(safe_fov * (kPi / 360.0f));

    const GLfloat projection[16] = {
        focal / safe_aspect, 0.0f, 0.0f, 0.0f,
        0.0f, focal, 0.0f, 0.0f,
        0.0f, 0.0f, -1.0f, -1.0f,
        0.0f, 0.0f, -2.0f * safe_near, 0.0f,
    };
    glLoadMatrixf(projection);
}

Math::Mat4 cameraView(const Scene::CameraState& camera)
{
    const Vec3 forward = Math::normalize(Camera::flightDirection(
        camera.transform.rotation.y,
        camera.transform.rotation.x
    ));
    const Vec3 right = Math::normalize(Camera::strafeDirection(camera.transform.rotation.y));
    const Vec3 up = Math::normalize(Math::cross(right, forward));
    return Math::viewMatrix(camera.transform.position, forward, right, up);
}

} // namespace

struct Rasterizer::Impl {
    bool initialized = false;
    bool enabled = true;
    int width = 1;
    int height = 1;
    std::unordered_map<std::uint32_t, unsigned int> textures;
    std::vector<Scene::RenderItem> render_items;

    unsigned int textureFor(std::uint32_t handle)
    {
        if (handle == Models::INVALID_TEXTURE) return 0u;

        const auto found = textures.find(handle);
        if (found != textures.end()) return found->second;

        const Models::TextureAsset* asset = Models::texture(handle);
        if (!asset || asset->image.width <= 0 || asset->image.height <= 0 || asset->image.rgba.empty()) {
            return 0u;
        }

        GLuint texture_id = 0u;
        glGenTextures(1, &texture_id);
        if (texture_id == 0u) return 0u;

        glBindTexture(GL_TEXTURE_2D, texture_id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA,
            asset->image.width,
            asset->image.height,
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            asset->image.rgba.data()
        );

        textures.emplace(handle, texture_id);
        return texture_id;
    }

    void clearTextures()
    {
        for (const auto& [handle, texture_id] : textures) {
            (void)handle;
            const GLuint id = static_cast<GLuint>(texture_id);
            if (id != 0u) glDeleteTextures(1, &id);
        }
        textures.clear();
    }

    void applyLight(const Scene::LightState& state)
    {
        if (state.valid) {
            const GLfloat position[4] = {
                state.transform.position.x,
                state.transform.position.y,
                state.transform.position.z,
                state.light.type == LightType::Directional ? 0.0f : 1.0f,
            };
            const float intensity = std::max(state.light.intensity, 0.0f);
            const GLfloat diffuse[4] = {
                std::clamp(state.light.color.x * intensity, 0.0f, 1.0f),
                std::clamp(state.light.color.y * intensity, 0.0f, 1.0f),
                std::clamp(state.light.color.z * intensity, 0.0f, 1.0f),
                1.0f,
            };
            glLightfv(GL_LIGHT0, GL_POSITION, position);
            glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);
            return;
        }

        const GLfloat position[4] = {-0.35f, 0.8f, 0.45f, 0.0f};
        const GLfloat diffuse[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        glLightfv(GL_LIGHT0, GL_POSITION, position);
        glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);
    }

    void draw(const Ecs::World& world, const GlobalIllumination::Field *gi)
    {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        const Scene::CameraState camera = Scene::cameraState(world);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        applyInfinitePerspective(
            camera.valid ? camera.fov_degrees : 60.0f,
            static_cast<float>(width) / static_cast<float>(height),
            camera.valid ? camera.near_plane : 0.1f
        );

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        if (camera.valid) {
            const Math::Mat4 view = cameraView(camera);
            glMultMatrixf(view.data());
        }

        const Scene::LightState light = Scene::lightState(world);
        const bool use_shared_gi = gi && gi->valid();
        if (use_shared_gi) glDisable(GL_LIGHTING);
        else {
            glEnable(GL_LIGHTING);
            applyLight(light);
        }

        Scene::collectRenderItems(world, render_items);

        for (const Scene::RenderItem& item : render_items) {
            const Models::MeshData* mesh = item.mesh;
            if (!mesh || mesh->indices.empty() || !item.transform) continue;

            const Models::MaterialData* material = item.material;
            const float opacity = material ? std::clamp(material->opacity, 0.0f, 1.0f) : 1.0f;
            const unsigned int texture_id = material ? textureFor(material->diffuse_texture) : 0u;
            const Models::TextureAsset* texture_asset = material
                && material->diffuse_texture != Models::INVALID_TEXTURE
                ? Models::texture(material->diffuse_texture)
                : nullptr;
            const bool transparent = opacity < 1.0f
                || (texture_asset && texture_asset->image.meaningful_alpha);

            if (transparent) {
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            } else {
                glDisable(GL_BLEND);
            }

            if (texture_id != 0u) {
                glEnable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, texture_id);
                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
            } else {
                glDisable(GL_TEXTURE_2D);
            }

            const Vec3 base_color = material
                ? Vec3{material->color.x, material->color.y, material->color.z}
                : Vec3{1.0f, 1.0f, 1.0f};

            if (!use_shared_gi) {
                glColor4f(base_color.x, base_color.y, base_color.z, opacity);
            }

            const Math::Mat4 model = Math::modelMatrix(*item.transform);
            const Math::Mat4 world_to_object = Math::inverseModelMatrix(*item.transform);
            glPushMatrix();
            glMultMatrixf(model.data());
            glBegin(GL_TRIANGLES);
            for (const std::uint32_t index : mesh->indices) {
                if (index >= mesh->vertices.size()) continue;
                const Models::Vertex& vertex = mesh->vertices[index];

                if (use_shared_gi) {
                    const Vec3 local_position{
                        vertex.position.x,
                        vertex.position.y,
                        vertex.position.z,
                    };
                    const Vec3 local_normal{
                        vertex.normal.x,
                        vertex.normal.y,
                        vertex.normal.z,
                    };
                    const Vec3 world_position = Math::transformPoint(model, local_position);
                    const Vec3 world_normal = Math::transformNormal(world_to_object, local_normal);
                    const Vec3 indirect = GlobalIllumination::sample(gi, world_position, world_normal);
                    const Vec3 direct = directIrradiance(world_position, world_normal, light);
                    const Vec3 lighting = multiply(add(direct, indirect), 1.0f / kPi);
                    const Vec3 color = multiply(base_color, lighting);
                    glColor4f(
                        std::clamp(color.x, 0.0f, 1.0f),
                        std::clamp(color.y, 0.0f, 1.0f),
                        std::clamp(color.z, 0.0f, 1.0f),
                        opacity
                    );
                }

                glNormal3f(vertex.normal.x, vertex.normal.y, vertex.normal.z);
                if (texture_id != 0u) glTexCoord2f(vertex.uv.x, 1.0f - vertex.uv.y);
                glVertex3f(vertex.position.x, vertex.position.y, vertex.position.z);
            }
            glEnd();
            glPopMatrix();
        }

        if (use_shared_gi) glEnable(GL_LIGHTING);
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_BLEND);
    }
};

Rasterizer::Rasterizer()
    : impl_(new Impl())
{
}

Rasterizer::~Rasterizer()
{
    shutdown();
    delete impl_;
}

bool Rasterizer::init()
{
    if (impl_->initialized) return true;

    if (glGetString(GL_VERSION) == nullptr) {
        std::fprintf(stderr, "[Rasterizer]: OpenGL initialization failed: no current context\n");
        return false;
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glEnable(GL_NORMALIZE);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glShadeModel(GL_SMOOTH);

    const GLfloat scene_ambient[4] = {0.06f, 0.06f, 0.07f, 1.0f};
    const GLfloat light_ambient[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    const GLfloat light_diffuse[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, scene_ambient);
    glLightfv(GL_LIGHT0, GL_AMBIENT, light_ambient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, light_diffuse);

    glClearColor(0.035f, 0.035f, 0.045f, 1.0f);
    glViewport(0, 0, impl_->width, impl_->height);
    impl_->initialized = true;

    std::fprintf(stderr, "[Rasterizer]: OpenGL backend active\n");
    return true;
}

void Rasterizer::resize(int width, int height)
{
    impl_->width = std::max(width, 1);
    impl_->height = std::max(height, 1);
    if (impl_->initialized) glViewport(0, 0, impl_->width, impl_->height);
}

bool Rasterizer::renderScene(const Ecs::World& world, Internal::FrameOutput& output)
{
    if (!impl_->initialized) return false;

    if (!impl_->enabled) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    } else {
        impl_->draw(world, output.global_illumination);
    }

    output.api = Internal::GraphicsApi::OpenGL;
    output.depth = Internal::DepthSource::Native;
    output.width = impl_->width;
    output.height = impl_->height;
    return true;
}

void Rasterizer::present(Internal::FrameOutput& output)
{
    (void)output;
}

void Rasterizer::shutdown()
{
    if (!impl_ || !impl_->initialized) return;
    Internal::shutdownFonts(Internal::GraphicsApi::OpenGL);
    impl_->clearTextures();
    impl_->render_items.clear();
    impl_->initialized = false;
}

bool Rasterizer::initialized() const
{
    return impl_ && impl_->initialized;
}

bool Rasterizer::enabled() const
{
    return impl_ && impl_->enabled;
}

void Rasterizer::setEnabled(bool enabled)
{
    if (impl_) impl_->enabled = enabled;
}

} // namespace Renderer