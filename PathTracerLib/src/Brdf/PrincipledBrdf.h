#pragma once

#include "BrdfBase.h"
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

PRINCIPLED_BRDF_FN Vec3 mirrorReflectOutgoing(Vec3 wo, Vec3 normal)
{
    return vecNormalize3(vecSub3(
        vecScale3(normal, 2.0f * vecDot3(normal, wo)),
        wo));
}

PRINCIPLED_BRDF_FN Vec3 dielectricF0(float ior)
{
    const float f0 = (ior - 1.0f) / (ior + 1.0f);
    const float v = f0 * f0;
    return vecMake3(v, v, v);
}

PRINCIPLED_BRDF_FN Vec3 specularF0(const BrdfContext& ctx)
{
    const Vec3 base = baseColor(ctx);
    const float metallic = clamp01(ctx.material.metallic);
    const Vec3 f0Dielectric = dielectricF0(materialIor(ctx));
    return vecMake3(
        f0Dielectric.x + (base.x - f0Dielectric.x) * metallic,
        f0Dielectric.y + (base.y - f0Dielectric.y) * metallic,
        f0Dielectric.z + (base.z - f0Dielectric.z) * metallic);
}

struct LobeWeights
{
    float diffuse = 0.0f;
    float specular = 0.0f;
    float transmit = 0.0f;
    float subsurface = 0.0f;
};

PRINCIPLED_BRDF_FN LobeWeights computeLobeWeights(const BrdfContext& ctx)
{
    LobeWeights weights{};
    const float metallic = clamp01(ctx.material.metallic);
    const float transmission = clamp01(ctx.material.transmission);
    const float subsurface = clamp01(ctx.material.subsurface);

    const float dielectric = 1.0f - metallic;
    weights.transmit = dielectric * transmission;
    weights.subsurface = dielectric * (1.0f - transmission) * subsurface;
    weights.diffuse = dielectric * (1.0f - transmission) * (1.0f - subsurface);
    weights.specular = metallic + dielectric * (1.0f - transmission);

    const float sum = weights.diffuse + weights.specular + weights.transmit + weights.subsurface;
    if (sum <= BrdfDetail::kMinPdf) {
        weights.diffuse = 1.0f;
        return weights;
    }

    const float invSum = 1.0f / sum;
    weights.diffuse *= invSum;
    weights.specular *= invSum;
    weights.transmit *= invSum;
    weights.subsurface *= invSum;
    return weights;
}

PRINCIPLED_BRDF_FN int pickLobe(const LobeWeights& weights, float u)
{
    if (u < weights.diffuse) {
        return 0;
    }
    u -= weights.diffuse;
    if (u < weights.specular) {
        return 1;
    }
    u -= weights.specular;
    if (u < weights.transmit) {
        return 2;
    }
    return 3;
}

PRINCIPLED_BRDF_FN float lobeWeight(const LobeWeights& weights, int lobe)
{
    switch (lobe) {
    case 0:
        return weights.diffuse;
    case 1:
        return weights.specular;
    case 2:
        return weights.transmit;
    case 3:
    default:
        return weights.subsurface;
    }
}

PRINCIPLED_BRDF_FN Vec3 evalDiffuse(const BrdfContext& ctx, Vec3 wi)
{
    const float cosWi = vecMax2(0.0f, vecDot3(ctx.normal, wi));
    const float cosWo = vecMax2(0.0f, vecDot3(ctx.normal, ctx.wo));
    if (cosWi <= 0.0f || cosWo <= 0.0f) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    const LobeWeights weights = computeLobeWeights(ctx);
    const Vec3 base = baseColor(ctx);
    return vecScale3(base, weights.diffuse * BrdfDetail::kInvPi);
}

