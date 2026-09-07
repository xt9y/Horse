# Self-Owned FBX and Image Decoders Design

## Goal

Remove runtime/build-time dependence on externally installed asset-decoding libraries while preserving the existing public Models API and current FBX/animation behavior.

Allowed dependencies are source/header implementations committed directly into this repository and compiled as part of the project. For this work, however, `stb_image` and the existing standalone `FbxParser` module are explicitly removed and replaced by engine-owned implementations.

The asset path must require no separately installed `stb`, `ufbx`, `zlib`, `libpng`, `libjpeg`, or equivalent package.

## Public API stability

Keep these public boundaries stable:

- `Models::load()`
- `Models::Fbx::load()` / `Models::Fbx::Document`
- `Models::Images::load()` / `loadMemory()`
- existing `MeshData`, `MaterialData`, skeleton, animation and skin-weight structures

Renderer/ECS code must not know which byte-level decoder produced the data.

## Directory layout

```text
Sources/Models/
├── Compression/
│   ├── Deflate.cpp/.hpp
│   └── Checksums.cpp/.hpp
├── Formats/
│   ├── Fbx.cpp/.hpp
│   ├── FbxBinary.cpp/.hpp
│   ├── FbxAscii.cpp/.hpp
│   ├── FbxDocument.cpp/.hpp
│   └── FbxSanitize.cpp/.hpp
└── Images/
    ├── Image.cpp/.hpp
    ├── Png.cpp/.hpp
    ├── Jpeg.cpp/.hpp
    └── Tga.cpp/.hpp
```

`Fbx.cpp` remains the semantic importer. Binary/ASCII byte parsing is moved behind internal engine-owned modules rather than the old generic `FbxParser` module.

## Compression

Implement an in-tree RFC1950/RFC1951 decoder sufficient for PNG and FBX compressed arrays:

- zlib header validation
- DEFLATE stored blocks
- fixed Huffman blocks
- dynamic Huffman blocks
- canonical Huffman decoding
- LZ77 length/distance copying
- Adler-32 verification
- strict bounds checking
- configurable output limit to prevent malformed asset expansion attacks

The same decoder is reused by PNG and FBX. No `-lz` link remains for asset loading.

## PNG

Implement native PNG decoding for the formats required by engine assets, with standards-correct handling rather than format-specific hacks.

Required:

- signature validation
- chunk iteration and CRC validation
- IHDR, PLTE, tRNS, IDAT, IEND
- concatenated IDAT streams
- color types 0, 2, 3, 4, 6
- bit depths supported by the PNG specification where practical, with at minimum robust 8-bit and 16-bit decoding
- PNG filters None/Sub/Up/Average/Paeth
- Adam7 interlace
- palette expansion
- grayscale/RGB/RGBA conversion to engine RGBA8
- transparency handling
- file and memory decoding through one byte-reader path
- overflow/dimension/output-size validation

`Images::Image` remains RGBA8 and records meaningful alpha exactly as today.

## JPEG

Implement a native baseline JPEG decoder first, with progressive support included in the same architecture rather than requiring an external library.

Required:

- SOI/EOI parsing
- marker scanning
- DQT
- DHT
- SOF0 baseline DCT
- SOF2 progressive DCT
- DRI/restart markers
- SOS scan parsing
- canonical Huffman entropy decode
- DC prediction
- AC run-length decode
- dequantization
- integer or float 8x8 IDCT
- 4:4:4, 4:2:2 and 4:2:0 sampling
- grayscale
- YCbCr -> RGB
- progressive spectral/successive refinement handling
- robust malformed-stream/error handling
- file and embedded-memory decoding

Output is always RGBA8 with alpha 255.

## TGA

Keep the existing native TGA implementation. `Image.cpp` becomes a dispatcher and signature sniffer rather than a stb wrapper.

## Image format selection

For files:

1. Prefer magic/signature detection.
2. Use extension only as a secondary hint/error detail.

For embedded FBX media:

1. PNG signature -> PNG decoder.
2. JPEG SOI -> JPEG decoder.
3. TGA where structurally recognizable or where FBX metadata provides a filename/type hint.
4. Return a precise unsupported/invalid-image error otherwise.

This prevents embedded textures from depending on temporary files or filename extensions.

## FBX byte parser

Delete `FbxParser.cpp/.hpp` and replace it with FBX-specific byte readers internal to the FBX module.

