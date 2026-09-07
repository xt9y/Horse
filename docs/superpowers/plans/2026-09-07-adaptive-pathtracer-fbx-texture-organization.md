# Adaptive Path Tracer + FBX Texture Organization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make camera movement much cheaper, reorganize OBJ/FBX implementation files, and make the Earth FBX resolve and render its diffuse/base-color textures instead of white fallback materials.

**Architecture:** Preserve the existing public model and renderer APIs. Reorganize only model implementation files, resolve FBX textures through Material→Texture→Video connections and native image decoding, then make the path tracer use divisor-4 internal rendering plus true sparse 2x2/4x4 compute dispatch with generation-based history validity.

**Tech Stack:** C++20, OpenGL 4.3 compute/compatibility shaders, lwcgl 2.9.3, c-build, native FBX/PNG/JPEG/TGA/DEFLATE implementation.

**Spec:** `docs/superpowers/specs/2026-09-07-adaptive-pathtracer-fbx-texture-organization-design.md`

## Global Constraints

- Preserve current public mesh/material/model/skeleton/animation/IK APIs.
- No external-installed FBX, image or compression dependency.
- No `stb_image`.
- Keep native DEFLATE/zlib, PNG, JPEG and TGA decoders.
- Keep native binary + ASCII FBX readers.
- Remove the `FbxParser` module/umbrella naming from the reorganized implementation.
- Preserve stackless world-BVH traversal.
- Preserve `samples_per_frame = 1` and `max_bounces = 3`.
- No wavefront, ReSTIR, SVGF or later renderer architecture transplant.
- Earth FBX remains a regression target.
- No workflow changes.

---

### Task 1: Reorganize FBX and OBJ implementation folders

**Files:**
- Move: `Sources/Models/Formats/Fbx.cpp` → `Sources/Models/Formats/FBX/Fbx.cpp`
- Move: `Sources/Models/Formats/Fbx.hpp` → `Sources/Models/Formats/FBX/Fbx.hpp`
- Move: `Sources/Models/Formats/FbxAscii.cpp` → `Sources/Models/Formats/FBX/Ascii.cpp`
- Move: `Sources/Models/Formats/FbxAscii.hpp` → `Sources/Models/Formats/FBX/Ascii.hpp`
- Move: `Sources/Models/Formats/FbxBinary.cpp` → `Sources/Models/Formats/FBX/Binary.cpp`
- Move: `Sources/Models/Formats/FbxBinary.hpp` → `Sources/Models/Formats/FBX/Binary.hpp`
- Move: `Sources/Models/Formats/FbxDocument.cpp` → `Sources/Models/Formats/FBX/Document.cpp`
- Move: `Sources/Models/Formats/FbxDocument.hpp` → `Sources/Models/Formats/FBX/Document.hpp`
- Move: `Sources/Models/Formats/FbxSanitize.cpp` → `Sources/Models/Formats/FBX/Sanitize.cpp`
- Move: `Sources/Models/Formats/FbxSanitize.hpp` → `Sources/Models/Formats/FBX/Sanitize.hpp`
- Move: `Sources/Models/Formats/Obj.cpp` → `Sources/Models/Formats/OBJ/Obj.cpp`
- Move: `Sources/Models/Formats/Obj.hpp` → `Sources/Models/Formats/OBJ/Obj.hpp`
- Delete: `Sources/Models/Formats/FbxParser.hpp`
- Modify: `Sources/Models/Models.cpp`
- Modify: `build.c`
- Test: `tests/model_format_layout_contract.cpp`

**Interfaces:**
- Consumes: existing `Models::load()` dispatch and FBX document/parser implementation.
- Produces: unchanged public model API; internal namespaces remain `Models::Fbx` / `Models::Obj`, while parser/document types live under `Models::Fbx::Internal`.

- [ ] **Step 1: Add a failing layout contract**

Create `tests/model_format_layout_contract.cpp`:

```cpp
#include <cassert>
#include <filesystem>

int main()
{
    namespace fs = std::filesystem;
    assert(fs::exists("Sources/Models/Formats/FBX/Fbx.cpp"));
    assert(fs::exists("Sources/Models/Formats/FBX/Ascii.cpp"));
    assert(fs::exists("Sources/Models/Formats/FBX/Binary.cpp"));
    assert(fs::exists("Sources/Models/Formats/FBX/Document.cpp"));
    assert(fs::exists("Sources/Models/Formats/OBJ/Obj.cpp"));
    assert(!fs::exists("Sources/Models/Formats/FbxParser.hpp"));
    return 0;
}
```

