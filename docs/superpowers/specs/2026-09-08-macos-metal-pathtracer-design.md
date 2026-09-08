# macOS Metal PathTracer Backend Design

## Goal

Keep `Renderer::PathTracer` as the only renderer API while making the existing path tracer run natively on macOS through `lwmgl`/Metal instead of falling back to rasterization.

## Constraints

- Work directly on `main`; do not create temporary branches or worktrees.
- Preserve the current repository structure and public ECS/model/animation APIs.
- Do not add an engine layer or a public renderer-backend abstraction.
- `Renderer::PathTracer` remains the public API used by GAME.
- Linux and other platforms with OpenGL 4.3 compute keep the existing OpenGL path tracer.
- macOS uses a native Metal compute/presentation backend through `lwmgl`.
- No fixed-function or other rasterizer fallback is allowed.
- Preserve the current CPU-side scene preparation: transforms, skinning, material assignment, world-space triangle generation, BVH generation, camera/light extraction, scene signatures, and accumulation reset rules.
- Preserve the current tracing behavior: progressive accumulation, phased pixel updates, cosine-weighted diffuse bounces, point-light direct lighting and shadow rays, texture sampling, exposure and tonemapping.
- M2 support must not depend on Metal hardware ray-tracing capability. The first Metal backend therefore consumes the same CPU-built BVH used by the OpenGL shader. `lwmgl` acceleration-structure APIs remain available independently.

## Architecture

`PathTracer.cpp` keeps the shared CPU logic and uses compile-time platform sections only for GPU resources and commands. There is no second public renderer class.

On non-Apple platforms:

- existing GLSL 4.3 compute shader;
- existing OpenGL SSBOs, accumulation texture and fullscreen present path.

On Apple platforms:

- `Metal.create(Display.getNativeWindow())` attaches a `CAMetalLayer` to the existing lwcgl/GLFW window;
- MSL compute shader mirrors the current GLSL path-tracing algorithm and buffer layouts;
- `lwmgl` buffers hold nodes, triangles, materials and frame uniforms;
- `lwmgl` textures hold the RGBA32F accumulation image and material textures;
- a Metal compute pipeline dispatches the tracer;
- a Metal render pipeline draws a fullscreen triangle to the drawable and performs the same average/exposure/tonemap/gamma conversion as the OpenGL present shader;
- `PathTracer::shutdown()` destroys Metal resources and calls `Metal.destroy()`, restoring the previous lwcgl window layer.

## Data layout

Existing GPU scene structs remain byte-compatible:

- `GpuNode`: 48 bytes;
- `GpuTriangle`: 128 bytes;
- `GpuMaterial`: 32 bytes.

The Metal per-frame uniform buffer uses only 16-byte vectors so C++ and MSL alignment is explicit and stable. Textures use fixed slots 0-15 just like the current GLSL path.

## Resize and lifecycle

- `PathTracer::init()` selects Metal at compile time on macOS and OpenGL elsewhere.
- macOS initialization fails clearly if `Display.getNativeWindow()` is unavailable or `lwmgl` cannot initialize.
- `resize()` updates the Metal drawable size and recreates only the trace-resolution accumulation texture when required.
- scene GPU buffers are recreated/uploaded only when the scene signature changes.
- per-frame uniforms are updated without recreating pipelines or shader libraries.
- no explicit `Metal.wait()` occurs in the normal frame path; presentation remains asynchronous. Synchronization is reserved for teardown/explicit idle requirements.

## Rasterizer cleanup

`Sources/Renderer/Render.cpp` is removed. `Sources/Renderer/Render.hpp` becomes only the renderer umbrella header that includes components and `PathTracer.hpp`. The `Rasterizer` class and compatibility fixed-function renderer disappear entirely.

GAME Earth and Sponza use `Renderer::PathTracer` directly.

## Build integration

On macOS Horse links installed `lwmgl` alongside `lwcgl` and includes `/usr/local/include/lwmgl-1.0.0`. Non-Apple builds do not require `lwmgl`.

The macOS link retains the existing Apple/OpenGL libraries required by lwcgl and additionally links `lwmgl` with `/usr/local/lib` rpath handling.

## Verification

1. `lwmgl` real lwcgl-window interop test must distinguish runner limitations from library failures and pass where an OpenGL-backed lwcgl window is available.
2. Add a Horse source/layout contract proving the public renderer has no `Rasterizer` type and the Metal shader/backend is present only for Apple compilation.
3. Build Horse on macOS against installed `lwcgl` + `lwmgl`.
4. Build GAME Earth and Sponza with `Renderer::PathTracer`.
5. Run Earth on macOS as the end-to-end regression target when the CI/host exposes a usable window; otherwise require compile/link verification in CI and the existing local Earth command for hardware validation.
