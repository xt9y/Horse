# Model Format Layout and FBX Texture Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split FBX/OBJ implementations into dedicated folders and make real FBX diffuse/base-color textures, including Earth embedded texture data, resolve through the self-owned image pipeline.

**Architecture:** Preserve the public `Models::*` API and existing namespaces while moving source files one directory deeper. Remove the `FbxParser` compatibility alias and have the semantic importer consume `FbxDocument` directly. Extend material texture lookup across Material -> Texture -> Video connections and decode embedded `Content` before external path fallbacks.

**Tech Stack:** C++20, `std::filesystem`, self-owned FBX binary/ASCII readers, self-owned PNG/JPEG/TGA/DEFLATE decoders, c-buildsystem.

**Spec:** `docs/superpowers/specs/2026-09-07-adaptive-pathtracer-fbx-layout-textures-design.md`

## Global Constraints

- No external-installed FBX, image, or compression dependency.
- No `stb_image`, `ufbx`, or `FbxParser` module.
- Preserve `Models::load()`, model/mesh/material/texture handles, skeleton, animation, IK, ECS, and renderer public APIs.
- Keep native binary + ASCII FBX readers and native DEFLATE/zlib, PNG, JPEG, TGA decoders.
- The real `Assets/Earth/Earth.fbx` remains the regression target.
- No workflow changes.

---

### Task 1: Lock the FBX texture regression

**Files:**
- Modify: `tests/earth_fbx_contract.cpp`
- Create in GAME: `tests/earth_texture_contract.cpp`
- Modify in GAME: `build.c`

**Interfaces:**
- Consumes: `Models::Fbx::load`, `Models::load`, `Models::part`, `Models::material`, `Models::texture`.
- Produces: a regression that fails while Earth materials remain textureless.

- [ ] **Step 1: Extend the direct FBX contract**

After geometry validation, require a decoded diffuse texture in at least one FBX part:

```cpp
bool found_texture = false;
for (const auto& part : document.parts) {
    if (part.material.diffuse_texture == Models::INVALID_TEXTURE) continue;
    const Models::TextureAsset *asset = Models::texture(part.material.diffuse_texture);
    if (!asset) continue;
    if (asset->image.width <= 0 || asset->image.height <= 0 || asset->image.rgba.empty()) continue;
    found_texture = true;
}
assert(found_texture);
```

- [ ] **Step 2: Add a GAME-level Earth texture contract**

Create `GAME/tests/earth_texture_contract.cpp`:

```cpp
#include "Sources/Models/Models.hpp"
#include "Sources/Models/Core/Texture.hpp"
#include <cassert>
#include <string>

int main()
{
    std::string error;
    const Models::ModelHandle model = Models::load("Assets/Earth/Earth.fbx", &error);
    assert(model != Models::INVALID_MODEL);
    assert(error.empty());

    bool found_texture = false;
    for (std::size_t i = 0; i < Models::partCount(model); ++i) {
        const Models::ModelPart *part = Models::part(model, i);
        if (!part) continue;
        const Models::MaterialData *material = Models::material(part->material);
        if (!material || material->diffuse_texture == Models::INVALID_TEXTURE) continue;
        const Models::TextureAsset *asset = Models::texture(material->diffuse_texture);
        if (!asset) continue;
        if (asset->image.width > 0 && asset->image.height > 0 && !asset->image.rgba.empty()) {
            found_texture = true;
            break;
        }
    }
    assert(found_texture);
    Models::clearCache();
    return 0;
}
```

Register it with the same C++ runtime/platform link pattern used by the existing GAME contracts.

- [ ] **Step 3: Verify RED locally**

Run from GAME:

```bash
c update ecs-model-rasterizer
c build test earth-texture-contract
```

Expected before the production fix: assertion failure because no Earth material exposes a valid decoded diffuse texture.

- [ ] **Step 4: Commit the regression only**

```bash
git add tests/earth_fbx_contract.cpp
git commit -m "test: require decoded Earth FBX texture"
```

In GAME:

```bash
git add tests/earth_texture_contract.cpp build.c
git commit -m "test: require textured Earth model"
```

---

### Task 2: Move FBX and OBJ implementations into dedicated folders

**Files:**
- Create/move: `Sources/Models/Formats/FBX/Fbx.cpp`
- Create/move: `Sources/Models/Formats/FBX/Fbx.hpp`
- Create/move: `Sources/Models/Formats/FBX/Ascii.cpp`
- Create/move: `Sources/Models/Formats/FBX/Ascii.hpp`
- Create/move: `Sources/Models/Formats/FBX/Binary.cpp`
- Create/move: `Sources/Models/Formats/FBX/Binary.hpp`
- Create/move: `Sources/Models/Formats/FBX/Document.cpp`
- Create/move: `Sources/Models/Formats/FBX/Document.hpp`
- Create/move: `Sources/Models/Formats/FBX/Sanitize.cpp`
- Create/move: `Sources/Models/Formats/FBX/Sanitize.hpp`
- Create/move: `Sources/Models/Formats/OBJ/Obj.cpp`
- Create/move: `Sources/Models/Formats/OBJ/Obj.hpp`
- Delete: `Sources/Models/Formats/FbxParser.hpp`
- Delete old flat FBX/OBJ paths.
- Modify: `Sources/Models/Models.cpp`
- Modify: `tests/earth_fbx_contract.cpp`
- Modify: `build.c`

**Interfaces:**
- Consumes: existing namespaces `Models::Fbx`, `Models::FbxBinary`, `Models::FbxAscii`, `Models::FbxDocument`, `Models::FbxSanitize`, `Models::Obj`.
- Produces: same namespaces and public APIs at new include paths.

