#include "CameraDevice.cuh"
#include "QmcSampler.cuh"
#include "SdfAccelScene.cuh"
#include "SdfVisualCore.h"

#include <cuda_runtime.h>
#include <thrust/device_ptr.h>
#include <thrust/sort.h>
#include <vector_types.h>

#include <cmath>
#include <cstdint>

namespace {

__device__ SdfFloat3 toSdfFloat3(float3 v)
{
    return SdfFloat3{v.x, v.y, v.z};
}

__device__ float3 toFloat3(SdfFloat3 v)
{
    return make_float3(v.x, v.y, v.z);
}

__device__ uint32_t expandBits16(uint32_t v)
{
    v = (v | (v << 8)) & 0x00FF00FFu;
    v = (v | (v << 4)) & 0x0F0F0F0Fu;
    v = (v | (v << 2)) & 0x33333333u;
    v = (v | (v << 1)) & 0x55555555u;
    return v;
}

__device__ uint32_t mortonCode2D(uint32_t x, uint32_t y)
{
    return expandBits16(x) | (expandBits16(y) << 1);
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
    const SdfAccelSceneGpu* scene,
    const SdfMarchParamsGpu* marchParams,
    const uint32_t* sobolMatrices,
    const unsigned int* pixelScramble,
    int sobolDimensionCount,
    int visualMode,
    int sdfTraversalMode)
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

    const SdfHit hit = sdfAccelRayMarch(
        toSdfFloat3(roFloat),
        toSdfFloat3(rdFloat),
        scene,
        marchParams,
        sdfTraversalMode);

    SdfFloat3 rgbSdf{};
    switch (visualMode) {
    case static_cast<int>(SdfDebugVisualMode::HitDistance):
        rgbSdf = distanceToHeatmap(hit.t, marchParams->maxDistance, hit.hit, marchParams);
        break;
    case static_cast<int>(SdfDebugVisualMode::Off):
        rgbSdf = normalToColor(hit.normal, hit.hit, marchParams);
        break;
    case static_cast<int>(SdfDebugVisualMode::StepCount):
    default:
        rgbSdf = stepsToHeatmap(hit.steps, marchParams->maxSteps, hit.hit, marchParams);
        break;
    }

    const float3 rgb = toFloat3(rgbSdf);
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
    const SdfAccelSceneGpu* scene,
    const SdfMarchParamsGpu* marchParams,
    const uint32_t* sobolMatrices,
    const unsigned int* pixelScramble,
    int sobolDimensionCount,
    int width,
    int height,
    int stride,
    int visualMode,
    int sdfTraversalMode)
{
    const int x = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    const int y = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
    if (x >= width || y >= height) {
        return;
    }

    if (camera == nullptr || scene == nullptr || marchParams == nullptr) {
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
        marchParams,
        sobolMatrices,
        pixelScramble,
        sobolDimensionCount,
        visualMode,
        sdfTraversalMode);
}

