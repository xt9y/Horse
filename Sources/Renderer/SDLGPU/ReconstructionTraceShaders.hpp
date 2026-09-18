#ifndef HORSE_RENDERER_SDLGPU_RECONSTRUCTION_TRACE_SHADERS_HPP
#define HORSE_RENDERER_SDLGPU_RECONSTRUCTION_TRACE_SHADERS_HPP

namespace Renderer::SDLGPU::ReconstructionTraceShaders {

inline constexpr const char *Entries = R"HLSL(
RWTexture2D<float4> SurfaceData : register(u3, space1);

float3 ShadeSparseRay(
    float3 origin,
    float3 direction,
    out float depth,
    out float4 surface_data)
{
    Hit hit = TraceClosest(origin, direction, 1.0e30);
    if (!hit.hit) {
        depth = 0.0;
        surface_data = 0.0.xxxx;
        return EnvironmentColor(direction);
    }

    depth = hit.t;
    Surface surface = MakeSurface(hit, direction);
    GpuMaterial material = Materials[min(surface.material, (uint)max(Counts.z - 1, 0))];
    if (material.misc.w > 0.5 && material.misc.w < 1.5 && surface.alpha < material.misc.x) {
        Hit second = TraceClosest(surface.position + direction * 0.002, direction, 1.0e30);
        if (!second.hit) {
            surface_data = 0.0.xxxx;
            return EnvironmentColor(direction);
        }
        surface = MakeSurface(second, direction);
        depth += second.t;
        material = Materials[min(surface.material, (uint)max(Counts.z - 1, 0))];
    }

    surface_data = float4(normalize(surface.normal), float(surface.material + 1u));
    if (material.misc.y > 0.5) return surface.albedo + surface.emission;

    float3 view = normalize(origin - surface.position);
    float3 indirect = SampleGI(surface.position, surface.normal) * surface.albedo *
        (1.0 - surface.metallic) * (1.0 - surface.transmission) * surface.ao;
    indirect += GI[6].xyz * GI[6].w * surface.albedo *
        (1.0 - surface.metallic) * (1.0 - surface.transmission) * surface.ao;
    float3 diffuse_transmission = EnvironmentColor(-surface.normal) * surface.albedo *
        surface.diffuse_transmission_color * surface.diffuse_transmission *
        (1.0 - surface.metallic);
    return max(
        DirectLight(surface, view) + indirect + EnvironmentSpecular(surface, view) +
        TransmissionEnvironment(surface, view) + diffuse_transmission + surface.emission,
        0.0.xxx);
}

bool SparseScheduled(uint2 pixel)
{
    uint grid = max(PathPolicy.x, 1u);
    uint count = grid * grid;
    uint phase = Frame.x % max(count, 1u);
    return (pixel.x % grid) + (pixel.y % grid) * grid == phase;
}

[numthreads(8,8,1)]
void SparseRayMain(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= (uint)Resolution.x || tid.y >= (uint)Resolution.y) return;
    if (!SparseScheduled(tid.xy)) return;

    float3 origin, direction;
    CameraRay(float2(tid.xy), 0.5.xx, origin, direction);
    float depth;
    float4 surface_data;
    float3 color = ShadeSparseRay(origin, direction, depth, surface_data);
    Output[tid.xy] = float4(color, 1.0);
    LinearDepth[tid.xy] = depth;
    Accumulation[tid.xy] = surface_data;
}

