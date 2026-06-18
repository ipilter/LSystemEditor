#pragma once

#include "Geometry/GeometryTypes.h"
#include "Geometry/MathCore.h"
#include "MeshAccel/MeshAccelTypes.h"

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
 * 2. Document param packing in TextureDescGpu::p0..p2.
 * 3. Add cases to evalProceduralScalar / evalProceduralRgb below.
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

PROCEDURAL_TEXTURE_FN float evalProceduralScalar(
    const TextureDescGpu& desc,
    TextureEvalContext ctx)
{
    switch (static_cast<TextureKind>(desc.kind)) {
    case TextureKind::ConstantScalar:
        return desc.p0.x;
    case TextureKind::Stripe1D: {
        const float freq = desc.p0.x;
        const float thickness = desc.p0.y;
        const float onValue = desc.p0.z;
        const float offValue = desc.p0.w;
        const float stripeCoord = proceduralFract(ctx.u1d * freq);
        return stripeCoord < thickness ? onValue : offValue;
    }
    case TextureKind::Grid2D: {
        const float freqX = desc.p0.x;
        const float freqY = desc.p0.y;
        const float thickness = desc.p0.z;
        const float fx = proceduralFract(ctx.uv.x * freqX);
        const float fy = proceduralFract(ctx.uv.y * freqY);
        return (fx < thickness || fy < thickness) ? 1.0f : 0.0f;
    }
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
    case TextureKind::Grid2D: {
        const float freqX = desc.p0.x;
        const float freqY = desc.p0.y;
        const float thickness = desc.p0.z;
        const Vec3 colorA = vecMake3(desc.p1.x, desc.p1.y, desc.p1.z);
        const Vec3 colorB = vecMake3(desc.p2.x, desc.p2.y, desc.p2.z);
        const float fx = proceduralFract(ctx.uv.x * freqX);
        const float fy = proceduralFract(ctx.uv.y * freqY);
        const float grid = (fx < thickness || fy < thickness) ? 1.0f : 0.0f;
        return vecMake3(
            colorA.x + (colorB.x - colorA.x) * grid,
            colorA.y + (colorB.y - colorA.y) * grid,
            colorA.z + (colorB.z - colorA.z) * grid);
    }
    default:
        return vecMake3(0.0f, 0.0f, 0.0f);
    }
}

PROCEDURAL_TEXTURE_FN float resolveChannelScalar(
    float inlineValue,
    uint32_t textureIndex,
    const TextureDescGpu* bank,
    uint32_t bankCount,
    TextureEvalContext ctx)
{
    if (textureIndex == 0u || bank == nullptr || textureIndex >= bankCount) {
        return inlineValue;
    }
    return evalProceduralScalar(bank[textureIndex], ctx);
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
    return evalProceduralRgb(bank[textureIndex], ctx);
}

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
};

PROCEDURAL_TEXTURE_FN ResolvedMaterial resolveMaterial(
    const MaterialGpu& material,
    TextureEvalContext ctx,
    const TextureDescGpu* bank,
    uint32_t bankCount)
{
    ResolvedMaterial resolved{};
    const Vec3 albedo = resolveChannelRgb(
        material.r,
        material.g,
        material.b,
        material.albedoTex,
        bank,
        bankCount,
        ctx);
    resolved.r = albedo.x;
    resolved.g = albedo.y;
    resolved.b = albedo.z;
    resolved.roughness = proceduralClamp01(resolveChannelScalar(
        material.roughness, material.roughnessTex, bank, bankCount, ctx));
    resolved.metallic = proceduralClamp01(resolveChannelScalar(
        material.metallic, material.metallicTex, bank, bankCount, ctx));
    resolved.transmission = proceduralClamp01(resolveChannelScalar(
        material.transmission, material.transmissionTex, bank, bankCount, ctx));
    resolved.thin = proceduralClamp01(resolveChannelScalar(
        material.thin, material.thinTex, bank, bankCount, ctx));
    resolved.ior = fmaxf(1.0e-3f, resolveChannelScalar(
        material.ior, material.iorTex, bank, bankCount, ctx));
    resolved.subsurface = proceduralClamp01(resolveChannelScalar(
        material.subsurface, material.subsurfaceTex, bank, bankCount, ctx));
    resolved.emission = fmaxf(0.0f, resolveChannelScalar(
        material.emission, material.emissionTex, bank, bankCount, ctx));
    return resolved;
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
    return material;
}

#undef PROCEDURAL_TEXTURE_FN
