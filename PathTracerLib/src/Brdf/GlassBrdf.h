#pragma once

#include "BrdfBase.h"

#if defined(__CUDACC__)
#define GLASS_BRDF_FN __host__ __device__ inline
#else
#define GLASS_BRDF_FN inline
#endif

namespace GlassBrdfDetail {

GLASS_BRDF_FN float glassTransparency(const BrdfContext& ctx)
{
    return vecMax2(0.0f, vecMin2(1.0f, ctx.material.transparency));
}

GLASS_BRDF_FN Vec3 glassScalar(float value)
{
    return vecMake3(value, value, value);
}

} // namespace GlassBrdfDetail

struct GlassBrdf : BrdfBase<GlassBrdf>
{
    GLASS_BRDF_FN Vec3 mirrorReflectionDirection(const BrdfContext& ctx) const
    {
        return vecNormalize3(vecSub3(
            vecScale3(ctx.normal, 2.0f * vecDot3(ctx.normal, ctx.wo)),
            ctx.wo));
    }

    GLASS_BRDF_FN Vec3 transmissionDirection(const BrdfContext& ctx) const
    {
        return vecNormalize3(vecScale3(ctx.wo, -1.0f));
    }

    GLASS_BRDF_FN Vec3 evalImpl(const BrdfContext& ctx, Vec3 wi) const
    {
        const float cosWo = vecMax2(0.0f, vecDot3(ctx.normal, ctx.wo));
        if (cosWo <= 0.0f) {
            return vecMake3(0.0f, 0.0f, 0.0f);
        }

        const float transparency = GlassBrdfDetail::glassTransparency(ctx);
        const float reflectance = 1.0f - transparency;

        const Vec3 reflected = mirrorReflectionDirection(ctx);
        if (vecDot3(wi, reflected) > 0.999f) {
            const float cosWi = vecMax2(0.0f, vecDot3(ctx.normal, wi));
            return GlassBrdfDetail::glassScalar(reflectance / vecMax2(cosWi, 1.0e-8f));
        }

        const Vec3 transmitted = transmissionDirection(ctx);
        if (vecDot3(wi, transmitted) > 0.999f) {
            const float cosWi = vecAbs(vecDot3(ctx.normal, transmitted));
            return GlassBrdfDetail::glassScalar(transparency / vecMax2(cosWi, 1.0e-8f));
        }

        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    GLASS_BRDF_FN BrdfSampleResult sampleImpl(const BrdfContext& ctx, float u1, float u2) const
    {
        (void)u2;
        BrdfSampleResult result{};

        const float cosWo = vecMax2(0.0f, vecDot3(ctx.normal, ctx.wo));
        if (cosWo <= 0.0f) {
            return result;
        }

        const float transparency = GlassBrdfDetail::glassTransparency(ctx);
        const float reflectance = 1.0f - transparency;

        if (u1 < transparency) {
            result.direction = transmissionDirection(ctx);
            const float cosWi = vecAbs(vecDot3(ctx.normal, result.direction));
            if (cosWi <= 0.0f) {
                return result;
            }

            result.pdf = transparency / vecMax2(cosWi, 1.0e-8f);
            result.transmitted = true;
        } else {
            result.direction = mirrorReflectionDirection(ctx);
            const float cosWi = vecMax2(0.0f, vecDot3(ctx.normal, result.direction));
            if (cosWi <= 0.0f) {
                return result;
            }

            result.pdf = reflectance / vecMax2(cosWi, 1.0e-8f);
            result.transmitted = false;
        }

        result.valid = result.pdf > BrdfDetail::kMinPdf;
        result.nextMediumEta = ctx.etaMedium;
        return result;
    }

    GLASS_BRDF_FN float pdfImpl(const BrdfContext& ctx, Vec3 wi) const
    {
        const float cosWo = vecMax2(0.0f, vecDot3(ctx.normal, ctx.wo));
        if (cosWo <= 0.0f) {
            return 0.0f;
        }

        const float transparency = GlassBrdfDetail::glassTransparency(ctx);
        const float reflectance = 1.0f - transparency;
        float pdf = 0.0f;

        const Vec3 transmitted = transmissionDirection(ctx);
        if (vecDot3(wi, transmitted) > 0.999f) {
            const float cosWi = vecAbs(vecDot3(ctx.normal, transmitted));
            pdf += transparency / vecMax2(cosWi, 1.0e-8f);
        }

        const Vec3 reflected = mirrorReflectionDirection(ctx);
        if (vecDot3(wi, reflected) > 0.999f) {
            const float cosWi = vecMax2(0.0f, vecDot3(ctx.normal, wi));
            pdf += reflectance / vecMax2(cosWi, 1.0e-8f);
        }

        return pdf;
    }
};

#undef GLASS_BRDF_FN
