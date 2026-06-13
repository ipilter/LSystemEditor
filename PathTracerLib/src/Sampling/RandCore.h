#pragma once

#include <curand_kernel.h>

#define RAND_CORE_FN __device__ __forceinline__

RAND_CORE_FN void randInitState(curandState* state, int pixelIndex, int sampleIndex, unsigned int globalSeed)
{
    const unsigned int seed = static_cast<unsigned int>(pixelIndex) * 747796405u + static_cast<unsigned int>(sampleIndex) +
        globalSeed * 2891336453u;
    curand_init(seed, 0, 0, state);
}

RAND_CORE_FN float rand01(curandState* state)
{
    return curand_uniform(state);
}

RAND_CORE_FN void rand02(curandState* state, float& u1, float& u2)
{
    u1 = curand_uniform(state);
    u2 = curand_uniform(state);
}

#undef RAND_CORE_FN
