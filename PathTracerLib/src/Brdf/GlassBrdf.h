#pragma once

#include "BrdfBase.h"
#include "MetalBrdf.h"

#if defined(__CUDACC__)
#define GLASS_BRDF_FN __host__ __device__ inline
#else
#define GLASS_BRDF_FN inline
#endif

namespace GlassBrdfDetail {

constexpr float kGrazingCos = 1.0e-4f;

} // namespace GlassBrdfDetail

struct GlassBrdf : BrdfBase<GlassBrdf>
{
    GLASS_BRDF_FN BrdfSampleResult sampleSpecularReflection(const BrdfContext& ctx, float fresnel) const
    {
        BrdfSampleResult result{};
        const Vec3 reflected = vecNormalize3(vecSub3(
            vecScale3(ctx.normal, 2.0f * vecDot3(ctx.normal, ctx.wo)),
            ctx.wo));
        const float cosWi = vecMax2(0.0f, vecDot3(ctx.normal, reflected));
        if (cosWi <= GlassBrdfDetail::kGrazingCos) {
            return result;
        }

        result.direction = reflected;
        result.pdf = fresnel / vecMax2(cosWi, 1.0e-8f);
        result.transmitted = false;
        result.valid = result.pdf > BrdfDetail::kMinPdf;
        return result;
    }

    GLASS_BRDF_FN Vec3 evalImpl(const BrdfContext& ctx, Vec3 wi) const
    {
        const float cosWo = vecMax2(0.0f, vecDot3(ctx.normal, ctx.wo));
        if (cosWo <= 0.0f) {
            return vecMake3(0.0f, 0.0f, 0.0f);
        }

        const Vec3 baseColor = brdfBaseColor(ctx.material);
        const float alpha = brdfAlphaFromRoughness(ctx.material.roughness);
        const float eta = ctx.material.ior;
        const float fresnel = brdfDielectricFresnel(cosWo, 1.0f, eta);

        Vec3 refracted{};
        if (brdfRefract3(ctx.wo, ctx.normal, eta, refracted)) {
            if (vecDot3(wi, refracted) > 0.999f) {
                const float cosT = vecAbs(vecDot3(ctx.normal, refracted));
                return vecScale3(baseColor, (1.0f - fresnel) / vecMax2(cosT, 1.0e-8f));
            }
        }

        const float cosWi = vecMax2(0.0f, vecDot3(ctx.normal, wi));
        if (cosWi <= 0.0f) {
            return vecMake3(0.0f, 0.0f, 0.0f);
        }

        const Vec3 reflected = vecNormalize3(vecSub3(
            vecScale3(ctx.normal, 2.0f * vecDot3(ctx.normal, ctx.wo)),
            ctx.wo));
        if (vecDot3(wi, reflected) > 0.999f) {
            return vecScale3(baseColor, fresnel / vecMax2(cosWi, 1.0e-8f));
        }

        const Vec3 h = vecNormalize3(vecAdd3(wi, ctx.wo));
        const float cosThetaH = vecMax2(0.0f, vecDot3(ctx.normal, h));
        const float cosThetaHo = vecMax2(0.0f, vecDot3(h, ctx.wo));
        if (cosThetaHo <= 0.0f) {
            return vecMake3(0.0f, 0.0f, 0.0f);
        }

        const float D = brdfGGXD(cosThetaH, alpha);
        const float G = brdfSmithG1(cosWo, alpha) * brdfSmithG1(cosWi, alpha);
        const float denom = vecMax2(4.0f * cosWo * cosWi, 1.0e-8f);
        return vecScale3(baseColor, fresnel * (D * G) / denom);
    }

