# Explicit Rasterizer / PathTracer Renderer Architecture Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reintroduce `Renderer::Rasterizer` as an explicit user-selected renderer while sharing renderer-independent scene interpretation with `Renderer::PathTracer` and preserving zero automatic fallback behavior.

**Architecture:** Add a small common renderer contract plus shared math/scene extraction modules. `PathTracer` and `Rasterizer` are sibling concrete implementations of `IRenderer`; they share camera/light/render-item/transform interpretation but own independent GPU state. The rasterizer initially uses lwcgl's OpenGL compatibility path on Linux and macOS, while the existing PathTracer platform split remains OpenGL 4.3 compute on non-Apple and Metal on Apple.

**Tech Stack:** C++20, Horse ECS/model APIs, lwcgl/OpenGL compatibility rasterization, existing lwmgl/Metal PathTracer backend, C-BuildSystem, GitHub Actions Linux/macOS.

**Spec:** `docs/superpowers/specs/2026-09-09-explicit-rasterizer-renderer-architecture-design.md`

## Global Constraints

- No automatic `PathTracer -> Rasterizer` fallback.
- No automatic `Rasterizer -> PathTracer` upgrade.
- No runtime backend enum/facade hiding the user's renderer choice.
- Existing `Renderer::PathTracer` call sites remain source-compatible.
- `Renderer::Rasterizer` owns no `PathTracer` object and calls no PathTracer function.
- `Renderer::PathTracer` references no Rasterizer implementation.
- Public ECS component layout remains unchanged.
- No Earth- or Sponza-specific renderer behavior.
- No Metal rasterizer in this pass.
- `PathTracerSettings` remains PathTracer-only.
- Both renderers are compiled into `libHorse`.
- Apple keeps `PathTracerMetal.cpp`; non-Apple keeps `PathTracer.cpp`; `Rasterizer.cpp` builds on both.
- Renderer-common code owns camera/light/render-item/transform interpretation; GPU resource creation remains backend-specific.

---

## File Structure

Create:

```text
Sources/Renderer/Renderer.hpp
Sources/Renderer/Math.hpp
Sources/Renderer/Math.cpp
Sources/Renderer/Scene.hpp
Sources/Renderer/Scene.cpp
Sources/Renderer/Rasterizer/Rasterizer.hpp
Sources/Renderer/Rasterizer/Rasterizer.cpp
tests/renderer_common_contract.cpp
tests/rasterizer_architecture_contract.cpp
tests/rasterizer_runtime_contract.cpp
```

Modify:

```text
Sources/Renderer/Render.hpp
Sources/Renderer/PathTracer/PathTracer.hpp
Sources/Renderer/PathTracer/PathTracer.cpp
Sources/Renderer/PathTracer/PathTracerMetal.cpp
build.c
.github/workflows/ci.yml
```

Responsibilities:

- `Renderer.hpp`: public lifecycle interface only.
- `Math.*`: renderer-neutral transform/vector/matrix operations already needed by both backends.
- `Scene.*`: renderer-neutral camera, light, and visible render-item extraction.
- `Rasterizer.*`: raster-only OpenGL state, texture cache, triangle submission, cleanup.
- `PathTracer/*`: keep only path-tracing-specific data preparation/GPU submission after common extraction.
- `Render.hpp`: umbrella exposing components, interface, PathTracer, and Rasterizer.

---

### Task 1: Introduce the Common Renderer Interface

**Files:**
- Create: `Sources/Renderer/Renderer.hpp`
- Modify: `Sources/Renderer/PathTracer/PathTracer.hpp`
- Test: `tests/renderer_common_contract.cpp`

**Interfaces:**
- Produces:
  ```cpp
  class Renderer::IRenderer {
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
  ```
- `Renderer::PathTracer final : public IRenderer` preserves its existing public settings API.

- [ ] **Step 1: Write the failing interface contract**

Create `tests/renderer_common_contract.cpp` with compile-time assertions:

```cpp
#include "Sources/Renderer/Renderer.hpp"
#include "Sources/Renderer/PathTracer/PathTracer.hpp"

#include <type_traits>

static_assert(std::is_abstract_v<Renderer::IRenderer>);
static_assert(std::is_base_of_v<Renderer::IRenderer, Renderer::PathTracer>);
static_assert(std::has_virtual_destructor_v<Renderer::IRenderer>);

int main()
{
    Renderer::PathTracer tracer;
    Renderer::IRenderer& renderer = tracer;
    (void)renderer.enabled();
    return 0;
}
```