- [ ] **Step 2: Verify RED**

Run the repository's existing contract-test build target or compile this contract with the same C++20 flags. Expected: FAIL because the new FBX/OBJ paths do not exist and `FbxParser.hpp` still exists.

- [ ] **Step 3: Move files and normalize internal includes**

Change includes from old flat paths such as:

```cpp
#include "Models/Formats/Fbx.hpp"
#include "Models/Formats/FbxParser.hpp"
```

to:

```cpp
#include "Models/Formats/FBX/Fbx.hpp"
#include "Models/Formats/FBX/Document.hpp"
```

Rename the parser/document implementation namespace to:

```cpp
namespace Models::Fbx::Internal {
```

and in `FBX/Fbx.cpp` use:

```cpp
using RawDocument = Internal::Document;
using RawNode = Internal::Node;
```

- [ ] **Step 4: Update model dispatch and build depth**

Update `Models.cpp` to include:

```cpp
#include "Models/Formats/FBX/Fbx.hpp"
#include "Models/Formats/OBJ/Obj.hpp"
```

Update `build.c`:

```c
c_sources(library, "Sources/*.cpp");
c_sources(library, "Sources/*/*.cpp");
c_sources(library, "Sources/*/*/*.cpp");
c_sources(library, "Sources/*/*/*/*.cpp");
```

- [ ] **Step 5: Verify GREEN**

Run the layout contract and the existing library build. Expected: both pass; no old `Sources/Models/Formats/Fbx*.cpp` or `Obj.cpp` remains in the flat directory.

- [ ] **Step 6: Commit**

```bash
git add Sources/Models/Formats Sources/Models/Models.cpp build.c tests/model_format_layout_contract.cpp
git commit -m "models: split FBX and OBJ format folders"
```

---

### Task 2: Add Earth FBX texture regression coverage

**Files:**
- Modify/Create: existing Earth FBX regression test under `tests/`
- Read: `Sources/Models/Models.hpp`
- Read: `Sources/Models/Core/Texture.hpp`

**Interfaces:**
- Consumes: `Models::load()`, `Models::partCount()`, `Models::part()`, `Models::material()`, `Models::texture()`.
- Produces: regression proving Earth has at least one decoded diffuse/base-color texture.

- [ ] **Step 1: Extend the Earth regression with texture assertions**

Add checks equivalent to:

```cpp
bool found_textured_material = false;
for (std::size_t i = 0; i < Models::partCount(model); ++i) {
    const Models::ModelPart *part = Models::part(model, i);
    assert(part != nullptr);
    const Models::MaterialData *material = Models::material(part->material);
    assert(material != nullptr);
    if (material->diffuse_texture == Models::INVALID_TEXTURE) continue;

    const Models::TextureAsset *texture = Models::texture(material->diffuse_texture);
    assert(texture != nullptr);
    assert(texture->image.width > 0);
    assert(texture->image.height > 0);
    assert(!texture->image.rgba.empty());
    found_textured_material = true;
}
assert(found_textured_material);
```

Keep the existing geometry/material-count assertions intact.

- [ ] **Step 2: Verify RED**

Run the Earth FBX contract. Expected: FAIL specifically at `found_textured_material` because the current loader produces white fallback materials for Earth.

- [ ] **Step 3: Commit the failing regression**

```bash
git add tests
git commit -m "test: require Earth FBX diffuse texture"
```

---

### Task 3: Resolve FBX Material→Texture→Video base-color textures

**Files:**
- Modify: `Sources/Models/Formats/FBX/Fbx.cpp`
- Test: Earth FBX regression from Task 2

**Interfaces:**
- Consumes: FBX `Scene`, connection lists, `Models::loadTexture()`, `Models::loadTextureMemory()`.
- Produces: `MaterialData.diffuse_texture` and `texture_path` populated from connected Texture/Video objects.

- [ ] **Step 1: Add connection-property matching helpers**

Implement:

```cpp
bool isBaseColorProperty(std::string_view property)
{
    return property == "DiffuseColor" ||
           property == "Diffuse" ||
           property == "BaseColor" ||
           property == "Maya|baseColor";
}
```

Add a helper returning Texture object ids connected to a material through `OP` connections whose property satisfies `isBaseColorProperty()`; retain a fallback to the first Texture connection if exporters omit property names.

