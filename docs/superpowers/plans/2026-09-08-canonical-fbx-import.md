# Canonical FBX Import Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Horse convert supported FBX files into canonical Horse mesh/material/skeleton/animation data with correct FBX units, axes, transforms, layer mappings, material graphs, skinning, and animation semantics, while failing explicitly for correctness-critical unsupported semantics.

**Architecture:** Keep `Models::Fbx::load()` and all public Horse model APIs unchanged. Split the current monolithic semantic importer into focused internal helpers for scene indexing/global conversion, transform evaluation, geometry/layer resolution, material/texture resolution, skinning, and animation; all of them consume the existing Horse-owned `FbxDocument::Document` and produce canonical Horse types. The uploaded Earth FBX is a regression input only; production code contains no Earth-specific names, paths, scales, material rules, or texture guesses.

**Tech Stack:** C++20, Horse-owned binary/ASCII FBX readers, `Animation::Mat4`/`Transform`, Horse image/texture pipeline, c-buildsystem, GitHub Actions macOS/Linux verification.

**Spec:** `docs/superpowers/specs/2026-09-08-canonical-fbx-import-design.md`

## Global Constraints

- No Autodesk FBX SDK, Assimp, ufbx, stb_image, or other external FBX/image dependency.
- Preserve `Models::load`, `Models::mesh`, `Models::material`, `Models::skeleton`, `Models::animation`, existing handles, and `Models::Fbx::load`.
- Preserve Horse-owned binary and ASCII FBX readers.
- FBX-specific semantics stay inside the FBX importer.
- Supported semantics import correctly; unsupported correctness-critical semantics fail with a useful error.
- No Earth-specific behavior in production code.
- Never fabricate textures or material properties absent from the source FBX.
- Keep macOS Metal and Linux OpenGL renderer behavior unchanged.

---

### Task 1: Lock Canonical Import Regressions

**Files:**
- Create: `tests/fbx_canonical_contract.cpp`
- Modify: `tests/earth_fbx_contract.cpp`
- Modify: `.github/workflows/ci.yml`

**Interfaces:**
- Consumes: `Models::Fbx::load(const std::string&, Document*, std::string*)`.
- Produces: executable contracts for unit/axis conversion, transform stack behavior, layer mapping, mirrored winding, and Earth invariants.

- [ ] **Step 1: Add an ASCII-FBX fixture writer inside the canonical test**

Use one self-contained C++ test that writes minimal ASCII FBX files into `/tmp` so no binary fixtures are needed:

```cpp
static std::string writeFixture(const char *name, const std::string& body)
{
    const std::string path = std::string("/tmp/") + name + ".fbx";
    std::ofstream file(path, std::ios::binary);
    file << "; FBX 7.4.0 project file\n" << body;
    file.close();
    return path;
}
```

The first fixture must declare `UnitScaleFactor: 100.0`, a non-default axis basis, one triangle, a parent Model transform, and UV/normal layer data.

- [ ] **Step 2: Assert canonical output rather than parser internals**

For each fixture call `Models::Fbx::load()` and assert final Horse values, for example:

```cpp
Models::Fbx::Document document;
std::string error;
assert(Models::Fbx::load(path, &document, &error));
assert(error.empty());
assert(document.parts.size() == 1u);
const Models::MeshData& mesh = document.parts[0].mesh;
assert(mesh.vertices.size() == 3u);
assert(std::abs(mesh.vertices[0].position.x - expected_x) < 1.0e-4f);
```

Add fixtures for centimeters/meters, Y-up/Z-up, mirrored basis, rotation orders, pre/post rotation, pivot/offset, geometric transform, hierarchy, `InheritType`, negative scale, UV `IndexToDirect`, and per-polygon materials.

- [ ] **Step 3: Strengthen the Earth contract**

Keep the existing finite geometry checks and add invariant assertions derived from the real file:

