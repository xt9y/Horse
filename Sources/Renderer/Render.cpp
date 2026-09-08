#include "Renderer/Render.hpp"

#include "Camera.hpp"
#include "Models/Core/Texture.hpp"
#include "Models/Models.hpp"

#include <lwcgl/glmodern.h>
#include <lwcgl/lwcgl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Renderer {
namespace {

void applyCamera(const Ecs::World& world)
{
    const Ecs::Entity camera_entity = Camera::activeCamera(world);
    if (camera_entity == Ecs::INVALID_ENTITY) return;

    const Transform *transform = world.get<Transform>(camera_entity);
    if (!transform) return;

    glRotatef(-transform->rotation.x, 1.0f, 0.0f, 0.0f);
    glRotatef(-transform->rotation.y, 0.0f, 1.0f, 0.0f);
    glRotatef(-transform->rotation.z, 0.0f, 0.0f, 1.0f);
    glTranslatef(-transform->position.x, -transform->position.y, -transform->position.z);
}

void applyTransform(const Transform& transform)
{
    glTranslatef(transform.position.x, transform.position.y, transform.position.z);
    glRotatef(transform.rotation.x, 1.0f, 0.0f, 0.0f);
    glRotatef(transform.rotation.y, 0.0f, 1.0f, 0.0f);
    glRotatef(transform.rotation.z, 0.0f, 0.0f, 1.0f);
    glScalef(transform.scale.x, transform.scale.y, transform.scale.z);
}

void applyInfinitePerspective(float fov_degrees, float aspect, float near_plane)
{
    constexpr float pi = 3.14159265358979323846f;
    const float safe_fov = std::clamp(fov_degrees, 1.0f, 179.0f);
    const float safe_aspect = aspect > 1.0e-6f ? aspect : 1.0f;
    const float safe_near = std::max(near_plane, 1.0e-4f);
    const float focal = 1.0f / std::tan(safe_fov * (pi / 360.0f));

    const GLfloat projection[16] = {
        focal / safe_aspect, 0.0f, 0.0f, 0.0f,
        0.0f, focal, 0.0f, 0.0f,
        0.0f, 0.0f, -1.0f, -1.0f,
        0.0f, 0.0f, -2.0f * safe_near, 0.0f,
    };
    glLoadMatrixf(projection);
}

bool supportsPathTracer()
{
    if (!lwcglModernGLAvailable()) return false;
    const int major = lwcglModernGLMajorVersion();
    const int minor = lwcglModernGLMinorVersion();
    if (major < 4 || (major == 4 && minor < 3)) return false;
    return GL43.glDispatchCompute && GL42.glBindImageTexture && GL30.glBindBufferBase;
}

} // namespace

bool Rasterizer::init()
{
    if (initialized_) return true;

    if (supportsPathTracer() && path_tracer_.init()) {
        use_path_tracer_ = true;
        initialized_ = true;
        return true;
    }

    use_path_tracer_ = false;
    if (!initCompatibility()) return false;

    initialized_ = true;
    std::fprintf(
        stderr,
        "[Rasterizer]: compatibility OpenGL backend active (%d.%d)\n",
        lwcglModernGLMajorVersion(),
        lwcglModernGLMinorVersion()
    );
    return true;
}

bool Rasterizer::initCompatibility()
{
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

    const GLfloat scene_ambient[] = {0.06f, 0.06f, 0.07f, 1.0f};
    const GLfloat light_ambient[] = {0.0f, 0.0f, 0.0f, 1.0f};
    const GLfloat light_diffuse[] = {1.0f, 1.0f, 1.0f, 1.0f};
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, scene_ambient);
    glLightfv(GL_LIGHT0, GL_AMBIENT, light_ambient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, light_diffuse);

    glClearColor(0.035f, 0.035f, 0.045f, 1.0f);
    glViewport(0, 0, width_, height_);
    return true;
}

void Rasterizer::resize(int width, int height)
{
    width_ = std::max(width, 1);
    height_ = std::max(height, 1);

    if (use_path_tracer_) {
        path_tracer_.resize(width_, height_);
        return;
    }

    glViewport(0, 0, width_, height_);
}

