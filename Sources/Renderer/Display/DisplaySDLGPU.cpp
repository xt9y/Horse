#include "Renderer/Internal/Display.hpp"

#include "Camera/Camera.hpp"
#include "Renderer/SDLGPU/Context.hpp"

#include <SDL3/SDL_gpu.h>

#include <algorithm>
#include <cstdint>

namespace Renderer::Internal {

bool renderDisplay(const Ecs::World& world, FrameOutput& output)
{
    if (output.api != GraphicsApi::SDLGPU || !output.command ||
        !output.color_texture || !output.display_texture)
        return false;

    float exposure_ev = 0.0f;
    std::uint32_t tone_mapping = static_cast<std::uint32_t>(Camera::ToneMapping::ACES);
    const Ecs::Entity camera = Camera::activeCamera(world);
    if (camera != Ecs::INVALID_ENTITY) {
        if (const Camera::CameraComponent *component = world.get<Camera::CameraComponent>(camera)) {
            exposure_ev = component->exposure_ev;
            tone_mapping = static_cast<std::uint32_t>(component->tone_mapping);
        }
    }

    auto *command = static_cast<SDL_GPUCommandBuffer *>(output.command);
    auto *source = static_cast<SDL_GPUTexture *>(output.color_texture);
    auto *destination = static_cast<SDL_GPUTexture *>(output.display_texture);
    if (source == destination) return true;

    if (!SDLGPU::transformColor(
            command,
            source,
            destination,
            static_cast<std::uint32_t>(std::max(output.width, 1)),
            static_cast<std::uint32_t>(std::max(output.height, 1)),
            exposure_ev,
            tone_mapping,
            false))
        return false;

    output.color_texture = destination;
    return true;
}

} // namespace Renderer::Internal
