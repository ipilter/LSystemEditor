#pragma once

#include "MeshAccel/MeshAccelTypes.h"

#include <cmath>
#include <cstring>
#include <cstddef>

inline TextureDescGpu packGridTexture(const float* params, const size_t paramCount)
{
    TextureDescGpu desc{};
    desc.kind = static_cast<uint32_t>(TextureKind::Grid2D);
    const float defaultFrequency = 8.0f;
    const float defaultThickness = 0.05f;
    float freqU = defaultFrequency;
    float freqV = defaultFrequency;
    float thickness = defaultThickness;

    if (paramCount == 7u) {
        freqU = params[6];
        freqV = params[6];
    } else if (paramCount == 8u) {
        freqU = params[6];
        freqV = params[6];
        thickness = params[7];
    } else if (paramCount == 9u) {
        freqU = params[6];
        freqV = params[7];
        thickness = params[8];
    } else if (paramCount >= 10u) {
        freqU = params[6];
        freqV = params[7];
        thickness = params[8];
    }

    desc.p0 = make_float4(freqU, freqV, thickness, 1.0f);
    desc.p1 = make_float4(params[0], params[1], params[2], 0.0f);
    desc.p2 = make_float4(params[3], params[4], params[5], 0.0f);
    return desc;
}

inline TextureDescGpu packStripeTexture(const float* params, const size_t paramCount)
{
    TextureDescGpu desc{};
    desc.kind = static_cast<uint32_t>(TextureKind::Stripe1D);
    const float onValue = paramCount > 2u ? params[2] : 1.0f;
    const float offValue = paramCount > 3u ? params[3] : 0.0f;
    desc.p0 = make_float4(params[0], params[1], onValue, offValue);
    return desc;
}

inline TextureDescGpu packNoiseTexture(const float* params, const size_t paramCount)
{
    TextureDescGpu desc{};
    desc.kind = static_cast<uint32_t>(TextureKind::Noise2D);
    const float scale = params[0];
    const float octaves = paramCount > 1u ? params[1] : 1.0f;
    const float seed = paramCount > 2u ? params[2] : 0.0f;
    const float minValue = paramCount > 3u ? params[3] : 0.0f;
    const float maxValue = paramCount > 4u ? params[4] : 1.0f;
    desc.p0 = make_float4(scale, octaves, seed, minValue);
    desc.p1 = make_float4(maxValue, 0.0f, 0.0f, 0.0f);
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
