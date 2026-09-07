# Adaptive Sparse Path Tracer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reduce path-tracer work to quarter-resolution internal rendering and true sparse compute dispatch, using 1/16 traced pixels while the camera moves and 1/4 while stationary without stale-history ghosting.

**Architecture:** Keep the current stackless world-space BVH and integrator. Replace full-image dispatch plus shader phase rejection with compact dispatches whose invocations map to sparse destination pixels. Replace full accumulation clears with a 16-bit phase-validity mask so stale texels stay resident but are never accumulated or presented until refreshed.

**Tech Stack:** C++20, OpenGL 4.3 compute shaders, GLSL 430, lwcgl 2.9.3 compatibility layer.

**Spec:** `docs/superpowers/specs/2026-09-07-adaptive-pathtracer-fbx-layout-textures-design.md`

## Global Constraints

- Keep stackless world-space BVH architecture.
- Default `resolution_divisor = 4`.
- Keep `samples_per_frame = 1`.
- Keep `max_bounces = 3`.
- No wavefront/ReSTIR/SVGF/native-primary architecture.
- Camera changes select aggressive movement mode; light changes only invalidate history.
- No workflow changes.

---

### Task 1: Define and test phase-mask behavior

**Files:**
- Create: `Sources/Renderer/PathTracer/PathTracerPhase.hpp`
- Create: `tests/path_tracer_phase_contract.cpp`
- Modify: `tests/path_tracer_world_bvh_contract.cpp`

**Interfaces:**
- Produces: `Renderer::PathTracerPhase::movingPhase`, `stationaryPhase`, `movingMask`, `stationaryMask`, `allMask`.
- Consumed by: `PathTracer.cpp` dispatch/reset logic.

- [ ] **Step 1: Write the phase contract first**

The test must verify:

```cpp
static_assert(Renderer::PathTracerPhase::allMask == 0xFFFFu);

std::uint16_t moving_union = 0u;
for (std::uint32_t frame = 0; frame < 16u; ++frame) {
    const auto phase = Renderer::PathTracerPhase::movingPhase(frame);
    assert(phase.x < 4u && phase.y < 4u);
    const std::uint16_t mask = Renderer::PathTracerPhase::movingMask(frame);
    assert(mask != 0u && (mask & (mask - 1u)) == 0u);
    moving_union |= mask;
}
assert(moving_union == 0xFFFFu);

std::uint16_t stationary_union = 0u;
for (std::uint32_t frame = 0; frame < 4u; ++frame) {
    const std::uint16_t mask = Renderer::PathTracerPhase::stationaryMask(frame);
    assert(std::popcount(mask) == 4);
    stationary_union |= mask;
}
assert(stationary_union == 0xFFFFu);
```

- [ ] **Step 2: Verify RED**

Compile/run the contract with the repository test harness. Expected failure: `PathTracerPhase.hpp` does not exist.

- [ ] **Step 3: Add the minimal constexpr helper**

Implement:

```cpp
namespace Renderer::PathTracerPhase {

struct Offset { std::uint32_t x; std::uint32_t y; };
inline constexpr std::uint16_t allMask = 0xFFFFu;

constexpr Offset movingPhase(std::uint32_t frame)
{
    const std::uint32_t phase = frame & 15u;
    return {phase & 3u, (phase >> 2u) & 3u};
}

constexpr std::uint16_t bit(std::uint32_t x, std::uint32_t y)
{
    return static_cast<std::uint16_t>(1u << ((y & 3u) * 4u + (x & 3u)));
}

constexpr std::uint16_t movingMask(std::uint32_t frame)
{
    const Offset phase = movingPhase(frame);
    return bit(phase.x, phase.y);
}

constexpr Offset stationaryPhase(std::uint32_t frame)
{
    const std::uint32_t phase = frame & 3u;
    return {phase & 1u, (phase >> 1u) & 1u};
}

constexpr std::uint16_t stationaryMask(std::uint32_t frame)
{
    const Offset phase = stationaryPhase(frame);
    std::uint16_t mask = 0u;
    for (std::uint32_t y = phase.y; y < 4u; y += 2u)
        for (std::uint32_t x = phase.x; x < 4u; x += 2u)
            mask = static_cast<std::uint16_t>(mask | bit(x, y));
    return mask;
}

}
```

- [ ] **Step 4: Verify GREEN**

Run the phase contract. Expected: pass.

- [ ] **Step 5: Commit**

```bash
git add Sources/Renderer/PathTracer/PathTracerPhase.hpp tests/path_tracer_phase_contract.cpp
git commit -m "test: define sparse path tracer phases"
```

---

