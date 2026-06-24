#pragma once

#include "MeshAccel/MeshAccelTypes.h"

#include <cstdint>
#include <vector>

struct BakedImage
{
    int width = 0;
    int height = 0;
    /** @brief RGBA8 row-major; alpha always 255 for color bakes. */
    std::vector<uint8_t> rgba;
};

enum class MaterialBakeChannel : uint8_t
{
    AlbedoRgb,
    Roughness,
    Metallic,
    EmissionRgb,
};

/** @brief Returns true when the material channel uses a procedural texture. */
bool materialChannelNeedsBake(
    const MaterialGpu& material,
    MaterialBakeChannel channel);

/** @brief Bakes one material channel from triangle UVs into an RGBA image. */
bool bakeMaterialChannel(
    const std::vector<TriangleGpu>& triangles,
    uint32_t materialIndex,
    const MaterialGpu& material,
    const std::vector<TextureDescGpu>& textures,
    MaterialBakeChannel channel,
    int resolution,
    BakedImage* outImage);

/** @brief Encodes RGBA8 image as PNG bytes (for glTF embedding). */
bool encodePngFromRgba(
    const BakedImage& image,
    std::vector<uint8_t>* outPngBytes);
