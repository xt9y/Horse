#include "Models/Internal/ResourcePump.hpp"

#include "Models/Internal/AsyncLoading.hpp"
#include "Models/Internal/TextureStreaming.hpp"

namespace Models::Internal {

std::size_t pumpResources()
{
    return pumpModelLoads() + pumpTextureResources();
}

} // namespace Models::Internal
