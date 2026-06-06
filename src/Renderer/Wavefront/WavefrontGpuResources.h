#pragma once

#include "WavefrontRayTypes.h"

#include <cstddef>
#include <cstdint>

struct WavefrontGpuResources
{
    WavefrontRayGpu* d_rays = nullptr;
    WavefrontRayGpu* d_raysSorted = nullptr;
    uint32_t* d_sortKeys = nullptr;
    uint32_t* d_sortKeysAlt = nullptr;
    int* d_sortIndices = nullptr;
    int* d_sortIndicesAlt = nullptr;
    int* d_shadowOccluded = nullptr;
    int* d_rayCount = nullptr;

    void* d_sortTemp = nullptr;
    std::size_t sortTempBytes = 0;
    int capacity = 0;
};

bool initWavefrontGpuResources(WavefrontGpuResources* resources, int width, int height);
void freeWavefrontGpuResources(WavefrontGpuResources* resources);
