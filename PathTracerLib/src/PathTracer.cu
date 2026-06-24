#include "CameraDevice.cuh"
#include "MeshAccel/MeshAccelScene.cuh"
#include "PathIntegratorCore.h"
#include "PathTracerAdaptiveSampling.h"
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

constexpr int kDebugOverlayAdaptiveSampling = 2;

__device__ Vec3 float3ToVec3(float3 v)
{
    return vecMake3(v.x, v.y, v.z);
}

__device__ float deviceSampleLuminance(float r, float g, float b)
{
    return r * 0.2126f + g * 0.7152f + b * 0.0722f;
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
    const float luminance = deviceSampleLuminance(radiance.x, radiance.y, radiance.z);
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

    const float jitterU = jitterSubpixel ? rand01(&rng) : 0.5f;
    const float jitterV = jitterSubpixel ? rand01(&rng) : 0.5f;
    const float u = (static_cast<float>(x) + jitterU) / static_cast<float>(width);
    const float v = (static_cast<float>(y) + jitterV) / static_cast<float>(height);

    float3 roFloat{};
    float3 rdFloat{};
    const float lensU1 = rand01(&rng);
    const float lensU2 = rand01(&rng);
    cameraPrimaryRaySampled(camera, u, v, lensU1, lensU2, roFloat, rdFloat);

    outRadiance = clampSampleFirefly(tracePathRand(
        float3ToVec3(roFloat),
        float3ToVec3(rdFloat),
        scene,
        params,
        env,
        &rng));
}