- [ ] **Step 1: Move files without semantic changes**

Use these include mappings:

```text
Models/Formats/Fbx.hpp          -> Models/Formats/FBX/Fbx.hpp
Models/Formats/FbxAscii.hpp     -> Models/Formats/FBX/Ascii.hpp
Models/Formats/FbxBinary.hpp    -> Models/Formats/FBX/Binary.hpp
Models/Formats/FbxDocument.hpp  -> Models/Formats/FBX/Document.hpp
Models/Formats/FbxSanitize.hpp  -> Models/Formats/FBX/Sanitize.hpp
Models/Formats/Obj.hpp          -> Models/Formats/OBJ/Obj.hpp
```

The implementation namespaces stay unchanged.

- [ ] **Step 2: Remove the parser alias**

Delete `FbxParser.hpp` and replace semantic-importer aliases in `FBX/Fbx.cpp`:

```cpp
using RawDocument = FbxDocument::Document;
using RawNode = FbxDocument::Node;
```

Replace all `FbxParser::Bytes` references with `FbxDocument::Bytes`.

- [ ] **Step 3: Update central model dispatch includes**

`Models.cpp` must include:

```cpp
#include "Models/Formats/FBX/Fbx.hpp"
#include "Models/Formats/FBX/Sanitize.hpp"
#include "Models/Formats/OBJ/Obj.hpp"
```

- [ ] **Step 4: Add the deeper source glob**

Append to the library source list:

```c
c_sources(library, "Sources/*/*/*/*.cpp");
```

Keep the existing globs unchanged.

- [ ] **Step 5: Compile-check the layout**

Run:

```bash
c build
```

Expected: the shared library builds with no missing old-format includes.

- [ ] **Step 6: Commit the layout-only change**

```bash
git add Sources/Models build.c tests/earth_fbx_contract.cpp
git commit -m "models: split FBX and OBJ format folders"
```

---

### Task 3: Resolve diffuse textures through Texture and Video connections

**Files:**
- Modify: `Sources/Models/Formats/FBX/Fbx.cpp`

**Interfaces:**
- Consumes: parsed `Scene`, `Object`, `Connection`, `FbxDocument::Bytes`, native `loadTexture` and `loadTextureMemory`.
- Produces: `MaterialData convertMaterial(...)` with valid `diffuse_texture` whenever a connected embedded or external image decodes.

- [ ] **Step 1: Add connection helpers**

Add helpers that inspect both incoming and outgoing connection tables:

```cpp
std::vector<ObjectId> connectedObjects(
    const Scene& scene,
    ObjectId id,
    const std::string& kind)
{
    std::vector<ObjectId> result;
    std::unordered_set<ObjectId> seen;
    auto collect = [&](const std::vector<std::size_t>& indexes, bool source_side) {
        for (std::size_t index : indexes) {
            const Connection& connection = scene.connections[index];
            const ObjectId candidate_id = source_side ? connection.source : connection.destination;
            const Object *candidate = object(scene, candidate_id);
            if (!candidate || candidate->kind != kind || !seen.insert(candidate_id).second) continue;
            result.push_back(candidate_id);
        }
    };
    if (const auto it = scene.incoming.find(id); it != scene.incoming.end()) collect(it->second, true);
    if (const auto it = scene.outgoing.find(id); it != scene.outgoing.end()) collect(it->second, false);
    return result;
}
```

Keep `materialTexture()` property preference for Diffuse/BaseColor but allow both connection directions.

- [ ] **Step 2: Generalize embedded-content extraction**

Add:

```cpp
const FbxDocument::Bytes *objectContent(const Object *value)
{
    if (!value) return nullptr;
    const RawNode *content = child(value->node, "Content");
    if (!content || content->properties.empty()) return nullptr;
    return std::get_if<FbxDocument::Bytes>(&content->properties[0].value);
}
```

Check Texture content first, then every connected Video object.

- [ ] **Step 3: Collect filename candidates**

For Texture and connected Video objects collect, in order:

```text
RelativeFilename
Filename
FileName
```

Normalize backslashes to slashes and de-duplicate strings without discarding order.

- [ ] **Step 4: Implement decode-first path fallback**

For each filename candidate construct ordered filesystem candidates:

```cpp
candidate_path; // only if absolute
source_path.parent_path() / candidate_path;
source_path.parent_path() / candidate_path.filename();
source_path.parent_path() / "Textures" / candidate_path.filename();
source_path.parent_path() / "textures" / candidate_path.filename();
```

For every unique path call `loadTexture()`. Continue after decode failure; stop only after a valid texture handle is returned.

- [ ] **Step 5: Preserve embedded-content priority**

For embedded data use a stable cache key:

```cpp
source_path.string() + "#" + texture->name + "#embedded"
```

Call `loadTextureMemory()` before any external candidate. Return immediately only after it yields a valid handle.

- [ ] **Step 6: Run the Earth texture contract**

From GAME:

```bash
c update ecs-model-rasterizer
c build test earth-texture-contract
```

Expected after GREEN: one test target passes and Earth exposes non-empty decoded RGBA data.

- [ ] **Step 7: Run the existing Earth runtime**

```bash
c build test earth
```

Expected: model loads, world cache remains 2304 triangles / 3 materials, and the visible globe is textured instead of uniformly white.

- [ ] **Step 8: Commit**

```bash
git add Sources/Models/Formats/FBX/Fbx.cpp tests/earth_fbx_contract.cpp
git commit -m "fbx: resolve embedded and connected textures"
```
