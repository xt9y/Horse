# Point-Light Path Tracer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace raster + hybrid GI with a cached OpenGL 4.3 compute path tracer using one ECS point light and progressive multi-bounce GI.

**Architecture:** `Renderer::PathTracer` owns GPU resources, scene caches, BVH/TLAS data, accumulation, and presentation. Generic ECS/Models/Animation remain independent. Static mesh BLAS data is cached; dynamic skinned BLAS data is keyed by pose revision; accumulation resets only on relevant scene/camera/light changes.

**Tech Stack:** C++20, OpenGL 4.3 compute/SSBO/image load-store through lwcgl v2.9.3, existing generic ECS, Models, Animation.

**Spec:** `docs/superpowers/specs/2026-09-06-path-tracer-design.md`

## Global Constraints

- Preserve generic ECS, Models/FBX, textures, and animation.
- Use only ECS point light illumination.
- No GBuffer, shadow maps, PCF/PCSS, fake ambient, or hybrid GI.
- Default half-resolution tracing, 1 sample/pixel/frame, 3 diffuse bounces.
- Static BLAS construction must not repeat because the camera moved.
- Do not touch workflow files.

---

### Task 1: Public path-tracer boundary

**Files:**
- Create: `Sources/Renderer/PathTracer/PathTracer.hpp`
- Modify: `Sources/Renderer/Render.hpp`
- Delete after migration: `Sources/Renderer/Gi/Gi.hpp`, `Sources/Renderer/Gi/Gi.cpp`

**Interfaces:**
- Produces `Renderer::PathTracer::{init,resize,render,shutdown,settings}`.
- `Render.hpp` provides `using Rasterizer = PathTracer` for compatibility.

- [x] Define `PathTracerSettings { enabled=true; resolution_divisor=2; samples_per_frame=1; max_bounces=3; exposure=1.0f; }`.
- [x] Define non-copyable `PathTracer` with PImpl.
- [x] Replace GI includes/accessors in `Render.hpp` with the compatibility alias.
- [x] Verify headers contain no GI/shadow APIs.

### Task 2: Scene cache and CPU acceleration data

**Files:**
- Create: `Sources/Renderer/PathTracer/PathTracer.cpp`

**Interfaces:**
- Internal `GpuNode`, `GpuTriangle`, `GpuMaterial`, `GpuInstance`, `MeshCache`.
- `syncScene(const Ecs::World&)` returns whether geometry/instances/material bindings changed.

- [x] Build local-space triangle arrays from `Models::MeshData` including UV/material data.
- [x] Build bounded median-split BVHs with <=8 triangles/leaf.
- [x] Cache static mesh BLAS by mesh handle.
- [x] For entities with `SkinBindingComponent`, CPU-skin vertices using current pose and rebuild only when pose revision changes.
- [x] Upload flattened BLAS node/triangle arrays and instance records to SSBOs.
- [x] Hash instance transforms/membership/mesh/material/pose revision so unchanged frames do not upload/rebuild scene data.

### Task 3: Compute path tracer

**Files:**
- Create: `Sources/Renderer/PathTracer/PathTracerShaders.hpp`
- Modify: `Sources/Renderer/PathTracer/PathTracer.cpp`

**Interfaces:**
- Compute shader inputs: camera vectors, point light, SSBO scene data, material records, texture samplers, accumulation image.

- [x] Generate jittered primary rays from camera FOV/aspect and transform.
- [x] Traverse TLAS and BLAS BVHs with bounded 48-entry stacks and 2048-step guards.
- [x] At closest hit, evaluate textured/base-color diffuse material.
- [x] Perform next-event estimation to the point light with an occlusion ray and inverse-square attenuation.
- [x] Continue with cosine-weighted diffuse sampling up to `max_bounces`.
- [x] Use black radiance on misses; use no ambient/environment term.
- [x] Add each stochastic sample to RGBA32F accumulation and increment sample count after dispatch.

### Task 4: Progressive cache invalidation and present

**Files:**
- Modify: `Sources/Renderer/PathTracer/PathTracer.cpp`

**Interfaces:**
- `sceneSignature()`, `cameraSignature()`, `lightSignature()` drive `resetAccumulation()`.

- [x] Reset accumulation on resize, camera change, active point-light change, transform/material/geometry change, or animation pose revision change.
- [x] Keep accumulation while signatures are unchanged.
- [x] Present average accumulated radiance with exposure, Reinhard tone map, and gamma 2.2 via a minimal fullscreen shader.
- [x] Log initialization and scene-cache rebuild statistics.

### Task 5: GAME point light

**Files:**
- Modify: `xt9y/GAME/main.cpp`

**Interfaces:**
- Uses existing `Renderer::LightComponent` with `LightType::Point` and `Renderer::Transform`.

- [x] Create one point-light entity based on Sponza bounds.
- [x] Scale point-light intensity from scene extent for inverse-square attenuation.
- [x] Keep existing models/camera/update flow; only start Glock animation when a clip name is explicitly requested so static accumulation can converge by default.

### Task 6: Static verification

**Files:** none

- [x] Search runtime source for `GI`, `shadowVisibility`, `GL_LIGHT0`, `glLight`, `GBuffer`, `PCSS`, `PCF`; old renderer implementation is removed.
- [x] Verify `PathTracer`, point-light selection, accumulation reset, and bounded traversal guards are present.
- [x] Verify `build.c` nested source glob includes `Sources/Renderer/PathTracer/*.cpp`.
- [x] Verify the implementation compare contains no `.github/workflows` changes.
- [ ] Local environment verification: `c build` in ECS-MODEL-RASTERIZER, then `c update ecs-model-rasterizer && c build run` in GAME.