- [ ] **Step 2: Compile the contract and verify RED**

Run on Linux CI/toolchain:

```bash
c++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -I. -ISources \
  -c tests/renderer_common_contract.cpp -o /tmp/renderer-common-contract.o
```

Expected: FAIL because `Renderer.hpp` / `IRenderer` does not exist.

- [ ] **Step 3: Add `IRenderer` and derive PathTracer**

Create `Sources/Renderer/Renderer.hpp` containing only the interface and required ECS include. Update `PathTracer.hpp` to:

```cpp
#include "Renderer/Renderer.hpp"

class PathTracer final : public IRenderer {
public:
    bool init() override;
    void resize(int width, int height) override;
    void render(const Ecs::World& world) override;
    void shutdown() override;
    bool initialized() const override;
    bool enabled() const override;
    void setEnabled(bool enabled) override;
    // Existing settings() API stays unchanged.
};
```

- [ ] **Step 4: Re-run compile contract and existing Horse build**

```bash
c++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -I. -ISources \
  -c tests/renderer_common_contract.cpp -o /tmp/renderer-common-contract.o
c clean || true
c build
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add Sources/Renderer/Renderer.hpp Sources/Renderer/PathTracer/PathTracer.hpp tests/renderer_common_contract.cpp
git commit -m "renderer: add shared renderer lifecycle interface"
```

---

### Task 2: Extract Shared Renderer Math and Scene Interpretation

**Files:**
- Create: `Sources/Renderer/Math.hpp`
- Create: `Sources/Renderer/Math.cpp`
- Create: `Sources/Renderer/Scene.hpp`
- Create: `Sources/Renderer/Scene.cpp`
- Modify: `tests/renderer_common_contract.cpp`

**Interfaces:**

`Math.hpp` produces:

```cpp
namespace Renderer::Math {
using Mat4 = std::array<float, 16>;

Mat4 identityMatrix();
Mat4 multiply(const Mat4& a, const Mat4& b);
Mat4 translation(float x, float y, float z);
Mat4 scaling(float x, float y, float z);
Mat4 rotationX(float degrees);
Mat4 rotationY(float degrees);
Mat4 rotationZ(float degrees);
Mat4 modelMatrix(const Transform& transform);
Mat4 inverseModelMatrix(const Transform& transform);
Vec3 transformPoint(const Mat4& matrix, const Vec3& point);
Vec3 transformNormal(const Mat4& world_to_object, const Vec3& normal);
float dot(const Vec3& a, const Vec3& b);
Vec3 cross(const Vec3& a, const Vec3& b);
Vec3 normalize(const Vec3& value);
}
```

`Scene.hpp` produces:

```cpp
namespace Renderer::Scene {
struct CameraState {
    Ecs::Entity entity = Ecs::INVALID_ENTITY;
    Transform transform{};
    float fov_degrees = 60.0f;
    float near_plane = 0.1f;
    bool valid = false;
};

struct LightState {
    Ecs::Entity entity = Ecs::INVALID_ENTITY;
    Transform transform{};
    LightComponent light{};
    bool valid = false;
};

struct RenderItem {
    Ecs::Entity entity = Ecs::INVALID_ENTITY;
    const Transform* transform = nullptr;
    const MeshComponent* mesh_component = nullptr;
    const Models::MeshData* mesh = nullptr;
    const Models::MaterialData* material = nullptr;
};

CameraState cameraState(const Ecs::World& world);
LightState lightState(const Ecs::World& world);
void collectRenderItems(const Ecs::World& world, std::vector<RenderItem>& out);
}
```

- [ ] **Step 1: Expand `renderer_common_contract.cpp` with synthetic-world behavior**

Build a world containing:

```cpp
Ecs::World world;
const auto camera = world.createEntity();
world.add<Renderer::Transform>(camera, {{1,2,3}, {10,20,30}, {1,1,1}});
world.add<Camera::CameraComponent>(camera, {75.0f, 0.25f, true});

const auto light = world.createEntity();
world.add<Renderer::Transform>(light, {{4,5,6}, {}, {1,1,1}});
world.add<Renderer::LightComponent>(light, {
    Renderer::LightType::Point, {0.5f, 0.6f, 0.7f}, 3.0f
});
```

