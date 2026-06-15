#pragma once

#include "DiffuseBrdf.h"
#include "GlassBrdf.h"
#include "MetalBrdf.h"

enum class BrdfType : int
{
    Diffuse = 0,
    Metal = 1,
    Glass = 2,
};

#if defined(__CUDACC__)
#define BRDF_DISPATCH_FN __host__ __device__ inline
#else
#define BRDF_DISPATCH_FN inline
#endif

BRDF_DISPATCH_FN BrdfType brdfForMaterial(const MaterialGpu& material)
{
    switch (material.kind) {
    case 1:
        return BrdfType::Metal;
    case 2:
        return BrdfType::Glass;
    default:
        return BrdfType::Diffuse;
    }
}

BRDF_DISPATCH_FN Vec3 brdfEval(BrdfType type, const BrdfContext& ctx, Vec3 wi)
{
    switch (type) {
    case BrdfType::Metal: {
        const MetalBrdf brdf{};
        return brdf.eval(ctx, wi);
    }
    case BrdfType::Glass: {
        const GlassBrdf brdf{};
        return brdf.eval(ctx, wi);
    }
    case BrdfType::Diffuse:
    default: {
        const DiffuseBrdf brdf{};
        return brdf.eval(ctx, wi);
    }
    }
}

BRDF_DISPATCH_FN BrdfSampleResult brdfSample(BrdfType type, const BrdfContext& ctx, float u1, float u2)
{
    switch (type) {
    case BrdfType::Metal: {
        const MetalBrdf brdf{};
        return brdf.sample(ctx, u1, u2);
    }
    case BrdfType::Glass: {
        const GlassBrdf brdf{};
        return brdf.sample(ctx, u1, u2);
    }
    case BrdfType::Diffuse:
    default: {
        const DiffuseBrdf brdf{};
        return brdf.sample(ctx, u1, u2);
    }
    }
}

BRDF_DISPATCH_FN float brdfPdf(BrdfType type, const BrdfContext& ctx, Vec3 wi)
{
    switch (type) {
    case BrdfType::Metal: {
        const MetalBrdf brdf{};
        return brdf.pdf(ctx, wi);
    }
    case BrdfType::Glass: {
        const GlassBrdf brdf{};
        return brdf.pdf(ctx, wi);
    }
    case BrdfType::Diffuse:
    default: {
        const DiffuseBrdf brdf{};
        return brdf.pdf(ctx, wi);
    }
    }
}

BRDF_DISPATCH_FN float brdfThroughputLuminance(Vec3 throughput)
{
    const DiffuseBrdf brdf{};
    return brdf.luminance(throughput);
}

#undef BRDF_DISPATCH_FN
