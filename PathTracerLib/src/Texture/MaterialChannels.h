#pragma once

#include <cstdint>

enum class MaterialChannelId : uint8_t
{
    Albedo,
    Roughness,
    Metallic,
    Transmission,
    Thin,
    Ior,
    Subsurface,
    Emission,
    DiffuseRoughness,
    ScatterRadiusR,
    ScatterRadiusG,
    ScatterRadiusB,
    Specular,
    Count
};

enum class ChannelComposition : uint8_t
{
    Replace,
    Multiply,
    Add,
};

enum class ChannelValueKind : uint8_t
{
    Scalar,
    Rgb,
};

struct ChannelBinding
{
    uint32_t textureIndex = 0;
    ChannelComposition composition = ChannelComposition::Multiply;
};

struct ResolvedMaterial
{
    float r = 0.8f;
    float g = 0.8f;
    float b = 0.8f;
    float roughness = 0.5f;
    float metallic = 0.0f;
    float transmission = 0.0f;
    float thin = 0.0f;
    float ior = 1.5f;
    float subsurface = 0.0f;
    float emission = 0.0f;
    float diffuseRoughness = 0.5f;
    float scatterRadiusR = 0.0f;
    float scatterRadiusG = 0.0f;
    float scatterRadiusB = 0.0f;
    float specular = 1.0f;
};

#if defined(__CUDACC__)
#define MATERIAL_CHANNELS_FN __host__ __device__ inline
#else
#define MATERIAL_CHANNELS_FN inline
#endif

MATERIAL_CHANNELS_FN ChannelValueKind channelValueKind(MaterialChannelId id)
{
    return id == MaterialChannelId::Albedo ? ChannelValueKind::Rgb : ChannelValueKind::Scalar;
}

MATERIAL_CHANNELS_FN ChannelComposition channelDefaultComposition(
    MaterialChannelId id,
    float inlineScalar = 0.0f,
    uint32_t textureIndex = 0u)
{
    if (id == MaterialChannelId::Albedo) {
        return ChannelComposition::Replace;
    }
    if (textureIndex != 0u && inlineScalar == 0.0f) {
        return ChannelComposition::Replace;
    }
    return ChannelComposition::Multiply;
}

/**
 * Phase 3 hook: blend multiple resolved layers into one material.
 * Phase 1: single implicit base layer only.
 */
MATERIAL_CHANNELS_FN ResolvedMaterial blendLayers(
    const ResolvedMaterial* /*layers*/,
    uint32_t /*layerCount*/)
{
    return ResolvedMaterial{};
}

#undef MATERIAL_CHANNELS_FN
