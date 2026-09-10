#ifndef RW_ENGINE_RENDERER_PATHTRACER_OPENGL_SHADERS_HPP
#define RW_ENGINE_RENDERER_PATHTRACER_OPENGL_SHADERS_HPP
#define trace legacy_trace
#define present_vertex legacy_present_vertex
#define present_fragment legacy_present_fragment
#include "Renderer/PathTracer/OpenGL/PathTracerWorldFastShaders.hpp"
#undef present_fragment
#undef present_vertex
#undef trace
#include "Renderer/PathTracer/OpenGL/PathTracerSharedGITrace.hpp"
#include "Renderer/PathTracer/OpenGL/PathTracerPresentShaders.hpp"
#endif
