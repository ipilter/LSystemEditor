#include "CameraDevice.cuh"
#include "QmcSampler.cuh"
#include "SdfRayMarcher.cuh"

#include <cuda_runtime.h>
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

__global__ void sampleKernel(
    float4* acc,
    uint32_t* counts,
    const CameraGpu* camera,
    const SdfSceneGpu* scene,
    const SdfMarchParamsGpu* marchParams,
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

    if (x % stride != 0 || y % stride != 0) {
        return;
    }

    if (camera == nullptr || scene == nullptr || marchParams == nullptr) {
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

    const SdfHit hit = sdfRayMarch(toSdfFloat3(roFloat), toSdfFloat3(rdFloat), scene, marchParams);

    SdfFloat3 rgbSdf{};
    switch (visualMode) {
    case static_cast<int>(SdfVisualMode::HitDistance):
        rgbSdf = distanceToHeatmap(hit.t, marchParams->maxDistance, hit.hit);
        break;
    case static_cast<int>(SdfVisualMode::Normals):
        rgbSdf = normalToColor(hit.normal, hit.hit);
        break;
    case static_cast<int>(SdfVisualMode::StepCount):
    default:
        rgbSdf = stepsToHeatmap(hit.steps, marchParams->maxSteps, hit.hit);
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

bool pathTracerSample(
    float4* d_buffer,
    uint32_t* d_samples,
    int width,
    int height,
    int stride,
    const CameraGpu* d_camera,
    const SdfSceneGpu* d_scene,
    const SdfMarchParamsGpu* d_marchParams,
    int visualMode,
    const uint32_t* sobolMatrices,
    const unsigned int* pixelScramble,
    int sobolDimensionCount,
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
