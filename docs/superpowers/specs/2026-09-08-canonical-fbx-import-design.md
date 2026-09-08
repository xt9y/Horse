# Canonical FBX Import Design

## Goal

Make Horse import supported FBX files into a single canonical Horse representation with correct scene semantics, independent of any one asset, renderer, operating system, or external FBX SDK.

The importer must follow this rule:

> A supported FBX is imported correctly, or loading fails explicitly with a useful error. Horse must never silently approximate a semantic that can materially change geometry, transforms, skinning, animation, materials, or texture assignment.

The uploaded `Earth.fbx` remains a regression target, but no production code may contain Earth-specific names, paths, dimensions, material rules, texture guesses, camera assumptions, or scale overrides.

## Constraints

- Keep Horse independent: no Autodesk FBX SDK, `ufbx`, Assimp, or other installed FBX dependency.
- Preserve the existing public model API: `Models::load`, `Models::mesh`, `Models::material`, `Models::skeleton`, `Models::animation`, and existing handles remain unchanged.
- Preserve binary and ASCII FBX support through Horse-owned readers.
- Keep FBX-specific semantics inside the FBX importer. Renderers, ECS, animation consumers, and GAME must receive canonical Horse data only.
- Do not introduce renderer-side FBX compensation.
- Do not invent data absent from the source file. Missing textures remain missing; missing opacity does not become atmosphere-specific transparency.
- Unsupported semantics that affect correctness are fatal import errors, not warnings followed by approximation.

## Canonical Horse Space

FBX scene data is converted exactly once during import into Horse's canonical coordinate system and unit system.

The conversion is derived from FBX `GlobalSettings`, including:

- `UnitScaleFactor`
- `OriginalUnitScaleFactor` where relevant
- `UpAxis` and `UpAxisSign`
- `FrontAxis` and `FrontAxisSign`
- `CoordAxis` and `CoordAxisSign`

All geometry, normals, node transforms, bind matrices, skeleton transforms, animation translations, and animation rotations must use the same conversion basis.

The canonical conversion must be represented by an explicit basis/unit transform instead of scattered sign swaps or hard-coded axis assumptions.

If the file declares an invalid or unsupported axis basis, loading fails with the offending GlobalSettings values in the error.

## FBX Transform Evaluation

Horse must evaluate the FBX model transform stack rather than reducing models to only `Lcl Translation`, `Lcl Rotation`, and `Lcl Scaling`.

Supported model properties must include:

- `Lcl Translation`
- `Lcl Rotation`
- `Lcl Scaling`
- `RotationOrder`
- `PreRotation`
- `PostRotation`
- `RotationOffset`
- `RotationPivot`
- `ScalingOffset`
- `ScalingPivot`
- `GeometricTranslation`
- `GeometricRotation`
- `GeometricScaling`
- parent model hierarchy
- `InheritType`

The evaluator must distinguish node transforms from geometric transforms. Geometric transforms affect attached geometry but do not become ordinary inherited child transforms.

Rotation order must be evaluated according to the FBX property rather than assuming one Euler order.

Post-rotation must use FBX's inverse/post-rotation semantics rather than simple component subtraction.

Parent inheritance must follow the declared FBX inheritance mode. If a mode cannot be implemented correctly, import fails explicitly.

## Geometry Conversion

For each mesh geometry:

1. Read control points and polygon vertex indices.
2. Preserve polygon boundaries from FBX negative end indices.
3. Resolve layer elements using their mapping and reference modes.
4. Triangulate polygons deterministically.
5. Apply the evaluated model and geometric transforms to unskinned geometry.
6. Convert into Horse's canonical axis/unit basis.
7. Transform normals with the inverse-transpose of the final linear transform and renormalize.
8. Detect negative determinant transforms and correct triangle winding/tangent orientation where required.
9. Compute final bounds from converted vertices.

Triangulation must support arbitrary simple FBX polygons, not only triangles/quads. A fan may only be used when it is geometrically valid; otherwise use a deterministic polygon triangulation strategy. Degenerate polygons may be skipped only with explicit validation and diagnostics.

## Layer Elements

The importer must correctly support the standard FBX mapping/reference combinations needed for mesh data:

Mapping modes:

- `ByPolygonVertex`
- `ByVertice` / `ByVertex`
- `ByPolygon`
- `AllSame`

Reference modes:

- `Direct`
- `IndexToDirect`
- `Index`

This applies to at least:

- normals
- UVs
- material indices

Out-of-range or malformed layer references must not silently resolve to index zero. They either use a documented safe fallback only when the FBX field is optional, or fail import when the semantic is required for correctness.

## Materials and Textures

Material assignment is resolved per polygon through `LayerElementMaterial` and model-material connections.

Texture resolution must handle valid FBX object graphs rather than assuming only one connection direction. Horse must resolve Material, Texture, and Video objects through both incoming and outgoing connection tables where allowed by FBX exporters.

For texture channels, diffuse/base-color lookup is preferred for the current Horse material model. The importer may ignore unsupported channels only when doing so does not alter geometry/transform correctness; unsupported material features should be surfaced through diagnostics so they are not mistaken for fully preserved material fidelity.

Texture source priority:

