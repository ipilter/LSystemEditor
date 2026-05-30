#pragma once

#include <cstddef>

class CudaGlInterop
{
public:
    CudaGlInterop();
    ~CudaGlInterop();

    bool registerPbo(unsigned int pbo, std::size_t byteSize, int slotIndex);
    void unregisterAll();
    bool initPboRandomGray(int slotIndex, int width, int height, unsigned int seed);

private:
    void* m_resources[2] = {nullptr, nullptr};
    std::size_t m_byteSizes[2] = {0, 0};
};
