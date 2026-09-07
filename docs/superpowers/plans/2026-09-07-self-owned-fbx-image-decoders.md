# Self-Owned FBX and Image Decoders Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove `stb_image`, the standalone `FbxParser` module, and installed asset-decoding/compression dependencies while preserving the existing Models/Animation/Renderer interfaces and loading PNG/JPEG/TGA plus binary/ASCII FBX entirely from repository-owned C++ code.

**Architecture:** Add one reusable bounded RFC1950/RFC1951 decoder under `Models/Compression`, native PNG/JPEG decoders under `Models/Images`, and FBX-specific document/binary/ASCII readers under `Models/Formats`. `Fbx.cpp` remains the semantic importer and consumes the new `FbxDocument` representation so Renderer/ECS/Animation remain unchanged.

**Tech Stack:** C++20, existing C-BuildSystem source globs, no installed image/FBX/compression package, repository-owned C++ only.

**Spec:** `docs/superpowers/specs/2026-09-07-self-owned-fbx-image-decoders-design.md`

## Global Constraints

- No `stb_image`, `ufbx`, `zlib`, `libpng`, `libjpeg`, or equivalent separately installed package is required by model/image loading.
- Source/header code committed directly to this repository is allowed, but the implementation in this plan remains engine-owned.
- Preserve `Models::load()`, `Models::Fbx::load()` / `Models::Fbx::Document`, `Models::Images::load()` / `loadMemory()`, `MeshData`, `MaterialData`, skeleton, animation, skinning, and IK public APIs.
- Do not reintroduce the previously reverted FBX transform-stack experiment unless a failing regression proves a semantic defect.
- No workflow files are added, modified, run, inspected, or relied upon.
- All byte decoders use checked offsets/sizes, explicit output limits, and descriptive `std::string *error` failures.
- Strict C++ warnings must not introduce new warnings.

---

## File Structure

### Compression

- Create `Sources/Models/Compression/Checksums.hpp`: Adler-32 and CRC-32 public internal helpers.
- Create `Sources/Models/Compression/Checksums.cpp`: table-free/small-table checksum implementations.
- Create `Sources/Models/Compression/Deflate.hpp`: bounded zlib/DEFLATE decode API.
- Create `Sources/Models/Compression/Deflate.cpp`: bit reader, canonical Huffman builder/decoder, stored/fixed/dynamic blocks, LZ77 copy, Adler verification.
- Create `tests/deflate_contract.cpp`: deterministic stored/fixed/dynamic/malformed/output-limit contracts.

### Images

- Create `Sources/Models/Images/Png.hpp/.cpp`: PNG byte decoder to `Images::Image`.
- Create `Sources/Models/Images/Jpeg.hpp/.cpp`: baseline + progressive JPEG byte decoder to `Images::Image`.
- Modify `Sources/Models/Images/Image.cpp`: signature dispatcher; no stb calls.
- Keep `Sources/Models/Images/Tga.hpp/.cpp` as the TGA implementation.
- Delete `Sources/Models/ThirdParty/StbImage.cpp`.
- Create `tests/image_png_contract.cpp`, `tests/image_jpeg_contract.cpp`, `tests/image_dispatch_contract.cpp`.

### FBX

- Create `Sources/Models/Formats/FbxDocument.hpp/.cpp`: FBX-only `Property`, `Node`, and `RawDocument` representation plus typed accessors.
- Create `Sources/Models/Formats/FbxBinary.hpp/.cpp`: binary FBX reader including 32/64-bit headers and compressed numeric arrays via `Deflate`.
- Create `Sources/Models/Formats/FbxAscii.hpp/.cpp`: ASCII tokenizer/parser producing the same `RawDocument`.
- Modify `Sources/Models/Formats/Fbx.cpp`: consume `FbxDocument`, dispatch binary/ASCII parsing, preserve semantic import.
- Delete `Sources/Models/Formats/FbxParser.hpp/.cpp` after all consumers move.
- Replace `tests/fbx_parser_contract.cpp` with `tests/fbx_binary_contract.cpp` and `tests/fbx_ascii_contract.cpp`.
- Extend `tests/fbx_import_contract.cpp` for embedded PNG/JPEG and current skin/animation preservation.

### Build

