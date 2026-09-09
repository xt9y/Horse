# Explicit Rasterizer / PathTracer Renderer Architecture

## Goal

Reintroduce rasterization as an explicit, user-selected renderer in Horse without restoring any automatic fallback behavior. `Renderer::PathTracer` and `Renderer::Rasterizer` remain independently constructible backends that implement the same renderer lifecycle and consume the same ECS/model scene data.

The change must also reduce duplication by extracting renderer-independent scene interpretation out of the backend implementations.

## Non-goals

- No automatic `PathTracer -> Rasterizer` fallback.
- No automatic `Rasterizer -> PathTracer` upgrade.
- No runtime backend enum/facade that hides which renderer the user selected.
- No Earth- or Sponza-specific behavior.
- No change to the public ECS component layout.
- No Metal rasterizer in this pass.
- No requirement to rewrite the existing path-tracing algorithms.

## Public API

Horse exposes a common renderer contract plus two concrete renderers.

```cpp
namespace Renderer {

class IRenderer {
public:
    virtual ~IRenderer() = default;

    virtual bool init() = 0;
    virtual void resize(int width, int height) = 0;
    virtual void render(const Ecs::World& world) = 0;
    virtual void shutdown() = 0;

    virtual bool initialized() const = 0;
    virtual bool enabled() const = 0;
    virtual void setEnabled(bool enabled) = 0;
};

class PathTracer final : public IRenderer { ... };
class Rasterizer final : public IRenderer { ... };

}
```

User selection stays explicit:

```cpp
Renderer::PathTracer renderer;
```

or:

```cpp
Renderer::Rasterizer renderer;
```

The existing `PathTracerSettings` API remains on `PathTracer`. Rasterizer-specific settings may be added only if they are genuinely needed by rasterization; PathTracer settings are never exposed through `Rasterizer`.

## Renderer/Common split

Create a renderer-common layer whose job is to interpret Horse ECS/model state once and provide backend-neutral render data.

Suggested layout:

```text
Sources/Renderer/
├── Renderer.hpp
├── Components.hpp
├── Scene.hpp
├── Scene.cpp
├── Render.hpp
├── PathTracer/
│   ├── PathTracer.hpp
│   ├── PathTracer.cpp
│   ├── PathTracerMetal.cpp
│   └── ...
└── Rasterizer/
    ├── Rasterizer.hpp
    └── Rasterizer.cpp
```

`Render.hpp` remains the umbrella include and exposes the common contract plus both concrete backends.

## Shared scene representation

The common scene layer owns renderer-independent interpretation of `Ecs::World`.

It provides compact views/state rather than copying all mesh data each frame.

### Camera state

```cpp
struct CameraState {
    Ecs::Entity entity;
    Transform transform;
    float fov_degrees;
    float near_plane;
    bool valid;
};
```

Responsibilities:

- resolve `Camera::activeCamera(world)`;
- fetch `Renderer::Transform` and `Camera::CameraComponent`;
- provide a single validity decision;
- expose camera values in backend-neutral form.

### Light state

```cpp
struct LightState {
    Ecs::Entity entity;
    Transform transform;
    LightComponent light;
    bool valid;
};
```

Responsibilities:

- select the first active light using one shared policy;
- preserve light type, transform, color, and intensity;
- provide the same selection semantics to both PathTracer and Rasterizer.

If no light exists, the common layer may expose `valid = false`; backend-specific default lighting remains backend behavior and must be explicit.

### Render items

```cpp
struct RenderItem {
    Ecs::Entity entity;
    const Transform* transform;
    const MeshComponent* mesh_component;
    const Models::MeshData* mesh;
    const Models::MaterialData* material;
};
```

A shared iterator/helper filters entities consistently:

- `RenderableComponent` exists and is visible;
- `MeshComponent` exists;
- `Transform` exists;
- mesh handle resolves;
- invalid indices/material handles are handled consistently.

The common layer does not upload textures or create GPU resources.

## Shared transform/math utilities

Move renderer-independent transform math out of PathTracer-private implementation into renderer-common code.

Shared utilities include:

- model matrix construction;
- inverse model matrix construction;
- point transformation;
- normal transformation;
- camera/view transform helpers where useful;
- common small vector/matrix operations required by both renderers.

The goal is not to create a large generic math library. Only operations already duplicated or immediately reusable by both renderers are extracted.

Transform semantics must remain identical to current Horse behavior.

## PathTracer integration

`PathTracer` keeps all path-tracing-specific responsibilities:

