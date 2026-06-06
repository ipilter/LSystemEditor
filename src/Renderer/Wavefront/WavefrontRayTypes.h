#pragma once

#include <cstdint>

struct WavefrontRayGpu
{
    float roX = 0.0f;
    float roY = 0.0f;
    float roZ = 0.0f;
    float rdX = 0.0f;
    float rdY = 0.0f;
    float rdZ = 0.0f;
    int pixelIndex = -1;
    int rayKind = 0;
    int shadowSampleIndex = 0;
};

struct WavefrontShadowResultGpu
{
    int occluded = 0;
};

enum WavefrontRayKind : int
{
    WavefrontRayPrimary = 0,
    WavefrontRayShadow = 1
};

#if defined(__CUDACC__)
__device__ inline uint32_t wavefrontMorton2D(uint32_t x, uint32_t y)
{
    auto part1By1 = [](uint32_t n) -> uint32_t {
        n &= 0x0000ffffu;
        n = (n | (n << 8)) & 0x00ff00ffu;
        n = (n | (n << 4)) & 0x0f0f0f0fu;
        n = (n | (n << 2)) & 0x33333333u;
        n = (n | (n << 1)) & 0x55555555u;
        return n;
    };

    return (part1By1(y) << 1) | part1By1(x);
}

__device__ inline uint32_t wavefrontMortonFromPixelIndex(int pixelIndex, int width)
{
    if (width <= 0 || pixelIndex < 0) {
        return 0;
    }
    const uint32_t x = static_cast<uint32_t>(pixelIndex % width);
    const uint32_t y = static_cast<uint32_t>(pixelIndex / width);
    return wavefrontMorton2D(x, y);
}

__device__ inline uint32_t wavefrontShadowSortKey(float ox, float oy, float oz)
{
    const uint32_t x = static_cast<uint32_t>(__float_as_uint(ox) >> 16);
    const uint32_t y = static_cast<uint32_t>(__float_as_uint(oy) >> 16);
    const uint32_t z = static_cast<uint32_t>(__float_as_uint(oz) >> 16);
    return (x * 73856093u) ^ (y * 19349663u) ^ (z * 83492791u);
}
#endif
