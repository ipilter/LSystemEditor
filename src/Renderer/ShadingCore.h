#pragma once

#include "Geometry/MathCore.h"
#include "MeshAccel/MeshAccelIntersectCore.h"
#include "QmcSamplerCore.h"
#include "RenderVisualCore.h"

#include <cmath>

#if defined(__CUDACC__)
#define SHADING_CORE_FN __host__ __device__ inline
#else
#define SHADING_CORE_FN inline
#endif

namespace ShadingCoreDetail {

constexpr float kRayTMax = 1.0e6f;
constexpr float kShadowBias = 1.0e-4f;
constexpr float kPi = 3.14159265f;
constexpr float kDegToRad = kPi / 180.0f;
constexpr int kDirectShadowSamples = 4;
constexpr int kSunSampleDimCount = kDirectShadowSamples * 2;

} // namespace ShadingCoreDetail

SHADING_CORE_FN int sunSampleDimBaseForBounce(int bounceIndex)
{
    if (bounceIndex <= 0) {
        return 2;
    }
    return 2 + ShadingCoreDetail::kSunSampleDimCount +
        (bounceIndex - 1) * (2 + ShadingCoreDetail::kSunSampleDimCount) + 2;
}

SHADING_CORE_FN int reflectionSampleDimBaseForBounce(int bounceIndex)
{
    if (bounceIndex <= 0) {
        return -1;
    }
    return 2 + ShadingCoreDetail::kSunSampleDimCount +
        (bounceIndex - 1) * (2 + ShadingCoreDetail::kSunSampleDimCount);
}

SHADING_CORE_FN Vec3 sunDirectionFromAngles(float azimuthDeg, float elevationDeg)
{
    const float az = azimuthDeg * ShadingCoreDetail::kDegToRad;
    const float el = elevationDeg * ShadingCoreDetail::kDegToRad;
    const float cosEl = cosf(el);
    return vecNormalize3(vecMake3(cosEl * sinf(az), sinf(el), cosEl * cosf(az)));
}

SHADING_CORE_FN void buildOrthonormalBasis(Vec3 n, Vec3& tangent, Vec3& bitangent)
{
    const Vec3 up = fabsf(n.y) < 0.999f ? vecMake3(0.0f, 1.0f, 0.0f) : vecMake3(1.0f, 0.0f, 0.0f);
    tangent = vecNormalize3(vecCross3(up, n));
    bitangent = vecNormalize3(vecCross3(n, tangent));
}

SHADING_CORE_FN Vec3 sampleCosineHemisphere(Vec3 normal, float u1, float u2)
{
    const float phi = 2.0f * ShadingCoreDetail::kPi * u1;
    const float r = sqrtf(u2);
    const float x = r * cosf(phi);
    const float y = r * sinf(phi);
    const float z = sqrtf(vecMax2(0.0f, 1.0f - u2));

    Vec3 tangent{};
    Vec3 bitangent{};
    buildOrthonormalBasis(normal, tangent, bitangent);
    return vecNormalize3(vecAdd3(
        vecAdd3(vecScale3(tangent, x), vecScale3(bitangent, y)),
        vecScale3(normal, z)));
}

SHADING_CORE_FN Vec3 sampleReflectionDirection(Vec3 normal, Vec3 incident, float roughness, float u1, float u2)
{
    const Vec3 perfect = vecSub3(incident, vecScale3(normal, 2.0f * vecDot3(incident, normal)));
    const Vec3 diffuse = sampleCosineHemisphere(normal, u1, u2);
    const float t = vecClamp(roughness, 0.0f, 1.0f);
    return vecNormalize3(vecLerp3(perfect, diffuse, t));
}

SHADING_CORE_FN Vec3 sampleSunDirection(
    const RenderParamsGpu* params,
    float u1,
    float u2)
{
    if (params == nullptr) {
        return vecMake3(0.0f, 1.0f, 0.0f);
    }

    const Vec3 baseDir = sunDirectionFromAngles(params->sunAzimuthDeg, params->sunElevationDeg);
    const float angularRadius = params->sunDiskSizeDeg * 0.5f * ShadingCoreDetail::kDegToRad;
    if (angularRadius <= 1.0e-6f) {
        return baseDir;
    }

    Vec3 tangent{};
    Vec3 bitangent{};
    buildOrthonormalBasis(baseDir, tangent, bitangent);

    const float r = sqrtf(u1) * sinf(angularRadius);
    const float phi = 2.0f * ShadingCoreDetail::kPi * u2;
    const Vec3 offset = vecAdd3(
        vecScale3(tangent, r * cosf(phi)),
        vecScale3(bitangent, r * sinf(phi)));
    return vecNormalize3(vecAdd3(baseDir, offset));
}

