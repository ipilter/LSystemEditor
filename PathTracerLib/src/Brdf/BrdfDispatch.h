#pragma once

#include "PrincipledBrdf.h"
#include "Spectral/SpectralCore.h"

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

BRDF_DISPATCH_FN float brdfEvalSpectral(const BrdfContext& ctx, Vec3 wi)
{
    const PrincipledBrdf brdf{};
    return brdf.evalSpectral(ctx, wi);
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

BRDF_DISPATCH_FN float brdfThroughputLuminance(Vec3 throughput)
{
    const PrincipledBrdf brdf{};
    return brdf.luminance(throughput);
}

BRDF_DISPATCH_FN float brdfThroughputLuminanceScalar(float throughput, float wavelengthNm)
{
    float xBar = 0.0f;
    float yBar = 0.0f;
    float zBar = 0.0f;
    spectralCmfAtWavelength(wavelengthNm, xBar, yBar, zBar);
    return throughput * yBar;
}

BRDF_DISPATCH_FN bool brdfSkipsEnvironmentNee(const MaterialGpu& material)
{
    return materialIsClearMedium(material);
}

BRDF_DISPATCH_FN Vec3 brdfApplyThroughput(Vec3 throughput, const BrdfContext& ctx, const BrdfSampleResult& sample, Vec3 bsdfValue)
{
    (void)ctx;
    if (!sample.valid || sample.pdf <= BrdfDetail::kMinPdf) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    const float cosTheta = sample.transmitted
        ? vecAbs(vecDot3(ctx.normal, sample.direction))
        : vecMax2(0.0f, vecDot3(ctx.normal, sample.direction));
    const Vec3 scale = vecScale3(bsdfValue, cosTheta / sample.pdf);

    return vecMake3(
        throughput.x * scale.x,
        throughput.y * scale.y,
        throughput.z * scale.z);
}

BRDF_DISPATCH_FN float brdfApplyThroughputScalar(
    float throughput,
    const BrdfContext& ctx,
    const BrdfSampleResult& sample,
    float bsdfValue)
{
    if (!sample.valid || sample.pdf <= BrdfDetail::kMinPdf) {
        return 0.0f;
    }

    const float cosTheta = sample.transmitted
        ? vecAbs(vecDot3(ctx.normal, sample.direction))
        : vecMax2(0.0f, vecDot3(ctx.normal, sample.direction));
    return throughput * bsdfValue * cosTheta / sample.pdf;
}

BRDF_DISPATCH_FN Vec3 brdfApplyInterfaceThroughput(Vec3 throughput, float fresnelReflectance, bool choseReflect)
{
    if (choseReflect) {
        return throughput;
    }

    const float transmitWeight = vecMax2(0.0f, 1.0f - fresnelReflectance);
    const float prob = vecMax2(transmitWeight, BrdfDetail::kMinPdf);
    return vecScale3(throughput, transmitWeight / prob);
}

BRDF_DISPATCH_FN float brdfApplyInterfaceThroughputScalar(
    float throughput,
    float fresnelReflectance,
    bool choseReflect)
{
    if (choseReflect) {
        return throughput;
    }

    const float transmitWeight = vecMax2(0.0f, 1.0f - fresnelReflectance);
    const float prob = vecMax2(transmitWeight, BrdfDetail::kMinPdf);
    return throughput * transmitWeight / prob;
}

#undef BRDF_DISPATCH_FN
