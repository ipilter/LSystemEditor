#pragma once

#include "Brdf/BrdfDispatch.h"
#include "Geometry/MathCore.h"
#include "Material/MaterialType.h"
#include "MeshAccel/MeshAccelIntersectCore.h"
#include "MeshAccel/MeshAccelTypes.h"
#include "Path/PathState.h"
#include "RenderTypes.h"
#include "Sampling/LightSamplingCore.h"
#include "Sampling/MisCore.h"
#include "Sampling/RandCore.h"
#include "SceneUnits.h"
#include "Texture/ProceduralTexture.h"

#include <cmath>

#if defined(__CUDACC__)
#define PATH_INTEGRATOR_CORE_FN __device__ __forceinline__
#else
#define PATH_INTEGRATOR_CORE_FN inline
#endif

namespace PathIntegratorCoreDetail {

constexpr float kRayTMax = SceneUnits::kDefaultRayTMaxMm;
constexpr float kMinPdf = 1.0e-8f;
constexpr int kRussianRouletteStartDepth = 3;
constexpr float kMaxRussianRouletteProb = 0.95f;
constexpr int kDefaultMaxPathDepth = 32;
constexpr int kUnlimitedPathDepth = 256;

} // namespace PathIntegratorCoreDetail

PATH_INTEGRATOR_CORE_FN MaterialGpu pathIntegratorResolveMaterial(
    const MaterialGpu& material,
    Vec2 uv,
    const MeshAccelSceneGpu* scene)
{
    const TextureEvalContext textureCtx{uv, uv.x};
    const ResolvedMaterial resolved = resolveMaterial(
        material,
        textureCtx,
        scene != nullptr ? scene->textures : nullptr,
        scene != nullptr ? scene->textureCount : 0u);
    MaterialGpu result = materialFromResolved(resolved);
    result.materialType = material.materialType;
    result.subsurface = material.subsurface;
    result.subsurfaceRadiusR = material.subsurfaceRadiusR;
    result.subsurfaceRadiusG = material.subsurfaceRadiusG;
    result.subsurfaceRadiusB = material.subsurfaceRadiusB;
    return result;
}

PATH_INTEGRATOR_CORE_FN Vec3 pathIntegratorEvaluateEnvironmentNee(
    Vec3 position,
    Vec3 normal,
    Vec3 wo,
    const MaterialGpu& material,
    Vec3 throughput,
    const MeshAccelSceneGpu* scene,
    const EnvironmentMapGpu* env,
    const RenderParamsGpu* params,
    curandState* rng,
    float hitDistanceMm,
    uint32_t sourceTriangleIndex)
{
    if (brdfSkipsEnvironmentNee(material, 1.0f)) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    float u1 = 0.0f;
    float u2 = 0.0f;
    rand02(rng, u1, u2);

    float lightPdf = 0.0f;
    const Vec3 wi = lightSampleEnvironmentOrBackground(env, params, u1, u2, lightPdf);
    if (lightPdf <= PathIntegratorCoreDetail::kMinPdf) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    if (lightIsOccluded(position, normal, wi, scene, hitDistanceMm, sourceTriangleIndex)) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    const float cosTheta = vecMax2(0.0f, vecDot3(normal, wi));
    if (cosTheta <= 0.0f) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    const BrdfContext ctx{normal, wo, material, 1.0f, 550.0f, 0};
    const Vec3 lightRadiance = lightEvalEnvironmentOrBackground(env, params, wi);
    const Vec3 bsdf = brdfEval(ctx, wi);
    const float bsdfPdfValue = brdfPdf(ctx, wi);
    const float misWeight = misBalanceWeight(lightPdf, bsdfPdfValue);
    const Vec3 scale = vecScale3(bsdf, misWeight * cosTheta / lightPdf);

    return vecMake3(
        throughput.x * lightRadiance.x * scale.x,
        throughput.y * lightRadiance.y * scale.y,
        throughput.z * lightRadiance.z * scale.z);
}

