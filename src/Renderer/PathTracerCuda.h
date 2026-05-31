#pragma once

#include <cuda_runtime.h>
#include <vector_types.h>

#include <cstdint>

bool pathTracerSample(
    float4* d_buffer,
    uint32_t* d_samples,
    int width,
    int height,
    int stride,
    const uint32_t* sobolMatrices,
    const unsigned int* pixelScramble,
    int sobolDimensionCount,
    cudaStream_t stream);

bool pathTracerCopyToPbo(
    const float4* acc,
    uchar4* pbo,
    int width,
    int height,
    int stride,
    cudaStream_t stream);

bool pathTracerInitPixelScramble(
    unsigned int* scrambles,
    int count,
    unsigned int globalSeed,
    cudaStream_t stream);
