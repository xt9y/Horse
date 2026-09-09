#ifndef RW_ENGINE_RENDERER_PATHTRACER_SHADERS_HPP
#define RW_ENGINE_RENDERER_PATHTRACER_SHADERS_HPP

#define trace legacy_trace
#define present_vertex legacy_present_vertex
#define present_fragment legacy_present_fragment
#include "Renderer/PathTracer/PathTracerWorldFastShaders.hpp"
#undef present_fragment
#undef present_vertex
#undef trace

#include "Renderer/PathTracer/PathTracerSharedGITrace.hpp"
#include "Renderer/PathTracer/PathTracerPresentShaders.hpp"

#endif