- [ ] **Step 2: Add Texture→Video resolution**

Implement a helper that follows Texture outgoing/incoming `OO` connections to an object whose kind is `Video`.

- [ ] **Step 3: Add embedded-content resolution**

Implement:

```cpp
const Internal::Bytes *embeddedContent(const Object& object)
{
    const RawNode *content = child(object.node, "Content");
    if (!content || content->properties.empty()) return nullptr;
    return content->properties[0].asBytes();
}
```

Try connected Video content first, then Texture content. Pass valid bytes to `loadTextureMemory()` with a stable cache key:

```cpp
source_path.string() + "#fbx-texture:" + texture.name
```

- [ ] **Step 4: Add portable external-path candidates**

Normalize backslashes before constructing filesystem paths:

```cpp
std::string normalizeFbxPath(std::string value)
{
    std::replace(value.begin(), value.end(), '\\', '/');
    return value;
}
```

For each serialized filename generate candidates in this exact order:

```text
<serialized relative path under fbx-dir>
<fbx-dir>/<basename>
<fbx-dir>/Textures/<basename>
<fbx-dir>/textures/<basename>
```

If the serialized path is absolute, do not try to prepend the FBX directory to the full absolute path; try it directly first, then basename fallbacks.

- [ ] **Step 5: Use the existing native texture loader for candidates**

For each candidate:

```cpp
std::string error;
TextureHandle handle = loadTexture(candidate.string(), &error);
if (handle != INVALID_TEXTURE) {
    result.diffuse_texture = handle;
    result.texture_path = candidate.string();
    return result;
}
```

- [ ] **Step 6: Verify Earth GREEN**

Run the Earth FBX regression. Expected: PASS with positive texture dimensions and non-empty RGBA bytes.

- [ ] **Step 7: Commit**

```bash
git add Sources/Models/Formats/FBX/Fbx.cpp tests
git commit -m "models: resolve FBX base color textures"
```

---

### Task 4: Change the default trace resolution divisor to 4

**Files:**
- Modify: `Sources/Renderer/PathTracer/PathTracer.hpp`
- Modify: `Sources/Renderer/PathTracer/PathTracer.cpp`
- Modify: `tests/path_tracer_world_bvh_contract.cpp`

**Interfaces:**
- Consumes: existing `PathTracerSettings::resolution_divisor` and `updateTraceResolution()`.
- Produces: default divisor 4 and support for divisor 4 without clamping back to 2.

- [ ] **Step 1: Make the contract RED**

Change:

```cpp
assert(settings.resolution_divisor == 2);
```

to:

```cpp
assert(settings.resolution_divisor == 4);
```

Also assert that `PathTracer.cpp` still clamps the divisor through `1..4`.

- [ ] **Step 2: Verify RED**

Run the path-tracer contract. Expected: FAIL because the default remains 2.

- [ ] **Step 3: Change only the default**

In `PathTracer.hpp`:

```cpp
int resolution_divisor = 4;
```

Leave:

```cpp
int samples_per_frame = 1;
int max_bounces = 3;
```

unchanged.

- [ ] **Step 4: Verify GREEN**

