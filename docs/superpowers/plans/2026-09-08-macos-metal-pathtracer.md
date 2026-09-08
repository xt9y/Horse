# macOS Metal PathTracer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the existing `Renderer::PathTracer` run natively on macOS through `lwmgl`/Metal while retaining the OpenGL 4.3 backend elsewhere and removing the rasterizer fallback.

**Architecture:** Keep one public `PathTracer` class and one shared CPU scene-preparation path. Platform-specific code inside `PathTracer.cpp` owns either OpenGL or `lwmgl` resources; a new MSL header mirrors the current GLSL compute/present behavior. GAME uses `Renderer::PathTracer` directly.

**Tech Stack:** C++20, lwcgl 2.9.3, lwmgl 1.0.0, OpenGL 4.3 compute, Metal Shading Language, C-BuildSystem.

**Spec:** `docs/superpowers/specs/2026-09-08-macos-metal-pathtracer-design.md`

## Global Constraints

- Work directly on `main`; do not create temporary branches or worktrees.
- Preserve current repository structure and public ECS/model/animation APIs.
- No engine layer and no public renderer-backend abstraction.
- `Renderer::PathTracer` remains the only renderer API.
- No rasterizer fallback.
- Preserve the current CPU BVH and path-tracing behavior.
- M2 support must not depend on hardware ray tracing.

---

### Task 1: Prove lwcgl/lwmgl window interop boundary

**Files:**
- Modify: `xt9y/lwmgl/tests/lwcgl_interop.c`
- Modify: `xt9y/lwmgl/tests/lwcgl_interop.cpp`
- Modify only if root cause is in wrapper runtime: `xt9y/lwcgl/src/display.c`

**Interfaces:**
- Consumes: `Display.create()`, `Display.getNativeWindow()`, `Metal.create(void*)`, `Metal.destroy()`.
- Produces: a verified contract that a real lwcgl GLFW window can host and release lwmgl without destroying the Display.

- [ ] **Step 1: Capture the exact failing context request and GLFW error in the existing failing interop test.**

The failure diagnostic must print `lwcglRequestedContextMajorVersion()`, `lwcglRequestedContextMinorVersion()`, `lwcglRequestedContextProfile()` and `glfwGetError()` after `Display.create()` fails.

- [ ] **Step 2: Run the macOS ARM64 CI and verify RED is caused by either an actual GLFW context failure or a wrapper bug, not Metal attachment.**

Expected: failure occurs before `Metal.create()` and reports the concrete GLFW reason.

- [ ] **Step 3: If the wrapper is at fault, add a failing lwcgl runtime test first and fix only that root cause; if the hosted runner itself has no OpenGL context, keep the interop test as a host-capability gate instead of modifying runtime semantics.**

- [ ] **Step 4: Re-run the full lwmgl suite.**

Expected: normal lwmgl tests, sanitizer/package checks and interop pass on a host exposing the required window capability; runner-unavailable OpenGL is explicitly classified rather than reported as a Metal failure.

---

### Task 2: Remove the rasterizer fallback contract

**Files:**
- Create: `tests/path_tracer_renderer_contract.cpp`
- Modify: `Sources/Renderer/Render.hpp`
- Delete: `Sources/Renderer/Render.cpp`

**Interfaces:**
- Consumes: `Renderer::PathTracer` from `Renderer/PathTracer/PathTracer.hpp`.
- Produces: umbrella header with no `Renderer::Rasterizer` class.

- [ ] **Step 1: Write the failing source contract.**

```cpp
#include "Sources/Renderer/Render.hpp"
#include <type_traits>

int main()
{
    static_assert(std::is_default_constructible_v<Renderer::PathTracer>);
    return 0;
}
```

The contract build additionally searches `Render.hpp`/`Render.cpp` for `class Rasterizer`, `renderCompatibility` and fixed-function compatibility implementation symbols and must fail while they exist.

- [ ] **Step 2: Verify RED against current main.**

Expected: source-layout check fails because `Rasterizer` and `Render.cpp` exist.

