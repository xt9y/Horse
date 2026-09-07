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
Static meshes build local-space BLAS data once and reuse it across frames. Instances carry object/world transforms and are collected into a TLAS. Static BLAS construction is not repeated because the camera moved. Skinned meshes cache dynamic BLAS data keyed by mesh handle + entity + pose revision and update when their pose changes. Flattened GPU scene buffers refresh only when the scene signature changes; a camera-only change resets accumulation without rebuilding scene geometry.

## Progressive accumulation
The path tracer renders a small number of stochastic samples per pixel each frame into an accumulation texture. Accumulation persists while camera, viewport, point light, materials, transforms, geometry, and animation pose are unchanged. Any relevant change resets the sample count. Presentation divides accumulated radiance by sample count, then applies exposure, Reinhard tone mapping, and gamma conversion.

## Materials/textures
Triangles store positions, normals, and UVs. Instance records select a material. Material GPU records store base color and an optional diffuse texture slot. Existing model texture assets are uploaded and cached. The initial GL 4.3 path binds up to 16 diffuse textures, the guaranteed-safe compute-stage budget; additional materials fall back to base color.

## Safety/performance
OpenGL 4.3 compute/SSBO support is required. Defaults are half-resolution tracing (`resolution_divisor = 2`), 1 sample/pixel/frame, and 3 bounces, with full-window filtered presentation. Traversal uses bounded 48-entry stacks and 2048-step guards. No scene-geometry rebuild happens because the camera moved.

## Preserved subsystems
Generic ECS, Camera, Models cleanup, OBJ, FBX/ufbx, PNG/JPG/TGA loading, Animation, and GAME asset setup remain. Workflow files are not touched.
