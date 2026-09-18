#include "Renderer/Internal/ShadowCache.hpp"

#include <cassert>

int main()
{
    using namespace Renderer::RasterizerSDLGPU::ShadowCache;

    Key local{};
    local.geometry_revision = 10u;
    local.light_signature = 20u;
    local.camera_signature = 30u;
    local.view = 2u;
    local.width = 1280u;
    local.height = 720u;
    local.resolution = 1024u;
    local.cascades = 4u;
    local.distance = 80.0f;
    local.near_plane = 0.05f;
    local.camera_dependent = false;

    Key moved_camera = local;
    moved_camera.camera_signature = 31u;
    moved_camera.width = 1920u;
    moved_camera.height = 1080u;
    assert(signature(local) == signature(moved_camera));

    Key directional = local;
    directional.camera_dependent = true;
    Key directional_moved = directional;
    directional_moved.camera_signature = 31u;
    assert(signature(directional) != signature(directional_moved));

    Key geometry_changed = local;
    geometry_changed.geometry_revision++;
    assert(signature(local) != signature(geometry_changed));

    Key light_changed = local;
    light_changed.light_signature++;
    assert(signature(local) != signature(light_changed));

    Key view_changed = local;
    view_changed.view++;
    assert(signature(local) != signature(view_changed));

    Key settings_changed = local;
    settings_changed.near_plane = 0.1f;
    assert(signature(local) != signature(settings_changed));

    return 0;
}
