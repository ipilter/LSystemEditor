#include "QmcGpuResources.h"

#include "PathTracerCuda.h"

#include <cuda_runtime.h>

#include <vector>

#include <QString>

namespace {

QString cudaErrorString(cudaError_t error)
{
    return QString::fromUtf8(cudaGetErrorString(error));
}

bool initSobolMatrices(QmcGpuResources* resources, QString* outError)
{
    if (resources == nullptr) {
        return false;
    }

    if (resources->sobolMatrices != nullptr) {
        return true;
    }

    const std::size_t matrixCount =
        static_cast<std::size_t>(kMaxSobolDimensions) * static_cast<std::size_t>(kSobolBits);
    std::vector<uint32_t> hostMatrices(matrixCount, 0u);
    if (!buildSobolMatricesHost(hostMatrices.data(), kMaxSobolDimensions)) {
        if (outError != nullptr) {
            *outError = QStringLiteral("Sobol matrix build failed");
        }
        return false;
    }

    const cudaError_t allocError = cudaMalloc(&resources->sobolMatrices, matrixCount * sizeof(uint32_t));
    if (allocError != cudaSuccess) {
        if (outError != nullptr) {
            *outError = cudaErrorString(allocError);
        }
        return false;
    }

    const cudaError_t copyError = cudaMemcpy(
        resources->sobolMatrices,
        hostMatrices.data(),
        matrixCount * sizeof(uint32_t),
        cudaMemcpyHostToDevice);
    if (copyError != cudaSuccess) {
        cudaFree(resources->sobolMatrices);
        resources->sobolMatrices = nullptr;
        if (outError != nullptr) {
            *outError = cudaErrorString(copyError);
        }
        return false;
    }

    return true;
}

} // namespace

void freeQmcGpuResources(QmcGpuResources* resources)
{
    if (resources == nullptr) {
        return;
    }

    if (resources->sobolMatrices != nullptr) {
        cudaFree(resources->sobolMatrices);
        resources->sobolMatrices = nullptr;
    }
    if (resources->pixelScramble != nullptr) {
        cudaFree(resources->pixelScramble);
        resources->pixelScramble = nullptr;
    }
    resources->scrambleCount = 0;
}

bool initQmcGpuResources(
    QmcGpuResources* resources,
    int width,
    int height,
    cudaStream_t stream,
    QString* outError)
{
    if (resources == nullptr || width <= 0 || height <= 0) {
        if (outError != nullptr) {
            *outError = QStringLiteral("invalid scramble dimensions");
        }
        return false;
    }

    if (resources->pixelScramble != nullptr) {
        cudaFree(resources->pixelScramble);
        resources->pixelScramble = nullptr;
        resources->scrambleCount = 0;
    }

    if (!initSobolMatrices(resources, outError)) {
        return false;
    }

    const int count = width * height;
    const cudaError_t allocError =
        cudaMalloc(&resources->pixelScramble, static_cast<std::size_t>(count) * sizeof(unsigned int));
    if (allocError != cudaSuccess) {
        if (outError != nullptr) {
            *outError = cudaErrorString(allocError);
        }
        return false;
    }

    resources->scrambleCount = count;
    if (!pathTracerInitPixelScramble(
            resources->pixelScramble,
            count,
            resources->scrambleSeed,
            stream)) {
        cudaFree(resources->pixelScramble);
        resources->pixelScramble = nullptr;
        resources->scrambleCount = 0;
        if (outError != nullptr) {
            *outError = QStringLiteral("pixel scramble kernel launch failed");
        }
        return false;
    }

    return true;
}

bool refreshPixelScramble(QmcGpuResources* resources, cudaStream_t stream)
{
    if (resources == nullptr || resources->pixelScramble == nullptr || resources->scrambleCount <= 0) {
        return false;
    }

    ++resources->scrambleSeed;
    return pathTracerInitPixelScramble(
        resources->pixelScramble,
        resources->scrambleCount,
        resources->scrambleSeed,
        stream);
}
