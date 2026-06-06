#include "WavefrontPathTracer.h"

#include "CameraDevice.cuh"
#include "MeshAccelScene.cuh"
#include "QmcSampler.cuh"
#include "PathIntegratorCore.h"
#include "RenderVisualCore.h"
#include "WavefrontGpuResources.h"
#include "WavefrontRayTypes.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cub/device/device_radix_sort.cuh>

#include <cuda_runtime.h>
#include <vector_types.h>

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

__global__ void resetRayCountKernel(int* rayCount)
{
    if (blockIdx.x == 0 && threadIdx.x == 0) {
        *rayCount = 0;
    }
}

__global__ void generatePrimaryRaysKernel(
    WavefrontRayGpu* rays,
    uint32_t* sortKeys,
    int* sortIndices,
    int* rayCount,
    int width,
    int height,
    int stride,
    const CameraGpu* camera,
    const uint32_t* sobolMatrices,
    const unsigned int* pixelScramble,
    int sobolDimensionCount,
    uint32_t* counts)
{
    const int x = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    const int y = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
    if (x >= width || y >= height || x % stride != 0 || y % stride != 0) {
        return;
    }

    const int pixelIndex = y * width + x;
    SampleContext ctx{};
    ctx.pixelIndex = pixelIndex;
    ctx.sampleIndex = static_cast<int>(counts[pixelIndex]);
    ctx.scramble = pixelScramble[pixelIndex];

    ctx.dimension = 0;
    const float jitterU = qmcNext1D(ctx, sobolMatrices, sobolDimensionCount);
    ctx.dimension = 1;
    const float jitterV = qmcNext1D(ctx, sobolMatrices, sobolDimensionCount);

    const float u = (static_cast<float>(x) + jitterU) / static_cast<float>(width);
    const float v = (static_cast<float>(y) + jitterV) / static_cast<float>(height);

    float3 roFloat{};
    float3 rdFloat{};
    cameraPrimaryRay(camera, u, v, roFloat, rdFloat);

    const int slot = atomicAdd(rayCount, 1);
    WavefrontRayGpu& ray = rays[slot];
    ray.roX = roFloat.x;
    ray.roY = roFloat.y;
    ray.roZ = roFloat.z;
    ray.rdX = rdFloat.x;
    ray.rdY = rdFloat.y;
    ray.rdZ = rdFloat.z;
    ray.pixelIndex = pixelIndex;
    ray.rayKind = WavefrontRayPrimary;
    sortKeys[slot] = wavefrontMortonFromPixelIndex(pixelIndex, width);
    sortIndices[slot] = slot;
}

__global__ void intersectPrimaryBatchKernel(
    const WavefrontRayGpu* sortedRays,
    MeshHit* primaryHits,
    int rayCount,
    const MeshAccelSceneGpu* scene)
{
    const int rayIndex = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    if (rayIndex >= rayCount) {
        return;
    }

    const WavefrontRayGpu& ray = sortedRays[rayIndex];
    if (ray.pixelIndex < 0) {
        return;
    }

    const Vec3 ro = vecMake3(ray.roX, ray.roY, ray.roZ);
    const Vec3 rd = vecMake3(ray.rdX, ray.rdY, ray.rdZ);
    const MeshHit hit = meshAccelTraceRay(ro, rd, scene, 0.0f, PathIntegratorDetail::kRayTMax);
    primaryHits[ray.pixelIndex] = hit;
}

__global__ void shadeAndAccumulateKernel(
    float4* acc,
    uint32_t* counts,
    int width,
    int height,
    int stride,
    const CameraGpu* camera,
    const MeshHit* primaryHits,
    const MeshAccelSceneGpu* scene,
    const RenderParamsGpu* renderParams,
    const EnvironmentMapGpu* environmentMap,
    const uint32_t* sobolMatrices,
    const unsigned int* pixelScramble,
    int sobolDimensionCount,
    int visualMode)
{
    const int x = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    const int y = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
    if (x >= width || y >= height || x % stride != 0 || y % stride != 0) {
        return;
    }

    const int idx = y * width + x;
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

    const Vec3 ro = toVec3(roFloat);
    const Vec3 rd = toVec3(rdFloat);
    const MeshHit hit = primaryHits[idx];

    Vec3 rgbVec{};
    switch (visualMode) {
    case static_cast<int>(RenderDebugVisualMode::Off):
        rgbVec = tracePathFromHit(
            hit,
            ro,
            rd,
            scene,
            renderParams,
            environmentMap,
            ctx,
            sobolMatrices,
            sobolDimensionCount);
        break;
    case static_cast<int>(RenderDebugVisualMode::Normals):
    default:
        rgbVec = normalToColor(hit.normal, hit.hit, renderParams);
        break;
    }

    const float3 rgb = toFloat3(rgbVec);
    const float4 prev = acc[idx];
    const float4 sample = make_float4(rgb.x, rgb.y, rgb.z, 1.0f);

    const float invN = 1.0f / static_cast<float>(n + 1);
    acc[idx] = make_float4(
        (prev.x * static_cast<float>(n) + sample.x) * invN,
        (prev.y * static_cast<float>(n) + sample.y) * invN,
        (prev.z * static_cast<float>(n) + sample.z) * invN,
        1.0f);
    counts[idx] = n + 1;
}

__global__ void clearPrimaryHitsKernel(MeshHit* primaryHits, int count)
{
    const int index = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    if (index >= count) {
        return;
    }

    primaryHits[index] = MeshHit{};
}

dim3 grid2d(int width, int height, dim3 block)
{
    return dim3(
        static_cast<unsigned int>((width + static_cast<int>(block.x) - 1) / static_cast<int>(block.x)),
        static_cast<unsigned int>((height + static_cast<int>(block.y) - 1) / static_cast<int>(block.y)));
}

