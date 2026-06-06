#pragma once

#include "CameraGpu.h"
#include "MeshAccel/MeshAccelTypes.h"
#include "Wavefront/WavefrontGpuResources.h"

#include <cuda_runtime.h>
#include <vector_types.h>

#include <cstdint>

struct MeshAccelSceneGpu;

bool pathTracerSampleWavefront(
    float4* d_buffer,
    uint32_t* d_samples,
    int width,
    int height,
    int stride,
    const CameraGpu* d_camera,
    const MeshAccelSceneGpu* d_scene,
    const RenderParamsGpu* d_renderParams,
    const EnvironmentMapGpu* d_environmentMap,
    int visualMode,
    const uint32_t* sobolMatrices,
    const unsigned int* pixelScramble,
    int sobolDimensionCount,
    WavefrontGpuResources* wavefront,
    cudaStream_t stream);