- Modify `build.c`: remove `c_git(..."stb"...)`, `c_dep_header_only(stb)`, `c_use(library, stb)`, and `c_link_system(target, "z")` once compression no longer uses the system library.

---

### Task 1: Engine-Owned Checksums and Bounded DEFLATE

**Files:**
- Create: `Sources/Models/Compression/Checksums.hpp`
- Create: `Sources/Models/Compression/Checksums.cpp`
- Create: `Sources/Models/Compression/Deflate.hpp`
- Create: `Sources/Models/Compression/Deflate.cpp`
- Create: `tests/deflate_contract.cpp`

**Interfaces:**
- Produces:
```cpp
namespace Models::Compression {
std::uint32_t adler32(const std::uint8_t *data, std::size_t size);
std::uint32_t crc32(const std::uint8_t *data, std::size_t size);

struct InflateOptions {
    std::size_t max_output = 256u * 1024u * 1024u;
    bool verify_adler32 = true;
};

bool inflateZlib(
    const std::uint8_t *data,
    std::size_t size,
    std::vector<std::uint8_t> *output,
    std::string *error = nullptr,
    InflateOptions options = {}
);
}
```
- Consumed later by PNG and binary FBX.

- [ ] **Step 1: Write checksum and stored-block RED contract**

Create `tests/deflate_contract.cpp` with fixed vectors instead of using zlib in the test:

```cpp
#include "Models/Compression/Checksums.hpp"
#include "Models/Compression/Deflate.hpp"
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

int main()
{
    using namespace Models::Compression;
    static constexpr std::uint8_t text[] = {'W','i','k','i','p','e','d','i','a'};
    assert(adler32(text, sizeof(text)) == 0x11e60398u);
    assert(crc32(reinterpret_cast<const std::uint8_t *>("123456789"), 9u) == 0xcbf43926u);

    // zlib header 0x78 0x01, one final stored DEFLATE block containing "hello",
    // Adler-32("hello") = 0x062c0215.
    const std::vector<std::uint8_t> stored = {
        0x78,0x01, 0x01,0x05,0x00,0xfa,0xff,
        'h','e','l','l','o', 0x06,0x2c,0x02,0x15
    };
    std::vector<std::uint8_t> out;
    std::string error;
    assert(inflateZlib(stored.data(), stored.size(), &out, &error));
    assert(std::string(out.begin(), out.end()) == "hello");
    assert(error.empty());
}
```

- [ ] **Step 2: Run the contract and verify RED**

Run:
```bash
c++ -std=c++20 -ISources tests/deflate_contract.cpp -o /tmp/deflate_contract
```
Expected: compile failure because `Models/Compression/Checksums.hpp` and `Deflate.hpp` do not exist.

- [ ] **Step 3: Implement checksums and stored blocks**

Implement Adler-32 modulo 65521 and CRC-32 polynomial `0xedb88320`. Implement a bounds-checked MSB-independent DEFLATE bit reader (least-significant bit first), validate CMF/FLG (`CM=8`, `(CMF<<8|FLG)%31==0`, no preset dictionary), then decode BTYPE=0 stored blocks by byte-aligning and validating LEN/NLEN.

The stored-block path must reject output growth when `output->size() + len > options.max_output` before allocation/copy.

- [ ] **Step 4: Add fixed-Huffman RED cases**

Extend the test with a hardcoded RFC1951 fixed-Huffman zlib stream generated once and committed as bytes. Assert the exact decompressed payload and add an `InflateOptions{4u, true}` case that fails with an error containing `output limit`.

- [ ] **Step 5: Implement canonical fixed Huffman decoding**

Inside `Deflate.cpp`, add internal structures:
```cpp
struct Huffman {
    std::array<std::uint16_t, 16> count{};
    std::vector<std::uint16_t> symbol;
};

bool buildHuffman(
    const std::uint8_t *lengths,
    std::size_t count,
    Huffman *table,
    std::string *error
);

bool decodeSymbol(BitReader *reader, const Huffman& table, std::uint16_t *symbol);
```
Use canonical code counts, reject oversubscribed/incomplete invalid trees, and implement literal/length codes 257..285 plus distance codes 0..29 with RFC1951 base/extra-bit tables.

