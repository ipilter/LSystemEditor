#pragma once

#include "Material/MaterialParams.h"
#include "Path/PathState.h"

/**
 * Future hook: delta-tracking random walk for participating media.
 * v1 integrator does not call into volume transport yet.
 */
struct VolumeWalkState
{
    Vec3 position{};
    Vec3 direction{};
};

#if defined(__CUDACC__)
#define VOLUME_SCATTERING_FN __host__ __device__ inline
#else
#define VOLUME_SCATTERING_FN inline
#endif

VOLUME_SCATTERING_FN bool volumeTransportEnabled(const MaterialGpu& material)
{
    (void)material;
    return false;
}

#undef VOLUME_SCATTERING_FN
