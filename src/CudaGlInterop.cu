#include <cuda_runtime.h>

namespace {

constexpr int kGrayMin = 32;
constexpr int kGrayMax = 224;

__global__ void initRandomGrayKernel(uchar4* pixels, int width, int height, unsigned int seed)
{
    const int x = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    const int y = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
    if (x >= width || y >= height) {
        return;
    }

    unsigned int hash = static_cast<unsigned int>(x + y * width) * 747796405u + seed;
    hash ^= hash >> 16;
    hash *= 2246822507u;
    hash ^= hash >> 13;

    const int range = kGrayMax - kGrayMin + 1;
    const unsigned char gray = static_cast<unsigned char>(kGrayMin + (hash % static_cast<unsigned int>(range)));
    pixels[y * width + x] = make_uchar4(gray, gray, gray, 255);
}

} // namespace

bool cudaInitRandomGray(void* devicePointer, int width, int height, unsigned int seed)
{
    if (devicePointer == nullptr || width <= 0 || height <= 0) {
        return false;
    }

    const dim3 block(16, 16);
    const dim3 grid(
        static_cast<unsigned int>((width + static_cast<int>(block.x) - 1) / static_cast<int>(block.x)),
        static_cast<unsigned int>((height + static_cast<int>(block.y) - 1) / static_cast<int>(block.y)));

    initRandomGrayKernel<<<grid, block>>>(
        static_cast<uchar4*>(devicePointer),
        width,
        height,
        seed);

    const cudaError_t launchError = cudaGetLastError();
    if (launchError != cudaSuccess) {
        return false;
    }

    return cudaDeviceSynchronize() == cudaSuccess;
}
