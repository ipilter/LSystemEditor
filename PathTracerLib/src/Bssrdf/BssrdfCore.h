#pragma once

#include "Material/MaterialType.h"
#include "MeshAccel/MeshAccelTypes.h"
#include "Path/PathState.h"

/**
 * Future hook: BSSRDF wrapper deciding surface BSDF vs volume entry.
 * v1 integrator uses surface Oren-Nayar only.
 */
struct BssrdfExitSample
{
    Vec3 exitPosition{};
    Vec3 exitNormal{};
    float pdf = 0.0f;
    bool valid = false;
};

#if defined(__CUDACC__)
#define BSSRDF_CORE_FN __host__ __device__ inline
#else
#define BSSRDF_CORE_FN inline
#endif

BSSRDF_CORE_FN bool bssrdfEnabled(const MaterialGpu& material)
{
    (void)material;
    return false;
}

#undef BSSRDF_CORE_FN
