#include "MeshAccelTextureBake.h"

#include "Texture/ProceduralTexture.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr float kUvEpsilon = 1.0e-6f;

bool pointInTriangle2d(float u, float v, const Vec2& a, const Vec2& b, const Vec2& c, float* outW, float* outBaryU, float* outBaryV)
{
    const float denom = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
    if (std::fabs(denom) < kUvEpsilon) {
        return false;
    }

    const float w = ((b.y - c.y) * (u - c.x) + (c.x - b.x) * (v - c.y)) / denom;
    const float bu = ((c.y - a.y) * (u - c.x) + (a.x - c.x) * (v - c.y)) / denom;
    const float bv = 1.0f - w - bu;

    if (w < -kUvEpsilon || bu < -kUvEpsilon || bv < -kUvEpsilon) {
        return false;
    }

    if (outW != nullptr) {
        *outW = w;
    }
    if (outBaryU != nullptr) {
        *outBaryU = bu;
    }
    if (outBaryV != nullptr) {
        *outBaryV = bv;
    }
    return true;
}

uint32_t textureIndexForChannel(const MaterialGpu& material, MaterialBakeChannel channel)
{
    switch (channel) {
    case MaterialBakeChannel::AlbedoRgb:
        return material.albedoTex;
    case MaterialBakeChannel::Roughness:
        return material.roughnessTex;
    case MaterialBakeChannel::Metallic:
        return material.metallicTex;
    case MaterialBakeChannel::EmissionRgb:
        return material.emissionTex;
    }
    return 0u;
}

void computeUvBounds(
    const std::vector<TriangleGpu>& triangles,
    uint32_t materialIndex,
    float* outMinU,
    float* outMinV,
    float* outMaxU,
    float* outMaxV,
    bool* outFound)
{
    float minU = 0.0f;
    float minV = 0.0f;
    float maxU = 0.0f;
    float maxV = 0.0f;
    bool found = false;

    for (const TriangleGpu& tri : triangles) {
        if (tri.materialIndex != materialIndex) {
            continue;
        }

        const Vec2 uvs[3] = {tri.uv0, tri.uv1, tri.uv2};
        for (const Vec2& uv : uvs) {
            if (!found) {
                minU = maxU = uv.x;
                minV = maxV = uv.y;
                found = true;
            } else {
                minU = std::min(minU, uv.x);
                minV = std::min(minV, uv.y);
                maxU = std::max(maxU, uv.x);
                maxV = std::max(maxV, uv.y);
            }
        }
    }

    if (outMinU != nullptr) {
        *outMinU = minU;
    }
    if (outMinV != nullptr) {
        *outMinV = minV;
    }
    if (outMaxU != nullptr) {
        *outMaxU = maxU;
    }
    if (outMaxV != nullptr) {
        *outMaxV = maxV;
    }
    if (outFound != nullptr) {
        *outFound = found;
    }
}

void fillChannelPixel(
    const MaterialGpu& material,
    const std::vector<TextureDescGpu>& textures,
    MaterialBakeChannel channel,
    const TextureEvalContext& ctx,
    uint8_t* rgbaOut)
{
    const TextureDescGpu* bank = textures.empty() ? nullptr : textures.data();
    const uint32_t bankCount = static_cast<uint32_t>(textures.size());
    const ResolvedMaterial resolved = resolveMaterial(material, ctx, bank, bankCount);

    switch (channel) {
    case MaterialBakeChannel::AlbedoRgb:
        rgbaOut[0] = static_cast<uint8_t>(proceduralClamp01(resolved.r) * 255.0f);
        rgbaOut[1] = static_cast<uint8_t>(proceduralClamp01(resolved.g) * 255.0f);
        rgbaOut[2] = static_cast<uint8_t>(proceduralClamp01(resolved.b) * 255.0f);
        rgbaOut[3] = 255;
        break;
    case MaterialBakeChannel::Roughness: {
        const float value = proceduralClamp01(resolved.roughness);
        const uint8_t byte = static_cast<uint8_t>(value * 255.0f);
        rgbaOut[0] = rgbaOut[1] = rgbaOut[2] = byte;
        rgbaOut[3] = 255;
        break;
    }
    case MaterialBakeChannel::Metallic: {
        const float value = proceduralClamp01(resolved.metallic);
        const uint8_t byte = static_cast<uint8_t>(value * 255.0f);
        rgbaOut[0] = rgbaOut[1] = rgbaOut[2] = byte;
        rgbaOut[3] = 255;
        break;
    }
    case MaterialBakeChannel::EmissionRgb: {
        const float scale = std::max(resolved.emission, 0.0f);
        rgbaOut[0] = static_cast<uint8_t>(proceduralClamp01(resolved.r * scale) * 255.0f);
        rgbaOut[1] = static_cast<uint8_t>(proceduralClamp01(resolved.g * scale) * 255.0f);
        rgbaOut[2] = static_cast<uint8_t>(proceduralClamp01(resolved.b * scale) * 255.0f);
        rgbaOut[3] = 255;
        break;
    }
    }
}