PRINCIPLED_BRDF_FN Vec3 evalSubsurface(const BrdfContext& ctx, Vec3 wi)
{
    const float cosWi = vecMax2(0.0f, vecDot3(ctx.normal, wi));
    const float cosWo = vecMax2(0.0f, vecDot3(ctx.normal, ctx.wo));
    if (cosWi <= 0.0f || cosWo <= 0.0f) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    const LobeWeights weights = computeLobeWeights(ctx);
    const Vec3 base = baseColor(ctx);
    return vecScale3(base, weights.subsurface * BrdfDetail::kInvPi);
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

    const LobeWeights weights = computeLobeWeights(ctx);
    const float alpha = brdfAlphaFromRoughness(ctx.material.roughness);
    const float D = brdfGGXD(cosThetaH, alpha);
    const float G = brdfSmithG1(cosWo, alpha) * brdfSmithG1(cosWi, alpha);
    const Vec3 F = brdfSchlickF(cosThetaHo, specularF0(ctx));
    const float denom = vecMax2(4.0f * cosWo * cosWi, 1.0e-8f);
    const float scale = weights.specular * (D * G) / denom;
    return vecMake3(F.x * scale, F.y * scale, F.z * scale);
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

PRINCIPLED_BRDF_FN Vec3 evalTransmit(const BrdfContext& ctx, Vec3 wi)
{
    const float cosWo = vecMax2(0.0f, vecDot3(ctx.normal, ctx.wo));
    if (cosWo <= 0.0f) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    const LobeWeights weights = computeLobeWeights(ctx);
    const float thin = clamp01(ctx.material.thin);
    const Vec3 base = baseColor(ctx);
    const float etaI = ctx.etaMedium;
    const float ior = materialIor(ctx);
    const float cosWoSigned = vecDot3(ctx.normal, ctx.wo);
    const float etaT = cosWoSigned > 0.0f ? ior : 1.0f;
    const float fresnel = brdfDielectricFresnel(cosWoSigned, etaI, etaT);
    const float reflectance = thin > 0.5f ? fresnel : fresnel;
    const float transmittance = 1.0f - reflectance;

    const Vec3 reflected = mirrorReflectOutgoing(ctx.wo, ctx.normal);
    if (vecDot3(wi, reflected) > 0.999f) {
        const float cosWi = vecMax2(0.0f, vecDot3(ctx.normal, wi));
        return vecScale3(base, weights.transmit * reflectance / vecMax2(cosWi, 1.0e-8f));
    }

    Vec3 transmitted{};
    float etaOutI = 0.0f;
    float etaOutT = 0.0f;
    float nextMediumEta = 1.0f;
    if (thin > 0.5f) {
        transmitted = vecNormalize3(vecScale3(ctx.wo, -1.0f));
    } else if (!refractDirection(ctx, transmitted, etaOutI, etaOutT, nextMediumEta)) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    (void)nextMediumEta;
    if (vecDot3(wi, transmitted) > 0.999f) {
        const float cosWi = vecAbs(vecDot3(ctx.normal, transmitted));
        return vecScale3(base, weights.transmit * transmittance / vecMax2(cosWi, 1.0e-8f));
    }

    return vecMake3(0.0f, 0.0f, 0.0f);
}

PRINCIPLED_BRDF_FN float pdfDiffuse(const BrdfContext& ctx, Vec3 wi)
{
    const LobeWeights weights = computeLobeWeights(ctx);
    return weights.diffuse * brdfCosineHemispherePdf(vecMax2(0.0f, vecDot3(ctx.normal, wi)));
}

PRINCIPLED_BRDF_FN float pdfSubsurface(const BrdfContext& ctx, Vec3 wi)
{
    const LobeWeights weights = computeLobeWeights(ctx);
    return weights.subsurface * brdfCosineHemispherePdf(vecMax2(0.0f, vecDot3(ctx.normal, wi)));
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

    const LobeWeights weights = computeLobeWeights(ctx);
    const float alpha = brdfAlphaFromRoughness(ctx.material.roughness);
    const float hPdf = brdfGGXD(cosThetaH, alpha) * cosThetaH;
    return weights.specular * hPdf / vecMax2(4.0f * cosThetaHo, 1.0e-8f);
}

PRINCIPLED_BRDF_FN float pdfTransmit(const BrdfContext& ctx, Vec3 wi)
{
    const float cosWo = vecMax2(0.0f, vecDot3(ctx.normal, ctx.wo));
    if (cosWo <= 0.0f) {
        return 0.0f;
    }

    const LobeWeights weights = computeLobeWeights(ctx);
    const float thin = clamp01(ctx.material.thin);
    const float etaI = ctx.etaMedium;
    const float ior = materialIor(ctx);
    const float cosWoSigned = vecDot3(ctx.normal, ctx.wo);
    const float etaT = cosWoSigned > 0.0f ? ior : 1.0f;
    const float fresnel = brdfDielectricFresnel(cosWoSigned, etaI, etaT);
    const float reflectance = fresnel;
    const float transmittance = 1.0f - reflectance;
    float pdf = 0.0f;

    const Vec3 reflected = mirrorReflectOutgoing(ctx.wo, ctx.normal);
    if (vecDot3(wi, reflected) > 0.999f) {
        const float cosWi = vecMax2(0.0f, vecDot3(ctx.normal, wi));
        pdf += weights.transmit * reflectance / vecMax2(cosWi, 1.0e-8f);
    }

    Vec3 transmitted{};
    if (thin > 0.5f) {
        transmitted = vecNormalize3(vecScale3(ctx.wo, -1.0f));
    } else {
        float etaOutI = 0.0f;
        float etaOutT = 0.0f;
        float nextMediumEta = 1.0f;
        if (!refractDirection(ctx, transmitted, etaOutI, etaOutT, nextMediumEta)) {
            return pdf;
        }
    }

    if (vecDot3(wi, transmitted) > 0.999f) {
        const float cosWi = vecAbs(vecDot3(ctx.normal, transmitted));
        pdf += weights.transmit * transmittance / vecMax2(cosWi, 1.0e-8f);
    }

    return pdf;
}

} // namespace PrincipledDetail

