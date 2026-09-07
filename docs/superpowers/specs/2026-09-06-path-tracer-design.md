# Point-Light Path Tracer Design

## Goal
Replace the current raster + hybrid GI/shadow system with one clean GPU compute path tracer while preserving generic ECS, Models/FBX, textures, and animation.

## Renderer boundary
`Renderer::PathTracer` is the public renderer. `Renderer::Rasterizer` remains only as a compatibility alias in `Render.hpp`.

## Lighting
Only ECS `LightComponent{LightType::Point}` lights are used. The first point light is the active light for the initial implementation. Light position comes from `Renderer::Transform`, color from `LightComponent::color`, intensity from `LightComponent::intensity`. Direct lighting uses inverse-square attenuation and an occlusion ray to the light.

## GI
Each pixel traces a camera ray, then up to three diffuse bounces by default. Every surface hit performs next-event estimation against the point light and then samples a cosine-weighted diffuse continuation ray. There is no ambient term, shadow map, PCF, PCSS, GBuffer, screen-space trace, or raster lighting. A miss returns black so all scene illumination comes from the point light and its bounces.

## Acceleration/cache
Static meshes build local-space BLAS data once and reuse it across frames. Instances carry object/world transforms and are collected into a TLAS. Static BLAS data is rebuilt only when the source mesh changes. Skinned meshes cache a dynamic BLAS keyed by mesh handle + animator entity + pose revision and update only when the pose changes. TLAS/instance data is rebuilt only when instance membership/transforms/BLAS bindings change.

## Progressive accumulation
The path tracer renders a small number of stochastic samples per pixel each frame into an accumulation texture. Accumulation persists while camera, viewport, point light, materials, transforms, geometry, and animation pose are unchanged. Any relevant change resets the sample count. Presentation divides accumulated radiance by sample count, then applies exposure, filmic/Reinhard tone mapping, and gamma conversion.

## Materials/textures
Triangles store positions, normals, UVs, and a material index. Material GPU records store base color and an optional texture slot. Existing model texture assets are uploaded once and cached. Up to a bounded set of diffuse textures is bound for path tracing; materials beyond the runtime texture-unit budget fall back to base color.

## Safety/performance
OpenGL 4.3 compute/SSBO support is required. Defaults are 1 sample/pixel/frame and 3 bounces. Traversal uses bounded stacks/step counts. No full-scene BVH rebuild happens because the camera moved. No per-frame static-geometry upload occurs.

## Preserved subsystems
Generic ECS, Camera, Models cleanup, OBJ, FBX/ufbx, PNG/JPG/TGA loading, Animation, and GAME asset setup remain. Workflow files are not touched.
