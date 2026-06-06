#pragma once

#include "BsdfCore.h"
#include "Geometry/MathCore.h"
#include "MeshAccel/MeshAccelIntersectCore.h"
#include "MeshAccel/MeshAccelTypes.h"

#include <cmath>

#if defined(__CUDACC__)
#define LIGHT_CORE_FN __host__ __device__ inline
#else
#define LIGHT_CORE_FN inline
#endif

namespace LightCoreDetail {

constexpr float kPi = 3.14159265f;
constexpr float kDegToRad = kPi / 180.0f;
constexpr float kMinPdf = 1.0e-8f;
constexpr float kShadowBias = 1.0e-4f;
constexpr float kRayTMax = 1.0e6f;

} // namespace LightCoreDetail

enum class LightType : int
{
    Sun = 0,
    Environment = 1,
    Emissive = 2,
};

LIGHT_CORE_FN Vec3 lightSunBaseDirection(const RenderParamsGpu* params)
{
    if (params == nullptr) {
        return vecMake3(0.0f, 1.0f, 0.0f);
    }
    const float az = params->sunAzimuthDeg * LightCoreDetail::kDegToRad;
    const float el = params->sunElevationDeg * LightCoreDetail::kDegToRad;
    const float cosEl = cosf(el);
    return vecNormalize3(vecMake3(cosEl * sinf(az), sinf(el), cosEl * cosf(az)));
}

LIGHT_CORE_FN void lightBuildBasis(Vec3 n, Vec3& tangent, Vec3& bitangent)
{
    const Vec3 up = fabsf(n.y) < 0.999f ? vecMake3(0.0f, 1.0f, 0.0f) : vecMake3(1.0f, 0.0f, 0.0f);
    tangent = vecNormalize3(vecCross3(up, n));
    bitangent = vecNormalize3(vecCross3(n, tangent));
}

LIGHT_CORE_FN Vec3 lightSunColor(const RenderParamsGpu* params)
{
    if (params == nullptr) {
        return vecMake3(1.0f, 1.0f, 1.0f);
    }
    const float intensity = params->sunIntensity > 0.0f ? params->sunIntensity : 1.0f;
    return vecMake3(params->sunColorR * intensity, params->sunColorG * intensity, params->sunColorB * intensity);
}

LIGHT_CORE_FN float lightSunAngularRadius(const RenderParamsGpu* params)
{
    if (params == nullptr) {
        return 0.0f;
    }
    return params->sunDiskSizeDeg * 0.5f * LightCoreDetail::kDegToRad;
}

LIGHT_CORE_FN Vec3 lightSampleSunDirection(const RenderParamsGpu* params, float u1, float u2, float& pdf)
{
    pdf = 0.0f;
    const Vec3 baseDir = lightSunBaseDirection(params);
    const float angularRadius = lightSunAngularRadius(params);
    if (angularRadius <= 1.0e-6f) {
        pdf = 1.0f;
        return baseDir;
    }

    const float cosThetaMax = cosf(angularRadius);
    const float cosTheta = 1.0f - u1 * (1.0f - cosThetaMax);
    const float sinTheta = sqrtf(vecMax2(0.0f, 1.0f - cosTheta * cosTheta));
    const float phi = 2.0f * LightCoreDetail::kPi * u2;

    Vec3 tangent{};
    Vec3 bitangent{};
    lightBuildBasis(baseDir, tangent, bitangent);
    const Vec3 local = vecAdd3(
        vecAdd3(vecScale3(tangent, sinTheta * cosf(phi)), vecScale3(bitangent, sinTheta * sinf(phi))),
        vecScale3(baseDir, cosTheta));
    pdf = 1.0f / (2.0f * LightCoreDetail::kPi * (1.0f - cosThetaMax));
    return vecNormalize3(local);
}

LIGHT_CORE_FN float lightPdfSunDirection(const RenderParamsGpu* params, Vec3 wi)
{
    const Vec3 baseDir = lightSunBaseDirection(params);
    const float angularRadius = lightSunAngularRadius(params);
    if (angularRadius <= 1.0e-6f) {
        return vecDot3(wi, baseDir) > 0.999f ? 1.0f : 0.0f;
    }

    const float cosThetaMax = cosf(angularRadius);
    const float cosTheta = vecDot3(vecNormalize3(wi), baseDir);
    if (cosTheta < cosThetaMax) {
        return 0.0f;
    }
    return 1.0f / (2.0f * LightCoreDetail::kPi * (1.0f - cosThetaMax));
}