### Binary FBX

Support:

- Kaydara binary signature
- FBX 7.x versions used by current assets
- pre-7500 32-bit node headers
- 7500+ 64-bit node headers
- nested nodes
- scalar properties: bool/int16/int32/int64/float/double
- strings
- raw byte blobs
- float/double/int32/int64/bool arrays
- uncompressed arrays
- zlib/DEFLATE-compressed arrays through the engine compression module
- node/property length validation
- malformed tree/end-offset validation

### ASCII FBX

Support:

- identifiers
- quoted strings and escapes
- integer/floating-point values
- arrays (`*N { a: ... }`)
- nested scopes
- comments and whitespace
- the same internal document representation as binary FBX

## FBX semantic import

Preserve and harden the existing importer behavior:

- object and connection graph
- geometry/control points
- polygon triangulation
- normal mapping/reference modes
- UV mapping/reference modes
- material mapping/reference modes
- materials and diffuse/opacity textures
- external and embedded media
- model hierarchy
- skin deformers and clusters
- cluster `Transform` and `TransformLink`
- top-4 normalized skin weights
- skeleton hierarchy
- animation stacks/layers/curve nodes/curves
- translation/rotation/scale animation sampling
- existing IK runtime integration

This change is primarily about ownership of parsing/decoding. It must not repeat the previously reverted transform-stack experiment unless a failing asset/test demonstrates a semantic problem.

## Earth regression

The current `Earth.fbx` is a required real regression asset.

Tests/source contracts must cover:

- binary FBX 7400 parsing
- compressed numeric arrays
- indexed UVs
- normals
- texture/media resolution
- successful production of renderable triangle data

The change is not considered runtime-verified until the user's GAME build loads and renders Earth without parser/image-decoder failure.

## Tests and development method

Use test-first implementation for each decoder layer.

Compression contracts:

- stored/fixed/dynamic DEFLATE streams
- malformed distance/length
- Adler mismatch
- output-limit enforcement

PNG contracts:

- each filter type
- indexed/palette + transparency
- RGB/RGBA
- interlace
- memory and file paths
- malformed chunks/CRC

JPEG contracts:

- grayscale baseline
- color baseline 4:4:4 and 4:2:0
- restart markers
- progressive image
- malformed Huffman/scan data

FBX contracts:

- ASCII
- binary 7400
- binary 7500+ header form
- compressed numeric arrays
- geometry + UV + normal extraction
- embedded PNG/JPEG media
- skin/cluster/animation preservation

Existing model/path-tracer contracts must remain unchanged unless they expose a real interface mismatch.

## Build changes

Remove:

- `c_git(... "stb" ...)`
- `Sources/Models/ThirdParty/StbImage.cpp`
- direct `<stb_image.h>` includes
- `Sources/Models/Formats/FbxParser.cpp/.hpp`
- `-lz` once no other non-asset system requires it

Add only engine-owned `.cpp/.hpp` files under `Sources/Models`, picked up by the existing source globs.

No workflow files are added, modified, run, or relied upon.

## Error handling and safety

Every byte decoder must:

- use checked integer arithmetic for offsets/sizes
- reject truncated input
- reject invalid references before indexing
- bound decompression output
- bound image dimensions and allocation sizes
- bound FBX recursion/node counts/array sizes
- return descriptive errors through existing `std::string *error` paths
- never trust asset-provided lengths without validating against input size

## Performance

Asset loading is not a per-frame operation, so correctness and bounded memory use take priority over micro-optimization. Still:

- decode directly from contiguous byte spans
- avoid per-byte heap allocation
- pre-size vectors where lengths are known
- use table-driven Huffman fast paths after correctness is established
- reuse temporary buffers within one decode
- never copy embedded image payloads unless ownership/lifetime requires it

Runtime rendering performance must remain unaffected after assets are loaded.

## Success criteria

1. No `stb_image` dependency or source remains.
2. No `FbxParser` module remains.
3. No installed image/FBX/compression package is required for model loading.
4. PNG/JPEG/TGA decode from files and FBX embedded memory.
5. Binary and ASCII FBX decode into the existing semantic importer.
6. Earth FBX reaches the same or better imported mesh/material result as the restored checkpoint.
7. Existing animation, skinning and IK APIs continue to work.
8. Strict C++ build emits no new warnings.
9. The user's GAME build/runtime is the final verification for lwcgl/GPU integration.
