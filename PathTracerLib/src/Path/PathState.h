#pragma once

#include "Brdf/BrdfBase.h"
#include "Spectral/SpectralCore.h"

/** @brief Hybrid RGB path + hero-wavelength subsurface transport state. */
struct PathState
{
    Vec3 throughput = vecMake3(1.0f, 1.0f, 1.0f);
    float sssThroughput = 1.0f;
    float wavelengthNm = 550.0f;
    float wavelengthPdf = 1.0f;
};

using PathSpectralState = PathState;

#if defined(__CUDACC__)
#define PATH_STATE_FN __device__ __forceinline__
#else
#define PATH_STATE_FN inline
#endif

PATH_STATE_FN Vec3 pathStateCombineRadiance(Vec3 rgbRadiance, float sssRadiance, const PathState& path)
{
    const Vec3 sssRgb = spectralToRgb(sssRadiance, path.wavelengthNm, path.wavelengthPdf);
    return vecAdd3(rgbRadiance, sssRgb);
}

#if defined(__CUDACC__)

#include "Sampling/RandCore.h"

PATH_STATE_FN void pathStateInitSpectral(PathState& path, curandState* rng)
{
    path.throughput = vecMake3(1.0f, 1.0f, 1.0f);
    path.sssThroughput = 1.0f;
    spectralSampleWavelength(rand01(rng), path.wavelengthNm, path.wavelengthPdf);
}

#endif

#undef PATH_STATE_FN