PATH_INTEGRATOR_CORE_FN Vec3 pathIntegratorEvaluateEmissiveNee(
    Vec3 position,
    Vec3 normal,
    Vec3 wo,
    const MaterialGpu& material,
    Vec3 throughput,
    const MeshAccelSceneGpu* scene,
    curandState* rng,
    float hitDistanceMm,
    uint32_t sourceTriangleIndex)
{
    if (scene == nullptr || scene->emissiveTriangleCount == 0) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    float uTri = 0.0f;
    float u1 = 0.0f;
    float u2 = 0.0f;
    rand02(rng, uTri, u1);
    u2 = rand01(rng);

    Vec3 lightPosition{};
    Vec3 lightNormal{};
    Vec3 lightRadiance{};
    float areaPdf = 0.0f;
    if (!lightSampleEmissiveTriangle(scene, uTri, u1, u2, lightPosition, lightNormal, lightRadiance, areaPdf)) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    const Vec3 toLight = vecSub3(lightPosition, position);
    const float dist2 = vecDot3(toLight, toLight);
    if (dist2 <= PathIntegratorCoreDetail::kMinPdf) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    const Vec3 wi = vecNormalize3(toLight);
    const float cosTheta = vecMax2(0.0f, vecDot3(normal, wi));
    const float cosLight = vecMax2(0.0f, vecDot3(lightNormal, vecScale3(wi, -1.0f)));
    if (cosTheta <= 0.0f || cosLight <= 0.0f) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    if (lightIsOccludedFrom(position, wi, scene, hitDistanceMm, sourceTriangleIndex)) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    const float lightPdf = areaPdf * dist2 / cosLight;
    if (lightPdf <= PathIntegratorCoreDetail::kMinPdf) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    const BrdfContext ctx{normal, wo, material, 1.0f, 550.0f, 0};
    const Vec3 bsdf = brdfEval(ctx, wi);
    const float bsdfPdfValue = brdfPdf(ctx, wi);
    const float misWeight = misBalanceWeight(lightPdf, bsdfPdfValue);
    const Vec3 scale = vecScale3(bsdf, misWeight * cosTheta / lightPdf);

    return vecMake3(
        throughput.x * lightRadiance.x * scale.x,
        throughput.y * lightRadiance.y * scale.y,
        throughput.z * lightRadiance.z * scale.z);
}

PATH_INTEGRATOR_CORE_FN bool pathIntegratorContinueAfterTrace(
    PathState& path,
    Vec3& radiance,
    const BrdfContext& ctx,
    const BrdfSampleResult& sample,
    Vec3 position,
    Vec3 normal,
    Vec3& currentOrigin,
    Vec3& currentDir,
    MeshHit& currentHit,
    const MeshAccelSceneGpu* scene,
    const EnvironmentMapGpu* env,
    const RenderParamsGpu* params,
    float continuationBias)
{
    const Vec3 bsdfValue = brdfEval(ctx, sample.direction);
    path.throughput = brdfApplyThroughput(path.throughput, ctx, sample, bsdfValue);

    const float biasSign = vecDot3(normal, sample.direction) >= 0.0f ? 1.0f : -1.0f;
    currentOrigin = vecAdd3(position, vecScale3(normal, biasSign * continuationBias));
    currentDir = sample.direction;
    currentHit = meshAccelTraceRay(
        currentOrigin,
        currentDir,
        scene,
        continuationBias,
        PathIntegratorCoreDetail::kRayTMax);
    if (!currentHit.hit) {
        const Vec3 envRadiance = lightEvalEnvironmentOrBackground(env, params, currentDir);
        const float lightPdf = lightPdfEnvironmentOrBackground(env, params, currentDir);
        const float misWeight = misBalanceWeight(sample.pdf, lightPdf);
        radiance = vecAdd3(
            radiance,
            vecMake3(
                path.throughput.x * envRadiance.x * misWeight,
                path.throughput.y * envRadiance.y * misWeight,
                path.throughput.z * envRadiance.z * misWeight));
        return false;
    }
    return true;
}

