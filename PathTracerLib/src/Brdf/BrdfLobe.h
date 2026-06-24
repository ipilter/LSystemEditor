#pragma once

#include "MeshAccel/MeshAccelTypes.h"

/**
 * Modular surface lobe weights. v1 uses diffuse only; future lobes plug in here.
 */
struct BrdfLobeWeights
{
    float diffuse = 1.0f;
    float specular = 0.0f;
    float transmission = 0.0f;
};

#if defined(__CUDACC__)
#define BRDF_LOBE_FN __host__ __device__ inline
#else
#define BRDF_LOBE_FN inline
#endif

BRDF_LOBE_FN BrdfLobeWeights computeSurfaceLobeWeights(const MaterialGpu& material)
{
    BrdfLobeWeights weights{};
    (void)material;
    weights.diffuse = 1.0f;
    return weights;
}

#undef BRDF_LOBE_FN