- [ ] **Step 6: Add dynamic-Huffman and malformed-distance RED cases**

Extend `tests/deflate_contract.cpp` with a committed dynamic-Huffman zlib stream, a corrupted distance stream, a bad Adler stream, and a truncated stream. Assertions must check `false` and non-empty diagnostic text.

- [ ] **Step 7: Implement dynamic Huffman blocks and Adler verification**

Implement HLIT/HDIST/HCLEN, code-length alphabet ordering `{16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15}`, repeat symbols 16/17/18, and distance validation (`distance != 0 && distance <= output.size()`). Verify trailing big-endian Adler-32 when enabled.

- [ ] **Step 8: Run the complete compression contract**

Run:
```bash
c++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -ISources \
  tests/deflate_contract.cpp \
  Sources/Models/Compression/Checksums.cpp \
  Sources/Models/Compression/Deflate.cpp \
  -o /tmp/deflate_contract && /tmp/deflate_contract
```
Expected: exit 0.

- [ ] **Step 9: Commit**

```bash
git add Sources/Models/Compression tests/deflate_contract.cpp
git commit -m "models: add self-owned bounded deflate decoder"
```

---

### Task 2: Native PNG Decoder

**Files:**
- Create: `Sources/Models/Images/Png.hpp`
- Create: `Sources/Models/Images/Png.cpp`
- Create: `tests/image_png_contract.cpp`

**Interfaces:**
- Consumes: `Compression::inflateZlib()`, `Compression::crc32()`.
- Produces:
```cpp
namespace Models::Images::Png {
bool matches(const std::uint8_t *data, std::size_t size);
bool decode(
    const std::uint8_t *data,
    std::size_t size,
    Image *image,
    std::string *error = nullptr
);
}
```

- [ ] **Step 1: Write basic RGBA + CRC RED contract**

Create a test containing a committed tiny valid 1x1 RGBA PNG byte array. Assert `matches()`, width/height=1, RGBA payload equality, and `meaningful_alpha` for alpha != 255. Duplicate the bytes, flip one byte inside an IDAT/IHDR payload without updating CRC, and assert decode failure contains `CRC`.

- [ ] **Step 2: Verify RED**

Run:
```bash
c++ -std=c++20 -ISources tests/image_png_contract.cpp -o /tmp/image_png_contract
```
Expected: missing `Png.hpp`.

- [ ] **Step 3: Implement signature/chunk/IHDR/IDAT decode**

Implement an offset reader with big-endian u32 reads. Validate PNG signature, chunk length against remaining bytes, CRC over `type||data`, exactly one IHDR before IDAT, dimensions >0 and bounded allocation, then concatenate IDAT bytes and call `inflateZlib()`.

Support color types 0/2/3/4/6 and legal bit-depth combinations. Start GREEN with 8-bit RGBA/RGB/grayscale; return precise `unsupported PNG bit depth/color type combination` only for combinations not yet covered by the current test.

- [ ] **Step 4: Add filter RED cases**

Add five tiny scanline fixtures covering filter bytes 0..4. Assert exact reconstructed bytes. Implement None/Sub/Up/Average/Paeth with byte-per-pixel based on source samples, not RGBA output width.

- [ ] **Step 5: Add palette+tRNS and 16-bit RED cases**

Add an indexed PNG fixture with PLTE+tRNS and a 16-bit grayscale/RGBA fixture. Assert palette expansion, transparency, and high-byte-to-RGBA8 conversion using `(sample * 255 + max/2)/max` semantics for sub-8-bit and high-byte/rounded scaling for 16-bit.

- [ ] **Step 6: Implement PLTE/tRNS and packed sample expansion**

Implement 1/2/4/8-bit packed sample extraction, palette bounds validation, grayscale/RGB transparent-key handling from tRNS, and meaningful alpha calculation.

- [ ] **Step 7: Add Adam7 RED contract**

Commit one tiny interlaced PNG fixture and assert all reconstructed pixels. Implement the seven passes using standard `(x_start,y_start,x_step,y_step)` tuples:
```cpp
{{0,0,8,8},{4,0,8,8},{0,4,4,8},{2,0,4,4},{0,2,2,4},{1,0,2,2},{0,1,1,2}}
```
Unfilter each pass independently and scatter decoded pixels into the final image.

