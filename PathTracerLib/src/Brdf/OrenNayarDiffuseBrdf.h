#pragma once

#include "Brdf/BrdfBase.h"
#include "Brdf/BrdfLobe.h"
#include "Spectral/SpectralCore.h"

#if defined(__CUDACC__)
#define OREN_NAYAR_BRDF_FN __host__ __device__ inline
#else
#define OREN_NAYAR_BRDF_FN inline
#endif

namespace OrenNayarDetail {

OREN_NAYAR_BRDF_FN float diffuseOrenNayarFactor(const BrdfContext& ctx, Vec3 wi)
{
    const float cosWi = vecMax2(0.0f, vecDot3(ctx.normal, wi));
    const float cosWo = vecMax2(0.0f, vecDot3(ctx.normal, ctx.wo));
    if (cosWi <= 0.0f || cosWo <= 0.0f) {
        return 0.0f;
    }
    const float sigma = brdfOrenNayarSigma(brdfDiffuseRoughness(ctx.material));
    return brdfOrenNayarFactor(sigma, cosWi, cosWo, vecDot3(wi, ctx.wo));
}

OREN_NAYAR_BRDF_FN Vec3 evalDiffuse(const BrdfContext& ctx, Vec3 wi)
{
    const float cosWi = vecMax2(0.0f, vecDot3(ctx.normal, wi));
    const float cosWo = vecMax2(0.0f, vecDot3(ctx.normal, ctx.wo));
    if (cosWi <= 0.0f || cosWo <= 0.0f) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    const BrdfLobeWeights weights = computeReflectLobeWeights(ctx.material);
    const Vec3 base = brdfBaseColor(ctx.material);
    const float orenNayar = diffuseOrenNayarFactor(ctx, wi);
    return vecScale3(base, weights.diffuse * orenNayar * BrdfDetail::kInvPi);
}

OREN_NAYAR_BRDF_FN float pdfDiffuse(const BrdfContext& ctx, Vec3 wi)
{
    const BrdfLobeWeights weights = computeReflectLobeWeights(ctx.material);
    return weights.diffuse * brdfCosineHemispherePdf(vecMax2(0.0f, vecDot3(ctx.normal, wi)));
}

} // namespace OrenNayarDetail

struct OrenNayarDiffuseBrdf : BrdfBase<OrenNayarDiffuseBrdf>
{
    OREN_NAYAR_BRDF_FN Vec3 evalImpl(const BrdfContext& ctx, Vec3 wi) const
    {
        return OrenNayarDetail::evalDiffuse(ctx, wi);
    }

    OREN_NAYAR_BRDF_FN BrdfSampleResult sampleImpl(const BrdfContext& ctx, float u1, float u2) const
    {
        BrdfSampleResult result{};
        Vec3 tangent{};
        Vec3 bitangent{};
        brdfBuildBasis(ctx.normal, tangent, bitangent);

        const Vec3 local = brdfSampleCosineHemisphereLocal(u1, u2);
        result.direction = brdfLocalToWorld(local, ctx.normal, tangent, bitangent);
        result.pdf = pdfImpl(ctx, result.direction);
        result.valid = vecDot3(ctx.normal, result.direction) > 0.0f && result.pdf > BrdfDetail::kMinPdf;
        result.transmitted = false;
        result.nextMediumEta = ctx.etaMedium;
        return result;
    }

    OREN_NAYAR_BRDF_FN float pdfImpl(const BrdfContext& ctx, Vec3 wi) const
    {
        return OrenNayarDetail::pdfDiffuse(ctx, wi);
    }

    OREN_NAYAR_BRDF_FN float evalSpectral(const BrdfContext& ctx, Vec3 wi) const
    {
        const Vec3 rgb = evalImpl(ctx, wi);
        return spectralRgbToScalar(rgb.x, rgb.y, rgb.z, ctx.wavelengthNm);
    }
};

#undef OREN_NAYAR_BRDF_FN