SHADING_CORE_FN MaterialGpu fetchMaterial(const MeshAccelSceneGpu* scene, uint32_t triangleIndex)
{
    MaterialGpu fallback{};
    fallback.r = 0.8f;
    fallback.g = 0.8f;
    fallback.b = 0.8f;
    fallback.roughness = 0.5f;
    fallback.metallic = 0.0f;

    if (scene == nullptr || scene->triangles == nullptr || triangleIndex >= scene->triangleCount) {
        return fallback;
    }

    const uint32_t materialIndex = scene->triangles[triangleIndex].materialIndex;
    if (scene->materials == nullptr || materialIndex >= scene->materialCount) {
        return fallback;
    }

    return scene->materials[materialIndex];
}

SHADING_CORE_FN bool isShadowed(Vec3 position, Vec3 normal, Vec3 lightDir, const MeshAccelSceneGpu* scene)
{
    const Vec3 origin = vecAdd3(position, vecScale3(normal, ShadingCoreDetail::kShadowBias));
    const MeshHit shadowHit =
        meshAccelTraceRay(origin, lightDir, scene, ShadingCoreDetail::kShadowBias, ShadingCoreDetail::kRayTMax);
    return shadowHit.hit;
}

SHADING_CORE_FN Vec3 sunColorFromParams(const RenderParamsGpu* params)
{
    if (params == nullptr) {
        return vecMake3(1.0f, 1.0f, 1.0f);
    }
    return vecMake3(params->sunColorR, params->sunColorG, params->sunColorB);
}

SHADING_CORE_FN Vec3 evaluateDirectBrdf(
    Vec3 normal,
    Vec3 viewDir,
    Vec3 lightDir,
    Vec3 baseColor,
    const MaterialGpu& material,
    Vec3 lightColor)
{
    const float nDotL = vecMax2(0.0f, vecDot3(normal, lightDir));
    if (nDotL <= 0.0f) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    const float diffuseWeight = (1.0f - material.metallic);
    Vec3 diffuse = vecScale3(
        vecMake3(
            baseColor.x * diffuseWeight,
            baseColor.y * diffuseWeight,
            baseColor.z * diffuseWeight),
        nDotL);

    const Vec3 halfVec = vecNormalize3(vecAdd3(lightDir, viewDir));
    const float nDotH = vecMax2(0.0f, vecDot3(normal, halfVec));
    const float shininess = vecLerp(8.0f, 256.0f, 1.0f - vecClamp(material.roughness, 0.0f, 1.0f));
    const float spec = powf(nDotH, shininess) * (material.metallic + (1.0f - material.metallic) * 0.05f);

    Vec3 specular = vecScale3(lightColor, spec * nDotL);
    return vecAdd3(vecMake3(
                       diffuse.x * lightColor.x,
                       diffuse.y * lightColor.y,
                       diffuse.z * lightColor.z),
        specular);
}

SHADING_CORE_FN Vec3 evaluateDirectLightingWithOcclusion(
    Vec3 position,
    Vec3 normal,
    Vec3 viewDir,
    const MaterialGpu& material,
    const MeshAccelSceneGpu* scene,
    const RenderParamsGpu* params,
    SampleContext& ctx,
    int sunDimBase,
    const uint32_t* sobolMatrices,
    int sobolDimensionCount,
    const int* precomputedOccluded,
    int precomputedOccludedOffset)
{
    Vec3 lighting = vecMake3(0.0f, 0.0f, 0.0f);
    const float invSamples = 1.0f / static_cast<float>(ShadingCoreDetail::kDirectShadowSamples);

    for (int sampleIndex = 0; sampleIndex < ShadingCoreDetail::kDirectShadowSamples; ++sampleIndex) {
        ctx.dimension = sunDimBase + sampleIndex * 2;
        const float sunU1 = qmcNext1D(ctx, sobolMatrices, sobolDimensionCount);
        ctx.dimension = sunDimBase + sampleIndex * 2 + 1;
        const float sunU2 = qmcNext1D(ctx, sobolMatrices, sobolDimensionCount);

        const Vec3 lightDir = sampleSunDirection(params, sunU1, sunU2);
        const bool occluded = precomputedOccluded != nullptr
            ? (precomputedOccluded[precomputedOccludedOffset + sampleIndex] != 0)
            : isShadowed(position, normal, lightDir, scene);
        if (occluded) {
            continue;
        }

        const Vec3 baseColor = vecMake3(material.r, material.g, material.b);
        const Vec3 lightColor = sunColorFromParams(params);
        const Vec3 contribution =
            evaluateDirectBrdf(normal, viewDir, lightDir, baseColor, material, lightColor);
        lighting = vecAdd3(lighting, vecScale3(contribution, invSamples));
    }

    return lighting;
}

SHADING_CORE_FN Vec3 evaluateDirectLighting(
    Vec3 position,
    Vec3 normal,
    Vec3 viewDir,
    const MaterialGpu& material,
    const MeshAccelSceneGpu* scene,
    const RenderParamsGpu* params,
    SampleContext& ctx,
    int sunDimBase,
    const uint32_t* sobolMatrices,
    int sobolDimensionCount)
{
    return evaluateDirectLightingWithOcclusion(
        position,
        normal,
        viewDir,
        material,
        scene,
        params,
        ctx,
        sunDimBase,
        sobolMatrices,
        sobolDimensionCount,
        nullptr,
        0);
}

