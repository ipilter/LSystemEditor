#pragma once

#include "Brdf/BrdfBase.h"
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

#undef LIGHT_CORE_FN
