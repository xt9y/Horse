#include "Renderer/Features.hpp"

namespace Renderer::Features {
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

} // namespace Renderer::Features
