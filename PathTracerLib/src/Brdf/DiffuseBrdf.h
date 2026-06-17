#pragma once

#include "BrdfBase.h"

#if defined(__CUDACC__)
#define DIFFUSE_BRDF_FN __host__ __device__ inline
#else
#define DIFFUSE_BRDF_FN inline
#endif

struct DiffuseBrdf : BrdfBase<DiffuseBrdf>
{
    DIFFUSE_BRDF_FN Vec3 evalImpl(const BrdfContext& ctx, Vec3 wi) const
    {
        const float cosWi = vecMax2(0.0f, vecDot3(ctx.normal, wi));
        const float cosWo = vecMax2(0.0f, vecDot3(ctx.normal, ctx.wo));
        if (cosWi <= 0.0f || cosWo <= 0.0f) {
            return vecMake3(0.0f, 0.0f, 0.0f);
        }

        const Vec3 base = vecMake3(ctx.material.r, ctx.material.g, ctx.material.b);
        const float diffuseWeight = (1.0f - ctx.material.metallic) * BrdfDetail::kInvPi;
        return vecScale3(base, diffuseWeight);
    }

    DIFFUSE_BRDF_FN BrdfSampleResult sampleImpl(const BrdfContext& ctx, float u1, float u2) const
    {
        BrdfSampleResult result{};

        Vec3 tangent{};
        Vec3 bitangent{};
        brdfBuildBasis(ctx.normal, tangent, bitangent);

        const Vec3 local = brdfSampleCosineHemisphereLocal(u1, u2);
        result.direction = brdfLocalToWorld(local, ctx.normal, tangent, bitangent);
        result.pdf = pdfImpl(ctx, result.direction);
        result.valid = vecDot3(ctx.normal, result.direction) > 0.0f && result.pdf > BrdfDetail::kMinPdf;
        result.nextMediumEta = ctx.etaMedium;
        return result;
    }

    DIFFUSE_BRDF_FN float pdfImpl(const BrdfContext& ctx, Vec3 wi) const
    {
        return brdfCosineHemispherePdf(vecMax2(0.0f, vecDot3(ctx.normal, wi)));
    }
};

#undef DIFFUSE_BRDF_FN