LIGHT_CORE_FN Vec3 lightEvalSunRadiance(const RenderParamsGpu* params, Vec3 wi)
{
    const float pdf = lightPdfSunDirection(params, wi);
    if (pdf <= 0.0f) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }
    return lightSunColor(params);
}

LIGHT_CORE_FN Vec3 lightDirectionToLatLong(Vec3 direction)
{
    const Vec3 w = vecNormalize3(direction);
    const float phi = atan2f(w.x, w.z);
    const float theta = acosf(vecClamp(w.y, -1.0f, 1.0f));
    const float u = (phi + LightCoreDetail::kPi) / (2.0f * LightCoreDetail::kPi);
    const float v = theta / LightCoreDetail::kPi;
    return vecMake3(u, v, 0.0f);
}

LIGHT_CORE_FN Vec3 lightLatLongToDirection(float u, float v)
{
    const float phi = u * 2.0f * LightCoreDetail::kPi - LightCoreDetail::kPi;
    const float theta = v * LightCoreDetail::kPi;
    const float sinTheta = sinf(theta);
    return vecNormalize3(vecMake3(sinTheta * sinf(phi), cosf(theta), sinTheta * cosf(phi)));
}

LIGHT_CORE_FN Vec3 lightEvalEnvironment(const EnvironmentMapGpu* env, Vec3 direction)
{
    if (env == nullptr || env->valid == 0 || env->pixels == nullptr || env->width <= 0 || env->height <= 0) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    const Vec3 uv = lightDirectionToLatLong(direction);
    const int x = vecClamp(static_cast<int>(uv.x * static_cast<float>(env->width)), 0, env->width - 1);
    const int y = vecClamp(static_cast<int>(uv.y * static_cast<float>(env->height)), 0, env->height - 1);
    const int index = (y * env->width + x) * 3;
    return vecMake3(env->pixels[index], env->pixels[index + 1], env->pixels[index + 2]);
}

LIGHT_CORE_FN Vec3 lightEvalEnvironmentOrBackground(
    const EnvironmentMapGpu* env,
    const RenderParamsGpu* params,
    Vec3 direction)
{
    const Vec3 envRadiance = lightEvalEnvironment(env, direction);
    if (env != nullptr && env->valid != 0) {
        return envRadiance;
    }
    if (params == nullptr) {
        return vecMake3(0.04f, 0.04f, 0.04f);
    }
    return vecMake3(params->backgroundR, params->backgroundG, params->backgroundB);
}

