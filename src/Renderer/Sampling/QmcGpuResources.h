#pragma once

#include "SobolTables.h"

#include <cuda_runtime.h>

#include <cstdint>

class QString;

struct QmcGpuResources
{
    uint32_t* sobolMatrices = nullptr;
    unsigned int* pixelScramble = nullptr;
    int scrambleCount = 0;
    unsigned int scrambleSeed = 1u;
};

void freeQmcGpuResources(QmcGpuResources* resources);

bool initQmcGpuResources(
    QmcGpuResources* resources,
    int width,
    int height,
    cudaStream_t stream,
    QString* outError = nullptr);

bool refreshPixelScramble(QmcGpuResources* resources, cudaStream_t stream);
