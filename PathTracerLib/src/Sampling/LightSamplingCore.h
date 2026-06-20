#pragma once

#include "Geometry/MathCore.h"
#include "MeshAccel/MeshAccelIntersectCore.h"
#include "MeshAccel/MeshAccelTypes.h"
#include "RenderTypes.h"

#include <cmath>

#if defined(__CUDACC__)
#define LIGHT_CORE_FN __host__ __device__ inline
#else
#define LIGHT_CORE_FN inline
#endif

namespace LightCoreDetail {

constexpr float kPi = 3.14159265f;
constexpr float kMinPdf = 1.0e-8f;
constexpr float kShadowBias = 1.0e-4f;
constexpr float kRayTMax = 1.0e6f;

} // namespace LightCoreDetail

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

LIGHT_CORE_FN Vec3 lightSolidEnvironmentRadiance(const RenderParamsGpu* params)
{
    if (params == nullptr) {
        return vecMake3(0.04f, 0.04f, 0.04f);
    }
    const float intensity = params->environmentIntensity;
    return vecMake3(
        params->backgroundR * intensity,
        params->backgroundG * intensity,
        params->backgroundB * intensity);
}

LIGHT_CORE_FN Vec3 lightEvalEnvironmentOrBackground(
    const EnvironmentMapGpu* env,
    const RenderParamsGpu* params,
    Vec3 direction)
{
    if (env != nullptr && env->valid != 0) {
        return lightEvalEnvironment(env, direction);
    }
    return lightSolidEnvironmentRadiance(params);
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
    pdf = (marginalPdf * rowPdf) * static_cast<float>(env->width * env->height) /
        (2.0f * LightCoreDetail::kPi * LightCoreDetail::kPi * sinTheta);
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
    return (marginalPdf * rowPdf) * static_cast<float>(env->width * env->height) /
        (2.0f * LightCoreDetail::kPi * LightCoreDetail::kPi * sinTheta);
}

LIGHT_CORE_FN Vec3 lightSampleSolidEnvironment(float u1, float u2, float& pdf)
{
    pdf = 1.0f / (4.0f * LightCoreDetail::kPi);
    return lightLatLongToDirection(u1, u2);
}

LIGHT_CORE_FN float lightPdfSolidEnvironment(Vec3 wi)
{
    (void)wi;
    return 1.0f / (4.0f * LightCoreDetail::kPi);
}

LIGHT_CORE_FN Vec3 lightSampleEnvironmentOrBackground(
    const EnvironmentMapGpu* env,
    const RenderParamsGpu* params,
    float uMarginal,
    float uRow,
    float& pdf)
{
    if (env != nullptr && env->valid != 0) {
        return lightSampleEnvironment(env, uMarginal, uRow, pdf);
    }
    (void)params;
    return lightSampleSolidEnvironment(uMarginal, uRow, pdf);
}