1. embedded content on the Texture object
2. embedded content on connected Video objects
3. absolute referenced filename
4. source-relative referenced filename
5. source directory plus basename
6. conventional `Textures/` and `textures/` sibling fallback

Filename properties checked include:

- `RelativeFilename`
- `Filename`
- `FileName`

Backslashes are normalized before path resolution.

Horse continues to use its own image/DEFLATE pipeline. No external image library is introduced.

If a material has no texture reference or embedded texture data, the importer preserves the material as textureless. It never guesses a texture based on model/material names.

## Skinning and Bind Poses

Skin clusters, skeleton hierarchy, and mesh bind palettes must be transformed through the same canonical basis/unit conversion as static geometry.

The importer must correctly use cluster matrices such as:

- `Transform`
- `TransformLink`
- associate-model transforms where present and supported

Per-mesh bind palettes remain per mesh; they must not be collapsed into one skeleton-global matrix when multiple meshes bind differently.

Skin weights are normalized after selecting the supported maximum influences per vertex. Invalid joint/control-point indices are import errors when they would corrupt the mesh.

Negative scale and coordinate-system conversion must not invert skinning unexpectedly.

## Animation

Animation tracks must evaluate FBX transforms using the same transform evaluator as the bind/static model state.

This includes:

- translation curves
- rotation curves with the node's FBX rotation order
- scaling curves
- pre/post rotations
- pivots/offsets where they affect animated transforms
- unit conversion for translation
- axis-basis conversion for translation and rotation
- correct parent hierarchy composition

Animations are sampled into Horse's existing `Animation::AnimationClip` representation, so no FBX-specific transform semantics leak past import.

If an animation uses a transform feature Horse cannot evaluate correctly, that animation/file fails import explicitly rather than being sampled with a simplified transform model.

## Error Model

FBX loading returns detailed errors through the existing error string.

Errors must identify the category and, where possible, the relevant object/model/property, for example:

- unsupported FBX rotation order on Model `Arm`
- invalid axis basis in GlobalSettings
- malformed UV `IndexToDirect` reference in Geometry `Body`
- unsupported transform inheritance mode on Model `Wheel`
- invalid skin cluster control-point index in Deformer `Cluster01`

The importer may emit non-fatal diagnostics only for data that can be omitted without changing the correctness of the supported representation.

## Internal Structure

The FBX importer should be separated conceptually into these internal units, without changing the public API:

1. **Raw document readers** — binary/ASCII parsing into FBX nodes/properties.
2. **Scene graph indexing** — objects and connections.
3. **Global conversion** — unit and axis-system canonicalization.
4. **Transform evaluator** — complete FBX model/geometric transform evaluation.
5. **Layer resolver** — mapping/reference lookup for normals, UVs, materials.
6. **Geometry converter** — triangulation, transformed vertices, winding, bounds.
7. **Material/texture resolver** — material graph and embedded/external images.
8. **Skeleton/skin converter** — hierarchy, weights, bind matrices.
9. **Animation converter** — curves through the same transform evaluator.

These can remain in the current file layout initially if moving files would create unrelated churn, but their responsibilities must be testable independently.

## Regression Strategy

Tests must cover both synthetic minimal FBX documents and real files.

Synthetic regressions must exercise at least:

- centimeters vs meters through `UnitScaleFactor`
- Y-up and Z-up axis systems
- handedness/coordinate-axis conversion
- each supported Euler rotation order
- pre/post rotation
- rotation pivot/offset
- scaling pivot/offset
- geometric transforms
- parent hierarchy
- each supported `InheritType`
- negative scale/winding
- normal inverse-transpose behavior
- UV mapping/reference combinations
- per-polygon materials
- Material -> Texture -> Video connections
- embedded texture data
- external relative texture paths
- skin bind matrices
- animated translation/rotation/scaling under transformed parents

The real uploaded `Earth.fbx` is a permanent regression target for:

- correct unit conversion
- correct axis/orientation conversion
- three mesh parts
- valid UV ranges
- finite normals/positions
- consistent bounds
- absence of invented textures

The Earth regression must explicitly assert that this file contains no resolvable texture and that Horse does not fabricate one.

## Success Criteria

The work is complete when:

- supported FBX transforms no longer rely on simplified TRS assumptions;
- FBX global units and axes are converted consistently across static meshes, skinning, and animation;
- standard layer-element mapping/reference modes are validated and resolved correctly;
- material/texture connections work generically without asset-specific code;
- unsupported correctness-critical semantics produce explicit import failures;
- Earth imports with correct scale/orientation/parts/UVs and no invented textures;
- existing Horse model APIs remain unchanged;
- macOS Metal and Linux OpenGL renderer builds remain unaffected by the importer changes;
- all new FBX regression tests and existing CI pass.

## Explicit Non-Goals

- Reconstructing textures absent from the FBX.
- Adding Earth-specific atmosphere/cloud rendering.
- Replacing Horse's model API with an FBX scene graph.
- Adding Autodesk SDK, Assimp, ufbx, stb_image, or another external parser/image dependency.
- Guaranteeing support for every proprietary or malformed FBX ever produced. The guarantee is: supported semantics import correctly; unsupported correctness-critical semantics fail clearly instead of rendering incorrectly.
