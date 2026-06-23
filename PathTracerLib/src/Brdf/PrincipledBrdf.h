#pragma once

#include "BrdfBase.h"
#include "BrdfDebug.h"
#include "Medium/MediumProperties.h"
#include "Spectral/SpectralCore.h"

#if defined(__CUDACC__)
#define PRINCIPLED_BRDF_FN __host__ __device__ inline
#else
#define PRINCIPLED_BRDF_FN inline
#endif

namespace PrincipledDetail {

PRINCIPLED_BRDF_FN float clamp01(float value)
{
    return vecMax2(0.0f, vecMin2(1.0f, value));
}

PRINCIPLED_BRDF_FN float materialIor(const BrdfContext& ctx)
{
    return spectralGlassIor(ctx.material, ctx.wavelengthNm);
}

PRINCIPLED_BRDF_FN Vec3 baseColor(const BrdfContext& ctx)
{
    return vecMake3(ctx.material.r, ctx.material.g, ctx.material.b);
}

PRINCIPLED_BRDF_FN float baseColorAtLambda(const BrdfContext& ctx)
{
    return spectralRgbReflectanceAtWavelength(
        ctx.material.r, ctx.material.g, ctx.material.b, ctx.wavelengthNm);
}

PRINCIPLED_BRDF_FN Vec3 mirrorReflectOutgoing(Vec3 wo, Vec3 normal)
{
    return vecNormalize3(vecSub3(
        vecScale3(normal, 2.0f * vecDot3(normal, wo)),
        wo));
}

PRINCIPLED_BRDF_FN Vec3 dielectricF0(float ior, float specularScale)
{
    const float f0 = (ior - 1.0f) / (ior + 1.0f);
    const float v = f0 * f0 * clamp01(specularScale);
    return vecMake3(v, v, v);
}

PRINCIPLED_BRDF_FN Vec3 specularF0(const BrdfContext& ctx)
{
    const Vec3 base = baseColor(ctx);
    const float metallic = clamp01(ctx.material.metallic);
    const Vec3 f0Dielectric = dielectricF0(materialIor(ctx), clamp01(ctx.material.specular));
    return vecMake3(
        f0Dielectric.x + (base.x - f0Dielectric.x) * metallic,
        f0Dielectric.y + (base.y - f0Dielectric.y) * metallic,
        f0Dielectric.z + (base.z - f0Dielectric.z) * metallic);
}

PRINCIPLED_BRDF_FN float specularF0AtLambda(const BrdfContext& ctx)
{
    const float base = baseColorAtLambda(ctx);
    const float metallic = clamp01(ctx.material.metallic);
    const float ior = materialIor(ctx);
    const float f0 = (ior - 1.0f) / (ior + 1.0f);
    const float f0Dielectric = f0 * f0 * clamp01(ctx.material.specular);
    return f0Dielectric + (base - f0Dielectric) * metallic;
}

PRINCIPLED_BRDF_FN float diffuseOrenNayarFactor(const BrdfContext& ctx, Vec3 wi)
{
    const float cosWi = vecMax2(0.0f, vecDot3(ctx.normal, wi));
    const float cosWo = vecMax2(0.0f, vecDot3(ctx.normal, ctx.wo));
    if (cosWi <= 0.0f || cosWo <= 0.0f) {
        return 0.0f;
    }
    const float sigma = brdfOrenNayarSigma(brdfDiffuseRoughness(ctx.material));
    return brdfOrenNayarFactor(sigma, cosWi, cosWo, vecDot3(wi, ctx.wo));
}

struct LobeWeights
{
    float diffuse = 0.0f;
    float specular = 0.0f;
};

PRINCIPLED_BRDF_FN LobeWeights computeReflectLobeWeights(const BrdfContext& ctx)
{
    LobeWeights weights{};
    const float metallic = clamp01(ctx.material.metallic);
    const float dielectric = 1.0f - metallic;
    weights.diffuse = dielectric;
    weights.specular = metallic + dielectric;
    if (weights.diffuse + weights.specular <= BrdfDetail::kMinPdf) {
        weights.diffuse = 1.0f;
        weights.specular = 0.0f;
        return weights;
    }

    const float invSum = 1.0f / (weights.diffuse + weights.specular);
    weights.diffuse *= invSum;
    weights.specular *= invSum;
    return weights;
}

PRINCIPLED_BRDF_FN int pickReflectLobe(const LobeWeights& weights, float u)
{
    if (u < weights.diffuse) {
        return 0;
    }
    return 1;
}

PRINCIPLED_BRDF_FN float interfaceFresnelReflectance(const BrdfContext& ctx)
{
    const float cosWoSigned = vecDot3(ctx.normal, ctx.wo);
    const float ior = materialIor(ctx);
    const float etaT = cosWoSigned > 0.0f ? ior : 1.0f;
    return brdfDielectricFresnel(cosWoSigned, ctx.etaMedium, etaT);
}

PRINCIPLED_BRDF_FN bool refractDirection(
    const BrdfContext& ctx,
    Vec3& outWi,
    float& etaI,
    float& etaT,
    float& nextMediumEta)
{
    const float cosWo = vecDot3(ctx.normal, ctx.wo);
    const float ior = materialIor(ctx);
    etaI = ctx.etaMedium;
    if (cosWo > 0.0f) {
        etaT = ior;
        nextMediumEta = ior;
    } else {
        etaT = 1.0f;
        nextMediumEta = 1.0f;
    }
    return brdfRefractRelative(ctx.wo, ctx.normal, etaI, etaT, outWi);
}

PRINCIPLED_BRDF_FN Vec3 evalDiffuse(const BrdfContext& ctx, Vec3 wi)
{
    const float cosWi = vecMax2(0.0f, vecDot3(ctx.normal, wi));
    const float cosWo = vecMax2(0.0f, vecDot3(ctx.normal, ctx.wo));
    if (cosWi <= 0.0f || cosWo <= 0.0f) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    const LobeWeights weights = computeReflectLobeWeights(ctx);
    const Vec3 base = baseColor(ctx);
    const float orenNayar = diffuseOrenNayarFactor(ctx, wi);
    return vecScale3(base, weights.diffuse * orenNayar * BrdfDetail::kInvPi);
}

PRINCIPLED_BRDF_FN float evalDiffuseSpectral(const BrdfContext& ctx, Vec3 wi)
{
    const float cosWi = vecMax2(0.0f, vecDot3(ctx.normal, wi));
    const float cosWo = vecMax2(0.0f, vecDot3(ctx.normal, ctx.wo));
    if (cosWi <= 0.0f || cosWo <= 0.0f) {
        return 0.0f;
    }

    const LobeWeights weights = computeReflectLobeWeights(ctx);
    const float base = baseColorAtLambda(ctx);
    const float orenNayar = diffuseOrenNayarFactor(ctx, wi);
    return base * weights.diffuse * orenNayar * BrdfDetail::kInvPi;
}

PRINCIPLED_BRDF_FN Vec3 evalSpecular(const BrdfContext& ctx, Vec3 wi)
{
    const float cosWo = vecMax2(0.0f, vecDot3(ctx.normal, ctx.wo));
    const float cosWi = vecMax2(0.0f, vecDot3(ctx.normal, wi));
    if (cosWo <= 0.0f || cosWi <= 0.0f) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    const Vec3 h = vecNormalize3(vecAdd3(wi, ctx.wo));
    const float cosThetaH = vecMax2(0.0f, vecDot3(ctx.normal, h));
    const float cosThetaHo = vecMax2(0.0f, vecDot3(h, ctx.wo));
    if (cosThetaHo <= 0.0f) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    const LobeWeights weights = computeReflectLobeWeights(ctx);
    const float alpha = brdfAlphaFromRoughness(ctx.material.roughness);
    const float D = brdfGGXD(cosThetaH, alpha);
    const float G = brdfSmithG1(cosWo, alpha) * brdfSmithG1(cosWi, alpha);
    const Vec3 F = brdfSchlickF(cosThetaHo, specularF0(ctx));
    const float energyComp = brdfGgxEnergyCompensation(ctx.material.roughness, cosWo);
    const float denom = vecMax2(4.0f * cosWo * cosWi, 1.0e-8f);
    const float scale = weights.specular * energyComp * (D * G) / denom;
    return vecMake3(F.x * scale, F.y * scale, F.z * scale);
}

PRINCIPLED_BRDF_FN float evalSpecularSpectral(const BrdfContext& ctx, Vec3 wi)
{
    const float cosWo = vecMax2(0.0f, vecDot3(ctx.normal, ctx.wo));
    const float cosWi = vecMax2(0.0f, vecDot3(ctx.normal, wi));
    if (cosWo <= 0.0f || cosWi <= 0.0f) {
        return 0.0f;
    }

    const Vec3 h = vecNormalize3(vecAdd3(wi, ctx.wo));
    const float cosThetaH = vecMax2(0.0f, vecDot3(ctx.normal, h));
    const float cosThetaHo = vecMax2(0.0f, vecDot3(h, ctx.wo));
    if (cosThetaHo <= 0.0f) {
        return 0.0f;
    }

    const LobeWeights weights = computeReflectLobeWeights(ctx);
    const float alpha = brdfAlphaFromRoughness(ctx.material.roughness);
    const float D = brdfGGXD(cosThetaH, alpha);
    const float G = brdfSmithG1(cosWo, alpha) * brdfSmithG1(cosWi, alpha);
    const float F = brdfSchlickFScalar(cosThetaHo, specularF0AtLambda(ctx));
    const float energyComp = brdfGgxEnergyCompensation(ctx.material.roughness, cosWo);
    const float denom = vecMax2(4.0f * cosWo * cosWi, 1.0e-8f);
    return weights.specular * energyComp * F * (D * G) / denom;
}

PRINCIPLED_BRDF_FN float pdfDiffuse(const BrdfContext& ctx, Vec3 wi)
{
    const LobeWeights weights = computeReflectLobeWeights(ctx);
    return weights.diffuse * brdfCosineHemispherePdf(vecMax2(0.0f, vecDot3(ctx.normal, wi)));
}

PRINCIPLED_BRDF_FN float pdfSpecular(const BrdfContext& ctx, Vec3 wi)
{
    const float cosWo = vecMax2(0.0f, vecDot3(ctx.normal, ctx.wo));
    const float cosWi = vecMax2(0.0f, vecDot3(ctx.normal, wi));
    if (cosWo <= 0.0f || cosWi <= 0.0f) {
        return 0.0f;
    }

    const Vec3 h = vecNormalize3(vecAdd3(wi, ctx.wo));
    const float cosThetaH = vecMax2(0.0f, vecDot3(ctx.normal, h));
    const float cosThetaHo = vecMax2(0.0f, vecDot3(h, ctx.wo));
    if (cosThetaHo <= 0.0f) {
        return 0.0f;
    }

    const LobeWeights weights = computeReflectLobeWeights(ctx);
    const float alpha = brdfAlphaFromRoughness(ctx.material.roughness);
    const float hPdf = brdfGGXD(cosThetaH, alpha) * cosThetaH;
    return weights.specular * hPdf / vecMax2(4.0f * cosThetaHo, 1.0e-8f);
}

} // namespace PrincipledDetail

