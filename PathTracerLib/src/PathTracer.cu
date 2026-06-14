#include "CameraDevice.cuh"
#include "MeshAccel/MeshAccelScene.cuh"
#include "PathIntegratorRandCore.h"
#include "PathTracerCuda.h"

#include <cuda_runtime.h>
#include <vector_types.h>

#include <cstdint>

namespace {

__device__ Vec3 float3ToVec3(float3 v)
{
    return vecMake3(v.x, v.y, v.z);
}

__device__ Vec3 clampSampleFirefly(Vec3 radiance)
{
    constexpr float kMaxLuminance = 100.0f;
    const float luminance = radiance.x * 0.2126f + radiance.y * 0.7152f + radiance.z * 0.0722f;
    if (luminance <= kMaxLuminance) {
        return radiance;
    }
    const float scale = kMaxLuminance / luminance;
    return vecMake3(radiance.x * scale, radiance.y * scale, radiance.z * scale);
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
    const EnvironmentMapGpu* env,
    const RenderParamsGpu* params,
    unsigned int globalSeed)
{
    if (x % stride != 0 || y % stride != 0) {
        return;
    }

    const uint32_t n = counts[idx];
    const int sampleIndex = static_cast<int>(n);

    curandState rng{};
    randInitState(&rng, idx, sampleIndex, globalSeed);

    const float u = (static_cast<float>(x) + rand01(&rng)) / static_cast<float>(width);
    const float v = (static_cast<float>(y) + rand01(&rng)) / static_cast<float>(height);

    float3 roFloat{};
    float3 rdFloat{};
    cameraPrimaryRay(camera, u, v, roFloat, rdFloat);

    Vec3 radiance = tracePathRand(
        float3ToVec3(roFloat),
        float3ToVec3(rdFloat),
        scene,
        params,
        env,
        &rng);
    radiance = clampSampleFirefly(radiance);
    const float4 sample = make_float4(radiance.x, radiance.y, radiance.z, 1.0f);

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
    const EnvironmentMapGpu* env,
    const RenderParamsGpu* params,
    int width,
    int height,
    int stride,
    unsigned int globalSeed)
{
    const int x = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    const int y = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
    if (x >= width || y >= height) {
        return;
    }

    if (camera == nullptr) {
        return;
    }

    const int idx = y * width + x;
    samplePixel(idx, x, y, width, height, stride, acc, counts, camera, scene, env, params, globalSeed);
}

__global__ void clearAccumulatorKernel(float4* acc, uint32_t* counts, int width, int height)
{
    const int x = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    const int y = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
    if (x >= width || y >= height) {
        return;
    }

    const int idx = y * width + x;
    acc[idx] = make_float4(0.0f, 0.0f, 0.0f, 1.0f);
    counts[idx] = 0;
}

__device__ float3 applyDisplayPipeline(float3 rgb, float exposure)
{
    rgb = make_float3(rgb.x * exposure, rgb.y * exposure, rgb.z * exposure);
    rgb = make_float3(
        rgb.x / (1.0f + rgb.x),
        rgb.y / (1.0f + rgb.y),
        rgb.z / (1.0f + rgb.z));
    return rgb;
}

__global__ void copyToPboKernel(
    const float4* acc,
    const uint32_t* counts,
    uchar4* pbo,
    int width,
    int height,
    int stride,
    float backgroundR,
    float backgroundG,
    float backgroundB,
    float exposure)
{
    const int x = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    const int y = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
    if (x >= width || y >= height) {
        return;
    }

    const int ax = x - (x % stride);
    const int ay = y - (y % stride);
    const int aidx = ay * width + ax;

    float3 rgb = make_float3(backgroundR, backgroundG, backgroundB);
    if (counts != nullptr && counts[aidx] > 0) {
        const float4 v = acc[aidx];
        rgb = make_float3(v.x, v.y, v.z);
    }

    rgb = applyDisplayPipeline(rgb, exposure);

    const unsigned char r = static_cast<unsigned char>(fminf(fmaxf(rgb.x, 0.0f), 1.0f) * 255.0f);
    const unsigned char g = static_cast<unsigned char>(fminf(fmaxf(rgb.y, 0.0f), 1.0f) * 255.0f);
    const unsigned char b = static_cast<unsigned char>(fminf(fmaxf(rgb.z, 0.0f), 1.0f) * 255.0f);
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
    cudaStream_t stream)
{
    if (d_buffer == nullptr || d_samples == nullptr || width <= 0 || height <= 0) {
        return false;
    }

    const dim3 block(16, 16);
    const dim3 grid = grid2d(width, height, block);
    clearAccumulatorKernel<<<grid, block, 0, stream>>>(d_buffer, d_samples, width, height);
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
    const EnvironmentMapGpu* d_env,
    const RenderParamsGpu* d_params,
    unsigned int globalSeed,
    cudaStream_t stream)
{
    if (d_buffer == nullptr || d_samples == nullptr || d_camera == nullptr || width <= 0 || height <= 0) {
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
        d_env,
        d_params,
        width,
        height,
        clampedStride,
        globalSeed);
    return checkLaunch(cudaSuccess);
}

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
    float exposure,
    cudaStream_t stream)
{
    if (acc == nullptr || counts == nullptr || pbo == nullptr || width <= 0 || height <= 0) {
        return false;
    }

    const int clampedStride = stride < 1 ? 1 : stride;
    const dim3 block(16, 16);
    const dim3 grid = grid2d(width, height, block);
    copyToPboKernel<<<grid, block, 0, stream>>>(
        acc,
        counts,
        pbo,
        width,
        height,
        clampedStride,
        backgroundR,
        backgroundG,
        backgroundB,
        exposure);
    return checkLaunch(cudaSuccess);
}
