#pragma once

#include "PrincipledBrdf.h"

#if defined(__CUDACC__)
#define BRDF_DISPATCH_FN __host__ __device__ inline
#else
#define BRDF_DISPATCH_FN inline
#endif

BRDF_DISPATCH_FN Vec3 brdfEval(const BrdfContext& ctx, Vec3 wi)
{
    const PrincipledBrdf brdf{};
    return brdf.eval(ctx, wi);
}

BRDF_DISPATCH_FN BrdfSampleResult brdfSample(const BrdfContext& ctx, float u1, float u2)
{
    const PrincipledBrdf brdf{};
    return brdf.sample(ctx, u1, u2);
}

BRDF_DISPATCH_FN float brdfPdf(const BrdfContext& ctx, Vec3 wi)
{
    const PrincipledBrdf brdf{};
    return brdf.pdf(ctx, wi);
}

BRDF_DISPATCH_FN float brdfThroughputLuminance(Vec3 throughput)
{
    const PrincipledBrdf brdf{};
    return brdf.luminance(throughput);
}

BRDF_DISPATCH_FN bool brdfSkipsEnvironmentNee(const MaterialGpu& material)
{
    const float transmission = vecMax2(0.0f, vecMin2(1.0f, material.transmission));
    const float metallic = vecMax2(0.0f, vecMin2(1.0f, material.metallic));
    return transmission > 0.99f && metallic < 0.01f;
}

BRDF_DISPATCH_FN Vec3 brdfApplyThroughput(Vec3 throughput, const BrdfContext& ctx, const BrdfSampleResult& sample, Vec3 bsdfValue)
{
    if (!sample.valid || sample.pdf <= BrdfDetail::kMinPdf) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    const float transmission = vecMax2(0.0f, vecMin2(1.0f, ctx.material.transmission));
    const float thin = vecMax2(0.0f, vecMin2(1.0f, ctx.material.thin));
    const bool pureTransmitLobe = transmission > 0.99f && ctx.material.metallic < 0.01f;

    Vec3 scale{};
    if (pureTransmitLobe && thin < 0.5f) {
        scale = vecScale3(bsdfValue, 1.0f / sample.pdf);
    } else if (sample.transmitted) {
        const float cosTheta = vecAbs(vecDot3(ctx.normal, sample.direction));
        scale = vecScale3(bsdfValue, cosTheta / sample.pdf);
        if (thin > 0.5f) {
            const Vec3 tint = brdfBaseColor(ctx.material);
            scale = vecMake3(scale.x * tint.x, scale.y * tint.y, scale.z * tint.z);
        }
    } else if (sample.subsurfaceScatter) {
        const float cosTheta = vecMax2(0.0f, vecDot3(ctx.normal, sample.direction));
        scale = vecScale3(bsdfValue, cosTheta / sample.pdf);
    } else {
        const float cosTheta = ctx.etaMedium > 1.0f + 1.0e-4f
            ? vecAbs(vecDot3(ctx.normal, sample.direction))
            : vecMax2(0.0f, vecDot3(ctx.normal, sample.direction));
        scale = vecScale3(bsdfValue, cosTheta / sample.pdf);
    }

    return vecMake3(
        throughput.x * scale.x,
        throughput.y * scale.y,
        throughput.z * scale.z);
}

#undef BRDF_DISPATCH_FN