- [ ] **Step 3: Replace `Render.hpp` with the umbrella-only header.**

```cpp
#ifndef RW_ENGINE_RENDER_HPP
#define RW_ENGINE_RENDER_HPP

#include "Renderer/Components.hpp"
#include "Renderer/PathTracer/PathTracer.hpp"

#endif
```

Delete `Sources/Renderer/Render.cpp`.

- [ ] **Step 4: Re-run the renderer contract.**

Expected: PASS with no fallback symbols.

---

### Task 3: Add the Metal shader and resource backend

**Files:**
- Create: `Sources/Renderer/PathTracer/PathTracerMetalShaders.hpp`
- Modify: `Sources/Renderer/PathTracer/PathTracer.hpp`
- Modify: `Sources/Renderer/PathTracer/PathTracer.cpp`
- Modify: `build.c`
- Create: `tests/path_tracer_metal_contract.cpp`

**Interfaces:**
- Consumes: `lwmgl` API through `<lwmgl/lwmgl.h>` and `Display.getNativeWindow()` from lwcgl.
- Produces: Apple implementation of the existing `PathTracer::{init,resize,render,shutdown}` API.

- [ ] **Step 1: Write a failing Metal source contract.**

The test/source check requires Apple code to reference `Metal.create`, `Metal.createBuffer`, `Metal.createTexture`, `Metal.createLibraryFromSource`, `Metal.createComputePipeline`, `Metal.dispatch`, `Metal.beginRenderToDrawable`, `Metal.draw`, `Metal.present`, and `Metal.destroy`, and requires `PathTracer.hpp` to contain no OpenGL types/includes.

- [ ] **Step 2: Verify RED.**

Expected: current PathTracer is OpenGL-only and the Metal shader header is absent.

- [ ] **Step 3: Remove OpenGL declarations from the public PIMPL header.**

`PathTracer.hpp` must include only `Ecs/Ecs.hpp` and ordinary C++ declarations; all GL/Metal headers remain private to `.cpp`.

- [ ] **Step 4: Add MSL structs with exact byte-compatible scene layout.**

MSL definitions:

```metal
struct Node {
    packed_float3 bmin; uint first;
    packed_float3 bmax; uint meta;
    uint4 extra;
};

struct Triangle {
    float4 p0; float4 p1; float4 p2;
    float4 n0; float4 n1; float4 n2;
    float4 uv01; float4 uv2;
};

struct Material {
    float4 base_color;
    int4 data;
};
```

Use explicit padding/static assertions in C++ so sizes stay `48/128/32` bytes.

- [ ] **Step 5: Add a 16-byte-vector Metal frame-uniform block.**

C++ and MSL must use matching groups for camera vectors, light vectors/scalars, resolution/aspect, counts, sample/frame/reset/bounce state and light-present state.

- [ ] **Step 6: Port the current GLSL tracing algorithm to MSL.**

Keep the same constants and behavior: `LEAF_BIT`, `RAY_EPSILON`, closest/any BVH traversal, texture slot switch 0-15, gamma decode, cosine hemisphere sampling, max 1-4 diffuse bounces, point-light inverse-square direct contribution, phased 4x4 reset / 2x2 stationary updates, accumulation alpha as sample count.

- [ ] **Step 7: Port the fullscreen present shader to MSL.**

Keep incomplete-block reconstruction, average by accumulation alpha, exposure multiplication, Reinhard mapping and `1/2.2` gamma.

- [ ] **Step 8: Add Apple GPU resources to `PathTracer::Impl`.**

Use `LWMGLLibrary`, `LWMGLFunction`, `LWMGLComputePipeline`, `LWMGLRenderPipeline`, `LWMGLTexture`, `LWMGLSampler` and `LWMGLBuffer`. Scene buffers are recreated only on scene sync; fixed-size frame-uniform buffer is updated per dispatch.

- [ ] **Step 9: Implement Apple texture cache and buffer upload helpers.**