- [ ] **Step 8: Verify PNG contract**

Run:
```bash
c++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -ISources \
  tests/image_png_contract.cpp \
  Sources/Models/Images/Png.cpp \
  Sources/Models/Compression/Checksums.cpp \
  Sources/Models/Compression/Deflate.cpp \
  -o /tmp/image_png_contract && /tmp/image_png_contract
```
Expected: exit 0.

- [ ] **Step 9: Commit**

```bash
git add Sources/Models/Images/Png.* tests/image_png_contract.cpp
git commit -m "models: add native PNG decoder"
```

---

### Task 3: Baseline JPEG Decoder

**Files:**
- Create: `Sources/Models/Images/Jpeg.hpp`
- Create: `Sources/Models/Images/Jpeg.cpp`
- Create: `tests/image_jpeg_contract.cpp`

**Interfaces:**
```cpp
namespace Models::Images::Jpeg {
bool matches(const std::uint8_t *data, std::size_t size);
bool decode(
    const std::uint8_t *data,
    std::size_t size,
    Image *image,
    std::string *error = nullptr
);
}
```

- [ ] **Step 1: Write baseline grayscale and color RED contract**

Commit small known-good JPEG byte fixtures directly in the test (one grayscale SOF0 and one YCbCr SOF0). Assert dimensions, alpha=255, and per-channel output within a tolerance of 2 because IDCT rounding may differ.

- [ ] **Step 2: Verify RED**

Run:
```bash
c++ -std=c++20 -ISources tests/image_jpeg_contract.cpp -o /tmp/image_jpeg_contract
```
Expected: missing `Jpeg.hpp`.

- [ ] **Step 3: Implement JPEG marker/frame/table parser**

Implement SOI/EOI, APP/COM skipping, DQT (8/16-bit quant tables), DHT canonical tables, SOF0 frame/components, DRI restart interval, and SOS scan descriptors. Reject arithmetic coding, lossless frames, invalid component/table indices, dimensions <=0, or allocations above the image limit.

Use:
```cpp
struct QuantTable { bool valid=false; std::array<std::uint16_t,64> value{}; };
struct HuffmanTable {
    bool valid=false;
    std::array<std::uint8_t,16> counts{};
    std::vector<std::uint8_t> symbols;
};
```

- [ ] **Step 4: Implement baseline entropy decode**

Implement byte-stuffing (`0xff 0x00`), marker detection, canonical Huffman symbol decode, receive/extend signed values, DC prediction per component, AC run-length (`0x00` EOB, `0xf0` ZRL), zig-zag placement, and dequantization.

- [ ] **Step 5: Implement 8x8 IDCT + sampling**

Implement a deterministic separable IDCT (float is acceptable for asset loading), clamp samples to 0..255 after +128 level shift, decode MCU sampling factors, and support 4:4:4, 4:2:2, 4:2:0 plus grayscale. Upsample chroma by coordinate mapping into component planes.

Use YCbCr conversion:
```cpp
R = Y + 1.40200f * (Cr - 128);
G = Y - 0.344136f * (Cb - 128) - 0.714136f * (Cr - 128);
B = Y + 1.77200f * (Cb - 128);
```

- [ ] **Step 6: Add restart-marker RED contract and implement it**

Commit a baseline fixture with DRI/RST markers. Assert decode succeeds. At each restart interval: byte-align entropy reader, require the expected RST0..RST7 sequence modulo 8, reset DC predictors, and continue.

- [ ] **Step 7: Verify baseline JPEG contract**

Run:
```bash
c++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -ISources \
  tests/image_jpeg_contract.cpp Sources/Models/Images/Jpeg.cpp \
  -o /tmp/image_jpeg_contract && /tmp/image_jpeg_contract
```
Expected: exit 0 for baseline/restart cases.

- [ ] **Step 8: Commit**

```bash
git add Sources/Models/Images/Jpeg.* tests/image_jpeg_contract.cpp
git commit -m "models: add native baseline JPEG decoder"
```

---

### Task 4: Progressive JPEG Completion

**Files:**
- Modify: `Sources/Models/Images/Jpeg.cpp`
- Modify: `tests/image_jpeg_contract.cpp`

**Interfaces:** Existing `Jpeg::decode()` remains unchanged.