Register one valid mesh/material through the existing generic model registration APIs, plus hidden/invalid entities. Assert:

```cpp
const auto camera_state = Renderer::Scene::cameraState(world);
assert(camera_state.valid);
assert(camera_state.entity == camera);
assert(camera_state.fov_degrees == 75.0f);

const auto light_state = Renderer::Scene::lightState(world);
assert(light_state.valid);
assert(light_state.entity == light);

std::vector<Renderer::Scene::RenderItem> items;
Renderer::Scene::collectRenderItems(world, items);
assert(items.size() == 1u);
assert(items[0].mesh != nullptr);
assert(items[0].material != nullptr);
```

Also assert model-matrix behavior against the existing transform order with a known point.

- [ ] **Step 2: Compile/run and verify RED**

Link against the current Horse library using the same CI pattern as existing model contracts. Expected: FAIL because `Math` / `Scene` APIs are missing.

- [ ] **Step 3: Implement `Math.*` by moving existing PathTracer-neutral math exactly**

Move the existing column-major matrix behavior without changing transform order. Keep BVH-only helpers (`minVec`, `maxVec`, component-axis helpers) private to PathTracer.

- [ ] **Step 4: Implement `Scene.*`**

Rules must be exact:

```cpp
CameraState cameraState(const Ecs::World& world)
{
    CameraState out;
    const Ecs::Entity entity = Camera::activeCamera(world);
    if (entity == Ecs::INVALID_ENTITY) return out;
    const auto* transform = world.get<Transform>(entity);
    const auto* camera = world.get<Camera::CameraComponent>(entity);
    if (!transform || !camera) return out;
    out.entity = entity;
    out.transform = *transform;
    out.fov_degrees = camera->fov_degrees;
    out.near_plane = camera->near_plane;
    out.valid = true;
    return out;
}
```

`lightState()` selects the first entity with both `LightComponent` and `Transform`.

`collectRenderItems()` must clear `out`, then include only entities with visible `RenderableComponent`, `MeshComponent`, `Transform`, and a resolvable mesh. Invalid material handles produce `material == nullptr` rather than dropping an otherwise valid mesh.

- [ ] **Step 5: Run common contract on Linux and macOS build jobs**

Expected: PASS with camera/light/visibility/material/math assertions.

- [ ] **Step 6: Commit**

```bash
git add Sources/Renderer/Math.* Sources/Renderer/Scene.* tests/renderer_common_contract.cpp
git commit -m "renderer: share scene extraction and transform math"
```

---

### Task 3: Refactor Both PathTracer Backends onto Common Renderer Data

**Files:**
- Modify: `Sources/Renderer/PathTracer/PathTracer.cpp`
- Modify: `Sources/Renderer/PathTracer/PathTracerMetal.cpp`
- Test: existing `tests/path_tracer_metal_shader_contract.cpp`
- Test: existing renderer contracts in `.github/workflows/ci.yml`

**Interfaces consumed:**
- `Renderer::Math::modelMatrix`, `inverseModelMatrix`, `transformPoint`, `transformNormal`
- `Renderer::Scene::cameraState`, `lightState`, `collectRenderItems`

- [ ] **Step 1: Add an architecture assertion that both PathTracer files include common modules**

In CI's renderer-contract job assert:

```python
for path in [
    'Sources/Renderer/PathTracer/PathTracer.cpp',
    'Sources/Renderer/PathTracer/PathTracerMetal.cpp',
]:
    source = Path(path).read_text()
    assert '#include "Renderer/Math.hpp"' in source
    assert '#include "Renderer/Scene.hpp"' in source
```

Run before refactor. Expected: RED.

- [ ] **Step 2: Replace private transform math in both PathTracer implementations**

Remove duplicate `Mat4`, `identityMatrix`, `multiply`, translation/scaling/rotation, `modelMatrix`, `inverseModelMatrix`, `transformPoint`, and `transformNormal` definitions where common equivalents apply.

Use aliases only where convenient:

