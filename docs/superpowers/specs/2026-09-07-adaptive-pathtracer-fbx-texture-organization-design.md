# Adaptive Path Tracer + FBX Texture Organization Design

## Scope

Implement three coupled changes without changing the public `Models::*` API or importing external FBX/image/compression dependencies:

1. Reduce path-tracing workload substantially while the camera is moving.
2. Split model format implementation into dedicated `FBX/` and `OBJ/` folders.
3. Fix FBX diffuse/base-color texture resolution so the Earth FBX renders textured instead of white.

## Global constraints

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

## Path tracer performance design

### Base trace resolution

Change the default path-trace resolution divisor from `2` to `4`.

For a 1280x720 output, the path tracer therefore renders at 320x180 internally. This reduces internal pixel count to one quarter of the current 640x360 trace buffer.

### Adaptive sparse phase mode

Use two phase grids:

- Camera stationary: 2x2 phase grid, one of 4 phases per frame.
- Camera moving: 4x4 phase grid, one of 16 phases per frame.

The compute shader must not dispatch the whole trace texture and then early-return most lanes. Dispatch dimensions are reduced to the active sparse phase lattice and each invocation reconstructs its real pixel coordinate from the phase offset.

Stationary pixel mapping:

```text
real_x = invocation_x * 2 + phase_x
real_y = invocation_y * 2 + phase_y
```

Moving pixel mapping:

```text
real_x = invocation_x * 4 + phase_x
real_y = invocation_y * 4 + phase_y
```

This keeps expensive path tracing work proportional to the active phase instead of launching inactive lanes.

### Camera-motion detection

The renderer already computes camera signatures. A camera signature change marks the current frame as moving. A short stationary grace window is used so the mode does not oscillate immediately between 4x4 and 2x2 because of tiny frame-to-frame input changes.

Light changes and scene changes invalidate accumulation but do not force camera-motion sparse mode. This is important for the orbiting Earth light: its motion must not permanently force the renderer into the 4x4 movement mode.

### History validity

Do not clear the full accumulation image every moving frame.

Track a CPU-side history generation and phase-validity state. Accumulation samples are considered valid only when they belong to the current history generation. Presentation reconstructs missing pixels only from valid samples in the current phase grid.

Because the existing RGBA32F accumulation alpha is used as sample count, generation metadata should be kept separately rather than overloading sample count semantics.

Preferred implementation: a compact integer validity image matching the trace resolution, storing the current history generation for each pixel. The compute shader writes both accumulation and generation for active pixels. Presentation accepts only texels whose generation matches the current generation.

This avoids stale-geometry ghosting without a full-image clear every frame.

## Model format organization

Reorganize:

```text
Sources/Models/Formats/
├── FBX/
│   ├── Fbx.cpp
│   ├── Fbx.hpp
│   ├── Ascii.cpp
│   ├── Ascii.hpp
│   ├── Binary.cpp
│   ├── Binary.hpp
│   ├── Document.cpp
│   ├── Document.hpp
│   ├── Sanitize.cpp
│   └── Sanitize.hpp
└── OBJ/
    ├── Obj.cpp
    └── Obj.hpp
```

`Models.cpp` remains the public dispatch boundary. Callers continue using `Models::load()` and existing handles/types.

The existing `FbxParser.hpp` umbrella is removed. Shared FBX document types live under the FBX folder and are named as FBX implementation details, not as a separate parser module.

`build.c` must add the deeper source glob:

```c
c_sources(library, "Sources/*/*/*/*.cpp");
```

## FBX texture resolution design

### Connection graph

Resolve FBX textures through actual FBX object connections rather than assuming only direct Texture filenames.

Supported path:

```text
Material
  -> Texture
  -> Video
```

Material-to-texture property connections relevant to base color include at least:

- `DiffuseColor`
- `Diffuse`
- `BaseColor`
- `Maya|baseColor`

Texture-to-Video connections are followed when present.

### Embedded texture precedence

Resolution order:

1. Embedded `Video::Content` bytes.
2. Embedded texture content directly on Texture if present.
3. Texture `RelativeFilename`.
4. Texture `FileName`.
5. Connected Video `RelativeFilename`.
6. Connected Video `FileName`.
7. Basename fallback relative to the FBX directory.
8. Basename fallback in `Textures/` and `textures/` beside the FBX.

### Path normalization

Normalize both `/` and `\\` separators. Absolute paths serialized from another machine must not prevent basename fallback.

Example:

```text
C:\project\earth\textures\earth.jpg
```

must be able to resolve as:

```text
<fbx-dir>/earth.jpg
<fbx-dir>/Textures/earth.jpg
<fbx-dir>/textures/earth.jpg
```

### Decode/cache path

All resolved files continue through `Models::loadTexture()` and embedded bytes through `Models::loadTextureMemory()`. No duplicate image decoder is added inside FBX.

### Material behavior

If a valid diffuse/base-color texture resolves, `MaterialData::diffuse_texture` must be valid and `texture_path` must identify the source/cache key. White fallback remains only when the FBX truly has no resolvable base-color texture.

## Regression requirements

### Earth FBX

The Earth regression must verify:

- model loads successfully;
- expected geometry remains present;
- three material parts remain present;
- at least one Earth material has a valid decoded diffuse/base-color texture;
- decoded texture dimensions are positive and RGBA data is non-empty.

### Path tracer contracts

Contracts must verify:

- default `resolution_divisor == 4`;
- stationary phase count is 4;
- moving phase count is 16;
- active compute shader uses sparse invocation-to-pixel mapping rather than whole-image phase rejection;
- `samples_per_frame == 1`;
- `max_bounces == 3`;
- stackless `node.extra.x` traversal remains active;
- generation/validity metadata is checked before presenting accumulation.

## Non-goals

- No physically based FBX material overhaul beyond getting base-color/diffuse textures connected correctly.
- No normal/roughness/metallic FBX rendering expansion in this task.
- No dynamic BLAS/TLAS architecture reintroduction.
- No quality increase while moving; movement prioritizes responsiveness.
- No external dependency installation.
