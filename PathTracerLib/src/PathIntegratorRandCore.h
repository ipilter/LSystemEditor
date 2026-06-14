#pragma once

#include "Brdf/BrdfDispatch.h"
#include "Geometry/MathCore.h"
#include "MeshAccel/MeshAccelIntersectCore.h"
#include "MeshAccel/MeshAccelTypes.h"
#include "RenderTypes.h"
#include "Sampling/LightSamplingCore.h"
#include "Sampling/MisCore.h"
#include "Sampling/RandCore.h"

#include <cmath>

#if defined(__CUDACC__)
#define PATH_INTEGRATOR_RAND_FN __device__ __forceinline__
#else
#define PATH_INTEGRATOR_RAND_FN inline
#endif

namespace PathIntegratorRandDetail {

constexpr float kRayTMax = 1.0e6f;
constexpr float kShadowBias = 1.0e-4f;
constexpr float kMinPdf = 1.0e-8f;
constexpr int kRussianRouletteStartDepth = 3;
constexpr float kMaxRussianRouletteProb = 0.95f;

} // namespace PathIntegratorRandDetail

PATH_INTEGRATOR_RAND_FN Vec3 pathIntegratorRandEmission(const MaterialGpu& material)
{
    const Vec3 baseColor = brdfBaseColor(material);
    return vecMake3(
        baseColor.x * material.emission,
        baseColor.y * material.emission,
        baseColor.z * material.emission);
}

PATH_INTEGRATOR_RAND_FN Vec3 pathIntegratorRandEvaluateEnvironmentNee(
    Vec3 position,
    Vec3 normal,
    Vec3 wo,
    const MaterialGpu& material,
    const MeshAccelSceneGpu* scene,
    const EnvironmentMapGpu* env,
    curandState* rng)
{
    if (env == nullptr || env->valid == 0) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    float u1 = 0.0f;
    float u2 = 0.0f;
    rand02(rng, u1, u2);

    float lightPdf = 0.0f;
    const Vec3 wi = lightSampleEnvironment(env, u1, u2, lightPdf);
    if (lightPdf <= PathIntegratorRandDetail::kMinPdf) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    if (lightIsOccluded(position, normal, wi, scene)) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    const float cosTheta = vecMax2(0.0f, vecDot3(normal, wi));
    if (cosTheta <= 0.0f) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    const BrdfType brdfType = brdfForMaterial(material);
    const BrdfContext ctx{normal, wo, material};
    const Vec3 bsdf = brdfEval(brdfType, ctx, wi);
    const float bsdfPdfValue = brdfPdf(brdfType, ctx, wi);
    const Vec3 lightRadiance = lightEvalEnvironment(env, wi);
    const float misWeight = misBalanceWeight(lightPdf, bsdfPdfValue);
    const float scale = misWeight * cosTheta / lightPdf;
    return vecMake3(
        bsdf.x * lightRadiance.x * scale,
        bsdf.y * lightRadiance.y * scale,
        bsdf.z * lightRadiance.z * scale);
}