PATH_INTEGRATOR_CORE_FN Vec3 tracePathCoreFromHit(
    const MeshHit& hit,
    Vec3 rayOrigin,
    Vec3 rayDir,
    const MeshAccelSceneGpu* scene,
    const RenderParamsGpu* params,
    const EnvironmentMapGpu* env,
    curandState* rng)
{
    PathState path{};
    path.throughput = vecMake3(1.0f, 1.0f, 1.0f);

    if (!hit.hit) {
        return lightEvalEnvironmentOrBackground(env, params, rayDir);
    }

    int maxDepth = PathIntegratorCoreDetail::kDefaultMaxPathDepth;
    int rrMinDepth = PathIntegratorCoreDetail::kRussianRouletteStartDepth;
    if (params != nullptr) {
        if (params->maxPathDepth > 0) {
            maxDepth = params->maxPathDepth;
        } else if (params->maxPathDepth == 0) {
            maxDepth = PathIntegratorCoreDetail::kUnlimitedPathDepth;
        }
        if (params->russianRouletteMinDepth >= 0) {
            rrMinDepth = params->russianRouletteMinDepth;
        }
    }

    Vec3 radiance = vecMake3(0.0f, 0.0f, 0.0f);
    Vec3 currentOrigin = rayOrigin;
    Vec3 currentDir = rayDir;
    MeshHit currentHit = hit;

    for (int depth = 0; depth < maxDepth; ++depth) {
        const Vec3 position = vecEvalRay(currentOrigin, currentDir, currentHit.t);
        const Vec3 normal = currentHit.normal;
        const Vec3 wo = vecScale3(currentDir, -1.0f);
        const MaterialGpu sourceMaterial = lightFetchMaterial(scene, currentHit.triangleIndex);
        const MaterialGpu material = pathIntegratorResolveMaterial(sourceMaterial, currentHit.uv, scene);

        const Vec3 emission = lightEmissiveRadiance(material);
        radiance = vecAdd3(
            radiance,
            vecMake3(
                path.throughput.x * emission.x,
                path.throughput.y * emission.y,
                path.throughput.z * emission.z));

        radiance = vecAdd3(
            radiance,
            pathIntegratorEvaluateEnvironmentNee(
                position,
                normal,
                wo,
                material,
                path.throughput,
                scene,
                env,
                params,
                rng,
                currentHit.t,
                currentHit.triangleIndex));

        radiance = vecAdd3(
            radiance,
            pathIntegratorEvaluateEmissiveNee(
                position,
                normal,
                wo,
                material,
                path.throughput,
                scene,
                rng,
                currentHit.t,
                currentHit.triangleIndex));

        const BrdfContext ctx{normal, wo, material, 1.0f, 550.0f, 0};

        float u1 = 0.0f;
        float u2 = 0.0f;
        rand02(rng, u1, u2);
        const BrdfSampleResult sample = brdfSampleReflect(ctx, u1, u2);

        if (!sample.valid || sample.pdf <= PathIntegratorCoreDetail::kMinPdf) {
            break;
        }

        const float continuationBias = SceneUnits::rayEpsilonMm(
            currentHit.t,
            scene != nullptr ? scene->sceneExtentMm : 0.0f);

        if (!pathIntegratorContinueAfterTrace(
                path,
                radiance,
                ctx,
                sample,
                position,
                normal,
                currentOrigin,
                currentDir,
                currentHit,
                scene,
                env,
                params,
                continuationBias)) {
            break;
        }

        const float throughputLuminance = brdfThroughputLuminance(path.throughput);
        if (!isfinite(throughputLuminance) || throughputLuminance <= 0.0f || throughputLuminance > 1.0e8f) {
            break;
        }

        if (depth >= rrMinDepth) {
            const float survivalProb = vecMin2(
                PathIntegratorCoreDetail::kMaxRussianRouletteProb,
                vecMax2(throughputLuminance, 0.05f));
            if (rand01(rng) > survivalProb) {
                break;
            }
            path.throughput = vecScale3(path.throughput, 1.0f / survivalProb);
        }
    }

    return radiance;
}

PATH_INTEGRATOR_CORE_FN Vec3 tracePathCore(
    Vec3 rayOrigin,
    Vec3 rayDir,
    const MeshAccelSceneGpu* scene,
    const RenderParamsGpu* params,
    const EnvironmentMapGpu* env,
    curandState* rng)
{
    const float primaryEpsilon = SceneUnits::rayEpsilonMm(
        0.0f,
        scene != nullptr ? scene->sceneExtentMm : 0.0f);
    const MeshHit hit = meshAccelTraceRay(
        rayOrigin, rayDir, scene, primaryEpsilon, PathIntegratorCoreDetail::kRayTMax);
    return tracePathCoreFromHit(hit, rayOrigin, rayDir, scene, params, env, rng);
}

PATH_INTEGRATOR_CORE_FN Vec3 tracePathRand(
    Vec3 rayOrigin,
    Vec3 rayDir,
    const MeshAccelSceneGpu* scene,
    const RenderParamsGpu* params,
    const EnvironmentMapGpu* env,
    curandState* rng)
{
    return tracePathCore(rayOrigin, rayDir, scene, params, env, rng);
}

#undef PATH_INTEGRATOR_CORE_FN