[numthreads(8,8,1)]
void SparsePathMain(uint3 tid : SV_DispatchThreadID)
{
    uint width = (uint)Resolution.x;
    uint height = (uint)Resolution.y;
    if (tid.x >= width || tid.y >= height) return;
    if (!SparseScheduled(tid.xy)) return;

    bool reset = (Frame.w & 1u) != 0u;
    if (reset) Accumulation[tid.xy] = 0.0.xxxx;

    uint rng = Hash(tid.x + tid.y * width + Frame.x * 747796405u + 1u);
    float3 sum = 0.0.xxx;
    float first_depth = 0.0;
    float4 first_surface = 0.0.xxxx;
    uint spp = max(Frame.z, 1u);

    for (uint sample = 0u; sample < spp; ++sample) {
        float2 jitter = float2(Random(rng), Random(rng));
        float3 origin, direction;
        CameraRay(float2(tid.xy), jitter, origin, direction);
        float3 throughput = 1.0.xxx;
        float3 radiance = 0.0.xxx;
        bool captured_first_surface = false;

        for (uint bounce = 0u; bounce < 4u; ++bounce) {
            Hit hit = TraceClosest(origin, direction, 1.0e30);
            if (!hit.hit) {
                radiance += throughput * EnvironmentColor(direction);
                break;
            }
            if (bounce == 0u) first_depth = hit.t;
            Surface surface = MakeSurface(hit, direction);
            GpuMaterial material = Materials[min(surface.material, (uint)max(Counts.z - 1, 0))];
            if (material.misc.w > 0.5 && material.misc.w < 1.5 && surface.alpha < material.misc.x) {
                origin = surface.position + direction * 0.002;
                continue;
            }
            if (!captured_first_surface) {
                first_surface = float4(normalize(surface.normal), float(surface.material + 1u));
                captured_first_surface = true;
            }
            radiance += throughput * surface.emission;
            if (material.misc.y > 0.5) {
                radiance += throughput * surface.albedo;
                break;
            }

            float3 view = -direction;
            radiance += throughput * DirectLight(surface, view);
            radiance += throughput * SampleGI(surface.position, surface.normal) * surface.albedo *
                0.2 * (1.0 - surface.metallic) * (1.0 - surface.transmission);

            float transmission_probability = surface.transmission * (1.0 - surface.metallic);
            float diffuse_transmission_probability = surface.diffuse_transmission *
                (1.0 - surface.metallic) * (1.0 - transmission_probability);
            float3 f0 = SurfaceF0(surface);
            float specular_probability = saturate(Max3(f0) + (1.0 - surface.roughness) * 0.2);
            float choice = Random(rng);

            if (choice < transmission_probability) {
                float eta = dot(direction, surface.normal) < 0.0 ? 1.0 / surface.ior : surface.ior;
                float3 refracted = refract(direction, surface.normal, eta);
                if (dot(refracted, refracted) < 1.0e-6)
                    refracted = reflect(direction, surface.normal);
                direction = normalize(refracted);
                throughput *= surface.albedo * VolumeAttenuation(surface) /
                    max(transmission_probability, 0.05);
                origin = surface.position + direction * 0.002;
            } else if (choice < transmission_probability + diffuse_transmission_probability) {
                direction = CosineHemisphere(-surface.normal, rng);
                throughput *= surface.albedo * surface.diffuse_transmission_color /
                    max(diffuse_transmission_probability, 0.05);
                origin = surface.position - surface.normal * 0.002;
            } else {
                float remaining = max(
                    1.0 - transmission_probability - diffuse_transmission_probability,
                    0.05);
                if (Random(rng) < specular_probability) {
                    float3 incident = direction;
                    float3 reflected = reflect(incident, surface.normal);
                    direction = normalize(lerp(
                        reflected,
                        CosineHemisphere(surface.normal, rng),
                        surface.roughness * surface.roughness));
                    float3 fresnel = Fresnel(f0, saturate(dot(-incident, surface.normal)));
                    throughput *= fresnel / max(specular_probability * remaining, 0.05);
                } else {
                    direction = CosineHemisphere(surface.normal, rng);
                    throughput *= surface.albedo * (1.0 - surface.metallic) /
                        max((1.0 - specular_probability) * remaining, 0.05);
                }
                origin = surface.position + surface.normal * 0.002;
            }

            if (bounce >= 2u) {
                float survive = max(saturate(Max3(throughput)), 0.1);
                if (Random(rng) > survive) break;
                throughput /= survive;
            }
        }
        sum += radiance;
    }

    float4 previous = reset ? 0.0.xxxx : Accumulation[tid.xy];
    float4 accumulated = previous + float4(sum, spp);
    Accumulation[tid.xy] = accumulated;
    Output[tid.xy] = float4(accumulated.rgb / max(accumulated.a, 1.0), 1.0);
    LinearDepth[tid.xy] = first_depth;
    SurfaceData[tid.xy] = first_surface;
}
)HLSL";

} // namespace Renderer::SDLGPU::ReconstructionTraceShaders

#endif