```cpp
using Renderer::Math::Mat4;
using Renderer::Math::modelMatrix;
using Renderer::Math::inverseModelMatrix;
using Renderer::Math::transformPoint;
using Renderer::Math::transformNormal;
```

- [ ] **Step 3: Replace camera/light extraction with shared states**

Use:

```cpp
const Scene::CameraState camera = Scene::cameraState(world);
const Scene::LightState light = Scene::lightState(world);
```

Preserve current PathTracer-specific fallback/default light behavior when `light.valid == false`.

- [ ] **Step 4: Replace duplicated visible entity filtering with `collectRenderItems()`**

Each `Impl` owns a reusable:

```cpp
std::vector<Scene::RenderItem> render_items;
```

and calls:

```cpp
Scene::collectRenderItems(world, render_items);
```

Use those items for world-triangle/material extraction. Do not move texture-slot packing, skinning, BVH construction, accumulation, or GPU packing into Scene.

- [ ] **Step 5: Run PathTracer verification**

Linux:

```bash
c clean || true
c build
```

macOS CI:

```bash
c clean || true
c build
# then existing path_tracer_metal_shader_contract
```

Expected: all existing sparse-camera, synchronization, geometric-normal, Metal shader, FBX/model contracts remain GREEN.

- [ ] **Step 6: Commit**

```bash
git add Sources/Renderer/PathTracer/PathTracer.cpp Sources/Renderer/PathTracer/PathTracerMetal.cpp .github/workflows/ci.yml
git commit -m "renderer: reuse common scene data in path tracer"
```

---

### Task 4: Add Standalone Rasterizer Public API with Zero PathTracer Coupling

**Files:**
- Create: `Sources/Renderer/Rasterizer/Rasterizer.hpp`
- Create: `tests/rasterizer_architecture_contract.cpp`
- Modify: `Sources/Renderer/Render.hpp`

**Interfaces:**

```cpp
namespace Renderer {
class Rasterizer final : public IRenderer {
public:
    struct Impl;

    Rasterizer();
    ~Rasterizer();
    Rasterizer(const Rasterizer&) = delete;
    Rasterizer& operator=(const Rasterizer&) = delete;

    bool init() override;
    void resize(int width, int height) override;
    void render(const Ecs::World& world) override;
    void shutdown() override;
    bool initialized() const override;
    bool enabled() const override;
    void setEnabled(bool enabled) override;

private:
    Impl* impl_;
};
}
```

- [ ] **Step 1: Write the architecture contract RED**

`tests/rasterizer_architecture_contract.cpp`:

```cpp
#include "Sources/Renderer/Render.hpp"
#include <type_traits>

static_assert(std::is_base_of_v<Renderer::IRenderer, Renderer::Rasterizer>);
static_assert(std::is_final_v<Renderer::Rasterizer>);

int main()
{
    Renderer::Rasterizer rasterizer;
    Renderer::IRenderer* renderer = &rasterizer;
    renderer->setEnabled(false);
    return renderer->enabled() ? 1 : 0;
}
```

CI also scans `Rasterizer.hpp/.cpp` and rejects these strings:

```text
PathTracer
supportsPathTracer
use_path_tracer
```

and scans PathTracer files for `Rasterizer`.

- [ ] **Step 2: Compile and verify RED**

Expected: FAIL because Rasterizer header/class does not exist.

- [ ] **Step 3: Add the PIMPL public header and umbrella include**

`Render.hpp` becomes:

```cpp
#include "Renderer/Components.hpp"
#include "Renderer/Renderer.hpp"
#include "Renderer/PathTracer/PathTracer.hpp"
#include "Renderer/Rasterizer/Rasterizer.hpp"
```

Do not put OpenGL includes in public Rasterizer headers.

- [ ] **Step 4: Compile architecture contract**

Expected: link may still fail until Task 5, but header/compile-time contract must pass.

- [ ] **Step 5: Commit**

```bash
git add Sources/Renderer/Rasterizer/Rasterizer.hpp Sources/Renderer/Render.hpp tests/rasterizer_architecture_contract.cpp
git commit -m "renderer: expose explicit rasterizer backend"
```

---

### Task 5: Implement the OpenGL Compatibility Rasterizer as a Real Sibling Backend

