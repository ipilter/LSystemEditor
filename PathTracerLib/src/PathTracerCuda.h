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
    const CameraGpu* d_camera,
    const MeshAccelSceneGpu* d_scene,
    const EnvironmentMapGpu* d_env,
    const RenderParamsGpu* d_params,
    unsigned int globalSeed,
    cudaStream_t stream);

bool pathTracerPreviewSample(
    float4* d_buffer,
    int width,
    int height,
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
    const RenderParamsGpu* d_params,
    float exposure,
    cudaStream_t stream);

bool pathTracerUpsamplePreviewToPbo(
    const float4* preview,
    int previewWidth,
    int previewHeight,
    int downscale,
    uchar4* pbo,
    int fullWidth,
    int fullHeight,
    const RenderParamsGpu* d_params,
    float exposure,
    cudaStream_t stream);
