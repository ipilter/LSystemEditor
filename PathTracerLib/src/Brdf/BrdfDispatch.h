#pragma once

#include "Brdf/OrenNayarDiffuseBrdf.h"

#if defined(__CUDACC__)
#define BRDF_DISPATCH_FN __host__ __device__ inline
#else
#define BRDF_DISPATCH_FN inline
#endif

BRDF_DISPATCH_FN Vec3 brdfEval(const BrdfContext& ctx, Vec3 wi)
{
    const OrenNayarDiffuseBrdf brdf{};
    return brdf.eval(ctx, wi);
}

BRDF_DISPATCH_FN BrdfSampleResult brdfSample(const BrdfContext& ctx, float u1, float u2)
{
    const OrenNayarDiffuseBrdf brdf{};
    return brdf.sample(ctx, u1, u2);
}

BRDF_DISPATCH_FN BrdfSampleResult brdfSampleReflect(const BrdfContext& ctx, float u1, float u2)
{
    return brdfSample(ctx, u1, u2);
}

BRDF_DISPATCH_FN float brdfPdf(const BrdfContext& ctx, Vec3 wi)
{
    const OrenNayarDiffuseBrdf brdf{};
    return brdf.pdf(ctx, wi);
}

BRDF_DISPATCH_FN float brdfThroughputLuminance(Vec3 throughput)
{
    const OrenNayarDiffuseBrdf brdf{};
    return brdf.luminance(throughput);
}

BRDF_DISPATCH_FN bool brdfSkipsEnvironmentNee(const MaterialGpu& material, float etaMedium)
{
    (void)material;
    (void)etaMedium;
    return false;
}

BRDF_DISPATCH_FN Vec3 brdfApplyThroughput(
    Vec3 throughput,
    const BrdfContext& ctx,
    const BrdfSampleResult& sample,
    Vec3 bsdfValue)
{
    if (!sample.valid || sample.pdf <= BrdfDetail::kMinPdf) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    const float cosTheta = vecMax2(0.0f, vecDot3(ctx.normal, sample.direction));
    const Vec3 scale = vecScale3(bsdfValue, cosTheta / sample.pdf);
    return vecMake3(
        throughput.x * scale.x,
        throughput.y * scale.y,
        throughput.z * scale.z);
}

#undef BRDF_DISPATCH_FN
