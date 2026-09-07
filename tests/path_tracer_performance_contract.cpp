#include "Sources/Renderer/PathTracer/PathTracer.hpp"
#include "Sources/Renderer/PathTracer/PathTracerGpu.hpp"
#include "Sources/Renderer/PathTracer/PrimaryTraceShader.hpp"
#include "Sources/Renderer/PathTracer/RestirShaders.hpp"
#include "Sources/Renderer/PathTracer/SvgfShaders.hpp"
#include "Sources/Renderer/PathTracer/WavefrontShaders.hpp"

#include <cassert>
#include <string_view>

int main()
{
    Renderer::PathTracerSettings settings;
    assert(settings.resolution_divisor == 1);

    const std::string_view common(Renderer::WavefrontShaders::common);
    const std::string_view primary(Renderer::PrimaryTraceShader::source);
    const std::string_view primary_shade(Renderer::WavefrontShaders::primary_shade);
    const std::string_view temporal(Renderer::RestirShaders::temporal_reuse);
    const std::string_view svgf(Renderer::SvgfShaders::temporal_filter);

    // Primary visibility stays native-resolution and is fused with intersection,
    // so we don't stream a full-frame primary-ray SSBO to memory and back.
    assert(primary.find("PrimaryRays") == std::string_view::npos);
    assert(primary.find("traceClosest") != std::string_view::npos);

    // The active acceleration path is TLAS -> per-instance BVH8 BLAS.
    assert(common.find("struct Instance") != std::string_view::npos);
    assert(common.find("uTlasNodeCount") != std::string_view::npos);
    assert(common.find("traceBlasClosest") != std::string_view::npos);

    // Expensive diffuse GI is 2x2 phased instead of reducing primary resolution.
    assert(primary_shade.find("pixel_phase") != std::string_view::npos);
    assert(primary_shade.find("uFrameIndex & 3u") != std::string_view::npos);

    // Temporal reuse must survive camera movement through reprojection.
    assert(temporal.find("uPreviousViewProjection") != std::string_view::npos);
    assert(svgf.find("uPreviousViewProjection") != std::string_view::npos);

    // Queue/control SSBO is also the indirect-dispatch command source.
    static_assert(Renderer::PathTracerGpu::HIT_DISPATCH_OFFSET == 16u);
    static_assert(Renderer::PathTracerGpu::BOUNCE_DISPATCH_OFFSET == 32u);
    static_assert(Renderer::PathTracerGpu::GI_PHASE_COUNT == 4u);

    return 0;
}