```cpp
assert(document.parts.size() == 3u);
bool any_texture = false;
for (const auto& part : document.parts) {
    assert(!part.mesh.vertices.empty());
    assert(part.mesh.indices.size() % 3u == 0u);
    any_texture |= part.material.diffuse_texture != Models::INVALID_TEXTURE;
}
assert(!any_texture);
```

Also assert the final canonical bounds are in sane engine units instead of approximately 100x FBX centimeters.

- [ ] **Step 4: Wire the tests into CI**

Compile `tests/fbx_canonical_contract.cpp` and `tests/earth_fbx_contract.cpp` against Horse on both Linux and macOS after the shared library build. The Earth contract may download/use `xt9y/Earth/Earth.fbx` explicitly in CI; production code must not.

- [ ] **Step 5: Verify RED**

Run the new contracts on current `main`. Expected: unit/axis/transform fixtures fail because the importer currently reduces transforms to simplified TRS and ignores GlobalSettings.

- [ ] **Step 6: Commit regression only**

```bash
git add tests/fbx_canonical_contract.cpp tests/earth_fbx_contract.cpp .github/workflows/ci.yml
git commit -m "test: lock canonical FBX import semantics"
```

---

### Task 2: Scene Index and Global Axis/Unit Conversion

**Files:**
- Create: `Sources/Models/Formats/FbxScene.hpp`
- Create: `Sources/Models/Formats/FbxScene.cpp`
- Modify: `Sources/Models/Formats/Fbx.cpp`
- Test: `tests/fbx_canonical_contract.cpp`

**Interfaces:**
- Consumes: `FbxDocument::Document`.
- Produces:

```cpp
namespace Models::FbxInternal {
using ObjectId = std::int64_t;

struct Object {
    ObjectId id = 0;
    std::string kind;
    std::string name;
    std::string subtype;
    const FbxDocument::Node *node = nullptr;
};

struct Connection {
    std::string type;
    ObjectId source = 0;
    ObjectId destination = 0;
    std::string property;
};

struct CanonicalBasis {
    Animation::Mat4 fbx_to_horse{};
    Animation::Mat4 horse_to_fbx{};
    float unit_scale = 1.0f;
    bool mirrored = false;
};

struct Scene {
    const FbxDocument::Document *document = nullptr;
    std::unordered_map<ObjectId, Object> objects;
    std::vector<ObjectId> object_order;
    std::vector<Connection> connections;
    std::unordered_map<ObjectId, std::vector<std::size_t>> incoming;
    std::unordered_map<ObjectId, std::vector<std::size_t>> outgoing;
    CanonicalBasis basis;
};

bool buildScene(const FbxDocument::Document&, Scene*, std::string* error);
const Object *object(const Scene&, ObjectId);
ObjectId parentModel(const Scene&, ObjectId);
}
```

- [ ] **Step 1: Move scene/object/connection indexing out of `Fbx.cpp` without semantic change**

Copy the current object/connection behavior into `FbxScene.cpp`, then update `Fbx.cpp` to consume `FbxInternal::Scene`.

- [ ] **Step 2: Parse `GlobalSettings/Properties70`**

Read `UnitScaleFactor`, `UpAxis`, `UpAxisSign`, `FrontAxis`, `FrontAxisSign`, `CoordAxis`, and `CoordAxisSign` using strict integer/scalar helpers. Treat absent settings as the FBX defaults; reject duplicate axes, zero signs, and non-finite/non-positive unit scale.

- [ ] **Step 3: Build one canonical basis matrix**

Construct source basis vectors from the declared axis indices/signs, validate orthogonality/handedness, and build `fbx_to_horse` so all later conversions use one matrix. Convert centimeters to Horse meters with:

```cpp
basis.unit_scale = static_cast<float>(unit_scale_factor / 100.0);
```

Apply this scale to translations/positions, not to pure directions/normals.

- [ ] **Step 4: Add conversion helpers**

Expose internal helpers:

```cpp
Animation::Vec3 canonicalPoint(const CanonicalBasis&, Animation::Vec3);
Animation::Vec3 canonicalVector(const CanonicalBasis&, Animation::Vec3);
Animation::Mat4 canonicalMatrix(const CanonicalBasis&, const Animation::Mat4&);
```

