#include "Renderer/AmbientOcclusion/AmbientOcclusion.hpp"

namespace Renderer::AmbientOcclusion {
namespace {

Settings value;

} // namespace

Settings& settings()
{
    return value;
}

const Settings& currentSettings()
{
    return value;
}

void setQuality(Quality quality_value)
{
    value.quality = quality_value;
    switch (quality_value) {
    case Quality::Low:
        value.sample_count = 4u;
        break;
    case Quality::Medium:
        value.sample_count = 6u;
        break;
    case Quality::High:
        value.sample_count = 8u;
        break;
    case Quality::Ultra:
        value.sample_count = 12u;
        break;
    }
}

Quality quality()
{
    return value.quality;
}

} // namespace Renderer::AmbientOcclusion