Run the contract. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add Sources/Renderer/PathTracer/PathTracer.hpp tests/path_tracer_world_bvh_contract.cpp
git commit -m "path tracer: quarter internal resolution"
```

---

### Task 5: Add camera-motion state without coupling it to light movement

**Files:**
- Modify: `Sources/Renderer/PathTracer/PathTracer.cpp`
- Test: `tests/path_tracer_world_bvh_contract.cpp`

**Interfaces:**
- Consumes: existing camera signature comparison in `PathTracer::render()`.
- Produces: `camera_moving` state and stationary grace counter used by sparse dispatch.

- [ ] **Step 1: Add source contract tokens**

Require the renderer source to contain fields equivalent to:

```cpp
bool camera_moving = false;
std::uint32_t stationary_frames = 0u;
```

and constants:

```cpp
static constexpr std::uint32_t kStationaryGraceFrames = 3u;
```

- [ ] **Step 2: Verify RED**

Run contract. Expected: FAIL because movement state does not exist.

- [ ] **Step 3: Implement movement classification at camera-signature update**

When camera signature changes:

```cpp
camera_moving = true;
stationary_frames = 0u;
```

When it does not change:

```cpp
if (stationary_frames < kStationaryGraceFrames) ++stationary_frames;
if (stationary_frames >= kStationaryGraceFrames) camera_moving = false;
```

Do not set `camera_moving` from light signature or scene signature changes.

- [ ] **Step 4: Verify GREEN**

Run contract and inspect the diff to confirm light invalidation still calls `resetAccumulation()` but does not force movement mode.

- [ ] **Step 5: Commit**

```bash
git add Sources/Renderer/PathTracer/PathTracer.cpp tests/path_tracer_world_bvh_contract.cpp
git commit -m "path tracer: track camera movement mode"
```

---

### Task 6: Replace whole-image phase rejection with true sparse compute dispatch

**Files:**
- Modify: `Sources/Renderer/PathTracer/PathTracerWorldFastShaders.hpp`
- Modify: `Sources/Renderer/PathTracer/PathTracer.cpp`
- Modify: `tests/path_tracer_world_bvh_contract.cpp`

**Interfaces:**
- Consumes: `camera_moving`, `frame_index`, trace dimensions.
- Produces: shader uniforms `uPhaseGrid`, `uPhaseOffset`, dispatch dimensions based on phase grid.

- [ ] **Step 1: Make the shader contract RED**

Require:

```glsl
uniform int uPhaseGrid;
uniform ivec2 uPhaseOffset;
```

Require the old full-image rejection to be absent:

```cpp
assert(shader.find("if (pixel_phase != phase) return;") == std::string_view::npos);
```

Require sparse coordinate mapping:

```cpp
assert(shader.find("gl_GlobalInvocationID.xy * uint(uPhaseGrid)") != std::string_view::npos);
```

- [ ] **Step 2: Verify RED**

Run contract. Expected: FAIL.

- [ ] **Step 3: Map sparse invocation IDs to real pixels**

In compute shader main:

```glsl
uvec2 sparse = gl_GlobalInvocationID.xy;
uvec2 real_pixel = sparse * uint(uPhaseGrid) + uvec2(uPhaseOffset);
ivec2 pixel = ivec2(real_pixel);
ivec2 size = ivec2(uResolution);
if (any(greaterThanEqual(pixel, size))) return;
```

Remove `pixel_phase` calculation and phase early return.

- [ ] **Step 4: Cache new uniform locations**

Add:

```cpp
GLint phase_grid = -1;
GLint phase_offset = -1;
```

and cache `uPhaseGrid`, `uPhaseOffset` once.

- [ ] **Step 5: Compute phase grid/offset on CPU**

Use:

```cpp
const int phase_grid = camera_moving ? 4 : 2;
const std::uint32_t phase_count = static_cast<std::uint32_t>(phase_grid * phase_grid);
const std::uint32_t phase = frame_index % phase_count;
const int phase_x = static_cast<int>(phase % static_cast<std::uint32_t>(phase_grid));
const int phase_y = static_cast<int>(phase / static_cast<std::uint32_t>(phase_grid));
```

Set uniforms with `glUniform1i` / `glUniform2i` through cached lwcgl functions.

- [ ] **Step 6: Dispatch only sparse dimensions**

Calculate:

```cpp
const int sparse_width = (trace_width + phase_grid - 1) / phase_grid;
const int sparse_height = (trace_height + phase_grid - 1) / phase_grid;
```

Then:

```cpp
GL43.glDispatchCompute(
    static_cast<GLuint>((sparse_width + 7) / 8),
    static_cast<GLuint>((sparse_height + 7) / 8),
    1u
);
```

- [ ] **Step 7: Verify GREEN**

Run contract. Confirm `PHASE_COUNT = 4` legacy constant is replaced by dynamic phase-grid logic and no whole-image phase reject remains.

- [ ] **Step 8: Commit**

```bash
git add Sources/Renderer/PathTracer/PathTracer.cpp Sources/Renderer/PathTracer/PathTracerWorldFastShaders.hpp tests/path_tracer_world_bvh_contract.cpp
git commit -m "path tracer: dispatch sparse movement phases"
```

---

### Task 7: Replace full accumulation clears with generation-validity metadata

**Files:**
- Modify: `Sources/Renderer/PathTracer/PathTracer.cpp`
- Modify: `Sources/Renderer/PathTracer/PathTracerWorldFastShaders.hpp`
- Modify: `tests/path_tracer_world_bvh_contract.cpp`

**Interfaces:**
- Consumes: accumulation RGBA32F image, reset/invalidation events.
- Produces: R32UI validity image and `history_generation` integer checked by trace/present shaders.

- [ ] **Step 1: Make the validity contract RED**

Require shader tokens:

```glsl
layout(r32ui, binding = 1) uniform uimage2D uValidity;
uniform uint uHistoryGeneration;
```

Require old full clear to be absent:

```cpp
assert(shader.find("imageStore(uAccumulation, pixel, vec4(0.0));") == std::string_view::npos);
```

Require presentation to compare validity generation before using a texel.

- [ ] **Step 2: Verify RED**

Run contract. Expected: FAIL.

- [ ] **Step 3: Add validity texture lifecycle**

Add `GLuint validity = 0u;` beside accumulation.

Create it at trace resolution:

```cpp
glGenTextures(1, &validity);
glBindTexture(GL_TEXTURE_2D, validity);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI,
             trace_width, trace_height, 0,
             GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