Matrix conversion must be `C * M * C^-1`, with translation units handled consistently.

- [ ] **Step 5: Verify unit/axis fixtures GREEN**

Run `fbx_canonical_contract`; only transform-stack fixtures should remain failing after this task.

- [ ] **Step 6: Commit**

```bash
git add Sources/Models/Formats/FbxScene.hpp Sources/Models/Formats/FbxScene.cpp Sources/Models/Formats/Fbx.cpp tests/fbx_canonical_contract.cpp
git commit -m "fbx: canonicalize global units and axes"
```

---

### Task 3: Complete FBX Transform Evaluator

**Files:**
- Create: `Sources/Models/Formats/FbxTransform.hpp`
- Create: `Sources/Models/Formats/FbxTransform.cpp`
- Modify: `Sources/Models/Formats/Fbx.cpp`
- Test: `tests/fbx_canonical_contract.cpp`

**Interfaces:**
- Consumes: `FbxInternal::Scene`, `FbxInternal::Object`.
- Produces:

```cpp
namespace Models::FbxInternal {
enum class RotationOrder { XYZ, XZY, YZX, YXZ, ZXY, ZYX };

struct TransformProperties {
    Animation::Vec3 translation{};
    Animation::Vec3 rotation_degrees{};
    Animation::Vec3 scale{1.0f, 1.0f, 1.0f};
    Animation::Vec3 pre_rotation{};
    Animation::Vec3 post_rotation{};
    Animation::Vec3 rotation_offset{};
    Animation::Vec3 rotation_pivot{};
    Animation::Vec3 scaling_offset{};
    Animation::Vec3 scaling_pivot{};
    Animation::Vec3 geometric_translation{};
    Animation::Vec3 geometric_rotation{};
    Animation::Vec3 geometric_scale{1.0f, 1.0f, 1.0f};
    RotationOrder rotation_order = RotationOrder::XYZ;
    int inherit_type = 0;
};

bool readTransformProperties(const Object&, TransformProperties*, std::string* error);
bool localModelMatrix(const TransformProperties&, Animation::Mat4*, std::string* error);
bool geometricMatrix(const TransformProperties&, Animation::Mat4*, std::string* error);
bool globalModelMatrix(const Scene&, ObjectId, Animation::Mat4*, std::string* error);
}
```

- [ ] **Step 1: Add general matrix primitives locally**

Implement translation, scale, axis-angle rotation, inverse, determinant, and Euler composition using `Animation::Mat4` column-major storage. Keep these helpers private to `FbxTransform.cpp` unless another FBX unit needs them.

- [ ] **Step 2: Implement all six Euler rotation orders**

Map FBX `RotationOrder` values 0..5 to `XYZ`, `XZY`, `YZX`, `YXZ`, `ZXY`, `ZYX`. Reject unsupported spherical rotation order (`6`) with a model/property-specific error.

- [ ] **Step 3: Implement FBX local transform stack**

Compose the documented stack explicitly rather than adding/subtracting Euler components:

```text
T * Roff * Rp * PreR * R * inverse(PostR) * inverse(Rp)
  * Soff * Sp * S * inverse(Sp)
```

Use matrix inversion for PostR/pivots and report singular cases.

- [ ] **Step 4: Implement geometric transform separately**

Compose `GeometricTranslation * GeometricRotation * GeometricScaling`; do not inherit it into child Model transforms.

- [ ] **Step 5: Implement parent inheritance modes**

Support FBX `InheritType` values 0 (`RrSs`), 1 (`RSrs`), and 2 (`Rrs`) with explicit parent/local decomposition rules. Unknown values fail import with the Model name and numeric mode.

- [ ] **Step 6: Canonicalize evaluated matrices once**

Return model/geometric matrices in Horse canonical basis so geometry, skeleton, and animation all consume the same representation.

- [ ] **Step 7: Verify transform fixtures GREEN**