PATH_INTEGRATOR_RAND_FN Vec3 tracePathRandFromHit(
    const MeshHit& hit,
    Vec3 rayOrigin,
    Vec3 rayDir,
    const MeshAccelSceneGpu* scene,
    const RenderParamsGpu* params,
    const EnvironmentMapGpu* env,
    curandState* rng)
{
    if (!hit.hit) {
        return lightEvalEnvironmentOrBackground(env, params, rayDir);
    }

    int maxDepth = 8;
    int rrMinDepth = PathIntegratorRandDetail::kRussianRouletteStartDepth;
    if (params != nullptr) {
        if (params->maxPathDepth > 0) {
            maxDepth = params->maxPathDepth;
        }
        if (params->russianRouletteMinDepth >= 0) {
            rrMinDepth = params->russianRouletteMinDepth;
        }
    }

    Vec3 radiance = vecMake3(0.0f, 0.0f, 0.0f);
    Vec3 throughput = vecMake3(1.0f, 1.0f, 1.0f);
    Vec3 currentOrigin = rayOrigin;
    Vec3 currentDir = rayDir;
    MeshHit currentHit = hit;

    for (int depth = 0; depth < maxDepth; ++depth) {
        const Vec3 position = vecEvalRay(currentOrigin, currentDir, currentHit.t);
        const Vec3 normal = currentHit.normal;
        const Vec3 wo = vecScale3(currentDir, -1.0f);
        const MaterialGpu material = lightFetchMaterial(scene, currentHit.triangleIndex);

        const Vec3 emission = pathIntegratorRandEmission(material);
        radiance = vecAdd3(radiance, vecMake3(
            throughput.x * emission.x,
            throughput.y * emission.y,
            throughput.z * emission.z));

        const Vec3 direct = pathIntegratorRandEvaluateEnvironmentNee(
            position, normal, wo, material, scene, env, rng);
        radiance = vecAdd3(radiance, vecMake3(
            throughput.x * direct.x,
            throughput.y * direct.y,
            throughput.z * direct.z));

        const BrdfType brdfType = brdfForMaterial(material);
        const BrdfContext ctx{normal, wo, material};

        float u1 = 0.0f;
        float u2 = 0.0f;
        rand02(rng, u1, u2);
        const BrdfSampleResult sample = brdfSample(brdfType, ctx, u1, u2);
        if (!sample.valid || sample.pdf <= PathIntegratorRandDetail::kMinPdf) {
            break;
        }

        const Vec3 bsdfValue = brdfEval(brdfType, ctx, sample.direction);
        const float cosTheta = vecMax2(0.0f, vecDot3(normal, sample.direction));
        throughput = vecMake3(
            throughput.x * bsdfValue.x * cosTheta / sample.pdf,
            throughput.y * bsdfValue.y * cosTheta / sample.pdf,
            throughput.z * bsdfValue.z * cosTheta / sample.pdf);

        if (depth >= rrMinDepth) {
            const float survivalProb = vecMin2(
                PathIntegratorRandDetail::kMaxRussianRouletteProb,
                vecMax2(brdfThroughputLuminance(throughput), 0.05f));
            if (rand01(rng) > survivalProb) {
                break;
            }
            throughput = vecScale3(throughput, 1.0f / survivalProb);
        }

        currentOrigin = vecAdd3(position, vecScale3(normal, PathIntegratorRandDetail::kShadowBias));
        currentDir = sample.direction;
        currentHit = meshAccelTraceRay(
            currentOrigin,
            currentDir,
            scene,
            PathIntegratorRandDetail::kShadowBias,
            PathIntegratorRandDetail::kRayTMax);
        if (!currentHit.hit) {
            const Vec3 envRadiance = lightEvalEnvironmentOrBackground(env, params, currentDir);
            float misWeight = 1.0f;
            if (env != nullptr && env->valid != 0) {
                const float lightPdf = lightPdfEnvironment(env, currentDir);
                misWeight = misBalanceWeight(sample.pdf, lightPdf);
            }
            radiance = vecAdd3(radiance, vecMake3(
                throughput.x * envRadiance.x * misWeight,
                throughput.y * envRadiance.y * misWeight,
                throughput.z * envRadiance.z * misWeight));
            break;
        }
    }

    return radiance;
}

PATH_INTEGRATOR_RAND_FN Vec3 tracePathRand(
    Vec3 rayOrigin,
    Vec3 rayDir,
    const MeshAccelSceneGpu* scene,
    const RenderParamsGpu* params,
    const EnvironmentMapGpu* env,
    curandState* rng)
{
    const MeshHit hit = meshAccelTraceRay(
        rayOrigin, rayDir, scene, 0.0f, PathIntegratorRandDetail::kRayTMax);
    return tracePathRandFromHit(hit, rayOrigin, rayDir, scene, params, env, rng);
}

#undef PATH_INTEGRATOR_RAND_FN
