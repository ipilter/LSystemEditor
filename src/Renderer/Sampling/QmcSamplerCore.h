#pragma once

#include "QmcSampler.h"

#include <cmath>
#include <cstdint>

#if defined(__CUDACC__)
#define QMC_CORE_FN __host__ __device__ inline
#else
#define QMC_CORE_FN inline
#endif

QMC_CORE_FN uint32_t hashPixelScramble(int pixelIndex, unsigned int globalSeed)
{
    unsigned int hash = static_cast<unsigned int>(pixelIndex) ^ globalSeed;
    hash ^= hash >> 16;
    hash *= 0x7feb352du;
    hash ^= hash >> 15;
    hash *= 0x846ca68bu;
    hash ^= hash >> 16;
    return hash;
}

/// Evaluates Sobol(index, dimension) using nlopt bit-major layout.
QMC_CORE_FN uint32_t qmcSobolRaw(
    uint32_t index,
    int dimension,
    const uint32_t* sobolMatrices,
    int sobolDimensionCount)
{
    if (sobolMatrices == nullptr || dimension < 0 || dimension >= sobolDimensionCount) {
        return 0;
    }

    uint32_t result = 0;
    int bit = 0;
    while (index != 0) {
        if ((index & 1u) != 0) {
            result ^= sobolMatrices[static_cast<std::size_t>(bit) * static_cast<std::size_t>(sobolDimensionCount) +
                                    static_cast<std::size_t>(dimension)];
        }
        index >>= 1;
        ++bit;
    }
    return result;
}

QMC_CORE_FN float qmcRotationFromContext(const SampleContext& ctx)
{
    const uint32_t dimSeed =
        ctx.scramble ^ (static_cast<uint32_t>(ctx.dimension) * 0x9e3779b9u);
    return static_cast<float>(dimSeed) * (1.0f / 4294967296.0f);
}

/// Cranley-Patterson rotation of a Sobol sample; preserves 1D low-discrepancy per pixel.
QMC_CORE_FN float qmcNext1D(
    const SampleContext& ctx,
    const uint32_t* sobolMatrices,
    int sobolDimensionCount)
{
    const uint32_t raw = qmcSobolRaw(
        static_cast<uint32_t>(ctx.sampleIndex),
        ctx.dimension,
        sobolMatrices,
        sobolDimensionCount);

    const float u = static_cast<float>(raw) * (1.0f / 4294967296.0f);
    const float rotation = qmcRotationFromContext(ctx);
    float value = u + rotation;
#if defined(__CUDACC__)
    value -= floorf(value);
#else
    value -= std::floor(value);
#endif
    return value;
}

#undef QMC_CORE_FN