Run the fixtures for rotation orders, pre/post rotation, pivots/offsets, hierarchy, geometric transforms, inheritance, and negative scale.

- [ ] **Step 8: Commit**

```bash
git add Sources/Models/Formats/FbxTransform.hpp Sources/Models/Formats/FbxTransform.cpp Sources/Models/Formats/Fbx.cpp tests/fbx_canonical_contract.cpp
git commit -m "fbx: evaluate complete model transforms"
```

---

### Task 4: Strict Layer Resolution, Triangulation, Normals, and Winding

**Files:**
- Create: `Sources/Models/Formats/FbxGeometry.hpp`
- Create: `Sources/Models/Formats/FbxGeometry.cpp`
- Modify: `Sources/Models/Formats/Fbx.cpp`
- Test: `tests/fbx_canonical_contract.cpp`

**Interfaces:**
- Consumes: canonical model/geometric transforms and `FbxDocument::Node` mesh geometry.
- Produces:

```cpp
namespace Models::FbxInternal {
struct GeometryPart {
    MeshData mesh;
    ObjectId material = 0;
};

bool convertGeometry(
    const Scene&,
    ObjectId geometry_id,
    const std::vector<ObjectId>& model_materials,
    const std::vector<Animation::SkinWeights>& control_weights,
    const std::vector<Animation::Mat4>& bind_palette,
    bool skinned,
    std::vector<GeometryPart>*,
    std::string* error);
}
```

- [ ] **Step 1: Replace permissive layer lookup with validated lookup**

For `ByPolygonVertex`, `ByVertex`/`ByVertice`, `ByPolygon`, and `AllSame`, calculate the mapped index first, then apply `Direct`, `IndexToDirect`, or `Index`. Any negative/out-of-range required normal/UV/material index returns an error naming geometry/layer/index.

- [ ] **Step 2: Implement deterministic ear-clipping for n-gons**

Project each polygon to its dominant 2D plane, determine winding, clip convex ears, reject self-intersecting/non-simple polygons that cannot be triangulated correctly. Triangles remain unchanged; valid convex polygons may take the same deterministic path.

- [ ] **Step 3: Apply final static geometry transform**

For unskinned geometry use:

```text
final = canonical_model_global * canonical_geometric
```

Apply `final` to positions. Apply inverse-transpose of `final`'s linear 3x3 to normals, then normalize.

- [ ] **Step 4: Correct mirrored winding**

If the final linear determinant is negative, swap triangle indices 1 and 2 (and corresponding per-corner data) after triangulation so front-face orientation remains canonical.

- [ ] **Step 5: Preserve per-polygon material assignment**

Create one `GeometryPart` per resolved material slot and reject material slots outside the model's material connection list when the file explicitly references them.

- [ ] **Step 6: Verify geometry/layer fixtures GREEN**

Run UV `IndexToDirect`, per-polygon materials, negative scale, normal inverse-transpose, and n-gon fixtures.

- [ ] **Step 7: Commit**

```bash
git add Sources/Models/Formats/FbxGeometry.hpp Sources/Models/Formats/FbxGeometry.cpp Sources/Models/Formats/Fbx.cpp tests/fbx_canonical_contract.cpp
git commit -m "fbx: convert geometry with strict layer semantics"
```

---

### Task 5: Generic Material and Texture Object Graph Resolution

**Files:**
- Create: `Sources/Models/Formats/FbxMaterial.hpp`
- Create: `Sources/Models/Formats/FbxMaterial.cpp`
- Modify: `Sources/Models/Formats/Fbx.cpp`
- Test: `tests/fbx_canonical_contract.cpp`

**Interfaces:**
- Consumes: `FbxInternal::Scene`, source FBX path, material object ID.
- Produces:

```cpp
namespace Models::FbxInternal {
bool convertMaterial(
    const Scene&,
    ObjectId material_id,
    const std::filesystem::path& source_path,
    MaterialData*,
    std::string* error);
}
```

- [ ] **Step 1: Resolve connected objects in both graph directions**

