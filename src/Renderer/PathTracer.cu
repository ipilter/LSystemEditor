#include <cuda_runtime.h>
#include <curand_kernel.h>
#include <vector_types.h>

#include <cstdint>
namespace {

__global__ void initRngKernel(curandState* states, int count, unsigned int seed)
{
    const int idx = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    if (idx >= count) {
        return;
    }
    curand_init(seed, idx, 0, &states[idx]);
}

__global__ void sampleKernel(float4* acc, uint32_t* counts, curandState* rng, int width, int height, int stride)
{
    const int x = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    const int y = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
    if (x >= width || y >= height) {
        return;
    }

    if (x % stride != 0 || y % stride != 0) {
        return;
    }

    const int idx = y * width + x;
    const float gray = curand_uniform(&rng[idx]);
    const float4 sample = make_float4(gray, gray, gray, 1.0f);
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
    const unsigned char c = static_cast<unsigned char>(fminf(v.x, 1.0f) * 255.0f);
    pbo[y * width + x] = make_uchar4(c, c, c, 255);
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

bool pathTracerInitRng(curandState* states, int count, unsigned int seed, cudaStream_t stream)
{
    if (states == nullptr || count <= 0) {
        return false;
    }

    const int blockSize = 256;
    const int gridSize = (count + blockSize - 1) / blockSize;
    initRngKernel<<<gridSize, blockSize, 0, stream>>>(states, count, seed);
    return checkLaunch(cudaSuccess);
}

bool pathTracerSample(
    float4* d_buffer,
    uint32_t* d_samples,
    int width,
    int height,
    int stride,
    curandState* rng,
    cudaStream_t stream)
{
    if (d_buffer == nullptr || d_samples == nullptr || rng == nullptr || width <= 0 || height <= 0) {
        return false;
    }

    const int clampedStride = stride < 1 ? 1 : stride;
    const dim3 block(16, 16);
    const dim3 grid = grid2d(width, height, block);
    sampleKernel<<<grid, block, 0, stream>>>(d_buffer, d_samples, rng, width, height, clampedStride);
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
