#include "QmcSampler.cuh"

#include "CameraGpu.h"

#include <cuda_runtime.h>
#include <vector_types.h>

#include <cmath>
#include <cstdint>

namespace {

__device__ float3 rotateByQuat(float4 q, float3 v)
{
    const float3 u = make_float3(q.x, q.y, q.z);
    const float s = q.w;
    const float3 cross1 = make_float3(
        u.y * v.z - u.z * v.y,
        u.z * v.x - u.x * v.z,
        u.x * v.y - u.y * v.x);
    const float3 scaledV = make_float3(v.x * s, v.y * s, v.z * s);
    const float3 cross2 = make_float3(
        u.y * cross1.z - u.z * cross1.y,
        u.z * cross1.x - u.x * cross1.z,
        u.x * cross1.y - u.y * cross1.x);
    return make_float3(
        v.x + 2.0f * cross1.x * s + 2.0f * cross2.x,
        v.y + 2.0f * cross1.y * s + 2.0f * cross2.y,
        v.z + 2.0f * cross1.z * s + 2.0f * cross2.z);
}

__device__ float3 normalize3(float3 v)
{
    const float len = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len <= 0.0f) {
        return make_float3(0.0f, 0.0f, -1.0f);
    }
    const float invLen = 1.0f / len;
    return make_float3(v.x * invLen, v.y * invLen, v.z * invLen);
}

__device__ float3 cameraRayDirection(const CameraGpu* camera, float u, float v)
{
    const float ndcX = 2.0f * u - 1.0f;
    const float ndcY = 1.0f - 2.0f * v;

    const float tanHalf = tanf(camera->fovY * 0.5f);
    const float top = camera->nearPlane * tanHalf;
    const float right = top * camera->aspect;

    const float3 viewDir = make_float3(ndcX * right, ndcY * top, -camera->nearPlane);
    return normalize3(rotateByQuat(camera->orientation, viewDir));
}

__device__ float3 directionToColor(float3 dir)
{
    return make_float3(
        0.5f * (dir.x + 1.0f),
        0.5f * (dir.y + 1.0f),
        0.5f * (-dir.z + 1.0f));
}

__global__ void sampleKernel(
    float4* acc,
    uint32_t* counts,
    const CameraGpu* camera,
    const uint32_t* sobolMatrices,
    const unsigned int* pixelScramble,
    int sobolDimensionCount,
    int width,
    int height,
    int stride)
{
    const int x = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    const int y = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
    if (x >= width || y >= height) {
        return;
    }

    if (x % stride != 0 || y % stride != 0) {
        return;
    }

    if (camera == nullptr) {
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
    const float3 dir = cameraRayDirection(camera, u, v);
    const float3 rgb = directionToColor(dir);
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
    const uint32_t* sobolMatrices,
    const unsigned int* pixelScramble,
    int sobolDimensionCount,
    cudaStream_t stream)
{
    if (d_buffer == nullptr || d_samples == nullptr || d_camera == nullptr || sobolMatrices == nullptr ||
        pixelScramble == nullptr || width <= 0 || height <= 0 || sobolDimensionCount <= 0) {
        return false;
    }

    const int clampedStride = stride < 1 ? 1 : stride;
    const dim3 block(16, 16);
    const dim3 grid = grid2d(width, height, block);
    sampleKernel<<<grid, block, 0, stream>>>(
        d_buffer,
        d_samples,
        d_camera,
        sobolMatrices,
        pixelScramble,
        sobolDimensionCount,
        width,
        height,
        clampedStride);
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