Create a de-duplicating helper that inspects `incoming` and `outgoing` connections and returns connected `Texture`/`Video` objects. Prefer connections whose property names indicate `DiffuseColor`, `BaseColor`, or equivalent diffuse channel.

- [ ] **Step 2: Resolve embedded bytes generically**

Check `Content` on Texture first, then connected Video objects via `Property::asBytes()`/`FbxDocument::Bytes`.

- [ ] **Step 3: Resolve external path candidates in deterministic order**

Read `RelativeFilename`, `Filename`, and `FileName` from Texture and connected Video objects. Normalize `\\` to `/`; try absolute, source-relative, source-dir basename, `Textures/`, and `textures/` candidates without duplicates.

- [ ] **Step 4: Preserve missing-texture semantics**

If there is no texture object/reference/content, return a valid textureless `MaterialData`. If a referenced image exists but Horse cannot decode it, return an import error including the candidate path and decode error instead of silently making it white.

- [ ] **Step 5: Verify embedded/external graph fixtures GREEN**

Use a tiny valid TGA/PNG generated by the test and an ASCII FBX Material->Texture->Video graph to prove both embedded and source-relative resolution.

- [ ] **Step 6: Commit**

```bash
git add Sources/Models/Formats/FbxMaterial.hpp Sources/Models/Formats/FbxMaterial.cpp Sources/Models/Formats/Fbx.cpp tests/fbx_canonical_contract.cpp
git commit -m "fbx: resolve material texture graphs generically"
```

---

### Task 6: Canonical Skeleton, Skin Clusters, and Bind Matrices

**Files:**
- Create: `Sources/Models/Formats/FbxSkin.hpp`
- Create: `Sources/Models/Formats/FbxSkin.cpp`
- Modify: `Sources/Models/Formats/Fbx.cpp`
- Test: `tests/fbx_canonical_contract.cpp`

**Interfaces:**
- Consumes: canonical transform evaluator, Skin/Cluster objects, control-point count.
- Produces:

```cpp
namespace Models::FbxInternal {
struct SkeletonBuild {
    std::vector<ObjectId> models;
    std::unordered_map<ObjectId, std::uint16_t> indices;
};

SkeletonBuild collectSkeleton(const Scene&);
bool makeSkeleton(const Scene&, const SkeletonBuild&, const std::string& name,
                  Animation::Skeleton*, std::string* error);
bool buildControlWeights(const Scene&, ObjectId skin, const SkeletonBuild&,
                         std::size_t control_count,
                         std::vector<Animation::SkinWeights>*, std::string* error);
bool buildMeshBindPalette(const Scene&, ObjectId skin, const SkeletonBuild&,
                          std::vector<Animation::Mat4>*, std::string* error);
}
```

- [ ] **Step 1: Validate cluster control-point/joint indices**

Negative or out-of-range control points/joints are fatal. Keep the strongest four influences, normalize them, and preserve zero-weight unskinned controls as zero weights.

- [ ] **Step 2: Canonicalize `Transform` and `TransformLink` matrices**

Parse FBX row-major arrays, convert to Horse matrices, apply the same global basis/unit conversion, and calculate each mesh-specific palette as the correct relative bind transform.

- [ ] **Step 3: Handle associate model explicitly**

If a cluster declares an associate model, either evaluate it correctly using the same transform evaluator or fail with `unsupported associate-model bind mode` rather than ignoring it.

- [ ] **Step 4: Build skeleton bind locals from canonical global matrices**

Derive each bone local from `inverse(parent_global) * bone_global`, decompose into Horse `Animation::Transform`, and fail if the matrix contains shear that cannot be represented by Horse's TRS animation type.

- [ ] **Step 5: Verify skin fixture GREEN**

Create a two-bone fixture with non-default axis/unit conversion and compare CPU-skinned positions/normals against expected canonical coordinates.

- [ ] **Step 6: Commit**

```bash
git add Sources/Models/Formats/FbxSkin.hpp Sources/Models/Formats/FbxSkin.cpp Sources/Models/Formats/Fbx.cpp tests/fbx_canonical_contract.cpp
git commit -m "fbx: canonicalize skinning and bind poses"
```