- [ ] **Step 1: Add progressive RED fixture**

Add a small SOF2 fixture containing DC-first, AC-first and successive-refinement scans. Assert final dimensions and RGB pixels within tolerance 3 of known reference values.

- [ ] **Step 2: Verify RED**

Run the Task 3 JPEG compile/run command. Expected: failure with the current unsupported SOF2/progressive path.

- [ ] **Step 3: Add persistent coefficient blocks**

Store one `std::array<std::int32_t,64>` coefficient block per component block. Parse SOS `Ss`, `Se`, `Ah`, `Al` and implement:
- DC first: decode differential and store `value << Al`.
- DC refinement: OR/set the `1 << Al` refinement bit.
- AC first: decode run/size, EOBRUN, store new nonzero coefficients shifted by `Al`.
- AC refinement: refine existing nonzero coefficients, handle EOBRUN, and insert new ±`1 << Al` coefficients.

Run IDCT only after all scans have been consumed.

- [ ] **Step 4: Add malformed progressive scan RED cases**

Assert failure for `Ss > Se`, `Se > 63`, invalid `Ah/Al` progression, and truncated entropy data.

- [ ] **Step 5: Verify full JPEG contract**

Run the Task 3 strict command. Expected: exit 0 including progressive cases.

- [ ] **Step 6: Commit**

```bash
git add Sources/Models/Images/Jpeg.cpp tests/image_jpeg_contract.cpp
git commit -m "models: support progressive JPEG decoding"
```

---

### Task 5: Replace stb Image Dispatch Completely

**Files:**
- Modify: `Sources/Models/Images/Image.cpp`
- Keep: `Sources/Models/Images/Image.hpp`
- Delete: `Sources/Models/ThirdParty/StbImage.cpp`
- Create: `tests/image_dispatch_contract.cpp`

**Interfaces:** Existing `Images::load()` and `loadMemory()` remain unchanged.

- [ ] **Step 1: Write dispatcher RED contract**

Use the PNG/JPEG fixture arrays plus a small valid TGA byte fixture. For each format call `loadMemory()` and assert correct dimensions. Write PNG bytes to a temporary file with a deliberately wrong `.jpg` suffix and assert `load()` still chooses PNG by signature.

- [ ] **Step 2: Verify RED against current stb implementation removal target**

Temporarily compile the test only with engine image sources excluding `StbImage.cpp`; expected link/compile failure because `Image.cpp` still references stb APIs.

- [ ] **Step 3: Rewrite `Image.cpp` as a signature dispatcher**

Read file bytes once into `std::vector<std::uint8_t>`, then call one internal function:
```cpp
bool decodeMemory(
    const std::uint8_t *data,
    std::size_t size,
    const std::string& hint,
    Image *image,
    std::string *error
);
```
Dispatch order: PNG magic, JPEG SOI, TGA structural/hint fallback. Do not call temporary files for memory decode.

- [ ] **Step 4: Delete `StbImage.cpp` and verify no stb reference**

Run:
```bash
grep -R "stb_image\|stbi_" Sources build.c tests && exit 1 || true
```
Expected: no output.

- [ ] **Step 5: Run dispatcher contract**

Compile with `Image.cpp`, `Png.cpp`, `Jpeg.cpp`, `Tga.cpp`, and compression sources under strict warnings; run and expect exit 0.

- [ ] **Step 6: Commit**

```bash
git add Sources/Models/Images tests/image_dispatch_contract.cpp
git rm Sources/Models/ThirdParty/StbImage.cpp
git commit -m "models: remove stb image loading"
```

---

### Task 6: FBX-Specific Raw Document Representation

**Files:**
- Create: `Sources/Models/Formats/FbxDocument.hpp`
- Create: `Sources/Models/Formats/FbxDocument.cpp`
- Create: `tests/fbx_document_contract.cpp`

