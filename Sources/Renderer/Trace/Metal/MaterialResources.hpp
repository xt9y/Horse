#ifndef HORSE_RENDERER_TRACE_METAL_MATERIAL_RESOURCES_HPP
#define HORSE_RENDERER_TRACE_METAL_MATERIAL_RESOURCES_HPP

#ifdef __APPLE__

#include "Renderer/Trace/MaterialSet.hpp"

#include <lwmgl/lwmgl.h>

#include <cstdint>
#include <string>

namespace Renderer::Trace::Metal {

class MaterialResources {
public:
    MaterialResources();
    ~MaterialResources();
    MaterialResources(const MaterialResources&) = delete;
    MaterialResources& operator=(const MaterialResources&) = delete;

    bool sync(const MaterialSet& materials, std::string *error = nullptr);
    bool bind(LWMGLCommand command, std::uint32_t first_texture_binding = 1u) const;
    void clear();
    bool ready() const;

private:
    struct Impl;
    Impl *impl_ = nullptr;
};

} // namespace Renderer::Trace::Metal

#endif
#endif
