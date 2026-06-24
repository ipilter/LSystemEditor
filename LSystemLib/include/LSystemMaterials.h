#pragma once

#include <string>
#include <vector>

/** @brief Parsed procedural texture from `{Kind, ...}` blocks. */
struct TextureDef
{
    std::string kind;
    std::vector<float> params;
};

/** @brief One material channel: inline constant or procedural texture. */
struct MaterialChannel
{
    enum class Mode
    {
        Inline,
        Texture,
    };

    Mode mode = Mode::Inline;
    float scalar = 0.f;
    float r = 0.f;
    float g = 0.f;
    float b = 0.f;
    TextureDef texture;
};

/** @brief Parsed material properties from `Mat(id) = { ... }` lines. */
struct MaterialEntry
{
    /** @brief Material type name: Opaque, Glass, Subsurface, Emissive. */
    std::string typeName = "Opaque";
    MaterialChannel albedo;
    MaterialChannel roughness;
    MaterialChannel metallic;
    MaterialChannel diffuseRoughness;
    MaterialChannel specular;
    MaterialChannel emission;
    MaterialChannel subsurface;
    MaterialChannel subsurfaceRadius;
    MaterialChannel sigmaA;
    MaterialChannel sigmaS;
    MaterialChannel mediumG;
    MaterialChannel ior;
    MaterialChannel abbe;
};

/** @brief One material definition collected during L-system parse. */
struct MaterialDefinition
{
    /** @brief Material name or numeric id as string (e.g. `"Glass"`, `"0"`). */
    std::string id;
    MaterialEntry entry;
};

inline float materialChannelScalar(const MaterialChannel& channel, float defaultValue = 0.f)
{
    return channel.mode == MaterialChannel::Mode::Inline ? channel.scalar : defaultValue;
}

inline float materialChannelR(const MaterialChannel& channel, float defaultValue = 0.8f)
{
    return channel.mode == MaterialChannel::Mode::Inline ? channel.r : defaultValue;
}

inline float materialChannelG(const MaterialChannel& channel, float defaultValue = 0.8f)
{
    return channel.mode == MaterialChannel::Mode::Inline ? channel.g : defaultValue;
}

inline float materialChannelB(const MaterialChannel& channel, float defaultValue = 0.8f)
{
    return channel.mode == MaterialChannel::Mode::Inline ? channel.b : defaultValue;
}
