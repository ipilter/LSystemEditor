#pragma once

#include "MeshAccel/MeshAccelTypes.h"

#include <cmath>

/**
 * Surface lobe weights for discrete path sampling.
 * diffuse + specular + subsurface sum to 1.
 */
struct BrdfLobeWeights
{
    float diffuse = 1.0f;
    float specular = 0.0f;
    float transmission = 0.0f;
    float subsurface = 0.0f;
};

#if defined(__CUDACC__)
#define BRDF_LOBE_FN __host__ __device__ inline
#else
#define BRDF_LOBE_FN inline
#endif

BRDF_LOBE_FN BrdfLobeWeights computeSurfaceLobeWeights(const MaterialGpu& material)
{
    BrdfLobeWeights weights{};
    const float sub = vecMin2(1.0f, vecMax2(0.0f, material.subsurface));
    const float surface = 1.0f - sub;
    const float metallic = vecMin2(1.0f, vecMax2(0.0f, material.metallic));
    const float wDiffuse = 1.0f - metallic;
    const float wSpecular = 1.0f;
    const float invSurfaceSum = 1.0f / vecMax2(wDiffuse + wSpecular, 1.0e-8f);

    weights.subsurface = sub;
    weights.diffuse = surface * wDiffuse * invSurfaceSum;
    weights.specular = surface * wSpecular * invSurfaceSum;
    return weights;
}

/** @brief Normalized diffuse/specular weights within the surface portion (excludes subsurface). */
BRDF_LOBE_FN BrdfLobeWeights computeReflectLobeWeights(const MaterialGpu& material)
{
    BrdfLobeWeights weights{};
    const float metallic = vecMin2(1.0f, vecMax2(0.0f, material.metallic));
    const float wDiffuse = 1.0f - metallic;
    const float wSpecular = 1.0f;
    const float invSum = 1.0f / vecMax2(wDiffuse + wSpecular, 1.0e-8f);
    weights.diffuse = wDiffuse * invSum;
    weights.specular = wSpecular * invSum;
    return weights;
}

#undef BRDF_LOBE_FN
