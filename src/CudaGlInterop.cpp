#include "CudaGlInterop.h"

#include <QtGui/qopengl.h>

#include <cuda_gl_interop.h>
#include <cuda_runtime.h>

namespace {

cudaGraphicsResource_t resourceAt(void* resource)
{
    return static_cast<cudaGraphicsResource_t>(resource);
}

} // namespace

bool cudaInitRandomGray(void* devicePointer, int width, int height, unsigned int seed);

CudaGlInterop::CudaGlInterop() = default;

CudaGlInterop::~CudaGlInterop()
{
    unregisterAll();
}

bool CudaGlInterop::registerPbo(unsigned int pbo, std::size_t byteSize, int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= 2 || pbo == 0 || byteSize == 0) {
        return false;
    }

    if (m_resources[slotIndex] != nullptr) {
        cudaGraphicsUnregisterResource(resourceAt(m_resources[slotIndex]));
        m_resources[slotIndex] = nullptr;
        m_byteSizes[slotIndex] = 0;
    }

    cudaFree(nullptr);

    cudaGraphicsResource_t resource = nullptr;
    const cudaError_t error = cudaGraphicsGLRegisterBuffer(
        &resource,
        static_cast<GLuint>(pbo),
        cudaGraphicsRegisterFlagsWriteDiscard);

    if (error != cudaSuccess) {
        return false;
    }

    m_resources[slotIndex] = resource;
    m_byteSizes[slotIndex] = byteSize;
    return true;
}

void CudaGlInterop::unregisterAll()
{
    for (int i = 0; i < 2; ++i) {
        if (m_resources[i] != nullptr) {
            cudaGraphicsUnregisterResource(resourceAt(m_resources[i]));
            m_resources[i] = nullptr;
            m_byteSizes[i] = 0;
        }
    }
}

bool CudaGlInterop::initPboRandomGray(int slotIndex, int width, int height, unsigned int seed)
{
    if (slotIndex < 0 || slotIndex >= 2 || m_resources[slotIndex] == nullptr || width <= 0 || height <= 0) {
        return false;
    }

    cudaGraphicsResource_t resource = resourceAt(m_resources[slotIndex]);

    cudaError_t error = cudaGraphicsMapResources(1, &resource, 0);
    if (error != cudaSuccess) {
        return false;
    }

    void* devicePointer = nullptr;
    std::size_t mappedSize = 0;
    error = cudaGraphicsResourceGetMappedPointer(&devicePointer, &mappedSize, resource);
    if (error != cudaSuccess) {
        cudaGraphicsUnmapResources(1, &resource, 0);
        return false;
    }

    const bool initialized = cudaInitRandomGray(devicePointer, width, height, seed);
    cudaGraphicsUnmapResources(1, &resource, 0);
    return initialized;
}
