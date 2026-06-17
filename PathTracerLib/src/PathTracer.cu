#include "CameraDevice.cuh"
#include "MeshAccel/MeshAccelScene.cuh"
#include "PathIntegratorRandCore.h"
#include "PathTracerCuda.h"
#include "Spectral/SpectralState.h"

#include <cuda_runtime.h>
#include <vector_types.h>

#include <cstdint>

__constant__ SpectralStateGpu kSpectralState{};

__device__ Rgb2SpecGpu spectralActiveModel()
{
    Rgb2SpecGpu model{};
    model.scale = kSpectralState.rgb2specScale;
    model.data = kSpectralState.rgb2specData;
    model.res = kSpectralState.rgb2specRes;
    model.whiteNormR = kSpectralState.whiteNormR;
    model.whiteNormG = kSpectralState.whiteNormG;
    model.whiteNormB = kSpectralState.whiteNormB;
    return model;
}

extern "C" cudaError_t spectralUploadStateToSymbol(const SpectralStateGpu* hostState)
{
    if (hostState == nullptr) {
        return cudaErrorInvalidValue;
    }
    return cudaMemcpyToSymbol(kSpectralState, hostState, sizeof(SpectralStateGpu));
}

namespace {

__device__ Vec3 float3ToVec3(float3 v)
{
    return vecMake3(v.x, v.y, v.z);
}

__device__ Vec3 clampSampleFirefly(Vec3 radiance)
{
    if (!isfinite(radiance.x) || !isfinite(radiance.y) || !isfinite(radiance.z)) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    radiance = vecMake3(
        vecMax2(0.0f, radiance.x),
        vecMax2(0.0f, radiance.y),
        vecMax2(0.0f, radiance.z));

    constexpr float kMaxLuminance = 100.0f;
    const float luminance = radiance.x * 0.2126f + radiance.y * 0.7152f + radiance.z * 0.0722f;
    if (luminance <= kMaxLuminance) {
        return radiance;
    }
    const float scale = kMaxLuminance / luminance;
    return vecMake3(radiance.x * scale, radiance.y * scale, radiance.z * scale);
}

__device__ void tracePixelRadiance(
    int idx,
    int x,
    int y,
    int width,
    int height,
    bool jitterSubpixel,
    int sampleIndex,
    const CameraGpu* camera,
    const MeshAccelSceneGpu* scene,
    const EnvironmentMapGpu* env,
    const RenderParamsGpu* params,
    unsigned int globalSeed,
    Vec3& outRadiance)
{
    curandState rng{};
    randInitState(&rng, idx, sampleIndex, globalSeed);

    const float pixelOffset = jitterSubpixel ? rand01(&rng) : 0.5f;
    const float u = (static_cast<float>(x) + pixelOffset) / static_cast<float>(width);
    const float v = (static_cast<float>(y) + pixelOffset) / static_cast<float>(height);

    float3 roFloat{};
    float3 rdFloat{};
    cameraPrimaryRay(camera, u, v, roFloat, rdFloat);

    outRadiance = clampSampleFirefly(tracePathRand(
        float3ToVec3(roFloat),
        float3ToVec3(rdFloat),
        scene,
        params,
        env,
        &rng));
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
    unsigned int globalSeed,
    bool previewPass)
{
    const int x = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    const int y = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
    if (x >= width || y >= height || camera == nullptr) {
        return;
    }

    const int idx = y * width + x;
    const int sampleIndex = previewPass ? 0 : static_cast<int>(counts[idx]);

    Vec3 radiance{};
    tracePixelRadiance(
        idx,
        x,
        y,
        width,
        height,
        !previewPass,
        sampleIndex,
        camera,
        scene,
        env,
        params,
        globalSeed,
        radiance);

    const float4 sample = make_float4(radiance.x, radiance.y, radiance.z, 1.0f);
    if (previewPass) {
        acc[idx] = sample;
        return;
    }

    const uint32_t n = counts[idx];
    const float4 prev = acc[idx];
    const float invN = 1.0f / static_cast<float>(n + 1);
    acc[idx] = make_float4(
        (prev.x * static_cast<float>(n) + sample.x) * invN,
        (prev.y * static_cast<float>(n) + sample.y) * invN,
        (prev.z * static_cast<float>(n) + sample.z) * invN,
        1.0f);
    counts[idx] = n + 1;
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

__device__ float3 backgroundRgb(const RenderParamsGpu* params)
{
    if (params == nullptr) {
        return make_float3(10.0f / 255.0f, 10.0f / 255.0f, 10.0f / 255.0f);
    }
    return make_float3(params->backgroundR, params->backgroundG, params->backgroundB);
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

__device__ uchar4 toneMapToUchar4(float3 rgb)
{
    const unsigned char r = static_cast<unsigned char>(fminf(fmaxf(rgb.x, 0.0f), 1.0f) * 255.0f);
    const unsigned char g = static_cast<unsigned char>(fminf(fmaxf(rgb.y, 0.0f), 1.0f) * 255.0f);
    const unsigned char b = static_cast<unsigned char>(fminf(fmaxf(rgb.z, 0.0f), 1.0f) * 255.0f);
    return make_uchar4(r, g, b, 255);
}

__device__ float3 readDisplayRgb(
    const float4* source,
    const uint32_t* counts,
    int sourceIndex,
    bool useAccumulator,
    const RenderParamsGpu* params)
{
    float3 rgb = backgroundRgb(params);
    if (source == nullptr) {
        return rgb;
    }

    if (useAccumulator) {
        if (counts != nullptr && counts[sourceIndex] > 0) {
            const float4 value = source[sourceIndex];
            rgb = make_float3(value.x, value.y, value.z);
        }
        return rgb;
    }

    const float4 value = source[sourceIndex];
    rgb = make_float3(value.x, value.y, value.z);
    return rgb;
}

__global__ void writeDisplayToPboKernel(
    const float4* source,
    const uint32_t* counts,
    uchar4* pbo,
    int outputWidth,
    int outputHeight,
    int sourceWidth,
    int sourceHeight,
    int downscale,
    bool useAccumulator,
    const RenderParamsGpu* params,
    float exposure)
{
    const int x = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    const int y = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
    if (x >= outputWidth || y >= outputHeight) {
        return;
    }

    int sourceIndex = y * outputWidth + x;
    if (downscale > 1 && sourceWidth > 0 && sourceHeight > 0) {
        const int clampedDownscale = downscale < 1 ? 1 : downscale;
        const int px = x / clampedDownscale;
        const int py = y / clampedDownscale;
        const int clampedPx = px < sourceWidth ? px : sourceWidth - 1;
        const int clampedPy = py < sourceHeight ? py : sourceHeight - 1;
        sourceIndex = clampedPy * sourceWidth + clampedPx;
    }

    float3 rgb = readDisplayRgb(source, counts, sourceIndex, useAccumulator, params);
    rgb = applyDisplayPipeline(rgb, exposure);
    pbo[y * outputWidth + x] = toneMapToUchar4(rgb);
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

bool launchSampleKernel(
    float4* d_buffer,
    uint32_t* d_samples,
    int width,
    int height,
    const CameraGpu* d_camera,
    const MeshAccelSceneGpu* d_scene,
    const EnvironmentMapGpu* d_env,
    const RenderParamsGpu* d_params,
    unsigned int globalSeed,
    bool previewPass,
    cudaStream_t stream)
{
    if (d_buffer == nullptr || d_camera == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (!previewPass && d_samples == nullptr) {
        return false;
    }

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
        globalSeed,
        previewPass);
    return checkLaunch(cudaSuccess);
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
    const CameraGpu* d_camera,
    const MeshAccelSceneGpu* d_scene,
    const EnvironmentMapGpu* d_env,
    const RenderParamsGpu* d_params,
    unsigned int globalSeed,
    cudaStream_t stream)
{
    return launchSampleKernel(
        d_buffer,
        d_samples,
        width,
        height,
        d_camera,
        d_scene,
        d_env,
        d_params,
        globalSeed,
        false,
        stream);
}

bool pathTracerPreviewSample(
    float4* d_buffer,
    int width,
    int height,
    const CameraGpu* d_camera,
    const MeshAccelSceneGpu* d_scene,
    const EnvironmentMapGpu* d_env,
    const RenderParamsGpu* d_params,
    unsigned int globalSeed,
    cudaStream_t stream)
{
    return launchSampleKernel(
        d_buffer,
        nullptr,
        width,
        height,
        d_camera,
        d_scene,
        d_env,
        d_params,
        globalSeed,
        true,
        stream);
}

bool pathTracerCopyToPbo(
    const float4* acc,
    const uint32_t* counts,
    uchar4* pbo,
    int width,
    int height,
    const RenderParamsGpu* d_params,
    float exposure,
    cudaStream_t stream)
{
    if (acc == nullptr || counts == nullptr || pbo == nullptr || d_params == nullptr || width <= 0 || height <= 0) {
        return false;
    }

    const dim3 block(16, 16);
    const dim3 grid = grid2d(width, height, block);
    writeDisplayToPboKernel<<<grid, block, 0, stream>>>(
        acc,
        counts,
        pbo,
        width,
        height,
        width,
        height,
        1,
        true,
        d_params,
        exposure);
    return checkLaunch(cudaSuccess);
}

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
    cudaStream_t stream)
{
    if (preview == nullptr || pbo == nullptr || d_params == nullptr || previewWidth <= 0 || previewHeight <= 0 ||
        fullWidth <= 0 || fullHeight <= 0) {
        return false;
    }

    const dim3 block(16, 16);
    const dim3 grid = grid2d(fullWidth, fullHeight, block);
    writeDisplayToPboKernel<<<grid, block, 0, stream>>>(
        preview,
        nullptr,
        pbo,
        fullWidth,
        fullHeight,
        previewWidth,
        previewHeight,
        downscale,
        false,
        d_params,
        exposure);
    return checkLaunch(cudaSuccess);
}
