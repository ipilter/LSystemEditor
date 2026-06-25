#pragma once

#include "Brdf/OrenNayarDiffuseBrdf.h"
#include "Brdf/PrincipledBrdf.h"
#include "Material/MaterialParams.h"

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

BRDF_DISPATCH_FN BrdfSampleResult brdfSampleReflect(const BrdfContext& ctx, float u1, float u2)
{
    const PrincipledBrdf brdf{};
    return brdf.sampleReflectImpl(ctx, u1, u2);
}

BRDF_DISPATCH_FN float brdfPdf(const BrdfContext& ctx, Vec3 wi)
{
    const PrincipledBrdf brdf{};
    return brdf.pdf(ctx, wi);
}

BRDF_DISPATCH_FN Vec3 brdfEvalDirectLighting(const BrdfContext& ctx, Vec3 wi)
{
    const BrdfLobeWeights lobes = computeSurfaceLobeWeights(ctx.material);
    const Vec3 surface = brdfEval(ctx, wi);
    if (lobes.subsurface <= 1.0e-6f) {
        return surface;
    }

    const float cosWi = vecMax2(0.0f, vecDot3(ctx.normal, wi));
    const float cosWo = vecMax2(0.0f, vecDot3(ctx.normal, ctx.wo));
    if (cosWi <= 0.0f || cosWo <= 0.0f) {
        return vecScale3(surface, 1.0f - lobes.subsurface);
    }

    const PhysicalMediumCoeffs coeffs = materialToPhysicalMedium(ctx.material, ctx.wavelengthNm);
    const float scatterAlbedo = mediumScatterAlbedoAtWavelength(coeffs, ctx.wavelengthNm);
    const Vec3 base = brdfBaseColor(ctx.material);
    const float orenNayar = OrenNayarDetail::diffuseOrenNayarFactor(ctx, wi);
    const Vec3 subsurfaceDiffuse = vecScale3(
        base,
        lobes.subsurface * scatterAlbedo * orenNayar * BrdfDetail::kInvPi);

    return vecAdd3(vecScale3(surface, 1.0f - lobes.subsurface), subsurfaceDiffuse);
}

BRDF_DISPATCH_FN float brdfPdfDirectLighting(const BrdfContext& ctx, Vec3 wi)
{
    const BrdfLobeWeights lobes = computeSurfaceLobeWeights(ctx.material);
    const float cosWi = vecMax2(0.0f, vecDot3(ctx.normal, wi));
    const float surfacePdf = brdfPdf(ctx, wi) * (1.0f - lobes.subsurface);
    const float subsurfacePdf = lobes.subsurface * brdfCosineHemispherePdf(cosWi);
    return surfacePdf + subsurfacePdf;
}

BRDF_DISPATCH_FN float brdfThroughputLuminance(Vec3 throughput)
{
    const PrincipledBrdf brdf{};
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