struct StbWriteContext
{
    std::vector<uint8_t>* bytes = nullptr;
};

void stbWriteToVector(void* context, void* data, int size)
{
    auto* ctx = static_cast<StbWriteContext*>(context);
    if (ctx == nullptr || ctx->bytes == nullptr || data == nullptr || size <= 0) {
        return;
    }
    const auto* src = static_cast<const uint8_t*>(data);
    ctx->bytes->insert(ctx->bytes->end(), src, src + size);
}

} // namespace

bool materialChannelNeedsBake(const MaterialGpu& material, MaterialBakeChannel channel)
{
    return textureIndexForChannel(material, channel) != 0u;
}

bool bakeMaterialChannel(
    const std::vector<TriangleGpu>& triangles,
    uint32_t materialIndex,
    const MaterialGpu& material,
    const std::vector<TextureDescGpu>& textures,
    MaterialBakeChannel channel,
    int resolution,
    BakedImage* outImage)
{
    if (outImage == nullptr || resolution <= 0) {
        return false;
    }

    float minU = 0.0f;
    float minV = 0.0f;
    float maxU = 1.0f;
    float maxV = 1.0f;
    bool found = false;
    computeUvBounds(triangles, materialIndex, &minU, &minV, &maxU, &maxV, &found);
    if (!found) {
        return false;
    }

    const float spanU = std::max(maxU - minU, kUvEpsilon);
    const float spanV = std::max(maxV - minV, kUvEpsilon);

    outImage->width = resolution;
    outImage->height = resolution;
    outImage->rgba.assign(static_cast<size_t>(resolution * resolution * 4), 0);

    for (int y = 0; y < resolution; ++y) {
        for (int x = 0; x < resolution; ++x) {
            const float u = minU + (static_cast<float>(x) + 0.5f) / static_cast<float>(resolution) * spanU;
            const float v = minV + (static_cast<float>(y) + 0.5f) / static_cast<float>(resolution) * spanV;

            bool filled = false;
            for (const TriangleGpu& tri : triangles) {
                if (tri.materialIndex != materialIndex) {
                    continue;
                }

                float w = 0.0f;
                float bu = 0.0f;
                float bv = 0.0f;
                if (!pointInTriangle2d(u, v, tri.uv0, tri.uv1, tri.uv2, &w, &bu, &bv)) {
                    continue;
                }

                const float u1d = tri.uv0.x * w + tri.uv1.x * bu + tri.uv2.x * bv;
                TextureEvalContext ctx{Vec2{u, v}, u1d};
                fillChannelPixel(material, textures, channel, ctx, outImage->rgba.data() + (static_cast<size_t>(y * resolution + x) * 4));
                filled = true;
                break;
            }

            if (!filled) {
                uint8_t* pixel = outImage->rgba.data() + (static_cast<size_t>(y * resolution + x) * 4);
                pixel[0] = pixel[1] = pixel[2] = 0;
                pixel[3] = 0;
            }
        }
    }

    return true;
}

bool encodePngFromRgba(const BakedImage& image, std::vector<uint8_t>* outPngBytes)
{
    if (outPngBytes == nullptr || image.width <= 0 || image.height <= 0
        || image.rgba.size() < static_cast<size_t>(image.width * image.height * 4)) {
        return false;
    }

    outPngBytes->clear();
    StbWriteContext ctx{outPngBytes};
    const int ok = stbi_write_png_to_func(
        stbWriteToVector,
        &ctx,
        image.width,
        image.height,
        4,
        image.rgba.data(),
        image.width * 4);
    return ok != 0;
}