struct PrincipledBrdf : BrdfBase<PrincipledBrdf>
{
    PRINCIPLED_BRDF_FN Vec3 evalImpl(const BrdfContext& ctx, Vec3 wi) const
    {
        const Vec3 diffuse = PrincipledDetail::evalDiffuse(ctx, wi);
        const Vec3 specular = PrincipledDetail::evalSpecular(ctx, wi);
        const Vec3 transmit = PrincipledDetail::evalTransmit(ctx, wi);
        const Vec3 subsurface = PrincipledDetail::evalSubsurface(ctx, wi);
        return vecAdd3(vecAdd3(diffuse, specular), vecAdd3(transmit, subsurface));
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

    PRINCIPLED_BRDF_FN BrdfSampleResult sampleSubsurface(const BrdfContext& ctx, float u1, float u2) const
    {
        return sampleDiffuse(ctx, u1, u2);
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

    PRINCIPLED_BRDF_FN BrdfSampleResult sampleTransmit(const BrdfContext& ctx, float u1, float u2) const
    {
        (void)u2;
        BrdfSampleResult result{};
        const float cosWo = vecMax2(0.0f, vecDot3(ctx.normal, ctx.wo));
        if (cosWo <= 0.0f) {
            return result;
        }

        const float thin = PrincipledDetail::clamp01(ctx.material.thin);
        const float etaI = ctx.etaMedium;
        const float ior = PrincipledDetail::materialIor(ctx);
        const float cosWoSigned = vecDot3(ctx.normal, ctx.wo);
        const float etaT = cosWoSigned > 0.0f ? ior : 1.0f;
        const float fresnel = brdfDielectricFresnel(cosWoSigned, etaI, etaT);
        const float reflectance = fresnel;
        const float transmittance = 1.0f - reflectance;

        if (u1 < transmittance) {
            if (thin > 0.5f) {
                result.direction = vecNormalize3(vecScale3(ctx.wo, -1.0f));
                result.nextMediumEta = ctx.etaMedium;
            } else {
                float etaOutI = 0.0f;
                float etaOutT = 0.0f;
                if (!PrincipledDetail::refractDirection(ctx, result.direction, etaOutI, etaOutT, result.nextMediumEta)) {
                    result.direction = PrincipledDetail::mirrorReflectOutgoing(ctx.wo, ctx.normal);
                    result.transmitted = false;
                    result.nextMediumEta = ctx.etaMedium;
                } else {
                    result.transmitted = true;
                }
            }
            if (thin > 0.5f) {
                result.transmitted = true;
            }

            const float cosWi = vecAbs(vecDot3(ctx.normal, result.direction));
            if (cosWi <= 0.0f) {
                return result;
            }
        } else {
            result.direction = PrincipledDetail::mirrorReflectOutgoing(ctx.wo, ctx.normal);
            const float cosWi = vecMax2(0.0f, vecDot3(ctx.normal, result.direction));
            if (cosWi <= 0.0f) {
                return result;
            }
            result.transmitted = false;
            result.nextMediumEta = ctx.etaMedium;
        }

        result.pdf = pdfImpl(ctx, result.direction);
        result.valid = result.pdf > BrdfDetail::kMinPdf;
        return result;
    }

    PRINCIPLED_BRDF_FN BrdfSampleResult sampleImpl(const BrdfContext& ctx, float u1, float u2) const
    {
        const PrincipledDetail::LobeWeights weights = PrincipledDetail::computeLobeWeights(ctx);
        const int lobe = PrincipledDetail::pickLobe(weights, u1);

        float localU1 = 0.0f;
        switch (lobe) {
        case 0:
            localU1 = u1 / vecMax2(weights.diffuse, BrdfDetail::kMinPdf);
            return sampleDiffuse(ctx, localU1, u2);
        case 1:
            localU1 = (u1 - weights.diffuse) / vecMax2(weights.specular, BrdfDetail::kMinPdf);
            return sampleSpecular(ctx, localU1, u2);
        case 2:
            localU1 = (u1 - weights.diffuse - weights.specular) / vecMax2(weights.transmit, BrdfDetail::kMinPdf);
            return sampleTransmit(ctx, localU1, u2);
        case 3:
        default:
            localU1 = (u1 - weights.diffuse - weights.specular - weights.transmit)
                / vecMax2(weights.subsurface, BrdfDetail::kMinPdf);
            return sampleSubsurface(ctx, localU1, u2);
        }
    }

    PRINCIPLED_BRDF_FN float pdfImpl(const BrdfContext& ctx, Vec3 wi) const
    {
        return PrincipledDetail::pdfDiffuse(ctx, wi)
            + PrincipledDetail::pdfSpecular(ctx, wi)
            + PrincipledDetail::pdfTransmit(ctx, wi)
            + PrincipledDetail::pdfSubsurface(ctx, wi);
    }
};

#undef PRINCIPLED_BRDF_FN