**Files:**
- Create: `Sources/Renderer/Rasterizer/Rasterizer.cpp`
- Create: `tests/rasterizer_runtime_contract.cpp`

**Interfaces consumed:**
- `Scene::cameraState`
- `Scene::lightState`
- `Scene::collectRenderItems`
- model/material/texture APIs already resolved through `RenderItem`

**Private implementation state:**

```cpp
struct Rasterizer::Impl {
    bool initialized = false;
    bool enabled = true;
    int width = 1;
    int height = 1;
    std::unordered_map<std::uint32_t, unsigned int> textures;
    std::vector<Scene::RenderItem> render_items;
};
```

- [ ] **Step 1: Write runtime contract before implementation**

The Linux contract creates an lwcgl display/context under Xvfb, registers:

1. one untextured triangle material;
2. one small RGBA textured triangle material;
3. one hidden renderable.

It creates an active camera and point light, then executes:

```cpp
Renderer::Rasterizer rasterizer;
assert(rasterizer.init());
rasterizer.resize(320, 200);
rasterizer.render(world);
rasterizer.setEnabled(false);
assert(!rasterizer.enabled());
rasterizer.render(world);
rasterizer.shutdown();
assert(!rasterizer.initialized());
```

The contract does not instantiate `PathTracer`.

- [ ] **Step 2: Run runtime contract and verify RED**

Expected: link failure because Rasterizer implementation is missing.

- [ ] **Step 3: Implement raster-only initialization**

`init()` must configure only raster state:

```cpp
glEnable(GL_DEPTH_TEST);
glDepthFunc(GL_LEQUAL);
glEnable(GL_CULL_FACE);
glCullFace(GL_BACK);
glEnable(GL_NORMALIZE);
glEnable(GL_LIGHTING);
glEnable(GL_LIGHT0);
glEnable(GL_COLOR_MATERIAL);
glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
glShadeModel(GL_SMOOTH);
```

Verify a current OpenGL context using `glGetString(GL_VERSION)`. If null, print:

```text
[Rasterizer]: OpenGL initialization failed: no current context
```

and return `false`.

- [ ] **Step 4: Implement camera/projection and light using common Scene state**

Keep the old infinite-perspective fixed-function projection helper private to Rasterizer.

Camera transforms come exclusively from `Scene::cameraState(world)`.

Light selection comes exclusively from `Scene::lightState(world)`. Preserve the old explicit default directional light only when `LightState.valid == false`.

- [ ] **Step 5: Implement render-item submission**

Call:

```cpp
Scene::collectRenderItems(world, impl_->render_items);
```

For each item:

- use `item.transform`, `item.mesh`, `item.material`;
- upload/cache `material->diffuse_texture` lazily in the Rasterizer-owned GL texture map;
- use `GL_REPEAT`, linear filtering, and the existing Horse UV convention;
- apply material color and opacity;
- enable blending only for material opacity or meaningful texture alpha;
- submit mesh indices as `GL_TRIANGLES` using normals, UVs, positions.

No ECS visibility/material filtering is duplicated in Rasterizer.

- [ ] **Step 6: Implement enabled/resize/shutdown lifecycle**

`setEnabled(false)` affects only `impl_->enabled`. Disabled render clears the framebuffer but does not submit scene geometry.

`shutdown()` deletes only rasterizer-created GL textures and resets rasterizer state. There is no call into PathTracer.

- [ ] **Step 7: Run Linux runtime contract under Xvfb**

Install/use `xvfb` in Linux CI and run:

```bash
xvfb-run -a /tmp/rasterizer-runtime-contract
```

Expected: PASS.

- [ ] **Step 8: Compile Rasterizer on macOS**

Because hosted macOS may not provide usable NSGL, compile/link the class into `libHorse` and compile the architecture contract without requiring a live context.

Expected: PASS.

- [ ] **Step 9: Commit**

```bash
git add Sources/Renderer/Rasterizer/Rasterizer.cpp tests/rasterizer_runtime_contract.cpp
git commit -m "renderer: restore standalone OpenGL rasterizer"
```

---

### Task 6: Build Both Renderers into `libHorse` and Strengthen CI Coupling Guards

**Files:**
- Modify: `build.c`
- Modify: `.github/workflows/ci.yml`

- [ ] **Step 1: Add build-output contract before changing build.c**

