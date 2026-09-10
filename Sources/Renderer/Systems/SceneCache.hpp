#ifndef RW_ENGINE_RENDERER_SYSTEMS_SCENE_CACHE_COMPAT_HPP
#define RW_ENGINE_RENDERER_SYSTEMS_SCENE_CACHE_COMPAT_HPP
#include "Renderer/Systems/Scene.hpp"
#include "Renderer/Scenes/SceneCache.hpp"
namespace Renderer::Systems {
using SceneCache = Renderer::Scenes::SceneCache;
using GpuNode = Renderer::Scenes::GpuNode;
using GpuTriangle = Renderer::Scenes::GpuTriangle;
using GpuMaterial = Renderer::Scenes::GpuMaterial;
using CameraState = Renderer::Scenes::CameraState;
using LightState = Renderer::Scenes::LightState;
inline constexpr std::uint32_t LeafBit = Renderer::Scenes::LeafBit;
inline constexpr std::uint32_t LeafSize = Renderer::Scenes::LeafSize;
inline constexpr std::size_t MaximumTriangles = Renderer::Scenes::MaximumTriangles;
using Renderer::Scenes::cameraState;
using Renderer::Scenes::lightState;
using Renderer::Scenes::cameraSignature;
using Renderer::Scenes::lightSignature;
}
#endif