unsigned int Rasterizer::textureFor(std::uint32_t handle)
{
    if (handle == Models::INVALID_TEXTURE) return 0u;

    const auto found = textures_.find(handle);
    if (found != textures_.end()) return found->second;

    const Models::TextureAsset *asset = Models::texture(handle);
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

    textures_.emplace(handle, texture_id);
    return texture_id;
}

void Rasterizer::render(const Ecs::World& world)
{
    if (!initialized_ || !enabled()) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        return;
    }

    if (use_path_tracer_) {
        path_tracer_.render(world);
        return;
    }

    renderCompatibility(world);
}

void Rasterizer::renderCompatibility(const Ecs::World& world)
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const Ecs::Entity camera_entity = Camera::activeCamera(world);
    const Camera::CameraComponent *camera = camera_entity == Ecs::INVALID_ENTITY
        ? nullptr
        : world.get<Camera::CameraComponent>(camera_entity);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    const float aspect = static_cast<float>(width_) / static_cast<float>(height_);
    applyInfinitePerspective(
        camera ? camera->fov_degrees : 60.0f,
        aspect,
        camera ? camera->near_plane : 0.1f
    );

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    applyCamera(world);

    bool light_set = false;
    for (const Ecs::Entity entity : world.entities()) {
        const LightComponent *light = world.get<LightComponent>(entity);
        const Transform *transform = world.get<Transform>(entity);
        if (!light || !transform) continue;

        GLfloat position[4] = {
            transform->position.x,
            transform->position.y,
            transform->position.z,
            light->type == LightType::Directional ? 0.0f : 1.0f,
        };
        const float intensity = std::clamp(light->intensity, 0.0f, 1.0f);
        const float scale = intensity > 0.0f ? intensity : 1.0f;
        const GLfloat diffuse[4] = {
            std::clamp(light->color.x * scale, 0.0f, 1.0f),
            std::clamp(light->color.y * scale, 0.0f, 1.0f),
            std::clamp(light->color.z * scale, 0.0f, 1.0f),
            1.0f,
        };
        glLightfv(GL_LIGHT0, GL_POSITION, position);
        glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);
        light_set = true;
        break;
    }

    if (!light_set) {
        const GLfloat position[] = {-0.35f, 0.8f, 0.45f, 0.0f};
        const GLfloat diffuse[] = {1.0f, 1.0f, 1.0f, 1.0f};
        glLightfv(GL_LIGHT0, GL_POSITION, position);
        glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);
    }

    for (const Ecs::Entity entity : world.entities()) {
        const RenderableComponent *renderable = world.get<RenderableComponent>(entity);
        const MeshComponent *mesh_component = world.get<MeshComponent>(entity);
        const Transform *transform = world.get<Transform>(entity);
        if (!renderable || !renderable->visible || !mesh_component || !transform) continue;

        const Models::MeshData *mesh = Models::mesh(mesh_component->mesh);
        if (!mesh || mesh->indices.empty()) continue;

        const Models::MaterialData *material = Models::material(mesh_component->material);
        const float opacity = material ? std::clamp(material->opacity, 0.0f, 1.0f) : 1.0f;
        const unsigned int texture_id = material ? textureFor(material->diffuse_texture) : 0u;
        const Models::TextureAsset *texture_asset = material
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

        if (material) {
            glColor4f(material->color.x, material->color.y, material->color.z, opacity);
        } else {
            glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        }

        glPushMatrix();
        applyTransform(*transform);
        glBegin(GL_TRIANGLES);
        for (const std::uint32_t index : mesh->indices) {
            if (index >= mesh->vertices.size()) continue;
            const Models::Vertex& vertex = mesh->vertices[index];
            glNormal3f(vertex.normal.x, vertex.normal.y, vertex.normal.z);
            if (texture_id != 0u) glTexCoord2f(vertex.uv.x, 1.0f - vertex.uv.y);
            glVertex3f(vertex.position.x, vertex.position.y, vertex.position.z);
        }
        glEnd();
        glPopMatrix();
    }

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
}

void Rasterizer::shutdown()
{
    if (use_path_tracer_) path_tracer_.shutdown();

    for (const auto& [handle, texture_id] : textures_) {
        (void)handle;
        const GLuint id = static_cast<GLuint>(texture_id);
        if (id != 0u) glDeleteTextures(1, &id);
    }
    textures_.clear();

    use_path_tracer_ = false;
    initialized_ = false;
}

} // namespace Renderer
