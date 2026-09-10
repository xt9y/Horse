#include "Renderer/HorizonGI/HorizonGI.hpp"

#include <algorithm>

namespace Renderer::HorizonGI {

Settings sanitized(Settings settings)
{
    settings.pass = Quality::sanitized(settings.pass);
    settings.directions = std::clamp(settings.directions, 1, MaximumDirections);
    settings.steps = std::clamp(settings.steps, 1, MaximumSteps);
    settings.radius = std::max(settings.radius, 0.001f);
    settings.thickness = std::max(settings.thickness, 0.0f);
    settings.ao_strength = std::max(settings.ao_strength, 0.0f);
    settings.indirect_strength = std::max(settings.indirect_strength, 0.0f);
    return settings;
}

} // namespace Renderer::HorizonGI