LIGHT_CORE_FN int lightFindCdfRow(const float* cdf, int count, float u)
{
    int low = 0;
    int high = count - 1;
    while (low < high) {
        const int mid = (low + high) / 2;
        if (cdf[mid] < u) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    return low;
}

LIGHT_CORE_FN Vec3 lightSampleEnvironment(
    const EnvironmentMapGpu* env,
    float uMarginal,
    float uRow,
    float& pdf)
{
    pdf = 0.0f;
    if (env == nullptr || env->valid == 0 || env->pixels == nullptr || env->marginalCdf == nullptr ||
        env->rowCdf == nullptr || env->width <= 0 || env->height <= 0) {
        return vecMake3(0.0f, 1.0f, 0.0f);
    }

    const int y = lightFindCdfRow(env->marginalCdf, env->height, uMarginal);
    const int rowOffset = y * (env->width + 1);
    const int x = lightFindCdfRow(env->rowCdf + rowOffset, env->width, uRow);

    const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(env->width);
    const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(env->height);
    const Vec3 direction = lightLatLongToDirection(u, v);

    const int pixelIndex = (y * env->width + x) * 3;
    const Vec3 radiance = vecMake3(
        env->pixels[pixelIndex],
        env->pixels[pixelIndex + 1],
        env->pixels[pixelIndex + 2]);
    const float luminance = vecMax2(vecMax2(radiance.x, radiance.y), radiance.z);
    const float rowPdf = env->rowCdf[rowOffset + x + 1] - env->rowCdf[rowOffset + x];
    const float marginalPdf = env->marginalCdf[y + 1] - env->marginalCdf[y];
    const float sinTheta = vecMax2(1.0e-4f, sinf(v * LightCoreDetail::kPi));
    pdf = (marginalPdf * rowPdf) / (2.0f * LightCoreDetail::kPi * LightCoreDetail::kPi * sinTheta);
    if (luminance <= 0.0f) {
        pdf = 0.0f;
    }
    return direction;
}

LIGHT_CORE_FN float lightPdfEnvironment(const EnvironmentMapGpu* env, Vec3 wi)
{
    if (env == nullptr || env->valid == 0 || env->pixels == nullptr || env->marginalCdf == nullptr ||
        env->rowCdf == nullptr || env->width <= 0 || env->height <= 0) {
        return 0.0f;
    }

    const Vec3 uv = lightDirectionToLatLong(wi);
    const int x = vecClamp(static_cast<int>(uv.x * static_cast<float>(env->width)), 0, env->width - 1);
    const int y = vecClamp(static_cast<int>(uv.y * static_cast<float>(env->height)), 0, env->height - 1);
    const int rowOffset = y * (env->width + 1);
    const float rowPdf = env->rowCdf[rowOffset + x + 1] - env->rowCdf[rowOffset + x];
    const float marginalPdf = env->marginalCdf[y + 1] - env->marginalCdf[y];
    const float sinTheta = vecMax2(1.0e-4f, sinf(uv.y * LightCoreDetail::kPi));
    return (marginalPdf * rowPdf) / (2.0f * LightCoreDetail::kPi * LightCoreDetail::kPi * sinTheta);
}

LIGHT_CORE_FN float lightTriangleArea(const TriangleGpu& tri)
{
    const Vec3 e1 = vecSub3(tri.v1, tri.v0);
    const Vec3 e2 = vecSub3(tri.v2, tri.v0);
    return 0.5f * vecLength3(vecCross3(e1, e2));
}

LIGHT_CORE_FN Vec3 lightSampleTrianglePoint(const TriangleGpu& tri, float u, float v, Vec3& normal)
{
    float su = sqrtf(u);
    const float b0 = 1.0f - su;
    const float b1 = su * (1.0f - v);
    const float b2 = su * v;
    const Vec3 point = vecAdd3(
        vecAdd3(vecScale3(tri.v0, b0), vecScale3(tri.v1, b1)),
        vecScale3(tri.v2, b2));
    normal = vecNormalize3(vecAdd3(
        vecAdd3(vecScale3(tri.n0, b0), vecScale3(tri.n1, b1)),
        vecScale3(tri.n2, b2)));
    return point;
}

LIGHT_CORE_FN bool lightSampleEmissiveTriangle(
    const MeshAccelSceneGpu* scene,
    float uPick,
    float uBary,
    float vBary,
    Vec3 shadingPoint,
    Vec3& outWi,
    Vec3& outRadiance,
    float& pdf)
{
    pdf = 0.0f;
    outRadiance = vecMake3(0.0f, 0.0f, 0.0f);
    if (scene == nullptr || scene->emissiveTriangleCount == 0 || scene->emissiveTriangleIndices == nullptr ||
        scene->triangles == nullptr || scene->materials == nullptr) {
        return false;
    }

    const int pick = vecClamp(
        static_cast<int>(uPick * static_cast<float>(scene->emissiveTriangleCount)),
        0,
        static_cast<int>(scene->emissiveTriangleCount) - 1);
    const uint32_t triIndex = scene->emissiveTriangleIndices[pick];
    if (triIndex >= scene->triangleCount) {
        return false;
    }

    const TriangleGpu& tri = scene->triangles[triIndex];
    Vec3 lightNormal{};
    const Vec3 lightPoint = lightSampleTrianglePoint(tri, uBary, vBary, lightNormal);
    const Vec3 toLight = vecSub3(lightPoint, shadingPoint);
    const float distanceSq = vecDot3(toLight, toLight);
    if (distanceSq <= 1.0e-8f) {
        return false;
    }
    const float distance = sqrtf(distanceSq);
    outWi = vecScale3(toLight, 1.0f / distance);

    const float cosAtLight = vecMax2(0.0f, vecDot3(lightNormal, vecScale3(outWi, -1.0f)));
    if (cosAtLight <= 0.0f) {
        return false;
    }

    const MaterialGpu& material = scene->materials[tri.materialIndex < scene->materialCount ? tri.materialIndex : 0];
    const Vec3 baseColor = bsdfBaseColor(material);
    outRadiance = vecMake3(
        baseColor.x * material.emission,
        baseColor.y * material.emission,
        baseColor.z * material.emission);

    float totalArea = 0.0f;
    for (uint32_t i = 0; i < scene->emissiveTriangleCount; ++i) {
        const uint32_t index = scene->emissiveTriangleIndices[i];
        if (index < scene->triangleCount) {
            totalArea += lightTriangleArea(scene->triangles[index]);
        }
    }
    const float triArea = lightTriangleArea(tri);
    if (totalArea <= 0.0f || triArea <= 0.0f) {
        return false;
    }

    pdf = (triArea / totalArea) / triArea;
    pdf *= distanceSq / cosAtLight;
    return pdf > LightCoreDetail::kMinPdf;
}

LIGHT_CORE_FN float lightPdfEmissiveTriangle(
    const MeshAccelSceneGpu* scene,
    Vec3 shadingPoint,
    Vec3 wi,
    uint32_t triIndex)
{
    if (scene == nullptr || scene->emissiveTriangleCount == 0 || triIndex >= scene->triangleCount) {
        return 0.0f;
    }

    const TriangleGpu& tri = scene->triangles[triIndex];
    Vec3 lightNormal = vecNormalize3(vecCross3(vecSub3(tri.v1, tri.v0), vecSub3(tri.v2, tri.v0)));
    const Vec3 centroid = vecScale3(vecAdd3(vecAdd3(tri.v0, tri.v1), tri.v2), 1.0f / 3.0f);
    const Vec3 toLight = vecSub3(centroid, shadingPoint);
    const float distanceSq = vecDot3(toLight, toLight);
    if (distanceSq <= 1.0e-8f) {
        return 0.0f;
    }
    const float cosAtLight = vecMax2(0.0f, vecDot3(lightNormal, vecScale3(vecNormalize3(toLight), -1.0f)));
    if (cosAtLight <= 0.0f) {
        return 0.0f;
    }

    float totalArea = 0.0f;
    for (uint32_t i = 0; i < scene->emissiveTriangleCount; ++i) {
        const uint32_t index = scene->emissiveTriangleIndices[i];
        if (index < scene->triangleCount) {
            totalArea += lightTriangleArea(scene->triangles[index]);
        }
    }
    const float triArea = lightTriangleArea(tri);
    if (totalArea <= 0.0f || triArea <= 0.0f) {
        return 0.0f;
    }

    float pdf = (triArea / totalArea) / triArea;
    pdf *= distanceSq / cosAtLight;
    return pdf;
}

LIGHT_CORE_FN bool lightIsOccluded(
    Vec3 position,
    Vec3 normal,
    Vec3 wi,
    const MeshAccelSceneGpu* scene)
{
    const Vec3 origin = vecAdd3(position, vecScale3(normal, LightCoreDetail::kShadowBias));
    const MeshHit shadowHit = meshAccelTraceRay(
        origin, wi, scene, LightCoreDetail::kShadowBias, LightCoreDetail::kRayTMax);
    return shadowHit.hit;
}

LIGHT_CORE_FN MaterialGpu lightFetchMaterial(const MeshAccelSceneGpu* scene, uint32_t triangleIndex)
{
    MaterialGpu fallback{};
    fallback.r = 0.8f;
    fallback.g = 0.8f;
    fallback.b = 0.8f;
    if (scene == nullptr || scene->materials == nullptr || triangleIndex >= scene->triangleCount) {
        return fallback;
    }
    const uint32_t materialIndex = scene->triangles[triangleIndex].materialIndex;
    if (materialIndex >= scene->materialCount) {
        return fallback;
    }
    return scene->materials[materialIndex];
}

#undef LIGHT_CORE_FN