CI checks source selection and public classes. Expected initial RED because `Rasterizer.cpp` is not compiled.

- [ ] **Step 2: Compile renderer-common and Rasterizer on all platforms**

Ensure `build.c` includes:

```c
c_sources(library, "Sources/Renderer/Math.cpp");
c_sources(library, "Sources/Renderer/Scene.cpp");
c_sources(library, "Sources/Renderer/Rasterizer/Rasterizer.cpp");
```

Keep exactly one PathTracer implementation selected by platform:

```c
#ifdef __APPLE__
    c_sources(library, "Sources/Renderer/PathTracer/PathTracerMetal.cpp");
#else
    c_sources(library, "Sources/Renderer/PathTracer/PathTracer.cpp");
#endif
```

If existing source globs already include common/raster files, do not duplicate them; instead make the platform-exclusive PathTracer selection explicit and verify there is exactly one object for each source.

- [ ] **Step 3: Replace the old CI rule that rejected any Rasterizer class**

The old rule:

```bash
grep -R -n -E 'class Rasterizer|renderCompatibility|...'
```

must be removed because Rasterizer is now valid.

Replace it with semantic coupling guards:

```python
raster_h = Path('Sources/Renderer/Rasterizer/Rasterizer.hpp').read_text()
raster_cpp = Path('Sources/Renderer/Rasterizer/Rasterizer.cpp').read_text()
path_gl = Path('Sources/Renderer/PathTracer/PathTracer.cpp').read_text()
path_metal = Path('Sources/Renderer/PathTracer/PathTracerMetal.cpp').read_text()

for source in (raster_h, raster_cpp):
    assert 'PathTracer' not in source
    assert 'supportsPathTracer' not in source
    assert 'use_path_tracer' not in source

assert 'Rasterizer' not in path_gl
assert 'Rasterizer' not in path_metal
```

Also assert both classes derive `IRenderer` through compile contracts.

- [ ] **Step 4: Run full Horse CI matrix**

Expected:

```text
renderer-contract                  PASS
renderer_common_contract          PASS
rasterizer_architecture_contract  PASS
rasterizer_runtime_contract       PASS on Linux/Xvfb
Linux/OpenGL PathTracer build      PASS
macOS/Metal PathTracer build       PASS
Metal shader contract             PASS
FBX/model contracts               PASS
```

- [ ] **Step 5: Commit**

```bash
git add build.c .github/workflows/ci.yml
git commit -m "ci: verify explicit renderer backends"
```

---

### Task 7: Final Compatibility and No-Fallback Verification

**Files:**
- Modify only if verification exposes a defect.

- [ ] **Step 1: Verify public umbrella API**

Compile:

```cpp
#include "Sources/Renderer/Render.hpp"

void choosePathTracer()
{
    Renderer::PathTracer renderer;
}

void chooseRasterizer()
{
    Renderer::Rasterizer renderer;
}
```

Expected: both compile without backend enums/factories.

- [ ] **Step 2: Verify `libHorse` exports both implementations**

Linux:

```bash
nm -C build/debug/libHorse.so | grep 'Renderer::PathTracer::init'
nm -C build/debug/libHorse.so | grep 'Renderer::Rasterizer::init'
```

macOS:

```bash
nm -C build/debug/libHorse.dylib | grep 'Renderer::PathTracer::init'
nm -C build/debug/libHorse.dylib | grep 'Renderer::Rasterizer::init'
```

Expected: both symbols are present.

- [ ] **Step 3: Search production source for fallback behavior**

Run:

```bash
grep -R -n -E 'supportsPathTracer|use_path_tracer|fallback.*Rasterizer|fallback.*PathTracer' Sources/Renderer
```

Expected: no matches.

Also verify Rasterizer production files contain no `PathTracer`, and PathTracer production files contain no `Rasterizer`.

- [ ] **Step 4: Run fresh Linux and macOS CI**

Do not rely on previous runs. Require the latest main commit's renderer-contract, Linux, and macOS jobs to complete GREEN.

- [ ] **Step 5: Document user selection in the final report**

Final usage must remain exactly:

```cpp
Renderer::PathTracer renderer;
```

or:

```cpp
Renderer::Rasterizer renderer;
```

There is no automatic selection and no fallback.
