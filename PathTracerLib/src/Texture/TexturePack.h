#pragma once

#include "MeshAccel/MeshAccelTypes.h"

#include <cmath>
#include <cstring>
#include <cstddef>

inline void packOnOffIntensities(const float* params, TextureDescGpu& desc)
{
    desc.p1 = make_float4(params[0], params[1], params[2], params[6]);
    desc.p2 = make_float4(params[3], params[4], params[5], params[7]);
}

inline TextureDescGpu packGridTexture(const float* params, const size_t paramCount)
{
    TextureDescGpu desc{};
    desc.kind = static_cast<uint32_t>(TextureKind::Grid2D);
    const float defaultFrequency = 8.0f;
    const float defaultThickness = 0.05f;
    float freqU = defaultFrequency;
    float freqV = defaultFrequency;
    float thickness = defaultThickness;

    if (paramCount == 10u) {
        freqU = params[8];
        freqV = params[8];
        thickness = params[9];
    } else if (paramCount >= 11u) {
        freqU = params[8];
        freqV = params[9];
        thickness = params[10];
    }

    desc.p0 = make_float4(freqU, freqV, thickness, 0.0f);
    packOnOffIntensities(params, desc);
    return desc;
}

inline TextureDescGpu packStripeTexture(const float* params, const size_t paramCount)
{
    TextureDescGpu desc{};
    desc.kind = static_cast<uint32_t>(TextureKind::Stripe1D);
    const float defaultThickness = 0.05f;
    const float freq = params[8];
    const float thickness = paramCount > 9u ? params[9] : defaultThickness;
    desc.p0 = make_float4(freq, thickness, 0.0f, 0.0f);
    packOnOffIntensities(params, desc);
    return desc;
}

inline TextureDescGpu packNoiseTexture(const float* params, const size_t paramCount)
{
    TextureDescGpu desc{};
    desc.kind = static_cast<uint32_t>(TextureKind::Noise2D);
    const float scale = params[8];
    const float octaves = paramCount > 9u ? params[9] : 1.0f;
    const float seed = paramCount > 10u ? params[10] : 0.0f;
    desc.p0 = make_float4(scale, octaves, seed, 0.0f);
    packOnOffIntensities(params, desc);
    return desc;
}

inline TextureDescGpu packTextureDesc(const char* kind, const float* params, const size_t paramCount)
{
    if (std::strcmp(kind, "Grid") == 0) {
        return packGridTexture(params, paramCount);
    }
    if (std::strcmp(kind, "Noise") == 0) {
        return packNoiseTexture(params, paramCount);
    }
    return packStripeTexture(params, paramCount);
}