dim3 grid1d(int count, int blockSize)
{
    return dim3(static_cast<unsigned int>((count + blockSize - 1) / blockSize));
}

__global__ void gatherSortedRaysKernel(
    const WavefrontRayGpu* rays,
    const int* sortedIndices,
    WavefrontRayGpu* sortedRays,
    int rayCount)
{
    const int index = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    if (index >= rayCount) {
        return;
    }
    sortedRays[index] = rays[sortedIndices[index]];
}

bool wavefrontEnsureSortTemp(WavefrontGpuResources* wavefront, int rayCount)
{
    if (wavefront == nullptr || rayCount <= 0) {
        return false;
    }

    std::size_t requiredBytes = 0;
    cub::DeviceRadixSort::SortPairs(
        nullptr,
        requiredBytes,
        wavefront->d_sortKeys,
        wavefront->d_sortKeysAlt,
        wavefront->d_sortIndices,
        wavefront->d_sortIndicesAlt,
        rayCount);

    if (requiredBytes <= wavefront->sortTempBytes && wavefront->d_sortTemp != nullptr) {
        return true;
    }

    if (wavefront->d_sortTemp != nullptr) {
        cudaFree(wavefront->d_sortTemp);
        wavefront->d_sortTemp = nullptr;
        wavefront->sortTempBytes = 0;
    }

    if (cudaMalloc(&wavefront->d_sortTemp, requiredBytes) != cudaSuccess) {
        return false;
    }

    wavefront->sortTempBytes = requiredBytes;
    return true;
}

bool sortAndGatherRays(
    WavefrontGpuResources* wavefront,
    WavefrontRayGpu* sortedRays,
    int rayCount,
    cudaStream_t stream)
{
    if (rayCount <= 0) {
        return true;
    }

    if (!wavefrontEnsureSortTemp(wavefront, rayCount)) {
        return false;
    }

    const cudaError_t sortError = cub::DeviceRadixSort::SortPairs(
        wavefront->d_sortTemp,
        wavefront->sortTempBytes,
        wavefront->d_sortKeys,
        wavefront->d_sortKeysAlt,
        wavefront->d_sortIndices,
        wavefront->d_sortIndicesAlt,
        rayCount,
        0,
        32,
        stream);
    if (sortError != cudaSuccess) {
        return false;
    }

    const int blockSize = 256;
    gatherSortedRaysKernel<<<grid1d(rayCount, blockSize), blockSize, 0, stream>>>(
        wavefront->d_rays,
        wavefront->d_sortIndicesAlt,
        sortedRays,
        rayCount);
    return cudaGetLastError() == cudaSuccess;
}

bool checkLaunch(cudaError_t error)
{
    if (error != cudaSuccess) {
        return false;
    }
    return cudaGetLastError() == cudaSuccess;
}

} // namespace

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
    cudaStream_t stream)
{
    if (d_buffer == nullptr || d_samples == nullptr || d_camera == nullptr || d_scene == nullptr ||
        d_renderParams == nullptr || sobolMatrices == nullptr || pixelScramble == nullptr ||
        wavefront == nullptr || wavefront->d_rays == nullptr || width <= 0 || height <= 0 ||
        sobolDimensionCount <= 0) {
        return false;
    }

    const int clampedStride = stride < 1 ? 1 : stride;
    const int pixelCount = width * height;
    const dim3 block(16, 16);
    const dim3 grid = grid2d(width, height, block);

    MeshHit* d_primaryHits = nullptr;
    if (cudaMalloc(&d_primaryHits, static_cast<std::size_t>(pixelCount) * sizeof(MeshHit)) != cudaSuccess) {
        return false;
    }

    const int clearBlock = 256;
    clearPrimaryHitsKernel<<<grid1d(pixelCount, clearBlock), clearBlock, 0, stream>>>(d_primaryHits, pixelCount);

    resetRayCountKernel<<<1, 1, 0, stream>>>(wavefront->d_rayCount);
    generatePrimaryRaysKernel<<<grid, block, 0, stream>>>(
        wavefront->d_rays,
        wavefront->d_sortKeys,
        wavefront->d_sortIndices,
        wavefront->d_rayCount,
        width,
        height,
        clampedStride,
        d_camera,
        sobolMatrices,
        pixelScramble,
        sobolDimensionCount,
        d_samples);

    int primaryRayCount = 0;
    if (cudaMemcpyAsync(&primaryRayCount, wavefront->d_rayCount, sizeof(int), cudaMemcpyDeviceToHost, stream) !=
            cudaSuccess ||
        cudaStreamSynchronize(stream) != cudaSuccess) {
        cudaFree(d_primaryHits);
        return false;
    }

    if (primaryRayCount > 0) {
        if (!sortAndGatherRays(wavefront, wavefront->d_raysSorted, primaryRayCount, stream)) {
            cudaFree(d_primaryHits);
            return false;
        }

        const int intersectBlock = 256;
        intersectPrimaryBatchKernel<<<grid1d(primaryRayCount, intersectBlock), intersectBlock, 0, stream>>>(
            wavefront->d_raysSorted,
            d_primaryHits,
            primaryRayCount,
            d_scene);
    }

    shadeAndAccumulateKernel<<<grid, block, 0, stream>>>(
        d_buffer,
        d_samples,
        width,
        height,
        clampedStride,
        d_camera,
        d_primaryHits,
        d_scene,
        d_renderParams,
        d_environmentMap,
        sobolMatrices,
        pixelScramble,
        sobolDimensionCount,
        visualMode);

    const bool ok = checkLaunch(cudaSuccess);
    cudaFree(d_primaryHits);
    return ok;
}