LIGHT_CORE_FN float lightPdfEnvironmentOrBackground(
    const EnvironmentMapGpu* env,
    const RenderParamsGpu* params,
    Vec3 wi)
{
    if (env != nullptr && env->valid != 0) {
        return lightPdfEnvironment(env, wi);
    }
    (void)params;
    return lightPdfSolidEnvironment(wi);
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

LIGHT_CORE_FN float lightTriangleArea(const TriangleGpu& tri)
{
    const Vec3 e1 = vecSub3(tri.v1, tri.v0);
    const Vec3 e2 = vecSub3(tri.v2, tri.v0);
    const Vec3 cross = vecCross3(e1, e2);
    return 0.5f * vecLength3(cross);
}

LIGHT_CORE_FN Vec3 lightTriangleNormal(const TriangleGpu& tri)
{
    const Vec3 e1 = vecSub3(tri.v1, tri.v0);
    const Vec3 e2 = vecSub3(tri.v2, tri.v0);
    return vecNormalize3(vecCross3(e1, e2));
}

LIGHT_CORE_FN Vec3 lightEmissiveRadiance(const MaterialGpu& material)
{
    return vecMake3(
        material.r * material.emission,
        material.g * material.emission,
        material.b * material.emission);
}

LIGHT_CORE_FN float lightEmissiveSelectionPdf(
    const MeshAccelSceneGpu* scene,
    uint32_t emissiveListIndex)
{
    if (scene == nullptr || scene->emissiveTriangleCdf == nullptr || scene->emissiveTriangleCount == 0) {
        return 0.0f;
    }
    if (emissiveListIndex >= scene->emissiveTriangleCount) {
        return 0.0f;
    }
    const float total = scene->emissiveTriangleCdf[scene->emissiveTriangleCount];
    if (total <= LightCoreDetail::kMinPdf) {
        return 0.0f;
    }
    const float prev = scene->emissiveTriangleCdf[emissiveListIndex];
    const float next = scene->emissiveTriangleCdf[emissiveListIndex + 1];
    return (next - prev) / total;
}

LIGHT_CORE_FN bool lightSampleEmissiveTriangle(
    const MeshAccelSceneGpu* scene,
    float uTri,
    float u1,
    float u2,
    Vec3& outPosition,
    Vec3& outNormal,
    Vec3& outRadiance,
    float& pdf)
{
    pdf = 0.0f;
    if (scene == nullptr || scene->emissiveTriangleCount == 0 || scene->emissiveTriangleIndices == nullptr ||
        scene->emissiveTriangleCdf == nullptr || scene->triangles == nullptr) {
        return false;
    }

    const float total = scene->emissiveTriangleCdf[scene->emissiveTriangleCount];
    if (total <= LightCoreDetail::kMinPdf) {
        return false;
    }

    const float target = uTri * total;
    int low = 0;
    int high = static_cast<int>(scene->emissiveTriangleCount) - 1;
    while (low < high) {
        const int mid = (low + high) / 2;
        if (scene->emissiveTriangleCdf[mid + 1] < target) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }

    const uint32_t emissiveListIndex = static_cast<uint32_t>(low);
    const uint32_t triangleIndex = scene->emissiveTriangleIndices[emissiveListIndex];
    const TriangleGpu& tri = scene->triangles[triangleIndex];
    const MaterialGpu material = lightFetchMaterial(scene, triangleIndex);

    const float sqrtU1 = sqrtf(vecMax2(0.0f, u1));
    const float b0 = 1.0f - sqrtU1;
    const float b1 = u2 * sqrtU1;
    const float b2 = 1.0f - b0 - b1;
    outPosition = vecAdd3(
        vecAdd3(vecScale3(tri.v0, b0), vecScale3(tri.v1, b1)),
        vecScale3(tri.v2, b2));
    outNormal = lightTriangleNormal(tri);
    outRadiance = lightEmissiveRadiance(material);

    const float area = lightTriangleArea(tri);
    if (area <= LightCoreDetail::kMinPdf) {
        return false;
    }

    const float triPdf = lightEmissiveSelectionPdf(scene, emissiveListIndex);
    pdf = triPdf / area;
    return pdf > LightCoreDetail::kMinPdf;
}

LIGHT_CORE_FN float lightPdfEmissiveTriangle(
    const MeshAccelSceneGpu* scene,
    Vec3 position,
    Vec3 wi,
    Vec3& outLightNormal)
{
    if (scene == nullptr || scene->emissiveTriangleCount == 0 || scene->emissiveTriangleIndices == nullptr ||
        scene->emissiveTriangleCdf == nullptr || scene->triangles == nullptr) {
        return 0.0f;
    }

    const float total = scene->emissiveTriangleCdf[scene->emissiveTriangleCount];
    if (total <= LightCoreDetail::kMinPdf) {
        return 0.0f;
    }

    float pdfSum = 0.0f;
    for (uint32_t emissiveListIndex = 0; emissiveListIndex < scene->emissiveTriangleCount; ++emissiveListIndex) {
        const uint32_t triangleIndex = scene->emissiveTriangleIndices[emissiveListIndex];
        const TriangleGpu& tri = scene->triangles[triangleIndex];
        const Vec3 normal = lightTriangleNormal(tri);
        const float cosLight = vecMax2(0.0f, vecDot3(normal, vecScale3(wi, -1.0f)));
        if (cosLight <= 0.0f) {
            continue;
        }

        const float area = lightTriangleArea(tri);
        if (area <= LightCoreDetail::kMinPdf) {
            continue;
        }

        const float dist2 = vecDot3(wi, wi);
        if (dist2 <= LightCoreDetail::kMinPdf) {
            continue;
        }

        const float triPdf = lightEmissiveSelectionPdf(scene, emissiveListIndex);
        const float solidAnglePdf = triPdf * dist2 / (area * cosLight);
        pdfSum += solidAnglePdf;
        outLightNormal = normal;
    }

    return pdfSum;
}

LIGHT_CORE_FN bool lightIsOccludedFrom(
    Vec3 position,
    Vec3 wi,
    const MeshAccelSceneGpu* scene)
{
    const Vec3 origin = vecAdd3(position, vecScale3(wi, LightCoreDetail::kShadowBias));
    const MeshHit shadowHit = meshAccelTraceRay(
        origin, wi, scene, LightCoreDetail::kShadowBias, LightCoreDetail::kRayTMax);
    return shadowHit.hit;
}

#undef LIGHT_CORE_FN