---

### Task 7: Animation Through the Same Transform Evaluator

**Files:**
- Create: `Sources/Models/Formats/FbxAnimation.hpp`
- Create: `Sources/Models/Formats/FbxAnimation.cpp`
- Modify: `Sources/Models/Formats/Fbx.cpp`
- Test: `tests/fbx_canonical_contract.cpp`

**Interfaces:**
- Consumes: animation curve objects, `TransformProperties`, canonical parent hierarchy.
- Produces:

```cpp
namespace Models::FbxInternal {
bool makeAnimations(const Scene&, const SkeletonBuild&,
                    std::vector<Animation::AnimationClip>*,
                    std::string* error);
}
```

- [ ] **Step 1: Keep current curve interpolation but sample properties, not simplified final TRS**

At each sample time, start from the model's bind `TransformProperties`, replace only animated translation/rotation/scale components, then call the same transform evaluator used for static import.

- [ ] **Step 2: Convert sampled globals to canonical bone locals**

For each sampled bone calculate canonical global matrix, then `inverse(parent_global) * child_global`, and decompose to `Animation::Transform`.

- [ ] **Step 3: Reject non-TRS sampled matrices**

If pivots/inheritance produce shear that Horse's `Animation::Transform` cannot represent, return a detailed import error instead of silently dropping shear.

- [ ] **Step 4: Verify animated parent/rotation-order fixture GREEN**

Use a parent with scale/rotation and a child animated in translation and a non-XYZ Euler order. Assert sampled world-space results through Horse's animation system.

- [ ] **Step 5: Commit**

```bash
git add Sources/Models/Formats/FbxAnimation.hpp Sources/Models/Formats/FbxAnimation.cpp Sources/Models/Formats/Fbx.cpp tests/fbx_canonical_contract.cpp
git commit -m "fbx: evaluate animations through canonical transforms"
```

---

### Task 8: Earth Regression and Final Cross-Platform Verification

**Files:**
- Modify: `tests/earth_fbx_contract.cpp`
- Modify: `.github/workflows/ci.yml`
- Modify only if needed for orchestration: `Sources/Models/Formats/Fbx.cpp`

**Interfaces:**
- Consumes: completed canonical FBX importer.
- Produces: final proof that the real Earth asset and synthetic semantic suite import correctly without renderer-specific compensation.

- [ ] **Step 1: Run the exact Earth FBX contract**

Expected invariants:

```text
3 imported parts
finite canonical positions/normals
valid UVs
canonical unit-scale bounds
no fabricated diffuse texture
no Earth-specific importer branch
```

- [ ] **Step 2: Run all synthetic FBX contracts**

Every supported semantic fixture must pass; intentionally unsupported/malformed fixtures must fail and assert a non-empty category/object-specific error string.

- [ ] **Step 3: Build Horse on macOS and Linux**

Run the existing CI equivalents:

```bash
c clean || true
c build
```

macOS must compile the Metal PathTracer backend; Linux must compile the OpenGL PathTracer backend.

- [ ] **Step 4: Build GAME Earth and Sponza against current Horse**

From GAME:

```bash
c update ecs-model-rasterizer
c build earth
c build sponza
```

Both must link without any renderer or example-side FBX compensation.

- [ ] **Step 5: Source audit for forbidden asset-specific behavior**

Run:

```bash
grep -R -n -E 'Earth|Atmosphere|Clouds|Earth\.fbx' Sources/Models/Formats
```

Expected: no matches in production importer code.

- [ ] **Step 6: Final CI verification**

Require the current Horse `main` workflow to show GREEN for renderer contract, Linux build/import tests, and macOS build/import/Metal shader contract.

- [ ] **Step 7: Commit any final regression-only adjustments**

```bash
git add tests .github/workflows/ci.yml Sources/Models/Formats/Fbx.cpp
git commit -m "test: verify canonical FBX import end to end"
```
