#ifndef HORSE_RENDERER_TRACE_OPENGL_MATERIAL_RESOURCES_HPP
#define HORSE_RENDERER_TRACE_OPENGL_MATERIAL_RESOURCES_HPP

#ifndef __APPLE__

#include "Renderer/Trace/MaterialSet.hpp"

#include <string>

namespace Renderer::Trace::OpenGL {

class MaterialResources {
public:
    MaterialResources();
    ~MaterialResources();
    MaterialResources(const MaterialResources&) = delete;
    MaterialResources& operator=(const MaterialResources&) = delete;

    bool sync(const MaterialSet& materials, std::string *error = nullptr);
    void bind() const;
    void clear();
    bool ready() const;

private:
    struct Impl;
    Impl *impl_ = nullptr;
};

} // namespace Renderer::Trace::OpenGL

#endif
#endif
