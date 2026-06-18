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
    float* d_lumMean,
    float* d_m2,
    uint8_t* d_converged,
    int* d_activeIndices,
    int* d_activeScratch,
    int* d_activeCount,
    int width,
    int height,
    cudaStream_t stream);

bool pathTracerClearRegionAccumulator(
    float4* d_buffer,
    uint32_t* d_samples,
    float* d_lumMean,
    float* d_m2,
    uint8_t* d_converged,
    int* d_activeIndices,
    int* d_activeCount,
    int width,
    int height,
    int minX,
    int minY,
    int maxX,
    int maxY,
    cudaStream_t stream);

bool pathTracerRebuildActiveList(
    const uint8_t* d_converged,
    const uint32_t* d_counts,
    int* d_activeIndices,
    int* d_activeCount,
    int width,
    int height,
    int maxSamplesPerPixel,
    cudaStream_t stream);

bool pathTracerAdaptiveSample(
    float4* d_buffer,
    uint32_t* d_samples,
    float* d_lumMean,
    float* d_m2,
    uint8_t* d_converged,
    const int* d_activeIndices,
    int activeCount,
    int width,
    int height,
    const CameraGpu* d_camera,
    const MeshAccelSceneGpu* d_scene,
    const EnvironmentMapGpu* d_env,
    const RenderParamsGpu* d_params,
    unsigned int globalSeed,
    cudaStream_t stream);

bool pathTracerCompactActiveList(
    const int* d_activeIndicesIn,
    int* d_activeIndicesOut,
    int* d_activeCount,
    const uint8_t* d_converged,
    const uint32_t* d_counts,
    int maxSamplesPerPixel,
    int inCount,
    int width,
    int height,
    bool useRegionFilter,
    int regionMinX,
    int regionMinY,
    int regionMaxX,
    int regionMaxY,
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
    const uint8_t* converged,
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