__device__ bool deviceIsPixelConverged(
    uint32_t sampleCount,
    float lumMean,
    float m2,
    int minSamples,
    float relativeErrorThreshold)
{
    if (sampleCount < static_cast<uint32_t>(minSamples)) {
        return false;
    }
    if (sampleCount == 0) {
        return false;
    }

    if (lumMean < kAdaptiveMinLuminanceForConvergence) {
        return false;
    }

    const float variance = m2 / static_cast<float>(sampleCount);
    const float stdDev = sqrtf(variance);
    const float denom = fmaxf(lumMean, kAdaptiveLuminanceEpsilon);
    const float relativeError = stdDev / denom;
    return relativeError < relativeErrorThreshold;
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

__global__ void sampleAdaptiveKernel(
    float4* acc,
    uint32_t* counts,
    float* lumMean,
    float* m2,
    uint8_t* converged,
    const int* activeIndices,
    int activeCount,
    const CameraGpu* camera,
    const MeshAccelSceneGpu* scene,
    const EnvironmentMapGpu* env,
    const RenderParamsGpu* params,
    int width,
    int height,
    unsigned int globalSeed)
{
    const int t = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    if (t >= activeCount || camera == nullptr || params == nullptr) {
        return;
    }

    const int idx = activeIndices[t];
    if (idx < 0 || converged[idx] != 0) {
        return;
    }

    const int x = idx % width;
    const int y = idx / width;
    const int sampleIndex = static_cast<int>(counts[idx]);

    Vec3 radiance{};
    tracePixelRadiance(
        idx,
        x,
        y,
        width,
        height,
        true,
        sampleIndex,
        camera,
        scene,
        env,
        params,
        globalSeed,
        radiance);

    const float4 sample = make_float4(radiance.x, radiance.y, radiance.z, 1.0f);
    const float sampleLum = deviceSampleLuminance(sample.x, sample.y, sample.z);

    const uint32_t n = counts[idx];
    const float prevLumMean = lumMean[idx];
    const float prevM2 = m2[idx];
    const float delta = sampleLum - prevLumMean;
    const float newLumMean = prevLumMean + delta / static_cast<float>(n + 1);
    const float newM2 = prevM2 + delta * (sampleLum - newLumMean);
    lumMean[idx] = newLumMean;
    m2[idx] = newM2;

    const float4 prev = acc[idx];
    const float invN = 1.0f / static_cast<float>(n + 1);
    acc[idx] = make_float4(
        (prev.x * static_cast<float>(n) + sample.x) * invN,
        (prev.y * static_cast<float>(n) + sample.y) * invN,
        (prev.z * static_cast<float>(n) + sample.z) * invN,
        1.0f);

    const uint32_t newCount = n + 1;
    counts[idx] = newCount;

    const int maxSamples = params->maxSamplesPerPixel;
    if (maxSamples > 0 && newCount >= static_cast<uint32_t>(maxSamples)) {
        converged[idx] = 1;
        return;
    }

    if ((newCount % static_cast<uint32_t>(kAdaptiveConvergenceCheckInterval)) != 0) {
        return;
    }

    if (deviceIsPixelConverged(
            newCount,
            newLumMean,
            newM2,
            params->minSamples,
            params->relativeErrorThreshold)) {
        converged[idx] = 1;
    }
}

__global__ void clearAccumulatorKernel(
    float4* acc,
    uint32_t* counts,
    float* lumMean,
    float* m2,
    uint8_t* converged,
    int* activeIndices,
    int* activeCount,
    int width,
    int height)
{
    const int x = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    const int y = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
    const int pixelCount = width * height;
    const int idx = y * width + x;

    if (x < width && y < height) {
        acc[idx] = make_float4(0.0f, 0.0f, 0.0f, 1.0f);
        counts[idx] = 0;
        lumMean[idx] = 0.0f;
        m2[idx] = 0.0f;
        converged[idx] = 0;
        activeIndices[idx] = idx;
    }

    if (idx == 0 && activeCount != nullptr) {
        *activeCount = pixelCount;
    }
}

__global__ void clearRegionAccumulatorKernel(
    float4* acc,
    uint32_t* counts,
    float* lumMean,
    float* m2,
    uint8_t* converged,
    int* activeIndices,
    int* activeCount,
    int width,
    int height,
    int minX,
    int minY,
    int maxX,
    int maxY)
{
    const int regionW = maxX - minX + 1;
    const int regionH = maxY - minY + 1;
    const int regionCount = regionW * regionH;
    const int t = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    if (t >= regionCount) {
        return;
    }

    const int localX = t % regionW;
    const int localY = t / regionW;
    const int x = minX + localX;
    const int y = minY + localY;
    const int idx = y * width + x;

    acc[idx] = make_float4(0.0f, 0.0f, 0.0f, 1.0f);
    counts[idx] = 0;
    lumMean[idx] = 0.0f;
    m2[idx] = 0.0f;
    converged[idx] = 0;
    activeIndices[t] = idx;

    if (t == 0 && activeCount != nullptr) {
        *activeCount = regionCount;
    }
}

__global__ void rebuildActiveListKernel(
    const uint8_t* converged,
    const uint32_t* counts,
    int* activeIndicesOut,
    int* activeCount,
    int width,
    int height,
    int maxSamplesPerPixel)
{
    const int idx = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    const int pixelCount = width * height;
    if (idx >= pixelCount) {
        return;
    }

    if (converged[idx] != 0) {
        return;
    }
    if (maxSamplesPerPixel > 0 && counts[idx] >= static_cast<uint32_t>(maxSamplesPerPixel)) {
        return;
    }

    const int slot = atomicAdd(activeCount, 1);
    activeIndicesOut[slot] = idx;
}

__global__ void compactActiveListKernel(
    const int* activeIndicesIn,
    int* activeIndicesOut,
    int* activeCount,
    const uint8_t* converged,
    const uint32_t* counts,
    int maxSamplesPerPixel,
    int inCount,
    int width,
    int height,
    bool useRegionFilter,
    int regionMinX,
    int regionMinY,
    int regionMaxX,
    int regionMaxY)
{
    const int tid = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    if (tid >= inCount) {
        return;
    }

    const int pixelIdx = activeIndicesIn[tid];
    if (converged[pixelIdx] != 0) {
        return;
    }
    if (maxSamplesPerPixel > 0 && counts[pixelIdx] >= static_cast<uint32_t>(maxSamplesPerPixel)) {
        return;
    }

    if (useRegionFilter) {
        const int x = pixelIdx % width;
        const int y = pixelIdx / width;
        if (x < regionMinX || x > regionMaxX || y < regionMinY || y > regionMaxY) {
            return;
        }
    }

    const int slot = atomicAdd(activeCount, 1);
    activeIndicesOut[slot] = pixelIdx;
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
    const uint8_t* converged,
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

    float3 rgb{};
    if (params != nullptr && params->debugOverlayMode == kDebugOverlayAdaptiveSampling) {
        if (useAccumulator && converged != nullptr) {
            if (converged[sourceIndex] != 0) {
                rgb = make_float3(0.0f, 0.35f, 0.0f);
            } else {
                rgb = make_float3(1.0f, 0.0f, 0.0f);
            }
        } else {
            rgb = make_float3(1.0f, 0.0f, 0.0f);
        }
        pbo[y * outputWidth + x] = toneMapToUchar4(rgb);
        return;
    }

    rgb = readDisplayRgb(source, counts, sourceIndex, useAccumulator, params);
    rgb = applyDisplayPipeline(rgb, exposure);
    pbo[y * outputWidth + x] = toneMapToUchar4(rgb);
}

__global__ void writeUvDebugToPboKernel(
    const CameraGpu* camera,
    const MeshAccelSceneGpu* scene,
    uchar4* pbo,
    int width,
    int height,
    const RenderParamsGpu* params)
{
    const int x = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    const int y = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
    if (x >= width || y >= height || camera == nullptr || scene == nullptr || pbo == nullptr) {
        return;
    }

    const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(width);
    const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(height);

    float3 roFloat{};
    float3 rdFloat{};
    cameraPrimaryRay(camera, u, v, roFloat, rdFloat);

    float3 rgb{};
    if (params != nullptr) {
        rgb = make_float3(params->backgroundR, params->backgroundG, params->backgroundB);
    }

    const MeshHit hit = meshAccelTraceRay(
        float3ToVec3(roFloat),
        float3ToVec3(rdFloat),
        scene,
        0.001f,
        1.0e30f);
    if (hit.hit) {
        rgb = make_float3(
            hit.uv.x - floorf(hit.uv.x),
            hit.uv.y - floorf(hit.uv.y),
            0.0f);
    }

    pbo[y * width + x] = toneMapToUchar4(rgb);
}

__global__ void writeNormalsDebugToPboKernel(
    const CameraGpu* camera,
    const MeshAccelSceneGpu* scene,
    uchar4* pbo,
    int width,
    int height,
    const RenderParamsGpu* params)
{
    const int x = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    const int y = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
    if (x >= width || y >= height || camera == nullptr || scene == nullptr || pbo == nullptr) {
        return;
    }

    const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(width);
    const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(height);

    float3 roFloat{};
    float3 rdFloat{};
    cameraPrimaryRay(camera, u, v, roFloat, rdFloat);

    float3 rgb{};
    if (params != nullptr) {
        rgb = make_float3(params->backgroundR, params->backgroundG, params->backgroundB);
    }

    const MeshHit hit = meshAccelTraceRay(
        float3ToVec3(roFloat),
        float3ToVec3(rdFloat),
        scene,
        0.001f,
        1.0e30f);
    if (hit.hit) {
        const Vec3 normal = hit.normal;
        rgb = make_float3(
            normal.x * 0.5f + 0.5f,
            normal.y * 0.5f + 0.5f,
            normal.z * 0.5f + 0.5f);
    }

    pbo[y * width + x] = toneMapToUchar4(rgb);
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
    float* d_lumMean,
    float* d_m2,
    uint8_t* d_converged,
    int* d_activeIndices,
    int* d_activeScratch,
    int* d_activeCount,
    int width,
    int height,
    cudaStream_t stream)
{
    (void)d_activeScratch;
    if (d_buffer == nullptr || d_samples == nullptr || d_lumMean == nullptr || d_m2 == nullptr ||
        d_converged == nullptr || d_activeIndices == nullptr || d_activeCount == nullptr || width <= 0 ||
        height <= 0) {
        return false;
    }

    const dim3 block(16, 16);
    const dim3 grid = grid2d(width, height, block);
    clearAccumulatorKernel<<<grid, block, 0, stream>>>(
        d_buffer,
        d_samples,
        d_lumMean,
        d_m2,
        d_converged,
        d_activeIndices,
        d_activeCount,
        width,
        height);
    return checkLaunch(cudaSuccess);
}

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
    cudaStream_t stream)
{
    if (d_buffer == nullptr || d_samples == nullptr || d_lumMean == nullptr || d_m2 == nullptr ||
        d_converged == nullptr || d_activeIndices == nullptr || d_activeCount == nullptr || width <= 0 ||
        height <= 0 || minX > maxX || minY > maxY) {
        return false;
    }

    const int regionW = maxX - minX + 1;
    const int regionH = maxY - minY + 1;
    const int regionCount = regionW * regionH;
    if (regionCount <= 0) {
        return false;
    }

    constexpr int kBlockSize = 256;
    const dim3 block(kBlockSize);
    const dim3 grid = grid1d(regionCount, kBlockSize);
    clearRegionAccumulatorKernel<<<grid, block, 0, stream>>>(
        d_buffer,
        d_samples,
        d_lumMean,
        d_m2,
        d_converged,
        d_activeIndices,
        d_activeCount,
        width,
        height,
        minX,
        minY,
        maxX,
        maxY);
    return checkLaunch(cudaSuccess);
}