### Task 2: Change default internal resolution and history state

**Files:**
- Modify: `Sources/Renderer/PathTracer/PathTracer.hpp`
- Modify: `Sources/Renderer/PathTracer/PathTracer.cpp`
- Modify: `tests/path_tracer_world_bvh_contract.cpp`

**Interfaces:**
- Consumes: phase helpers from Task 1.
- Produces: `valid_phase_mask`, reset semantics, camera-motion flag passed to dispatch.

- [ ] **Step 1: Update contract default**

Change the existing default assertion to:

```cpp
assert(settings.resolution_divisor == 4);
assert(settings.samples_per_frame == 1);
assert(settings.max_bounces == 3);
```

- [ ] **Step 2: Verify RED**

Expected: contract fails because current default is 2.

- [ ] **Step 3: Change the default**

In `PathTracerSettings`:

```cpp
int resolution_divisor = 4;
```

Update `updateTraceResolution()` clamp so 4 remains valid without changing the current upper bound semantics.

- [ ] **Step 4: Replace reset state**

Add to `Impl`:

```cpp
std::uint16_t valid_phase_mask = 0u;
```

Change `resetAccumulation()` to:

```cpp
void resetAccumulation()
{
    sample_count = 0u;
    valid_phase_mask = 0u;
}
```

`resetFrameHistory()` additionally resets `frame_index = 0u`.

Remove `phase_count` and `reset_pending`; validity is represented only by the mask.

- [ ] **Step 5: Detect camera motion separately**

In `render()` calculate before changing stored signature:

```cpp
const bool camera_changed = next_camera_signature != impl_->camera_signature;
```

Scene/light changes still call `resetAccumulation()`. Camera changes also reset accumulation. Call:

```cpp
impl_->dispatch(camera, light, camera_changed);
```

Light changes must not set `camera_changed`.

- [ ] **Step 6: Commit**

```bash
git add Sources/Renderer/PathTracer/PathTracer.hpp Sources/Renderer/PathTracer/PathTracer.cpp tests/path_tracer_world_bvh_contract.cpp
git commit -m "path tracer: track sparse history validity"
```

---

### Task 3: Convert compute shader to true sparse pixel mapping

**Files:**
- Modify: `Sources/Renderer/PathTracer/PathTracerWorldFastShaders.hpp`
- Modify: `Sources/Renderer/PathTracer/PathTracer.cpp`
- Modify: `tests/path_tracer_world_bvh_contract.cpp`

**Interfaces:**
- New trace uniforms: `uPhaseStride`, `uPhaseOffset`, `uReuseHistory`.
- Existing `uResolution` remains full trace-buffer resolution.

- [ ] **Step 1: Update shader contract to require sparse mapping**

Require source fragments equivalent to:

```glsl
uniform int uPhaseStride;
uniform ivec2 uPhaseOffset;
uniform int uReuseHistory;
ivec2 pixel = ivec2(gl_GlobalInvocationID.xy) * uPhaseStride + uPhaseOffset;
```

Require that the old whole-image phase reject is absent:

```cpp
assert(shader.find("if (pixel_phase != phase) return;") == std::string_view::npos);
```

- [ ] **Step 2: Verify RED**

Expected: contract fails against the existing full-image dispatch shader.

- [ ] **Step 3: Modify compute shader entry mapping**

Replace the existing pixel initialization/phase rejection with:

```glsl
ivec2 pixel = ivec2(gl_GlobalInvocationID.xy) * uPhaseStride + uPhaseOffset;
ivec2 size = ivec2(uResolution);
if (any(greaterThanEqual(pixel, size))) return;
```

Remove the physical full-image clear.

Set previous accumulation with:

```glsl
vec4 previous = uReuseHistory != 0
    ? imageLoad(uAccumulation, pixel)
    : vec4(0.0);
```

Keep the same ray generation, path integrator, BVH traversal, material sampling, and alpha sample count.

- [ ] **Step 4: Cache new uniform locations in C++**

Replace obsolete reset/phase uniforms with:

```cpp
GLint phase_stride = -1;
GLint phase_offset = -1;
GLint reuse_history = -1;
```

Add an integer-vector setter if needed:

```cpp
void setIVec2(GLint location, int x, int y)
{
    if (location >= 0) GL20.glUniform2i(location, x, y);
}
```

- [ ] **Step 5: Dispatch compact work rectangles**

Change signature:

```cpp
void dispatch(const CameraState& camera, const LightState& light, bool camera_moving)
```

Choose mode:

