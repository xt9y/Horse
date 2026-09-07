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
- Default 1 sample/pixel/frame, 3 diffuse bounces.
- Static geometry must not rebuild because the camera moved.
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

- [ ] Define `PathTracerSettings { enabled=true; samples_per_frame=1; max_bounces=3; exposure=1.0f; }`.
- [ ] Define non-copyable `PathTracer` with PImpl.
- [ ] Replace GI includes/accessors in `Render.hpp` with the compatibility alias.
- [ ] Verify headers contain no GI/shadow APIs.

### Task 2: Scene cache and CPU acceleration data

**Files:**
- Create: `Sources/Renderer/PathTracer/PathTracer.cpp`

**Interfaces:**
- Internal `GpuNode`, `GpuTriangle`, `GpuMaterial`, `GpuInstance`, `MeshCache`.
- `syncScene(const Ecs::World&)` returns whether geometry/instances/material bindings changed.

- [ ] Build local-space triangle arrays from `Models::MeshData` including UV/material data.
- [ ] Build bounded median-split BVHs with <=8 triangles/leaf.
- [ ] Cache static mesh BLAS by mesh handle.
- [ ] For entities with `SkinBindingComponent`, CPU-skin vertices using current pose and rebuild only when pose revision changes.
- [ ] Upload flattened BLAS node/triangle arrays and instance records to SSBOs.
- [ ] Hash instance transforms/membership/mesh/material/pose revision so unchanged frames do not upload/rebuild scene data.

### Task 3: Compute path tracer

**Files:**
- Modify: `Sources/Renderer/PathTracer/PathTracer.cpp`

**Interfaces:**
- Compute shader inputs: camera matrices/vectors, point light, SSBO scene data, material records, texture samplers, accumulation image.

- [ ] Generate jittered primary rays from camera FOV/aspect and transform.
- [ ] Traverse instance list and BLAS BVHs with bounded stack/step counts.
- [ ] At closest hit, evaluate textured/base-color diffuse material.
- [ ] Perform next-event estimation to the point light with an occlusion ray and inverse-square attenuation.
- [ ] Continue with cosine-weighted diffuse sampling up to `max_bounces`.
- [ ] Use black radiance on misses; use no ambient/environment term.
- [ ] Add each stochastic sample to RGBA32F accumulation and increment sample count only after successful dispatch.

### Task 4: Progressive cache invalidation and present

**Files:**
- Modify: `Sources/Renderer/PathTracer/PathTracer.cpp`

**Interfaces:**
- `sceneSignature()`, `cameraSignature()`, `lightSignature()` drive `resetAccumulation()`.

- [ ] Reset accumulation on resize, camera change, active point-light change, transform/material/geometry change, or animation pose revision change.
- [ ] Keep accumulation indefinitely while signatures are unchanged.
- [ ] Present average accumulated radiance with exposure, Reinhard tone map, and gamma 2.2 via a minimal fullscreen shader.
- [ ] Log initialization and scene-cache rebuild statistics once per rebuild.

### Task 5: GAME point light

**Files:**
- Modify: `xt9y/GAME/main.cpp`

**Interfaces:**
- Uses existing `Renderer::LightComponent` with `LightType::Point` and `Renderer::Transform`.

- [ ] Create one point-light entity near/above Sponza after camera setup.
- [ ] Set white color and an intensity suitable for inverse-square attenuation.
- [ ] Keep existing models, animation update, camera update, and renderer call flow unchanged.

### Task 6: Static verification

**Files:** none

- [ ] Search source for `GI`, `shadowVisibility`, `GL_LIGHT0`, `glLight`, `GBuffer`, `PCSS`, `PCF`; runtime renderer source must contain none of them.
- [ ] Search for `PathTracer`, point-light selection, accumulation reset, and bounded traversal guards.
- [ ] Verify `build.c` nested source glob already includes `Sources/Renderer/PathTracer/*.cpp`.
- [ ] Verify no `.github/workflows` file changed.
- [ ] Local user verification: `c build` in ECS-MODEL-RASTERIZER, then `c update ecs-model-rasterizer && c build run` in GAME.