**Interfaces:**
```cpp
namespace Models::FbxDocument {
using Bytes = std::vector<std::uint8_t>;
using Value = std::variant<
    std::int16_t, bool, std::int32_t, float, double, std::int64_t,
    std::string, Bytes,
    std::vector<float>, std::vector<double>,
    std::vector<std::int32_t>, std::vector<std::int64_t>,
    std::vector<std::uint8_t>
>;

struct Property {
    char type = 0;
    Value value = std::int32_t{0};
    std::int64_t asInt64(std::int64_t fallback=0) const;
    double asDouble(double fallback=0.0) const;
    std::string asString(std::string fallback={}) const;
    const std::vector<double> *asDoubleArray() const;
    const std::vector<float> *asFloatArray() const;
    const std::vector<std::int32_t> *asInt32Array() const;
    const std::vector<std::int64_t> *asInt64Array() const;
};

struct Node {
    std::string name;
    std::vector<Property> properties;
    std::vector<Node> children;
    const Node *child(const std::string&) const;
    std::vector<const Node *> childrenNamed(const std::string&) const;
    std::vector<double> numericArray() const;
};

struct RawDocument {
    std::uint32_t version=0;
    bool binary=false;
    Node root;
};
}
```

- [ ] **Step 1: Write accessor RED contract**

Construct `Property`/`Node` values directly and assert conversions and child lookup. Verify incompatible conversions return supplied fallbacks, not exceptions.

- [ ] **Step 2: Implement accessors**

Move only representation/accessor behavior from the old parser into this FBX-specific namespace. No byte parsing belongs in these files.

- [ ] **Step 3: Run strict standalone contract**

Compile `fbx_document_contract.cpp + FbxDocument.cpp`; expected exit 0.

- [ ] **Step 4: Commit**

```bash
git add Sources/Models/Formats/FbxDocument.* tests/fbx_document_contract.cpp
git commit -m "models: add FBX raw document representation"
```

---

### Task 7: Native Binary FBX Reader Without zlib

**Files:**
- Create: `Sources/Models/Formats/FbxBinary.hpp`
- Create: `Sources/Models/Formats/FbxBinary.cpp`
- Create: `tests/fbx_binary_contract.cpp`

**Interfaces:**
```cpp
namespace Models::FbxBinary {
bool matches(const std::uint8_t *data, std::size_t size);
bool parse(
    const std::uint8_t *data,
    std::size_t size,
    FbxDocument::RawDocument *out,
    std::string *error=nullptr
);
}
```

- [ ] **Step 1: Port the 7400 contract to hardcoded compressed bytes**

Replace test-time `<zlib.h>` use. The contract constructs a binary FBX 7400 container with a precomputed zlib-compressed double array and asserts version, binary flag, `Vertices` numeric values, and string property extraction.

- [ ] **Step 2: Verify RED**

Compile with FbxDocument/Compression but no FbxBinary implementation; expected missing symbol/header failure.

- [ ] **Step 3: Implement binary header/node/property reader**

Validate exact 23-byte Kaydara signature and version. For version <7500 read node header `(u32 end_offset,u32 property_count,u32 property_bytes,u8 name_len)` and 13-byte null records. For >=7500 use three u64 fields and 25-byte null records. Reject end offsets outside input, backwards offsets, property regions beyond node end, recursion depth >256, and total nodes >1,000,000.

- [ ] **Step 4: Implement scalar/string/raw properties**

Support FBX property type tags `Y,C,I,F,D,L,S,R`. Every length is validated before read/copy.

- [ ] **Step 5: Implement numeric arrays through engine DEFLATE**

Support tags `f,d,i,l,b`. Parse `(u32 length,u32 encoding,u32 compressed_length)`. Encoding 0 reads raw little-endian elements; encoding 1 calls `Compression::inflateZlib()` with `max_output = element_count * element_size` exactly. Reject all other encodings and size mismatches.

- [ ] **Step 6: Add 7500+ RED contract and malformed offsets**

Add one minimal version 7500 fixture using 64-bit node headers. Also test a node end offset beyond input and an array whose decompressed size is wrong.

- [ ] **Step 7: Verify strict binary contract**

Compile/run with `FbxBinary.cpp`, `FbxDocument.cpp`, compression sources; expected exit 0.

- [ ] **Step 8: Commit**

```bash
git add Sources/Models/Formats/FbxBinary.* tests/fbx_binary_contract.cpp
git commit -m "models: add self-owned binary FBX reader"
```

---

### Task 8: Native ASCII FBX Reader