bool pathTracerRebuildActiveList(
    const uint8_t* d_converged,
    const uint32_t* d_counts,
    int* d_activeIndices,
    int* d_activeCount,
    int width,
    int height,
    int maxSamplesPerPixel,
    cudaStream_t stream)
{
    if (d_converged == nullptr || d_counts == nullptr || d_activeIndices == nullptr || d_activeCount == nullptr ||
        width <= 0 || height <= 0) {
        return false;
    }

    const cudaError_t memsetError = cudaMemsetAsync(d_activeCount, 0, sizeof(int), stream);
    if (memsetError != cudaSuccess) {
        return false;
    }

    const int pixelCount = width * height;
    constexpr int kBlockSize = 256;
    const dim3 block(kBlockSize);
    const dim3 grid = grid1d(pixelCount, kBlockSize);
    rebuildActiveListKernel<<<grid, block, 0, stream>>>(
        d_converged,
        d_counts,
        d_activeIndices,
        d_activeCount,
        width,
        height,
        maxSamplesPerPixel);
    return checkLaunch(cudaSuccess);
}

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
    cudaStream_t stream)
{
    if (d_buffer == nullptr || d_samples == nullptr || d_lumMean == nullptr || d_m2 == nullptr ||
        d_converged == nullptr || d_activeIndices == nullptr || d_camera == nullptr || d_params == nullptr ||
        width <= 0 || height <= 0 || activeCount <= 0) {
        return false;
    }

    constexpr int kBlockSize = 256;
    const dim3 block(kBlockSize);
    const dim3 grid = grid1d(activeCount, kBlockSize);
    sampleAdaptiveKernel<<<grid, block, 0, stream>>>(
        d_buffer,
        d_samples,
        d_lumMean,
        d_m2,
        d_converged,
        d_activeIndices,
        activeCount,
        d_camera,
        d_scene,
        d_env,
        d_params,
        width,
        height,
        globalSeed);
    return checkLaunch(cudaSuccess);
}

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
    cudaStream_t stream)
{
    if (d_activeIndicesIn == nullptr || d_activeIndicesOut == nullptr || d_activeCount == nullptr ||
        d_converged == nullptr || d_counts == nullptr || inCount <= 0 || width <= 0 || height <= 0) {
        return false;
    }

    const cudaError_t memsetError = cudaMemsetAsync(d_activeCount, 0, sizeof(int), stream);
    if (memsetError != cudaSuccess) {
        return false;
    }

    constexpr int kBlockSize = 256;
    const dim3 block(kBlockSize);
    const dim3 grid = grid1d(inCount, kBlockSize);
    compactActiveListKernel<<<grid, block, 0, stream>>>(
        d_activeIndicesIn,
        d_activeIndicesOut,
        d_activeCount,
        d_converged,
        d_counts,
        maxSamplesPerPixel,
        inCount,
        width,
        height,
        useRegionFilter,
        regionMinX,
        regionMinY,
        regionMaxX,
        regionMaxY);
    return checkLaunch(cudaSuccess);
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
    const uint8_t* converged,
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
        converged,
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

bool pathTracerWriteUvDebugToPbo(
    const CameraGpu* d_camera,
    const MeshAccelSceneGpu* d_scene,
    uchar4* pbo,
    int width,
    int height,
    const RenderParamsGpu* d_params,
    cudaStream_t stream)
{
    if (d_camera == nullptr || d_scene == nullptr || pbo == nullptr || d_params == nullptr || width <= 0 ||
        height <= 0) {
        return false;
    }

    const dim3 block(16, 16);
    const dim3 grid = grid2d(width, height, block);
    writeUvDebugToPboKernel<<<grid, block, 0, stream>>>(d_camera, d_scene, pbo, width, height, d_params);
    return checkLaunch(cudaSuccess);
}

bool pathTracerWriteNormalsDebugToPbo(
    const CameraGpu* d_camera,
    const MeshAccelSceneGpu* d_scene,
    uchar4* pbo,
    int width,
    int height,
    const RenderParamsGpu* d_params,
    cudaStream_t stream)
{
    if (d_camera == nullptr || d_scene == nullptr || pbo == nullptr || d_params == nullptr || width <= 0 ||
        height <= 0) {
        return false;
    }

    const dim3 block(16, 16);
    const dim3 grid = grid2d(width, height, block);
    writeNormalsDebugToPboKernel<<<grid, block, 0, stream>>>(d_camera, d_scene, pbo, width, height, d_params);
    return checkLaunch(cudaSuccess);
}