```

Delete it with accumulation.

- [ ] **Step 4: Replace reset flag with generation increment**

Maintain:

```cpp
std::uint32_t history_generation = 1u;
```

On accumulation invalidation:

```cpp
sample_count = 0u;
phase_count = 0u;
++history_generation;
if (history_generation == 0u) history_generation = 1u;
```

Generation wrap may trigger a one-time validity texture clear to zero before returning to generation 1.

- [ ] **Step 5: Write active pixels with current generation**

Compute shader:

```glsl
uint previous_generation = imageLoad(uValidity, pixel).r;
vec4 previous = previous_generation == uHistoryGeneration
    ? imageLoad(uAccumulation, pixel)
    : vec4(0.0);

imageStore(uAccumulation, pixel,
           vec4(previous.rgb + sample_radiance, previous.a + 1.0));
imageStore(uValidity, pixel, uvec4(uHistoryGeneration, 0u, 0u, 0u));
```

- [ ] **Step 6: Make presentation generation-aware**

Bind validity texture as an integer sampler or image readable by the present shader. A candidate accumulation texel is valid only if its stored generation equals `uHistoryGeneration` and its alpha is positive.

For moving 4x4 mode, search the current 4x4 block for the nearest valid texel. For stationary 2x2 mode, search the current 2x2 block. Only use bilinear accumulation sampling when every texel in the relevant block is current-generation valid.

- [ ] **Step 7: Verify GREEN**

Run path-tracer contract. Inspect shader source to confirm no full-image reset store remains.

- [ ] **Step 8: Commit**

```bash
git add Sources/Renderer/PathTracer/PathTracer.cpp Sources/Renderer/PathTracer/PathTracerWorldFastShaders.hpp tests/path_tracer_world_bvh_contract.cpp
git commit -m "path tracer: invalidate history by generation"
```

---

### Task 8: End-to-end verification

**Files:**
- No production changes unless a test exposes a concrete regression.

**Interfaces:**
- Consumes: all previous tasks.
- Produces: verified Earth texture loading and responsive Sponza camera movement.

- [ ] **Step 1: Run model-format and Earth contracts**

Run the repository contract targets covering the new folder layout and Earth FBX texture. Expected: PASS.

- [ ] **Step 2: Build the shared library**

From `ECS-MODEL-RASTERIZER` run the normal `c build` command. Expected: shared library links successfully with all deeper FBX/OBJ sources.

- [ ] **Step 3: Update GAME and run Earth**

From `GAME`:

```bash
c update ecs-model-rasterizer
c build test earth
```

Expected runtime evidence:

```text
[PathTracer]: world cache 2304 triangles, 1023 nodes, 3 materials
```

and Earth visibly uses its base-color texture rather than uniform white fallback.

- [ ] **Step 4: Run Sponza**

```bash
c build test sponza
```

Move/rotate the camera continuously. Expected behavior:

- no stale-geometry ghost trail;
- camera motion uses 4x4 sparse phase mode;
- stopped camera returns to 2x2 sparse phase mode after the grace window;
- no black presentation frames;
- significantly lower movement workload than the current whole-image 1/4 phase implementation.

- [ ] **Step 5: Verify renderer quality defaults**

Confirm source still contains:

```cpp
resolution_divisor = 4;
samples_per_frame = 1;
max_bounces = 3;
```

and active traversal remains stackless via `node.extra.x`.

- [ ] **Step 6: Commit any verification-only test/build wiring if required**

If and only if verification required new non-production contract registration, commit that wiring separately:

```bash
git add build.c tests
git commit -m "test: wire adaptive path tracer regressions"
```