__global__ void raySortFillKernel(
    uint32_t* keys,
    uint32_t* indices,
    int width,
    int height)
{
    const int x = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    const int y = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
    if (x >= width || y >= height) {
        return;
    }

    const int idx = y * width + x;
    keys[idx] = mortonCode2D(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
    indices[idx] = static_cast<uint32_t>(idx);
}

__global__ void sampleKernelSorted(
    float4* acc,
    uint32_t* counts,
    const CameraGpu* camera,
    const SdfAccelSceneGpu* scene,
    const SdfMarchParamsGpu* marchParams,
    const uint32_t* sobolMatrices,
    const unsigned int* pixelScramble,
    const uint32_t* sortedIndices,
    int sobolDimensionCount,
    int width,
    int height,
    int stride,
    int visualMode,
    int sdfTraversalMode)
{
    const int rank = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    const int pixelCount = width * height;
    if (rank >= pixelCount) {
        return;
    }

    if (camera == nullptr || scene == nullptr || marchParams == nullptr || sortedIndices == nullptr) {
        return;
    }

    const int idx = static_cast<int>(sortedIndices[rank]);
    const int x = idx % width;
    const int y = idx / width;
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
        marchParams,
        sobolMatrices,
        pixelScramble,
        sobolDimensionCount,
        visualMode,
        sdfTraversalMode);
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

bool pathTracerFreeRaySortBuffers(uint32_t** keys, uint32_t** indices, int* pixelCount)
{
    if (keys == nullptr || indices == nullptr || pixelCount == nullptr) {
        return false;
    }

    if (*keys != nullptr) {
        cudaFree(*keys);
        *keys = nullptr;
    }
    if (*indices != nullptr) {
        cudaFree(*indices);
        *indices = nullptr;
    }
    *pixelCount = 0;
    return true;
}

bool pathTracerInitRaySortBuffers(
    uint32_t** keys,
    uint32_t** indices,
    int* pixelCount,
    int width,
    int height,
    cudaStream_t stream)
{
    if (keys == nullptr || indices == nullptr || pixelCount == nullptr || width <= 0 || height <= 0) {
        return false;
    }

    const int newCount = width * height;
    if (*pixelCount == newCount && *keys != nullptr && *indices != nullptr) {
        return true;
    }

    pathTracerFreeRaySortBuffers(keys, indices, pixelCount);

    const std::size_t bytes = static_cast<std::size_t>(newCount) * sizeof(uint32_t);
    if (cudaMalloc(keys, bytes) != cudaSuccess) {
        pathTracerFreeRaySortBuffers(keys, indices, pixelCount);
        return false;
    }
    if (cudaMalloc(indices, bytes) != cudaSuccess) {
        pathTracerFreeRaySortBuffers(keys, indices, pixelCount);
        return false;
    }

    *pixelCount = newCount;
    (void)stream;
    return true;
}

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
    const SdfAccelSceneGpu* d_scene,
    const SdfMarchParamsGpu* d_marchParams,
    int visualMode,
    int sdfTraversalMode,
    const uint32_t* sobolMatrices,
    const unsigned int* pixelScramble,
    int sobolDimensionCount,
    int enableRaySort,
    uint32_t* d_raySortKeys,
    uint32_t* d_raySortIndices,
    cudaStream_t stream)
{
    if (d_buffer == nullptr || d_samples == nullptr || d_camera == nullptr || d_scene == nullptr ||
        d_marchParams == nullptr || sobolMatrices == nullptr || pixelScramble == nullptr || width <= 0 ||
        height <= 0 || sobolDimensionCount <= 0) {
        return false;
    }

    const int clampedStride = stride < 1 ? 1 : stride;
    const dim3 block(16, 16);
    const dim3 grid = grid2d(width, height, block);
    const int pixelCount = width * height;
    const bool useRaySort =
        enableRaySort != 0 && d_raySortKeys != nullptr && d_raySortIndices != nullptr;

    if (useRaySort) {
        raySortFillKernel<<<grid, block, 0, stream>>>(d_raySortKeys, d_raySortIndices, width, height);
        if (!checkLaunch(cudaSuccess)) {
            return false;
        }

        thrust::device_ptr<uint32_t> keysPtr(d_raySortKeys);
        thrust::device_ptr<uint32_t> indicesPtr(d_raySortIndices);
        thrust::sort_by_key(keysPtr, keysPtr + pixelCount, indicesPtr);

        const int threadsPerBlock = 256;
        const int blocks = (pixelCount + threadsPerBlock - 1) / threadsPerBlock;
        sampleKernelSorted<<<blocks, threadsPerBlock, 0, stream>>>(
            d_buffer,
            d_samples,
            d_camera,
            d_scene,
            d_marchParams,
            sobolMatrices,
            pixelScramble,
            d_raySortIndices,
            sobolDimensionCount,
            width,
            height,
            clampedStride,
            visualMode,
            sdfTraversalMode);
        return checkLaunch(cudaSuccess);
    }

    sampleKernel<<<grid, block, 0, stream>>>(
        d_buffer,
        d_samples,
        d_camera,
        d_scene,
        d_marchParams,
        sobolMatrices,
        pixelScramble,
        sobolDimensionCount,
        width,
        height,
        clampedStride,
        visualMode,
        sdfTraversalMode);
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