**Files:**
- Create: `Sources/Models/Formats/FbxAscii.hpp`
- Create: `Sources/Models/Formats/FbxAscii.cpp`
- Create: `tests/fbx_ascii_contract.cpp`

**Interfaces:**
```cpp
namespace Models::FbxAscii {
bool parse(
    const std::uint8_t *data,
    std::size_t size,
    FbxDocument::RawDocument *out,
    std::string *error=nullptr
);
}
```

- [ ] **Step 1: Write ASCII RED fixture**

Use a compact ASCII FBX containing `FBXVersion: 7400`, a Geometry node, `Vertices: *9 { a: ... }`, `PolygonVertexIndex`, quoted names, comments, scientific notation, and escaped quotes. Assert resulting tree and numeric arrays.

- [ ] **Step 2: Implement tokenizer**

Token types: identifier, string, integer, real, colon, comma, `{`, `}`, `*`, newline/end. Skip `;` comments through newline. Strings handle `\\`, `\"`, `\n`, `\r`, `\t`. Parse numbers with `std::from_chars` where supported and a locale-independent fallback for floating point.

- [ ] **Step 3: Implement recursive node parser**

Parse `Name: property, property { children }`, and special `*N { a: ... }` arrays into a Node whose first property holds the typed numeric vector. Bound recursion/nodes/array elements with the same limits as binary.

- [ ] **Step 4: Add malformed ASCII RED cases**

Assert descriptive failure for unterminated string, unbalanced braces, invalid declared array count, and numeric overflow.

- [ ] **Step 5: Verify strict ASCII contract**

Compile/run with FbxAscii+FbxDocument; expected exit 0.

- [ ] **Step 6: Commit**

```bash
git add Sources/Models/Formats/FbxAscii.* tests/fbx_ascii_contract.cpp
git commit -m "models: add self-owned ASCII FBX reader"
```

---

### Task 9: Switch Semantic FBX Importer to New Readers

**Files:**
- Modify: `Sources/Models/Formats/Fbx.cpp`
- Modify: `tests/fbx_import_contract.cpp`

**Interfaces:**
- Replace `using RawDocument = FbxParser::Document;` with `FbxDocument::RawDocument`.
- Replace `using RawNode = FbxParser::Node;` with `FbxDocument::Node`.
- `Models::Fbx::load()` public signature remains unchanged.

- [ ] **Step 1: Add importer RED coverage for binary/ASCII dispatch**

Retain the existing semantic ASCII fixture assertions for mesh, material, skeleton, skin weights and animation. Add a tiny binary document path through `FbxBinary` that reaches semantic geometry extraction.

- [ ] **Step 2: Replace parser include/types without changing semantic algorithms**

`Fbx.cpp` includes `FbxAscii.hpp`, `FbxBinary.hpp`, and `FbxDocument.hpp`. Add one private loader:
```cpp
bool parseRawFile(const std::string& path, FbxDocument::RawDocument *out, std::string *error)
{
    // read bytes once; binary magic => FbxBinary::parse, otherwise FbxAscii::parse
}
```
Do not modify existing triangulation, mapping/reference, material, skeleton, cluster, animation, or sanitization logic except for type/namespace adaptation required to compile.

- [ ] **Step 3: Add embedded PNG/JPEG RED coverage**

Extend semantic test data with Video/Texture objects whose `Content` raw bytes are the PNG/JPEG test fixtures. Assert importer creates valid `diffuse_texture` handles through `Images::loadMemory()`.

- [ ] **Step 4: Verify semantic contracts**

Run the strict standalone importer contract using Models/Animation source files needed by `Fbx.cpp`. Then run:
```bash
c build
```
Expected: dependency library builds with no new warnings on the local project toolchain.

- [ ] **Step 5: Commit**

```bash
git add Sources/Models/Formats/Fbx.cpp tests/fbx_import_contract.cpp
git commit -m "models: route FBX import through engine-owned readers"
```

---

### Task 10: Remove Old Parser and Installed Asset Dependencies

**Files:**
- Delete: `Sources/Models/Formats/FbxParser.hpp`
- Delete: `Sources/Models/Formats/FbxParser.cpp`
- Delete/replace: `tests/fbx_parser_contract.cpp`
- Modify: `build.c`

- [ ] **Step 1: Assert dependency-clean RED state**

