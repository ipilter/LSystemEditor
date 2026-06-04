#pragma once

#include "CameraGpu.h"
#include "MeshAccel/MeshAccelTypes.h"

#include <cuda_runtime.h>
#include <vector_types.h>

#include <cstdint>

struct MeshAccelSceneGpu;

bool pathTracerClearAccumulator(
    float4* d_buffer,
    uint32_t* d_samples,
    int width,
    int height,
    float backgroundR,
    float backgroundG,
    float backgroundB,
    cudaStream_t stream);

bool pathTracerSample(
    float4* d_buffer,
    uint32_t* d_samples,
    int width,
    int height,
    int stride,
    const CameraGpu* d_camera,
    const MeshAccelSceneGpu* d_scene,
    const RenderParamsGpu* d_renderParams,
    int visualMode,
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
