#pragma once

#include "CameraGpu.h"

#include <cuda_runtime.h>
#include <vector_types.h>

#include <cstdint>

bool pathTracerClearAccumulator(
    float4* d_buffer,
    uint32_t* d_samples,
    int width,
    int height,
    cudaStream_t stream);

bool pathTracerSample(
    float4* d_buffer,
    uint32_t* d_samples,
    int width,
    int height,
    int stride,
    const CameraGpu* d_camera,
    cudaStream_t stream);

bool pathTracerCopyToPbo(
    const float4* acc,
    const uint32_t* counts,
    uchar4* pbo,
    int width,
    int height,
    int stride,
    float backgroundR,
    float backgroundG,
    float backgroundB,
    cudaStream_t stream);
