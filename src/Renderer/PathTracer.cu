#include "CameraDevice.cuh"
#include "MeshAccelScene.cuh"
#include "QmcSampler.cuh"
#include "RenderVisualCore.h"

#include <cuda_runtime.h>
#include <vector_types.h>

#include <cmath>
#include <cstdint>

namespace {

__device__ Vec3 toVec3(float3 v)
{
    return Vec3{v.x, v.y, v.z};
}

__device__ float3 toFloat3(Vec3 v)
{
    return make_float3(v.x, v.y, v.z);
}

__device__ void samplePixel(
    int idx,
    int x,
    int y,
    int width,
    int height,
    int stride,
    float4* acc,
    uint32_t* counts,
    const CameraGpu* camera,
    const MeshAccelSceneGpu* scene,
    const RenderParamsGpu* renderParams,
    const uint32_t* sobolMatrices,
    const unsigned int* pixelScramble,
    int sobolDimensionCount,
    int visualMode)
{
    if (x % stride != 0 || y % stride != 0) {
        return;
    }

    const uint32_t n = counts[idx];
    SampleContext ctx{};
    ctx.pixelIndex = idx;
    ctx.sampleIndex = static_cast<int>(n);
    ctx.scramble = pixelScramble[idx];

    ctx.dimension = 0;
    const float jitterU = qmcNext1D(ctx, sobolMatrices, sobolDimensionCount);
    ctx.dimension = 1;
    const float jitterV = qmcNext1D(ctx, sobolMatrices, sobolDimensionCount);

    const float u = (static_cast<float>(x) + jitterU) / static_cast<float>(width);
    const float v = (static_cast<float>(y) + jitterV) / static_cast<float>(height);

    float3 roFloat{};
    float3 rdFloat{};
    cameraPrimaryRay(camera, u, v, roFloat, rdFloat);

    const MeshHit hit = meshAccelTraceRay(
        toVec3(roFloat),
        toVec3(rdFloat),
        scene,
        0.0f,
        renderParams != nullptr ? renderParams->maxDistance : 100.0f);

    Vec3 rgbVec{};
    switch (visualMode) {
    case static_cast<int>(RenderDebugVisualMode::HitDistance):
        rgbVec = distanceToHeatmap(
            hit.t,
            renderParams != nullptr ? renderParams->maxDistance : 100.0f,
            hit.hit,
            renderParams);
        break;
    case static_cast<int>(RenderDebugVisualMode::Off):
    default:
        rgbVec = normalToColor(hit.normal, hit.hit, renderParams);
        break;
    }

    const float3 rgb = toFloat3(rgbVec);
    const float4 sample = make_float4(rgb.x, rgb.y, rgb.z, 1.0f);

    const float4 prev = acc[idx];
    const float invN = 1.0f / static_cast<float>(n + 1);
    acc[idx] = make_float4(
        (prev.x * static_cast<float>(n) + sample.x) * invN,
        (prev.y * static_cast<float>(n) + sample.y) * invN,
        (prev.z * static_cast<float>(n) + sample.z) * invN,
        1.0f);
    counts[idx] = n + 1;
}

__global__ void sampleKernel(
    float4* acc,
    uint32_t* counts,
    const CameraGpu* camera,
    const MeshAccelSceneGpu* scene,
    const RenderParamsGpu* renderParams,
    const uint32_t* sobolMatrices,
    const unsigned int* pixelScramble,
    int sobolDimensionCount,
    int width,
    int height,
    int stride,
    int visualMode)
{
    const int x = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    const int y = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
    if (x >= width || y >= height) {
        return;
    }

    if (camera == nullptr || scene == nullptr || renderParams == nullptr) {
        return;
    }

    const int idx = y * width + x;
    samplePixel(
        idx,
        x,
        y,
        width,
        height,
        stride,
        acc,
        counts,
        camera,
        scene,
        renderParams,
        sobolMatrices,
        pixelScramble,
        sobolDimensionCount,
        visualMode);
}

__global__ void clearAccumulatorKernel(float4* acc, uint32_t* counts, int width, int height, float4 background)
{
    const int x = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    const int y = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
    if (x >= width || y >= height) {
        return;
    }

    const int idx = y * width + x;
    acc[idx] = background;
    counts[idx] = 0;
}

__global__ void copyToPboKernel(const float4* acc, uchar4* pbo, int width, int height, int stride)
{
    const int x = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    const int y = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
    if (x >= width || y >= height) {
        return;
    }

    const int ax = x - (x % stride);
    const int ay = y - (y % stride);
    const int aidx = ay * width + ax;
    const float4 v = acc[aidx];
    const unsigned char r = static_cast<unsigned char>(fminf(fmaxf(v.x, 0.0f), 1.0f) * 255.0f);
    const unsigned char g = static_cast<unsigned char>(fminf(fmaxf(v.y, 0.0f), 1.0f) * 255.0f);
    const unsigned char b = static_cast<unsigned char>(fminf(fmaxf(v.z, 0.0f), 1.0f) * 255.0f);
    pbo[y * width + x] = make_uchar4(r, g, b, 255);
}

dim3 grid2d(int width, int height, dim3 block)
{
    return dim3(
        static_cast<unsigned int>((width + static_cast<int>(block.x) - 1) / static_cast<int>(block.x)),
        static_cast<unsigned int>((height + static_cast<int>(block.y) - 1) / static_cast<int>(block.y)));
}

bool checkLaunch(cudaError_t error)
{
    if (error != cudaSuccess) {
        return false;
    }
    return cudaGetLastError() == cudaSuccess;
}

} // namespace

bool pathTracerClearAccumulator(
    float4* d_buffer,
    uint32_t* d_samples,
    int width,
    int height,
    float backgroundR,
    float backgroundG,
    float backgroundB,
    cudaStream_t stream)
{
    if (d_buffer == nullptr || d_samples == nullptr || width <= 0 || height <= 0) {
        return false;
    }

    const float4 background = make_float4(backgroundR, backgroundG, backgroundB, 1.0f);
    const dim3 block(16, 16);
    const dim3 grid = grid2d(width, height, block);
    clearAccumulatorKernel<<<grid, block, 0, stream>>>(d_buffer, d_samples, width, height, background);
    return checkLaunch(cudaSuccess);
}

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
    cudaStream_t stream)
{
    if (d_buffer == nullptr || d_samples == nullptr || d_camera == nullptr || d_scene == nullptr ||
        d_renderParams == nullptr || sobolMatrices == nullptr || pixelScramble == nullptr || width <= 0 ||
        height <= 0 || sobolDimensionCount <= 0) {
        return false;
    }

    const int clampedStride = stride < 1 ? 1 : stride;
    const dim3 block(16, 16);
    const dim3 grid = grid2d(width, height, block);

    sampleKernel<<<grid, block, 0, stream>>>(
        d_buffer,
        d_samples,
        d_camera,
        d_scene,
        d_renderParams,
        sobolMatrices,
        pixelScramble,
        sobolDimensionCount,
        width,
        height,
        clampedStride,
        visualMode);
    return checkLaunch(cudaSuccess);
}

bool pathTracerCopyToPbo(const float4* acc, uchar4* pbo, int width, int height, int stride, cudaStream_t stream)
{
    if (acc == nullptr || pbo == nullptr || width <= 0 || height <= 0) {
        return false;
    }

    const int clampedStride = stride < 1 ? 1 : stride;
    const dim3 block(16, 16);
    const dim3 grid = grid2d(width, height, block);
    copyToPboKernel<<<grid, block, 0, stream>>>(acc, pbo, width, height, clampedStride);
    return checkLaunch(cudaSuccess);
}
