#include "WavefrontGpuResources.h"

#include <cuda_runtime.h>

#include <algorithm>

namespace {

bool allocateRayBuffers(WavefrontGpuResources* resources, int capacity)
{
    if (resources == nullptr || capacity <= 0) {
        return false;
    }

    const std::size_t rayBytes = static_cast<std::size_t>(capacity) * sizeof(WavefrontRayGpu);
    const std::size_t indexBytes = static_cast<std::size_t>(capacity) * sizeof(int);
    const std::size_t keyBytes = static_cast<std::size_t>(capacity) * sizeof(uint32_t);

    if (cudaMalloc(&resources->d_rays, rayBytes) != cudaSuccess) {
        return false;
    }
    if (cudaMalloc(&resources->d_raysSorted, rayBytes) != cudaSuccess) {
        return false;
    }
    if (cudaMalloc(&resources->d_sortKeys, keyBytes) != cudaSuccess) {
        return false;
    }
    if (cudaMalloc(&resources->d_sortKeysAlt, keyBytes) != cudaSuccess) {
        return false;
    }
    if (cudaMalloc(&resources->d_sortIndices, indexBytes) != cudaSuccess) {
        return false;
    }
    if (cudaMalloc(&resources->d_sortIndicesAlt, indexBytes) != cudaSuccess) {
        return false;
    }
    if (cudaMalloc(&resources->d_shadowOccluded, indexBytes) != cudaSuccess) {
        return false;
    }
    if (cudaMalloc(&resources->d_rayCount, sizeof(int)) != cudaSuccess) {
        return false;
    }

    resources->capacity = capacity;
    return true;
}

} // namespace

bool initWavefrontGpuResources(WavefrontGpuResources* resources, int width, int height)
{
    if (resources == nullptr || width <= 0 || height <= 0) {
        return false;
    }

    freeWavefrontGpuResources(resources);

    const int capacity = width * height * 8;
    return allocateRayBuffers(resources, capacity);
}

void freeWavefrontGpuResources(WavefrontGpuResources* resources)
{
    if (resources == nullptr) {
        return;
    }

    if (resources->d_rays != nullptr) {
        cudaFree(resources->d_rays);
        resources->d_rays = nullptr;
    }
    if (resources->d_raysSorted != nullptr) {
        cudaFree(resources->d_raysSorted);
        resources->d_raysSorted = nullptr;
    }
    if (resources->d_sortKeys != nullptr) {
        cudaFree(resources->d_sortKeys);
        resources->d_sortKeys = nullptr;
    }
    if (resources->d_sortKeysAlt != nullptr) {
        cudaFree(resources->d_sortKeysAlt);
        resources->d_sortKeysAlt = nullptr;
    }
    if (resources->d_sortIndices != nullptr) {
        cudaFree(resources->d_sortIndices);
        resources->d_sortIndices = nullptr;
    }
    if (resources->d_sortIndicesAlt != nullptr) {
        cudaFree(resources->d_sortIndicesAlt);
        resources->d_sortIndicesAlt = nullptr;
    }
    if (resources->d_shadowOccluded != nullptr) {
        cudaFree(resources->d_shadowOccluded);
        resources->d_shadowOccluded = nullptr;
    }
    if (resources->d_rayCount != nullptr) {
        cudaFree(resources->d_rayCount);
        resources->d_rayCount = nullptr;
    }
    if (resources->d_sortTemp != nullptr) {
        cudaFree(resources->d_sortTemp);
        resources->d_sortTemp = nullptr;
    }

    resources->sortTempBytes = 0;
    resources->capacity = 0;
}