struct PrincipledBrdf : BrdfBase<PrincipledBrdf>
{
    PRINCIPLED_BRDF_FN Vec3 evalImpl(const BrdfContext& ctx, Vec3 wi) const
    {
        return vecAdd3(
            PrincipledDetail::evalDiffuse(ctx, wi),
            PrincipledDetail::evalSpecular(ctx, wi));
    }

    PRINCIPLED_BRDF_FN float evalSpectral(const BrdfContext& ctx, Vec3 wi) const
    {
        return PrincipledDetail::evalDiffuseSpectral(ctx, wi)
            + PrincipledDetail::evalSpecularSpectral(ctx, wi);
    }

    PRINCIPLED_BRDF_FN BrdfSampleResult sampleDiffuse(const BrdfContext& ctx, float u1, float u2) const
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

    PRINCIPLED_BRDF_FN BrdfSampleResult sampleSpecular(const BrdfContext& ctx, float u1, float u2) const
    {
        BrdfSampleResult result{};
        const float cosWo = vecMax2(0.0f, vecDot3(ctx.normal, ctx.wo));
        if (cosWo <= 0.0f) {
            return result;
        }

        const float alpha = brdfAlphaFromRoughness(ctx.material.roughness);
        Vec3 tangent{};
        Vec3 bitangent{};
        brdfBuildBasis(ctx.normal, tangent, bitangent);

        float hPdf = 0.0f;
        const Vec3 hLocal = brdfSampleGGXHalfLocal(alpha, u1, u2, hPdf);
        const Vec3 h = brdfLocalToWorld(hLocal, ctx.normal, tangent, bitangent);
        const float cosThetaHo = vecMax2(0.0f, vecDot3(h, ctx.wo));
        if (cosThetaHo <= 0.0f || hPdf <= BrdfDetail::kMinPdf) {
            return result;
        }

        result.direction = vecNormalize3(vecSub3(vecScale3(h, 2.0f * cosThetaHo), ctx.wo));
        const float cosWi = vecMax2(0.0f, vecDot3(ctx.normal, result.direction));
        if (cosWi <= 0.0f) {
            return result;
        }

        result.pdf = pdfImpl(ctx, result.direction);
        result.valid = result.pdf > BrdfDetail::kMinPdf;
        result.transmitted = false;
        result.nextMediumEta = ctx.etaMedium;
        return result;
    }

