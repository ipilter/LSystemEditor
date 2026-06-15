#pragma once

#include "BrdfBase.h"

#if defined(__CUDACC__)
#define METAL_BRDF_FN __host__ __device__ inline
#else
#define METAL_BRDF_FN inline
#endif

struct MetalBrdf : BrdfBase<MetalBrdf>
{
    METAL_BRDF_FN Vec3 evalImpl(const BrdfContext& ctx, Vec3 wi) const
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

        const float alpha = brdfAlphaFromRoughness(ctx.material.roughness);
        const float D = brdfGGXD(cosThetaH, alpha);
        const float G = brdfSmithG1(cosWo, alpha) * brdfSmithG1(cosWi, alpha);
        const Vec3 f0 = brdfBaseColor(ctx.material);
        const Vec3 F = brdfSchlickF(cosThetaHo, f0);
        const float denom = vecMax2(4.0f * cosWo * cosWi, 1.0e-8f);
        return vecScale3(F, (D * G) / denom);
    }

    METAL_BRDF_FN BrdfSampleResult sampleImpl(const BrdfContext& ctx, float u1, float u2) const
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

        result.direction = vecNormalize3(brdfReflect3(ctx.wo, h));
        const float cosWi = vecMax2(0.0f, vecDot3(ctx.normal, result.direction));
        if (cosWi <= 0.0f) {
            return result;
        }

        result.pdf = hPdf / vecMax2(4.0f * cosThetaHo, 1.0e-8f);
        result.valid = result.pdf > BrdfDetail::kMinPdf;
        return result;
    }

    METAL_BRDF_FN float pdfImpl(const BrdfContext& ctx, Vec3 wi) const
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

        const float alpha = brdfAlphaFromRoughness(ctx.material.roughness);
        const float hPdf = brdfGGXD(cosThetaH, alpha) * cosThetaH;
        return hPdf / vecMax2(4.0f * cosThetaHo, 1.0e-8f);
    }
};

#undef METAL_BRDF_FN
