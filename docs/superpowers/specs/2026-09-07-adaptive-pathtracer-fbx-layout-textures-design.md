# Adaptive Path Tracer, Model Format Layout, and FBX Texture Design

## Scope

This change combines three approved requirements that must land coherently:

1. Reduce path-tracer work substantially, especially while the camera is moving.
2. Split the model format implementation into explicit `FBX/` and `OBJ/` folders without changing the public `Models::*` API.
3. Fix FBX material/texture resolution so the Earth FBX renders its real texture instead of white while preserving shadows/lighting.

The current stackless world-BVH path tracer, current material/mesh/skeleton/animation public APIs, current native image decoders, and current no-external-dependency policy remain authoritative.

## Hard Constraints

- No external-installed FBX, image, or compression dependency.
- No `stb_image`.
- No `ufbx`.
- No `FbxParser` module or compatibility header.
- Keep the self-owned binary + ASCII FBX byte readers.
- Keep the self-owned DEFLATE/zlib, PNG, JPEG, and TGA decoders.
- Preserve `Models::load()`, model/mesh/material/texture handles, skeleton, animation, IK, ECS, and renderer public APIs.
- Preserve the current stackless world-space BVH architecture.
- Preserve `samples_per_frame = 1` and `max_bounces = 3`.
- Do not import later wavefront/ReSTIR/SVGF/native-primary architecture.
- Do not change workflows.
- The real `Assets/Earth/Earth.fbx` is a regression target.

## 1. Adaptive Path-Tracer Resolution and Sparse Dispatch

### Default trace resolution

Change the default trace resolution from:

```cpp
resolution_divisor = 2;
```

to:

```cpp
resolution_divisor = 4;
```

At a 1280x720 output this produces a 320x180 trace buffer instead of 640x360. This is one quarter of the previous internal pixel count.

### Camera-motion mode

Camera movement is determined from the existing camera signature comparison in `PathTracer::render()`.

When the camera changed for the current frame:

- use a 4x4 sparse phase grid,
- trace exactly one of 16 phase positions,
- dispatch only the compact work-item rectangle needed for those pixels,
- map each invocation back to its destination pixel with:

```text
pixel = work_item * 4 + phase_offset(frame_index & 15)
```

This must replace the old full-image dispatch plus shader early return. Inactive pixels must not launch compute invocations.

### Stationary mode

When the camera did not change for the current frame:

- use the existing 2x2 phase pattern concept,
- trace one of four 2x2 classes,
- dispatch only one quarter of the trace-buffer pixels,
- map each invocation with:

```text
pixel = work_item * 2 + phase_offset(frame_index & 3)
```

This preserves faster convergence after movement stops.

### Light changes

Light movement invalidates accumulated lighting history but does not select camera-motion mode. The orbiting Earth light therefore does not force the 4x4 sparse camera mode.

## 2. History Validity Without Full-Buffer Clears

The current ghosting fix physically clears the whole accumulation image on reset. That becomes disproportionately expensive once sparse dispatch is introduced.

Replace physical full-image clearing with a 16-bit valid-phase mask maintained by `PathTracer::Impl`.

### Mask model

A trace-buffer pixel belongs to one of 16 phase identities based on `(x mod 4, y mod 4)`.

- A camera/light/scene accumulation reset sets `valid_phase_mask = 0`.
- A 4x4 camera-motion dispatch makes exactly one bit valid.
- A 2x2 stationary dispatch updates four compatible 4x4 phase bits at once.
- Once all 16 bits are valid, the whole accumulation image belongs to the current history generation.

The accumulation texture itself is not cleared. Stale texels remain physically present but are ignored until their phase bit becomes valid again.

### Accumulation write behavior

Before dispatch, C++ determines whether every phase represented by the current dispatch was already valid.

Pass a uniform indicating whether previous accumulation may be loaded for the pixels being updated.

If the target phase was not valid:

```glsl
previous = vec4(0.0);
```

Otherwise:

```glsl
previous = imageLoad(uAccumulation, pixel);
```

This prevents stale history from being added to new history without clearing the texture.

### Presentation

The present shader receives the 16-bit valid phase mask.

For each output sample:

- if all 16 phase bits are valid, use normal filtered texture sampling;
- otherwise, inspect the local 4x4 trace-buffer block and select the nearest texel whose phase bit is valid;
- never sample a texel whose phase bit is invalid;
- if no valid phase exists, output black for that local block.

This is the required replacement for alpha-based validity. Alpha remains sample count only.