SHADING_CORE_FN Vec3 shadeOffModeWithPrimaryOcclusion(
    const MeshHit& hit,
    Vec3 rayOrigin,
    Vec3 rayDir,
    const MeshAccelSceneGpu* scene,
    const RenderParamsGpu* params,
    SampleContext& ctx,
    const uint32_t* sobolMatrices,
    int sobolDimensionCount,
    const int* primaryShadowOccluded,
    int primaryShadowOccludedOffset)
{
    if (!hit.hit) {
        return renderMissBackground(params);
    }

    const Vec3 position = vecEvalRay(rayOrigin, rayDir, hit.t);
    const Vec3 normal = hit.normal;
    const Vec3 viewDir = vecScale3(rayDir, -1.0f);
    const MaterialGpu material = fetchMaterial(scene, hit.triangleIndex);

    Vec3 radiance = evaluateDirectLightingWithOcclusion(
        position,
        normal,
        viewDir,
        material,
        scene,
        params,
        ctx,
        sunSampleDimBaseForBounce(0),
        sobolMatrices,
        sobolDimensionCount,
        primaryShadowOccluded,
        primaryShadowOccludedOffset);

    int bounceCount = 0;
    if (params != nullptr && params->secondaryBounceCount > 0) {
        bounceCount = params->secondaryBounceCount;
    }
    if (bounceCount <= 0) {
        return radiance;
    }

    Vec3 throughput = vecMake3(1.0f, 1.0f, 1.0f);
    Vec3 currentPos = position;
    Vec3 currentNormal = normal;
    Vec3 incomingDir = viewDir;
    MaterialGpu currentMaterial = material;

    for (int bounce = 0; bounce < bounceCount; ++bounce) {
        const int reflectDimBase = reflectionSampleDimBaseForBounce(bounce + 1);
        ctx.dimension = reflectDimBase;
        const float u1 = qmcNext1D(ctx, sobolMatrices, sobolDimensionCount);
        ctx.dimension = reflectDimBase + 1;
        const float u2 = qmcNext1D(ctx, sobolMatrices, sobolDimensionCount);

        const Vec3 outgoing =
            sampleReflectionDirection(currentNormal, incomingDir, currentMaterial.roughness, u1, u2);
        const float cosOut = vecMax2(0.0f, vecDot3(currentNormal, outgoing));
        if (cosOut <= 0.0f) {
            break;
        }

        const Vec3 baseColor = vecMake3(currentMaterial.r, currentMaterial.g, currentMaterial.b);
        const float diffuseWeight = (1.0f - currentMaterial.metallic);
        throughput = vecMake3(
            throughput.x * baseColor.x * diffuseWeight * cosOut,
            throughput.y * baseColor.y * diffuseWeight * cosOut,
            throughput.z * baseColor.z * diffuseWeight * cosOut);

        const Vec3 nextOrigin = vecAdd3(currentPos, vecScale3(currentNormal, ShadingCoreDetail::kShadowBias));
        const MeshHit bounceHit = meshAccelTraceRay(
            nextOrigin, outgoing, scene, ShadingCoreDetail::kShadowBias, ShadingCoreDetail::kRayTMax);
        if (!bounceHit.hit) {
            radiance = vecAdd3(radiance, vecMake3(
                throughput.x * (params != nullptr ? params->backgroundR : 0.04f),
                throughput.y * (params != nullptr ? params->backgroundG : 0.04f),
                throughput.z * (params != nullptr ? params->backgroundB : 0.04f)));
            break;
        }

        currentPos = vecEvalRay(nextOrigin, outgoing, bounceHit.t);
        currentNormal = bounceHit.normal;
        incomingDir = outgoing;
        currentMaterial = fetchMaterial(scene, bounceHit.triangleIndex);

        const Vec3 bounceDirect = evaluateDirectLighting(
            currentPos,
            currentNormal,
            vecScale3(incomingDir, -1.0f),
            currentMaterial,
            scene,
            params,
            ctx,
            sunSampleDimBaseForBounce(bounce + 1),
            sobolMatrices,
            sobolDimensionCount);

        radiance = vecAdd3(radiance, vecMake3(
            throughput.x * bounceDirect.x,
            throughput.y * bounceDirect.y,
            throughput.z * bounceDirect.z));
    }

    return radiance;
}

SHADING_CORE_FN Vec3 shadeOffMode(
    const MeshHit& hit,
    Vec3 rayOrigin,
    Vec3 rayDir,
    const MeshAccelSceneGpu* scene,
    const RenderParamsGpu* params,
    SampleContext& ctx,
    const uint32_t* sobolMatrices,
    int sobolDimensionCount)
{
    return shadeOffModeWithPrimaryOcclusion(
        hit,
        rayOrigin,
        rayDir,
        scene,
        params,
        ctx,
        sobolMatrices,
        sobolDimensionCount,
        nullptr,
        0);
}

#undef SHADING_CORE_FN
