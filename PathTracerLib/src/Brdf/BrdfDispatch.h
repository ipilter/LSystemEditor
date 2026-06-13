#pragma once

#include "DiffuseBrdf.h"

enum class BrdfType : int
{
    Diffuse = 0,
};

#if defined(__CUDACC__)
#define BRDF_DISPATCH_FN __host__ __device__ inline
#else
#define BRDF_DISPATCH_FN inline
#endif

BRDF_DISPATCH_FN BrdfType brdfForMaterial(const MaterialGpu& material)
{
    (void)material;
    return BrdfType::Diffuse;
}

BRDF_DISPATCH_FN Vec3 brdfEval(BrdfType type, const BrdfContext& ctx, Vec3 wi)
{
    switch (type) {
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