## 3. Model Format Directory Structure

Move the format implementation from the flat `Sources/Models/Formats/` directory into:

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

Remove `Sources/Models/Formats/FbxParser.hpp` entirely.

The semantic importer uses `Models::FbxDocument` directly; no `Models::FbxParser` alias remains.

Existing namespaces may remain (`Models::Fbx`, `Models::FbxBinary`, `Models::FbxAscii`, `Models::FbxDocument`, `Models::FbxSanitize`, `Models::Obj`) to avoid unrelated API churn. Only include paths and file locations change.

Update all internal includes and tests to the new paths.

Because the source tree becomes one directory deeper, add:

```c
c_sources(library, "Sources/*/*/*/*.cpp");
```

to `build.c` while preserving all existing source globs.

## 4. FBX Material and Texture Resolution

### Existing behavior to preserve

The FBX importer already:

- resolves materials connected to models,
- resolves textures connected to materials,
- prefers `Diffuse` / `BaseColor` property connections,
- reads `RelativeFilename` / `FileName`,
- can decode embedded `Video::Content` when found,
- decodes textures through the native image pipeline.

The fix must extend that path rather than replace it.

### Connection graph

Resolve diffuse/base-color images through the complete FBX graph:

```text
Material
   <- OP/OO - Texture
   <- OO/OP - Video
```

Support both connection directions that appear in real FBX exporters by using the scene connection tables rather than assuming only one orientation.

For a chosen diffuse/base-color texture:

1. inspect the Texture object itself for embedded content if present,
2. inspect connected Video objects for `Content`,
3. inspect Texture `RelativeFilename` / `FileName`,
4. inspect connected Video `RelativeFilename` / `Filename` / `FileName`.

Embedded image bytes take priority over external paths.

### Path normalization and fallback

Normalize `\\` to `/` before constructing `std::filesystem::path`.

For every external candidate, try in this order:

1. candidate as an absolute path if it exists,
2. `<fbx directory>/<relative candidate>`,
3. `<fbx directory>/<basename>`,
4. `<fbx directory>/Textures/<basename>`,
5. `<fbx directory>/textures/<basename>`.

Stop at the first image that decodes successfully.

A failed first candidate must not permanently force `INVALID_TEXTURE`; continue through the remaining candidates.

### Earth regression

The Earth asset submodule currently contains only `Earth.fbx`, with no sibling image files. The regression therefore must verify that the real FBX produces at least one material with a valid decoded diffuse texture. This specifically exercises embedded FBX texture content if the asset contains it.

The Earth regression must continue to verify valid geometry, finite positions/normals/UVs, and nonzero triangle count.

## 5. Tests and Contracts

### Path tracer

Extend the path-tracer contract to assert:

- default `resolution_divisor == 4`,
- `samples_per_frame == 1`,
- `max_bounces == 3`,
- active compute shader uses sparse work-item-to-pixel mapping,
- moving stride 4 and stationary stride 2 are supported,
- no full accumulation clear exists,
- a valid phase-mask uniform exists in trace/present paths,
- present shader rejects invalid phases.

Add a small C++ contract for phase masks that verifies:

- each 4x4 moving phase maps to one unique bit,
- the four stationary 2x2 phases each map to four bits,
- the union of all stationary masks equals `0xFFFF`,
- moving phase offsets cover all 16 `(x,y)` combinations.

### Model formats

Add/update source-layout contracts so:

- new FBX/OBJ include paths compile,
- old flat format includes are not used,
- `FbxParser.hpp` is absent from production includes.

### FBX texture

Extend `earth_fbx_contract.cpp` so the loaded Earth document must contain at least one material whose `diffuse_texture != INVALID_TEXTURE` and whose referenced `TextureAsset` has non-empty decoded RGBA data.

## 6. Verification

Required local verification after implementation:

```bash
c build
c build test path-tracer-world-bvh-contract
c build test earth-fbx-contract -- Assets/Earth/Earth.fbx
```

Then from `GAME`:

```bash
c update ecs-model-rasterizer
c build test earth-sun-orbit-contract
c build test earth
c build test sponza
```

Runtime acceptance:

- Earth geometry remains correct and is visibly textured rather than white.
- Earth sun continues to orbit and cast moving shadows.
- Sponza no longer shows stale geometry after camera movement.
- Camera movement is materially more responsive due to 4x internal resolution and true 1/16 sparse moving dispatch.
- Stopping the camera refills the image using 1/4 sparse stationary dispatch.