```cpp
const int stride = camera_moving ? 4 : 2;
const auto offset = camera_moving
    ? PathTracerPhase::movingPhase(frame_index)
    : PathTracerPhase::stationaryPhase(frame_index);
const std::uint16_t target_mask = camera_moving
    ? PathTracerPhase::movingMask(frame_index)
    : PathTracerPhase::stationaryMask(frame_index);
const bool reuse_history = (valid_phase_mask & target_mask) == target_mask;
```

Dispatch dimensions:

```cpp
const int work_width = std::max((trace_width - static_cast<int>(offset.x) + stride - 1) / stride, 0);
const int work_height = std::max((trace_height - static_cast<int>(offset.y) + stride - 1) / stride, 0);
GL43.glDispatchCompute(
    static_cast<GLuint>((work_width + 7) / 8),
    static_cast<GLuint>((work_height + 7) / 8),
    1u
);
```

After dispatch:

```cpp
valid_phase_mask = static_cast<std::uint16_t>(valid_phase_mask | target_mask);
++frame_index;
```

- [ ] **Step 6: Commit**

```bash
git add Sources/Renderer/PathTracer/PathTracer.cpp Sources/Renderer/PathTracer/PathTracerWorldFastShaders.hpp tests/path_tracer_world_bvh_contract.cpp
git commit -m "path tracer: dispatch sparse camera phases"
```

---

### Task 4: Present only valid history phases

**Files:**
- Modify: `Sources/Renderer/PathTracer/PathTracerWorldFastShaders.hpp`
- Modify: `Sources/Renderer/PathTracer/PathTracer.cpp`
- Modify: `tests/path_tracer_world_bvh_contract.cpp`

**Interfaces:**
- New present uniform: `uValidPhaseMask`.
- Consumes: 16-bit C++ validity mask from Task 2/3.

- [ ] **Step 1: Extend contract**

Require:

```glsl
uniform uint uValidPhaseMask;
```

and phase-bit validation based on `(x & 3, y & 3)`. Require absence of the old alpha-as-validity test.

- [ ] **Step 2: Verify RED**

Expected: contract fails because present shader still uses alpha validity.

- [ ] **Step 3: Add mask helper in GLSL**

```glsl
uint phaseBit(ivec2 pixel)
{
    uint x = uint(pixel.x) & 3u;
    uint y = uint(pixel.y) & 3u;
    return 1u << (y * 4u + x);
}

bool phaseValid(ivec2 pixel)
{
    return (uValidPhaseMask & phaseBit(pixel)) != 0u;
}
```

- [ ] **Step 4: Reconstruct from local 4x4 block**

If `uValidPhaseMask == 0xFFFFu`, use normal filtered `texture()`.

Otherwise search all 16 positions in the containing 4x4 block and choose the nearest `phaseValid(candidate)` texel. Do not use alpha to determine freshness. If none is valid, return `vec4(0.0)`.

- [ ] **Step 5: Pass validity mask from C++**

Cache `uValidPhaseMask` in `PresentUniformLocations` and upload with `glUniform1ui` through a small helper:

```cpp
void setUInt(GLint location, std::uint32_t value)
{
    if (location >= 0) GL20.glUniform1ui(location, value);
}
```

Use:

```cpp
setUInt(present_uniforms.valid_phase_mask, impl_->valid_phase_mask);
```

- [ ] **Step 6: Commit**

```bash
git add Sources/Renderer/PathTracer/PathTracer.cpp Sources/Renderer/PathTracer/PathTracerWorldFastShaders.hpp tests/path_tracer_world_bvh_contract.cpp
git commit -m "path tracer: present only valid sparse history"
```

---

### Task 5: Verify runtime behavior

**Files:** no production changes unless verification exposes a defect.

- [ ] **Step 1: Build the renderer**

```bash
c build
```

Expected: shared library links successfully.

- [ ] **Step 2: Update GAME dependency**

```bash
cd ../GAME
c update ecs-model-rasterizer
```

- [ ] **Step 3: Run Sponza**

```bash
c build test sponza
```

Acceptance:

- no black shader-init failure,
- no stale geometry while moving,
- movement uses visibly lower-resolution sparse reconstruction but remains responsive,
- stopping movement refills detail over subsequent frames.

- [ ] **Step 4: Run Earth**

```bash
c build test earth
```

Acceptance:

- Earth stays textured,
- orbiting sun works,
- light movement does not force 1/16 camera-motion mode,
- no stale-history ghosting.

- [ ] **Step 5: Final scope check**

Confirm defaults remain:

```cpp
resolution_divisor = 4;
samples_per_frame = 1;
max_bounces = 3;
```

Confirm no wavefront/ReSTIR/SVGF files became active and the active shader remains the stackless world-BVH shader.