    PRINCIPLED_BRDF_FN BrdfSampleResult sampleReflectImpl(const BrdfContext& ctx, float u1, float u2) const
    {
        const PrincipledDetail::LobeWeights weights = PrincipledDetail::computeReflectLobeWeights(ctx);
        const int lobe = PrincipledDetail::pickReflectLobe(weights, u1);
        if (lobe == 0) {
            const float localU1 = u1 / vecMax2(weights.diffuse, BrdfDetail::kMinPdf);
            return sampleDiffuse(ctx, localU1, u2);
        }

        const float localU1 = (u1 - weights.diffuse) / vecMax2(weights.specular, BrdfDetail::kMinPdf);
        return sampleSpecular(ctx, localU1, u2);
    }

    PRINCIPLED_BRDF_FN BrdfSampleResult sampleImpl(const BrdfContext& ctx, float u1, float u2) const
    {
        return sampleReflectImpl(ctx, u1, u2);
    }

    PRINCIPLED_BRDF_FN float pdfImpl(const BrdfContext& ctx, Vec3 wi) const
    {
        return PrincipledDetail::pdfDiffuse(ctx, wi) + PrincipledDetail::pdfSpecular(ctx, wi);
    }
};

PRINCIPLED_BRDF_FN BrdfSampleResult brdfSampleRefract(const BrdfContext& ctx, float /*u1*/, float /*u2*/)
{
    BrdfSampleResult result{};
    float etaOutI = 0.0f;
    float etaOutT = 0.0f;
    if (!PrincipledDetail::refractDirection(ctx, result.direction, etaOutI, etaOutT, result.nextMediumEta)) {
        return result;
    }

    result.valid = true;
    result.transmitted = true;
    result.pdf = 1.0f;
    return result;
}

#undef PRINCIPLED_BRDF_FN