Run:
```bash
grep -R "FbxParser\|stb_image\|stbi_\|<zlib.h>" Sources tests build.c
```
Expected before cleanup: remaining old parser/build references are listed.

- [ ] **Step 2: Delete old parser files/test and remove stb build dependency**

Remove the old files. In `build.c`, delete the complete `C_Dependency *stb = c_git(...)` block, `c_dep_header_only`, `c_dep_include`, and `c_use`.

- [ ] **Step 3: Remove installed zlib link**

Delete `c_link_system(target, "z");`. Do not replace it with another installed compression library.

- [ ] **Step 4: Prove no forbidden references remain**

Run:
```bash
if grep -R -n -E "FbxParser|stb_image|stbi_|<zlib.h>|c_link_system\(target, \"z\"\)|c_git\([^\n]*stb" Sources tests build.c; then
  exit 1
fi
```
Expected: exit 0 and no matches.

- [ ] **Step 5: Build**

Run:
```bash
c build
```
Expected: exit 0 with no new strict-warning diagnostics.

- [ ] **Step 6: Commit**

```bash
git add build.c Sources tests
git commit -m "models: remove external asset decoding dependencies"
```

---

### Task 11: Earth and End-to-End Regression Pass

**Files:**
- Create: `tests/earth_fbx_contract.cpp`
- Modify only if a real failing contract requires it: relevant decoder/importer file.

**Interfaces:** Public APIs remain unchanged.

- [ ] **Step 1: Add Earth contract**

The test takes the Earth asset path from argv rather than embedding the 20+ MB asset:
```cpp
int main(int argc, char **argv)
{
    assert(argc == 2);
    Models::Fbx::Document document;
    std::string error;
    assert(Models::Fbx::load(argv[1], &document, &error));
    assert(error.empty());
    assert(!document.parts.empty());
    std::size_t triangles = 0;
    for (const auto& part : document.parts) {
        assert(part.mesh.indices.size() % 3u == 0u);
        triangles += part.mesh.indices.size() / 3u;
        for (const auto& vertex : part.mesh.vertices) {
            assert(std::isfinite(vertex.position.x));
            assert(std::isfinite(vertex.position.y));
            assert(std::isfinite(vertex.position.z));
            assert(std::isfinite(vertex.uv.x));
            assert(std::isfinite(vertex.uv.y));
        }
    }
    assert(triangles > 0u);
}
```

- [ ] **Step 2: Run the Earth contract against the real asset**

On a checkout containing the Earth submodule/asset, compile the contract with the library or add it to a local manual test command, then run:
```bash
/tmp/earth_fbx_contract path/to/Earth.fbx
```
Expected: exit 0. If this fails, record the exact parser/image error and fix only that proven decoder/importer defect with a new minimal regression before proceeding.

- [ ] **Step 3: Run all standalone decoder contracts**

Run compression, PNG, JPEG, dispatcher, FBX document, binary, ASCII, semantic import, animation IK, BVH, and path-tracer contracts. Every command must exit 0.

- [ ] **Step 4: Run repository build**

Run:
```bash
c build
```
Expected: exit 0, no new warnings.

- [ ] **Step 5: Commit Earth contract**

```bash
git add tests/earth_fbx_contract.cpp
git commit -m "tests: cover Earth through self-owned FBX pipeline"
```

- [ ] **Step 6: User GAME verification**

After pushing/committing the library changes, the user runs:
```bash
cd ~/Pro/GAME
c update ecs-model-rasterizer
c build
c run
```
Success requires Earth to reach the renderer without an FBX/image decoder failure. Visual/path-tracer defects are debugged separately unless evidence identifies imported data as their source.

---

## Self-Review

- Spec coverage: compression, PNG, baseline/progressive JPEG, TGA dispatch, binary/ASCII FBX, semantic import, embedded media, dependency removal, safety bounds, Earth regression, build/runtime verification are each assigned to explicit tasks.
- Placeholder scan: no TBD/TODO/"implement later" steps remain; each task names files, APIs, failure conditions, test commands and commit boundaries.
- Type consistency: `FbxDocument::RawDocument`, `FbxDocument::Node`, `Png::decode`, `Jpeg::decode`, and `Compression::inflateZlib` are defined once and consumed under the same signatures in later tasks.
