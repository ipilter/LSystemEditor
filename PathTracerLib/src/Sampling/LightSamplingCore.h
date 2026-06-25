#pragma once

#include "Geometry/MathCore.h"
#include "MeshAccel/MeshAccelIntersectCore.h"
#include "MeshAccel/MeshAccelTypes.h"
#include "RenderTypes.h"
#include "Sampling/MisCore.h"
#include "SceneUnits.h"
#include "Spectral/SpectralCore.h"

#include <cmath>

#if defined(__CUDACC__)
#define LIGHT_CORE_FN __host__ __device__ inline
#else
#define LIGHT_CORE_FN inline
#endif

namespace LightCoreDetail {

constexpr float kPi = 3.14159265f;
constexpr float kMinPdf = 1.0e-8f;
constexpr float kRayTMax = SceneUnits::kDefaultRayTMaxMm;

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

LIGHT_CORE_FN float lightEnvironmentRotationYRad(const RenderParamsGpu* params)
{
    if (params == nullptr) {
        return 0.0f;
    }
    return static_cast<float>(params->environmentRotationYDeg) * (LightCoreDetail::kPi / 180.0f);
}

LIGHT_CORE_FN Vec3 lightRotateY(float angleRad, Vec3 direction)
{
    const Vec3 d = vecNormalize3(direction);
    const float c = cosf(angleRad);
    const float s = sinf(angleRad);
    return vecMake3(d.x * c - d.z * s, d.y, d.x * s + d.z * c);
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
    const float rotationRad = lightEnvironmentRotationYRad(params);
    if (env != nullptr && env->valid != 0) {
        return lightEvalEnvironment(env, lightRotateY(-rotationRad, direction));
    }
    return lightSolidEnvironmentRadiance(params);
}

LIGHT_CORE_FN float lightEvalEnvironmentSpectral(
    const EnvironmentMapGpu* env,
    const RenderParamsGpu* params,
    Vec3 direction,
    float lambdaNm)
{
    const Vec3 rgb = lightEvalEnvironmentOrBackground(env, params, direction);
    return spectralEnvironmentRadianceAtWavelength(rgb, lambdaNm);
}

LIGHT_CORE_FN float lightEmissiveRadianceSpectral(const MaterialGpu& material, float lambdaNm)
{
    const float albedo = spectralRgbReflectanceAtWavelength(
        material.r, material.g, material.b, lambdaNm);
    return albedo * material.emission;
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
    const float rotationRad = lightEnvironmentRotationYRad(params);
    if (env != nullptr && env->valid != 0) {
        return lightRotateY(rotationRad, lightSampleEnvironment(env, uMarginal, uRow, pdf));
    }
    (void)params;
    return lightRotateY(rotationRad, lightSampleSolidEnvironment(uMarginal, uRow, pdf));
}

LIGHT_CORE_FN float lightPdfEnvironmentOrBackground(
    const EnvironmentMapGpu* env,
    const RenderParamsGpu* params,
    Vec3 wi)
{
    const float rotationRad = lightEnvironmentRotationYRad(params);
    if (env != nullptr && env->valid != 0) {
        return lightPdfEnvironment(env, lightRotateY(-rotationRad, wi));
    }
    (void)params;
    return lightPdfSolidEnvironment(wi);
}

LIGHT_CORE_FN float lightSceneExtentMm(const MeshAccelSceneGpu* scene)
{
    if (scene == nullptr) {
        return 0.0f;
    }
    return scene->sceneExtentMm;
}

LIGHT_CORE_FN Vec3 lightShadowOffsetNormal(
    Vec3 shadingNormal,
    const MeshAccelSceneGpu* scene,
    uint32_t sourceTriangleIndex)
{
    if (scene == nullptr || scene->triangles == nullptr || sourceTriangleIndex >= scene->triangleCount) {
        return shadingNormal;
    }

    Vec3 offsetNormal = meshAccelTriangleGeometricNormal(scene->triangles[sourceTriangleIndex]);
    if (vecDot3(offsetNormal, shadingNormal) < 0.0f) {
        offsetNormal = vecScale3(offsetNormal, -1.0f);
    }
    return offsetNormal;
}

LIGHT_CORE_FN bool lightShadowHitOccludes(
    const MeshHit& shadowHit,
    uint32_t sourceTriangleIndex,
    float epsilon)
{
    if (!shadowHit.hit) {
        return false;
    }
    if (sourceTriangleIndex < UINT32_MAX && shadowHit.triangleIndex == sourceTriangleIndex &&
        shadowHit.t <= 2.0f * epsilon) {
        return false;
    }
    return true;
}

LIGHT_CORE_FN bool lightIsOccluded(
    Vec3 position,
    Vec3 shadingNormal,
    Vec3 wi,
    const MeshAccelSceneGpu* scene,
    float hitDistanceMm,
    uint32_t sourceTriangleIndex)
{
    const float epsilon = SceneUnits::rayEpsilonMm(hitDistanceMm, lightSceneExtentMm(scene));
    const Vec3 offsetNormal = lightShadowOffsetNormal(shadingNormal, scene, sourceTriangleIndex);
    const Vec3 origin = vecAdd3(position, vecScale3(offsetNormal, epsilon));
    MeshHit shadowHit = meshAccelTraceRay(
        origin, wi, scene, epsilon, LightCoreDetail::kRayTMax);
    if (lightShadowHitOccludes(shadowHit, sourceTriangleIndex, epsilon)) {
        return true;
    }
    if (shadowHit.hit) {
        shadowHit = meshAccelTraceRay(
            origin, wi, scene, shadowHit.t + epsilon, LightCoreDetail::kRayTMax);
        return lightShadowHitOccludes(shadowHit, sourceTriangleIndex, epsilon);
    }
    return false;
}

LIGHT_CORE_FN bool lightIsOccludedBefore(
    Vec3 position,
    Vec3 shadingNormal,
    Vec3 wi,
    float maxDistanceMm,
    const MeshAccelSceneGpu* scene,
    float hitDistanceMm,
    uint32_t sourceTriangleIndex)
{
    if (maxDistanceMm <= LightCoreDetail::kMinPdf) {
        return true;
    }

    const float epsilon = SceneUnits::rayEpsilonMm(hitDistanceMm, lightSceneExtentMm(scene));
    const Vec3 offsetNormal = lightShadowOffsetNormal(shadingNormal, scene, sourceTriangleIndex);
    const Vec3 origin = vecAdd3(position, vecScale3(offsetNormal, epsilon));
    const float tMax = vecMax2(epsilon, maxDistanceMm - epsilon);
    const MeshHit shadowHit = meshAccelTraceRay(origin, wi, scene, epsilon, tMax);
    return lightShadowHitOccludes(shadowHit, sourceTriangleIndex, epsilon);
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

LIGHT_CORE_FN bool lightEmissiveListIndexForTriangle(
    const MeshAccelSceneGpu* scene,
    uint32_t triangleIndex,
    uint32_t& outEmissiveListIndex)
{
    if (scene == nullptr || scene->emissiveTriangleIndices == nullptr || scene->emissiveTriangleCount == 0) {
        return false;
    }
    for (uint32_t i = 0; i < scene->emissiveTriangleCount; ++i) {
        if (scene->emissiveTriangleIndices[i] == triangleIndex) {
            outEmissiveListIndex = i;
            return true;
        }
    }
    return false;
}

LIGHT_CORE_FN bool lightTriangleIsEmissive(
    const MeshAccelSceneGpu* scene,
    uint32_t triangleIndex)
{
    uint32_t ignored = 0;
    return lightEmissiveListIndexForTriangle(scene, triangleIndex, ignored);
}

LIGHT_CORE_FN float lightPdfEmissiveTriangleForIndex(
    const MeshAccelSceneGpu* scene,
    Vec3 fromPosition,
    Vec3 toPosition,
    uint32_t triangleIndex,
    Vec3& outLightNormal)
{
    if (scene == nullptr || scene->triangles == nullptr || triangleIndex >= scene->triangleCount) {
        return 0.0f;
    }

    uint32_t emissiveListIndex = 0;
    if (!lightEmissiveListIndexForTriangle(scene, triangleIndex, emissiveListIndex)) {
        return 0.0f;
    }

    const Vec3 toLight = vecSub3(toPosition, fromPosition);
    const float dist2 = vecDot3(toLight, toLight);
    if (dist2 <= LightCoreDetail::kMinPdf) {
        return 0.0f;
    }

    const TriangleGpu& tri = scene->triangles[triangleIndex];
    const Vec3 normal = lightTriangleNormal(tri);
    outLightNormal = normal;
    const Vec3 wi = vecNormalize3(toLight);
    const float cosLight = vecMax2(0.0f, vecDot3(normal, vecScale3(wi, -1.0f)));
    if (cosLight <= 0.0f) {
        return 0.0f;
    }

    const float area = lightTriangleArea(tri);
    if (area <= LightCoreDetail::kMinPdf) {
        return 0.0f;
    }

    const float triPdf = lightEmissiveSelectionPdf(scene, emissiveListIndex);
    return triPdf * dist2 / (area * cosLight);
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
        const Vec3 triCenter = vecScale3(
            vecAdd3(vecAdd3(tri.v0, tri.v1), tri.v2),
            1.0f / 3.0f);
        const float triPdf = lightPdfEmissiveTriangleForIndex(
            scene, position, triCenter, triangleIndex, outLightNormal);
        if (triPdf > LightCoreDetail::kMinPdf) {
            pdfSum += triPdf;
        }
    }

    return pdfSum;
}

LIGHT_CORE_FN Vec3 lightDirectEmissionWithMis(
    const MeshAccelSceneGpu* scene,
    Vec3 prevPosition,
    Vec3 hitPosition,
    uint32_t hitTriangleIndex,
    Vec3 throughput,
    Vec3 emission,
    float prevBsdfPdf,
    bool applyMis)
{
    const float emissionLuminance =
        emission.x * 0.2126f + emission.y * 0.7152f + emission.z * 0.0722f;
    if (emissionLuminance <= LightCoreDetail::kMinPdf) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    float misWeight = 1.0f;
    if (applyMis) {
        Vec3 lightNormal{};
        const float lightPdf = lightPdfEmissiveTriangleForIndex(
            scene, prevPosition, hitPosition, hitTriangleIndex, lightNormal);
        misWeight = misBalanceWeight(prevBsdfPdf, lightPdf);
    }

    return vecMake3(
        throughput.x * emission.x * misWeight,
        throughput.y * emission.y * misWeight,
        throughput.z * emission.z * misWeight);
}

LIGHT_CORE_FN bool lightIsOccludedFrom(
    Vec3 position,
    Vec3 wi,
    const MeshAccelSceneGpu* scene,
    float hitDistanceMm,
    uint32_t sourceTriangleIndex)
{
    const float epsilon = SceneUnits::rayEpsilonMm(hitDistanceMm, lightSceneExtentMm(scene));
    const Vec3 origin = vecAdd3(position, vecScale3(wi, epsilon));
    MeshHit shadowHit = meshAccelTraceRay(
        origin, wi, scene, epsilon, LightCoreDetail::kRayTMax);
    if (lightShadowHitOccludes(shadowHit, sourceTriangleIndex, epsilon)) {
        return true;
    }
    if (shadowHit.hit) {
        shadowHit = meshAccelTraceRay(
            origin, wi, scene, shadowHit.t + epsilon, LightCoreDetail::kRayTMax);
        return lightShadowHitOccludes(shadowHit, sourceTriangleIndex, epsilon);
    }
    return false;
}

LIGHT_CORE_FN bool lightIsOccludedFromBefore(
    Vec3 position,
    Vec3 wi,
    float maxDistanceMm,
    const MeshAccelSceneGpu* scene,
    float hitDistanceMm,
    uint32_t sourceTriangleIndex)
{
    if (maxDistanceMm <= LightCoreDetail::kMinPdf) {
        return true;
    }

    const float epsilon = SceneUnits::rayEpsilonMm(hitDistanceMm, lightSceneExtentMm(scene));
    const Vec3 origin = vecAdd3(position, vecScale3(wi, epsilon));
    const float tMax = vecMax2(epsilon, maxDistanceMm - epsilon);
    const MeshHit shadowHit = meshAccelTraceRay(origin, wi, scene, epsilon, tMax);
    return lightShadowHitOccludes(shadowHit, sourceTriangleIndex, epsilon);
}

#undef LIGHT_CORE_FN