    GLASS_BRDF_FN BrdfSampleResult sampleImpl(const BrdfContext& ctx, float u1, float u2) const
    {
        BrdfSampleResult result{};

        const float cosWo = vecMax2(0.0f, vecDot3(ctx.normal, ctx.wo));
        if (cosWo <= 0.0f) {
            return result;
        }

        const float eta = ctx.material.ior;
        const float fresnel = brdfDielectricFresnel(cosWo, 1.0f, eta);
        const float alpha = brdfAlphaFromRoughness(ctx.material.roughness);

        if (u1 < fresnel) {
            if (cosWo < GlassBrdfDetail::kGrazingCos) {
                return sampleSpecularReflection(ctx, fresnel);
            }

            Vec3 tangent{};
            Vec3 bitangent{};
            brdfBuildBasis(ctx.normal, tangent, bitangent);

            float hPdf = 0.0f;
            const float u1Remapped = u1 / vecMax2(fresnel, 1.0e-8f);
            const Vec3 hLocal = brdfSampleGGXHalfLocal(alpha, u1Remapped, u2, hPdf);
            const Vec3 h = brdfLocalToWorld(hLocal, ctx.normal, tangent, bitangent);
            const float cosThetaHo = vecMax2(0.0f, vecDot3(h, ctx.wo));
            if (cosThetaHo <= 0.0f || hPdf <= BrdfDetail::kMinPdf) {
                return sampleSpecularReflection(ctx, fresnel);
            }

            result.direction = vecNormalize3(brdfReflect3(ctx.wo, h));
            const float cosWi = vecMax2(0.0f, vecDot3(ctx.normal, result.direction));
            if (cosWi <= 0.0f) {
                return sampleSpecularReflection(ctx, fresnel);
            }

            result.pdf = fresnel * hPdf / vecMax2(4.0f * cosThetaHo, 1.0e-8f);
            result.transmitted = false;
            result.valid = result.pdf > BrdfDetail::kMinPdf;
            if (!result.valid) {
                return sampleSpecularReflection(ctx, fresnel);
            }
            return result;
        }

        Vec3 refracted{};
        if (!brdfRefract3(ctx.wo, ctx.normal, eta, refracted)) {
            return sampleSpecularReflection(ctx, fresnel);
        }

        const float cosWi = vecAbs(vecDot3(ctx.normal, refracted));
        if (cosWi <= 0.0f) {
            return result;
        }

        result.direction = refracted;
        result.pdf = (1.0f - fresnel) / vecMax2(cosWi, 1.0e-8f);
        result.transmitted = true;
        result.valid = result.pdf > BrdfDetail::kMinPdf;
        return result;
    }

    GLASS_BRDF_FN float pdfImpl(const BrdfContext& ctx, Vec3 wi) const
    {
        const float cosWo = vecMax2(0.0f, vecDot3(ctx.normal, ctx.wo));
        if (cosWo <= 0.0f) {
            return 0.0f;
        }

        const float eta = ctx.material.ior;
        const float fresnel = brdfDielectricFresnel(cosWo, 1.0f, eta);
        const float alpha = brdfAlphaFromRoughness(ctx.material.roughness);
        float pdf = 0.0f;

        Vec3 refracted{};
        if (brdfRefract3(ctx.wo, ctx.normal, eta, refracted)) {
            if (vecDot3(wi, refracted) > 0.999f) {
                const float cosT = vecAbs(vecDot3(ctx.normal, refracted));
                pdf += (1.0f - fresnel) / vecMax2(cosT, 1.0e-8f);
            }
        }

        const float cosWi = vecMax2(0.0f, vecDot3(ctx.normal, wi));
        if (cosWi > 0.0f) {
            const Vec3 reflected = vecNormalize3(vecSub3(
                vecScale3(ctx.normal, 2.0f * vecDot3(ctx.normal, ctx.wo)),
                ctx.wo));
            if (vecDot3(wi, reflected) > 0.999f) {
                pdf += fresnel / vecMax2(cosWi, 1.0e-8f);
            } else {
                const Vec3 h = vecNormalize3(vecAdd3(wi, ctx.wo));
                const float cosThetaH = vecMax2(0.0f, vecDot3(ctx.normal, h));
                const float cosThetaHo = vecMax2(0.0f, vecDot3(h, ctx.wo));
                if (cosThetaHo > 0.0f) {
                    const float hPdf = brdfGGXD(cosThetaH, alpha) * cosThetaH;
                    pdf += fresnel * hPdf / vecMax2(4.0f * cosThetaHo, 1.0e-8f);
                }
            }
        }

        return pdf;
    }
};

#undef GLASS_BRDF_FN
