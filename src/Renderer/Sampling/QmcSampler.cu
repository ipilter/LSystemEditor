#include "QmcSampler.h"
#include "QmcSamplerCore.h"
#include "PathTracerCuda.h"

#include <cuda_runtime.h>

namespace {

__global__ void initPixelScrambleKernel(unsigned int* scrambles, int count, unsigned int globalSeed)
{
    const int idx = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    if (idx >= count) {
        return;
    }

    scrambles[idx] = hashPixelScramble(idx, globalSeed);
}

bool checkLaunch()
{
    const cudaError_t error = cudaGetLastError();
    return error == cudaSuccess;
}

} // namespace

bool pathTracerInitPixelScramble(
    unsigned int* scrambles,
    int count,
    unsigned int globalSeed,
    cudaStream_t stream)
{
    if (scrambles == nullptr || count <= 0) {
        return false;
    }

    const int blockSize = 256;
    const int gridSize = (count + blockSize - 1) / blockSize;
    initPixelScrambleKernel<<<gridSize, blockSize, 0, stream>>>(scrambles, count, globalSeed);
    return checkLaunch();
}