Diffuse textures: `LWMGL_RGBA8_UNORM`, sampled, shared storage, linear/repeat sampler. Accumulation: `LWMGL_RGBA32_FLOAT`, sampled/read/write, private storage.

- [ ] **Step 10: Implement Apple `init()`.**

Use:

```cpp
if (!Display.getNativeWindow()) return false;
if (Metal.create(Display.getNativeWindow()) != 0) return false;
```

Then create library/functions/pipelines/sampler/uniform buffer/accumulation texture. No GL 4.3 capability check is executed on Apple.

- [ ] **Step 11: Implement Apple dispatch/present.**

One command buffer per frame: begin compute, bind pipeline/buffers/textures/sampler, dispatch `(trace_width+7)/8 x (trace_height+7)/8`, end compute, begin drawable render, bind present pipeline/accumulation/uniforms, draw fullscreen triangle, end, present, commit, destroy command. Do not wait in the frame path.

- [ ] **Step 12: Implement Apple resize/shutdown.**

Resize the drawable, recreate accumulation only when trace resolution changes, destroy every owned handle, call `Metal.waitIdle()` before final teardown when created, then `Metal.destroy()`.

- [ ] **Step 13: Update `build.c`.**

On Apple add:

```c
c_include(target, "/usr/local/include/lwmgl-1.0.0");
c_link_flag(target, "-llwmgl");
```

Keep `/usr/local/lib` and rpath handling already present. Non-Apple builds do not reference lwmgl.

- [ ] **Step 14: Run Metal source contract and build Horse on macOS.**

Expected: PASS/compile/link with no OpenGL 4.3 requirement on the Apple code path.

---

### Task 4: Restore GAME to PathTracer directly

**Files:**
- Modify: `xt9y/GAME/Examples/earth.cpp`
- Modify: `xt9y/GAME/Examples/sponza.cpp`
- Modify: `xt9y/GAME/build.c`
- Create: `xt9y/GAME/tests/path_tracer_example_contract.cpp`

**Interfaces:**
- Consumes: Horse `Renderer::PathTracer` and Apple-linked lwmgl.
- Produces: Earth/Sponza examples that instantiate the path tracer directly.

- [ ] **Step 1: Write a failing source contract that rejects `Renderer::Rasterizer` in Earth/Sponza and requires `Renderer::PathTracer`.**

- [ ] **Step 2: Verify RED on current GAME main.**

- [ ] **Step 3: Replace renderer members/construction with `Renderer::PathTracer`.**

Keep the existing init/resize/render/shutdown/settings calls unchanged.

- [ ] **Step 4: On Apple add lwmgl include/link flags to GAME consumers if required for transitive shared-library resolution.**

Use `/usr/local/include/lwmgl-1.0.0`, `-L/usr/local/lib`, `-llwmgl`, existing rpath.

- [ ] **Step 5: Build Earth and Sponza.**

Expected: both compile/link using `PathTracer`; no `Rasterizer` symbol remains.

---

### Task 5: End-to-end verification

**Files:**
- Modify/add CI only if necessary to encode repeatable build checks.

**Interfaces:**
- Consumes: installed lwcgl 2.9.3, lwmgl 1.0.0, Horse main, GAME main.
- Produces: verified Mac path-traced build.

- [ ] **Step 1: Run the complete lwmgl normal, sanitizer, package and lwcgl-interoperability checks.**

- [ ] **Step 2: Build/install latest lwcgl v2.9.3 and latest lwmgl main.**

- [ ] **Step 3: Build Horse shared library on macOS.**

- [ ] **Step 4: Build GAME Earth and Sponza.**

- [ ] **Step 5: Run Earth where a drawable-capable macOS host is available.**

Expected startup path on Apple reports Metal/lwmgl PathTracer initialization, never `OpenGL 4.3 compatibility context required` and never a rasterizer fallback.

- [ ] **Step 6: Search all three repositories for stale fallback references.**

Reject remaining production/example occurrences of `Renderer::Rasterizer`, `renderCompatibility`, `[Rasterizer]: compatibility`, or a conditional fallback from PathTracer to fixed-function OpenGL.
