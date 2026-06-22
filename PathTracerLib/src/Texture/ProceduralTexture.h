#pragma once

#include "Geometry/GeometryTypes.h"
#include "Geometry/MathCore.h"
#include "MeshAccel/MeshAccelTypes.h"
#include "Texture/MaterialChannels.h"

#include <cmath>
#include <cstdint>

#if defined(__CUDACC__)
#define PROCEDURAL_TEXTURE_FN __host__ __device__ inline
#else
#define PROCEDURAL_TEXTURE_FN inline
#endif

/**
 * Tag-based procedural texture evaluation (no virtual calls).
 *
 * To add a new texture type:
 * 1. Add a TextureKind value in MeshAccelTypes.h.
 * 2. Document param packing in TextureDescGpu::p0..p2 and TexturePack.h.
 * 3. Add mask/scalar cases to evalProceduralScalar / evalProceduralRgb below.
 * 4. Add parser + packTextureDesc on the host.
 * 5. Add unit tests.
 */

PROCEDURAL_TEXTURE_FN float proceduralFract(float value)
{
    return value - floorf(value);
}

PROCEDURAL_TEXTURE_FN float proceduralClamp01(float value)
{
    return fminf(fmaxf(value, 0.0f), 1.0f);
}

PROCEDURAL_TEXTURE_FN float proceduralHash01(int x, int y, int seed)
{
    uint32_t n = static_cast<uint32_t>(x * 374761393 + y * 668265263 + seed * 362437);
    n = (n ^ (n >> 13)) * 1274126177u;
    n = n ^ (n >> 16);
    return static_cast<float>(n & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

PROCEDURAL_TEXTURE_FN float proceduralValueNoise01(float u, float v, int seed)
{
    const int x0 = static_cast<int>(floorf(u));
    const int y0 = static_cast<int>(floorf(v));
    const int x1 = x0 + 1;
    const int y1 = y0 + 1;
    const float tx = u - static_cast<float>(x0);
    const float ty = v - static_cast<float>(y0);

    const float v00 = proceduralHash01(x0, y0, seed);
    const float v10 = proceduralHash01(x1, y0, seed);
    const float v01 = proceduralHash01(x0, y1, seed);
    const float v11 = proceduralHash01(x1, y1, seed);

    const float a = v00 + (v10 - v00) * tx;
    const float b = v01 + (v11 - v01) * tx;
    return a + (b - a) * ty;
}

PROCEDURAL_TEXTURE_FN float evalGridMask(const TextureDescGpu& desc, TextureEvalContext ctx)
{
    const float freqX = desc.p0.x;
    const float freqY = desc.p0.y;
    const float thickness = desc.p0.z;
    const float fx = proceduralFract(ctx.uv.x * freqX);
    const float fy = proceduralFract(ctx.uv.y * freqY);
    return (fx < thickness || fy < thickness) ? 1.0f : 0.0f;
}

PROCEDURAL_TEXTURE_FN float evalStripeMask(const TextureDescGpu& desc, TextureEvalContext ctx)
{
    const float freq = desc.p0.x;
    const float thickness = desc.p0.y;
    const float stripeCoord = proceduralFract(ctx.u1d * freq);
    return stripeCoord < thickness ? 1.0f : 0.0f;
}

PROCEDURAL_TEXTURE_FN float evalNoiseMask01(const TextureDescGpu& desc, TextureEvalContext ctx)
{
    const float scale = desc.p0.x;
    const int octaves = static_cast<int>(fmaxf(1.0f, floorf(desc.p0.y + 0.5f)));
    const int seed = static_cast<int>(desc.p0.z);

    float u = ctx.uv.x * scale;
    float v = ctx.uv.y * scale;
    float amplitude = 1.0f;
    float total = 0.0f;
    float weightSum = 0.0f;

    for (int octave = 0; octave < octaves; ++octave) {
        total += proceduralValueNoise01(u, v, seed + octave) * amplitude;
        weightSum += amplitude;
        u *= 2.0f;
        v *= 2.0f;
        amplitude *= 0.5f;
    }

    return weightSum > 0.0f ? total / weightSum : 0.0f;
}

PROCEDURAL_TEXTURE_FN Vec3 evalOnOffRgb(const TextureDescGpu& desc, float mask)
{
    const Vec3 onColor = vecMake3(
        desc.p1.x * desc.p1.w,
        desc.p1.y * desc.p1.w,
        desc.p1.z * desc.p1.w);
    const Vec3 offColor = vecMake3(
        desc.p2.x * desc.p2.w,
        desc.p2.y * desc.p2.w,
        desc.p2.z * desc.p2.w);
    return vecMake3(
        offColor.x + (onColor.x - offColor.x) * mask,
        offColor.y + (onColor.y - offColor.y) * mask,
        offColor.z + (onColor.z - offColor.z) * mask);
}

PROCEDURAL_TEXTURE_FN float evalOnOffScalar(const TextureDescGpu& desc, float mask)
{
    const float onValue =
        (desc.p1.x * 0.2126f + desc.p1.y * 0.7152f + desc.p1.z * 0.0722f) * desc.p1.w;
    const float offValue =
        (desc.p2.x * 0.2126f + desc.p2.y * 0.7152f + desc.p2.z * 0.0722f) * desc.p2.w;
    return offValue + (onValue - offValue) * mask;
}

PROCEDURAL_TEXTURE_FN float evalTextureMask(const TextureDescGpu& desc, TextureEvalContext ctx)
{
    switch (static_cast<TextureKind>(desc.kind)) {
    case TextureKind::Grid2D:
        return evalGridMask(desc, ctx);
    case TextureKind::Stripe1D:
        return evalStripeMask(desc, ctx);
    case TextureKind::Noise2D:
        return evalNoiseMask01(desc, ctx);
    default:
        return 0.0f;
    }
}

PROCEDURAL_TEXTURE_FN float evalProceduralScalar(
    const TextureDescGpu& desc,
    TextureEvalContext ctx)
{
    switch (static_cast<TextureKind>(desc.kind)) {
    case TextureKind::ConstantScalar:
        return desc.p0.x;
    case TextureKind::Stripe1D:
        return evalOnOffScalar(desc, evalStripeMask(desc, ctx));
    case TextureKind::Grid2D:
        return evalOnOffScalar(desc, 1.0f - evalGridMask(desc, ctx));
    case TextureKind::Noise2D:
        return evalOnOffScalar(desc, evalNoiseMask01(desc, ctx));
    default:
        return 0.0f;
    }
}

PROCEDURAL_TEXTURE_FN Vec3 evalProceduralRgb(
    const TextureDescGpu& desc,
    TextureEvalContext ctx)
{
    switch (static_cast<TextureKind>(desc.kind)) {
    case TextureKind::ConstantRgb:
        return vecMake3(desc.p0.x, desc.p0.y, desc.p0.z);
    case TextureKind::Grid2D:
        return evalOnOffRgb(desc, 1.0f - evalGridMask(desc, ctx));
    case TextureKind::Stripe1D:
        return evalOnOffRgb(desc, evalStripeMask(desc, ctx));
    case TextureKind::Noise2D:
        return evalOnOffRgb(desc, evalNoiseMask01(desc, ctx));
    default:
        return vecMake3(0.0f, 0.0f, 0.0f);
    }
}

PROCEDURAL_TEXTURE_FN float applyChannelCompositionScalar(
    float inlineValue,
    float textureValue,
    ChannelComposition composition)
{
    switch (composition) {
    case ChannelComposition::Replace:
        return textureValue;
    case ChannelComposition::Multiply:
        return inlineValue * textureValue;
    case ChannelComposition::Add:
        return inlineValue + textureValue;
    default:
        return textureValue;
    }
}

PROCEDURAL_TEXTURE_FN Vec3 applyChannelCompositionRgb(
    float inlineR,
    float inlineG,
    float inlineB,
    Vec3 textureValue,
    ChannelComposition composition)
{
    switch (composition) {
    case ChannelComposition::Replace:
        return textureValue;
    case ChannelComposition::Multiply:
        return vecMake3(
            inlineR * textureValue.x,
            inlineG * textureValue.y,
            inlineB * textureValue.z);
    case ChannelComposition::Add:
        return vecMake3(
            inlineR + textureValue.x,
            inlineG + textureValue.y,
            inlineB + textureValue.z);
    default:
        return textureValue;
    }
}

PROCEDURAL_TEXTURE_FN float resolveChannelScalar(
    MaterialChannelId channelId,
    float inlineValue,
    uint32_t textureIndex,
    const TextureDescGpu* bank,
    uint32_t bankCount,
    TextureEvalContext ctx)
{
    if (textureIndex == 0u || bank == nullptr || textureIndex >= bankCount) {
        return inlineValue;
    }

    const float textureValue = evalProceduralScalar(bank[textureIndex], ctx);
    const ChannelComposition composition = channelDefaultComposition(
        channelId,
        inlineValue,
        textureIndex);
    return applyChannelCompositionScalar(inlineValue, textureValue, composition);
}

PROCEDURAL_TEXTURE_FN Vec3 resolveChannelRgb(
    float inlineR,
    float inlineG,
    float inlineB,
    uint32_t textureIndex,
    const TextureDescGpu* bank,
    uint32_t bankCount,
    TextureEvalContext ctx)
{
    if (textureIndex == 0u || bank == nullptr || textureIndex >= bankCount) {
        return vecMake3(inlineR, inlineG, inlineB);
    }

    const Vec3 textureValue = evalProceduralRgb(bank[textureIndex], ctx);
    const ChannelComposition composition = channelDefaultComposition(MaterialChannelId::Albedo);
    return applyChannelCompositionRgb(
        inlineR,
        inlineG,
        inlineB,
        textureValue,
        composition);
}

PROCEDURAL_TEXTURE_FN void resolveChannel(
    MaterialChannelId channelId,
    float inlineR,
    float inlineG,
    float inlineB,
    float inlineScalar,
    uint32_t textureIndex,
    const TextureDescGpu* bank,
    uint32_t bankCount,
    TextureEvalContext ctx,
    ResolvedMaterial& outResolved)
{
    if (channelValueKind(channelId) == ChannelValueKind::Rgb) {
        const Vec3 rgb = resolveChannelRgb(
            inlineR,
            inlineG,
            inlineB,
            textureIndex,
            bank,
            bankCount,
            ctx);
        outResolved.r = rgb.x;
        outResolved.g = rgb.y;
        outResolved.b = rgb.z;
        return;
    }

    const float scalar = resolveChannelScalar(
        channelId,
        inlineScalar,
        textureIndex,
        bank,
        bankCount,
        ctx);

    switch (channelId) {
    case MaterialChannelId::Roughness:
        outResolved.roughness = proceduralClamp01(scalar);
        break;
    case MaterialChannelId::Metallic:
        outResolved.metallic = proceduralClamp01(scalar);
        break;
    case MaterialChannelId::Transmission:
        outResolved.transmission = proceduralClamp01(scalar);
        break;
    case MaterialChannelId::Thin:
        outResolved.thin = proceduralClamp01(scalar);
        break;
    case MaterialChannelId::Ior:
        outResolved.ior = fmaxf(1.0e-3f, scalar);
        break;
    case MaterialChannelId::Subsurface:
        outResolved.subsurface = proceduralClamp01(scalar);
        break;
    case MaterialChannelId::Emission:
        outResolved.emission = fmaxf(0.0f, scalar);
        break;
    case MaterialChannelId::DiffuseRoughness:
        outResolved.diffuseRoughness = scalar;
        break;
    case MaterialChannelId::ScatterRadiusR:
        outResolved.scatterRadiusR = fmaxf(0.0f, scalar);
        break;
    case MaterialChannelId::ScatterRadiusG:
        outResolved.scatterRadiusG = fmaxf(0.0f, scalar);
        break;
    case MaterialChannelId::ScatterRadiusB:
        outResolved.scatterRadiusB = fmaxf(0.0f, scalar);
        break;
    case MaterialChannelId::Specular:
        outResolved.specular = proceduralClamp01(scalar);
        break;
    default:
        break;
    }
}

PROCEDURAL_TEXTURE_FN ResolvedMaterial resolveLayer(
    const MaterialGpu& material,
    TextureEvalContext ctx,
    const TextureDescGpu* bank,
    uint32_t bankCount)
{
    ResolvedMaterial resolved{};

    resolveChannel(
        MaterialChannelId::Albedo,
        material.r,
        material.g,
        material.b,
        0.0f,
        material.albedoTex,
        bank,
        bankCount,
        ctx,
        resolved);
    resolveChannel(
        MaterialChannelId::Roughness,
        0.0f,
        0.0f,
        0.0f,
        material.roughness,
        material.roughnessTex,
        bank,
        bankCount,
        ctx,
        resolved);
    resolveChannel(
        MaterialChannelId::Metallic,
        0.0f,
        0.0f,
        0.0f,
        material.metallic,
        material.metallicTex,
        bank,
        bankCount,
        ctx,
        resolved);
    resolveChannel(
        MaterialChannelId::Transmission,
        0.0f,
        0.0f,
        0.0f,
        material.transmission,
        material.transmissionTex,
        bank,
        bankCount,
        ctx,
        resolved);
    resolveChannel(
        MaterialChannelId::Thin,
        0.0f,
        0.0f,
        0.0f,
        material.thin,
        material.thinTex,
        bank,
        bankCount,
        ctx,
        resolved);
    resolveChannel(
        MaterialChannelId::Ior,
        0.0f,
        0.0f,
        0.0f,
        material.ior,
        material.iorTex,
        bank,
        bankCount,
        ctx,
        resolved);
    resolveChannel(
        MaterialChannelId::Subsurface,
        0.0f,
        0.0f,
        0.0f,
        material.subsurface,
        material.subsurfaceTex,
        bank,
        bankCount,
        ctx,
        resolved);
    resolveChannel(
        MaterialChannelId::Emission,
        0.0f,
        0.0f,
        0.0f,
        material.emission,
        material.emissionTex,
        bank,
        bankCount,
        ctx,
        resolved);
    resolveChannel(
        MaterialChannelId::DiffuseRoughness,
        0.0f,
        0.0f,
        0.0f,
        material.diffuseRoughness,
        material.diffuseRoughnessTex,
        bank,
        bankCount,
        ctx,
        resolved);
    if (resolved.diffuseRoughness < 0.0f) {
        resolved.diffuseRoughness = resolved.roughness;
    } else {
        resolved.diffuseRoughness = proceduralClamp01(resolved.diffuseRoughness);
    }
    resolveChannel(
        MaterialChannelId::ScatterRadiusR,
        0.0f,
        0.0f,
        0.0f,
        material.scatterRadiusR,
        material.scatterRadiusRTex,
        bank,
        bankCount,
        ctx,
        resolved);
    resolveChannel(
        MaterialChannelId::ScatterRadiusG,
        0.0f,
        0.0f,
        0.0f,
        material.scatterRadiusG,
        material.scatterRadiusGTex,
        bank,
        bankCount,
        ctx,
        resolved);
    resolveChannel(
        MaterialChannelId::ScatterRadiusB,
        0.0f,
        0.0f,
        0.0f,
        material.scatterRadiusB,
        material.scatterRadiusBTex,
        bank,
        bankCount,
        ctx,
        resolved);
    resolveChannel(
        MaterialChannelId::Specular,
        0.0f,
        0.0f,
        0.0f,
        material.specular,
        material.specularTex,
        bank,
        bankCount,
        ctx,
        resolved);

    return resolved;
}

PROCEDURAL_TEXTURE_FN ResolvedMaterial resolveMaterial(
    const MaterialGpu& material,
    TextureEvalContext ctx,
    const TextureDescGpu* bank,
    uint32_t bankCount)
{
    return resolveLayer(material, ctx, bank, bankCount);
}

PROCEDURAL_TEXTURE_FN MaterialGpu materialFromResolved(const ResolvedMaterial& resolved)
{
    MaterialGpu material{};
    material.r = resolved.r;
    material.g = resolved.g;
    material.b = resolved.b;
    material.roughness = resolved.roughness;
    material.metallic = resolved.metallic;
    material.transmission = resolved.transmission;
    material.thin = resolved.thin;
    material.ior = resolved.ior;
    material.subsurface = resolved.subsurface;
    material.emission = resolved.emission;
    material.diffuseRoughness = resolved.diffuseRoughness;
    material.scatterRadiusR = resolved.scatterRadiusR;
    material.scatterRadiusG = resolved.scatterRadiusG;
    material.scatterRadiusB = resolved.scatterRadiusB;
    material.specular = resolved.specular;
    return material;
}

#undef PROCEDURAL_TEXTURE_FN