- BVH construction/traversal data;
- accumulation;
- sparse moving-camera sampling;
- texture slot packing;
- material GPU packing;
- bounce/shadow logic;
- GLSL compute backend;
- Metal compute backend;
- PathTracer settings.

It consumes the shared `CameraState`, `LightState`, render-item traversal, and transform utilities instead of duplicating those pieces in both `PathTracer.cpp` and `PathTracerMetal.cpp`.

This refactor must not change PathTracer output intentionally.

## Rasterizer design

`Rasterizer` is a standalone backend with no `PathTracer` member and no knowledge of PathTracer support.

Initial implementation uses the already-proven lwcgl OpenGL compatibility path.

Responsibilities:

- initialize raster-only OpenGL state;
- own rasterizer GPU texture objects/cache;
- build projection/view state from shared camera data;
- submit shared render items as triangles;
- apply material color, diffuse texture, opacity, normals, and UVs;
- apply shared light selection;
- handle resize/viewport;
- clean up only rasterizer-owned GPU resources.

It must not contain:

- `supportsPathTracer()`;
- `PathTracer path_tracer_`;
- `use_path_tracer_`;
- calls to `PathTracer::init/render/shutdown`;
- any fallback message or branch.

If rasterizer initialization fails, `Rasterizer::init()` returns `false`.

## Rasterizer enabled state

Rasterizer owns its own simple enabled flag:

```cpp
bool enabled_ = true;
```

`setEnabled(false)` causes `render()` to skip scene submission and clear according to rasterizer policy. It does not mutate any PathTracer state.

## Platform behavior

### Linux

- `PathTracer`: existing OpenGL 4.3 compute backend.
- `Rasterizer`: lwcgl OpenGL raster backend.

### macOS

- `PathTracer`: existing Metal backend via lwmgl.
- `Rasterizer`: lwcgl OpenGL raster backend for this pass.

This is explicitly user-selected. Horse does not inspect platform/capabilities and silently choose between the two renderers.

A future Metal rasterizer may replace `Rasterizer` internals behind the same public API without affecting user code.

## Error handling

Backend initialization errors remain backend-specific and explicit.

Examples:

```text
[PathTracer]: Metal initialization failed: ...
```

```text
[Rasterizer]: OpenGL initialization failed: ...
```

Failure of one backend must never cause Horse to instantiate or initialize the other backend.

## Build integration

Horse builds both public renderer classes into `libHorse`.

The existing platform split for PathTracer remains:

- Apple: `PathTracerMetal.cpp`;
- non-Apple: `PathTracer.cpp`.

`Rasterizer.cpp` is compiled on both supported platforms.

Renderer-common `Scene.cpp` is compiled once and shared by both.

## Testing

### Architecture contract

CI must reject fallback coupling. It should fail if Rasterizer contains references such as:

- `PathTracer` member ownership;
- `supportsPathTracer`;
- `use_path_tracer`;
- direct `PathTracer::init/render/shutdown` calls.

### Common scene contract

Synthetic ECS world verifies:

- active camera extraction;
- light extraction;
- visible render-item filtering;
- invalid/hidden entities excluded;
- mesh/material pointers resolve correctly;
- transform math matches current behavior.

### PathTracer regression

Existing Linux/OpenGL and macOS/Metal PathTracer contracts remain green after shared-code extraction.

### Rasterizer contract

At minimum:

- construction through `IRenderer*` or reference;
- init/resize/render/shutdown lifecycle;
- textured and untextured material handling;
- hidden renderables are skipped;
- no PathTracer backend is initialized as a side effect.

Hosted macOS NSGL limitations may require compile/structure verification where a real GL context is unavailable, but local/lwcgl-compatible runtime behavior remains the intended target.

## Compatibility

Existing user code using `Renderer::PathTracer` continues to compile.

New explicit rasterizer usage is:

```cpp
Renderer::Rasterizer renderer;
```

No caller is forced through a renderer factory or backend enum.

## Success criteria

The change is complete when:

1. `PathTracer` and `Rasterizer` both implement one shared lifecycle interface.
2. Camera/light/render-item/transform interpretation is shared rather than duplicated.
3. Rasterizer has zero dependency on PathTracer.
4. PathTracer has zero dependency on Rasterizer.
5. No automatic fallback exists anywhere in Horse.
6. Existing PathTracer Linux and macOS tests remain green.
7. Rasterizer compiles on Linux and macOS and its available runtime contracts pass.
8. `Render.hpp` exposes both backends cleanly.
9. `libHorse` contains both renderer choices while preserving explicit user selection.
