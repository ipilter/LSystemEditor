#pragma once

#include "CameraGpu.h"
#include "MeshAccel/MeshAccelTypes.h"
#include "RenderTypes.h"

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
    const MeshAccelSceneGpu* d_scene,
    const EnvironmentMapGpu* d_env,
    const RenderParamsGpu* d_params,
    unsigned int globalSeed,
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
