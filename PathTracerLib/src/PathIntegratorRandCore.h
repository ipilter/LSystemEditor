#pragma once

#include "Brdf/BrdfDispatch.h"
#include "Brdf/BrdfDebug.h"
#include "Brdf/SubsurfaceCore.h"
#include "Geometry/MathCore.h"
#include "MeshAccel/MeshAccelIntersectCore.h"
#include "MeshAccel/MeshAccelTypes.h"
#include "RenderTypes.h"
#include "Sampling/LightSamplingCore.h"
#include "Sampling/MisCore.h"
#include "Sampling/RandCore.h"
#include "Texture/ProceduralTexture.h"

#include <cmath>

#if defined(__CUDACC__)
#define PATH_INTEGRATOR_RAND_FN __device__ __forceinline__
#else
#define PATH_INTEGRATOR_RAND_FN inline
#endif

namespace PathIntegratorRandDetail {

constexpr float kRayTMax = 1.0e6f;
constexpr float kShadowBias = 1.0e-4f;
constexpr float kRelativeShadowBias = 1.0e-3f;
constexpr float kMinPdf = 1.0e-8f;
constexpr float kAirIor = 1.0f;
constexpr float kDefaultWavelengthNm = 550.0f;
constexpr int kRussianRouletteStartDepth = 3;
constexpr float kMaxRussianRouletteProb = 0.95f;
constexpr int kDefaultMaxPathDepth = 32;
constexpr int kUnlimitedPathDepth = 256;

} // namespace PathIntegratorRandDetail

PATH_INTEGRATOR_RAND_FN Vec3 vecMul3(Vec3 a, Vec3 b)
{
    return vecMake3(a.x * b.x, a.y * b.y, a.z * b.z);
}

PATH_INTEGRATOR_RAND_FN MaterialGpu pathIntegratorRandResolveMaterial(
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
    return materialFromResolved(resolved);
}

PATH_INTEGRATOR_RAND_FN Vec3 pathIntegratorRandEmission(const MaterialGpu& material)
{
    return vecMake3(
        material.r * material.emission,
        material.g * material.emission,
        material.b * material.emission);
}

PATH_INTEGRATOR_RAND_FN Vec3 pathIntegratorRandEvaluateEnvironmentNee(
    Vec3 position,
    Vec3 normal,
    Vec3 wo,
    const MaterialGpu& material,
    float etaMedium,
    float wavelengthNm,
    const MeshAccelSceneGpu* scene,
    const EnvironmentMapGpu* env,
    const RenderParamsGpu* params,
    curandState* rng)
{
    if (brdfSkipsEnvironmentNee(material)) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    float u1 = 0.0f;
    float u2 = 0.0f;
    rand02(rng, u1, u2);

    float lightPdf = 0.0f;
    const Vec3 wi = lightSampleEnvironmentOrBackground(env, params, u1, u2, lightPdf);
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

    const BrdfContext ctx{normal, wo, material, etaMedium, wavelengthNm};
    const Vec3 lightRadiance = lightEvalEnvironmentOrBackground(env, params, wi);
    const Vec3 bsdf = brdfEval(ctx, wi);
    const float bsdfPdfValue = brdfPdf(ctx, wi);
    const float misWeight = misBalanceWeight(lightPdf, bsdfPdfValue);
    const float scale = misWeight * cosTheta / lightPdf;

    return vecMul3(bsdf, vecScale3(lightRadiance, scale));
}

PATH_INTEGRATOR_RAND_FN Vec3 pathIntegratorRandEvaluateEmissiveNee(
    Vec3 position,
    Vec3 normal,
    Vec3 wo,
    const MaterialGpu& material,
    float etaMedium,
    float wavelengthNm,
    const MeshAccelSceneGpu* scene,
    curandState* rng)
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
    if (dist2 <= PathIntegratorRandDetail::kMinPdf) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    const Vec3 wi = vecNormalize3(toLight);
    const float cosTheta = vecMax2(0.0f, vecDot3(normal, wi));
    const float cosLight = vecMax2(0.0f, vecDot3(lightNormal, vecScale3(wi, -1.0f)));
    if (cosTheta <= 0.0f || cosLight <= 0.0f) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    if (lightIsOccludedFrom(position, wi, scene)) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    const float lightPdf = areaPdf * dist2 / cosLight;
    if (lightPdf <= PathIntegratorRandDetail::kMinPdf) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    const BrdfContext ctx{normal, wo, material, etaMedium, wavelengthNm};
    const Vec3 bsdf = brdfEval(ctx, wi);
    const float bsdfPdfValue = brdfPdf(ctx, wi);
    const float misWeight = misBalanceWeight(lightPdf, bsdfPdfValue);
    const float scale = misWeight * cosTheta / lightPdf;

    return vecMul3(bsdf, vecScale3(lightRadiance, scale));
}

PATH_INTEGRATOR_RAND_FN Vec3 pathIntegratorRandApplySubsurfaceTierB(
    Vec3 throughput,
    const MaterialGpu& material,
    Vec3 normal,
    const BrdfSampleResult& sample,
    curandState* rng)
{
    if (!sample.subsurfaceScatter || sample.subsurfaceInternalSteps <= 0) {
        return throughput;
    }

    const float maxRadius = brdfMaxScatterRadius(material);
    if (maxRadius <= SubsurfaceDetail::kMinRadius) {
        return throughput;
    }

    const Vec3 albedo = vecMake3(material.r, material.g, material.b);
    for (int step = 0; step < sample.subsurfaceInternalSteps; ++step) {
        float uStep1 = 0.0f;
        float uStep2 = 0.0f;
        rand02(rng, uStep1, uStep2);
        const float uStep3 = rand01(rng);
        (void)subsurfaceInternalStepDirection(normal, uStep1, uStep2);
        const float meanFreePath = maxRadius * (-logf(vecMax2(1.0e-8f, uStep3)));
        const float scatterSurvival = expf(-meanFreePath / maxRadius);
        throughput = vecMul3(throughput, vecScale3(albedo, scatterSurvival));
    }

    const float exitDistance = vecLength3(sample.subsurfaceExitOffset);
    if (exitDistance > 0.0f) {
        const float exitFactor = expf(-exitDistance / maxRadius);
        throughput = vecScale3(throughput, exitFactor);
    }

    return throughput;
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

    int maxDepth = PathIntegratorRandDetail::kDefaultMaxPathDepth;
    int rrMinDepth = PathIntegratorRandDetail::kRussianRouletteStartDepth;
    int brdfDebugFlags = BrdfDebugFlags::kNone;
    if (params != nullptr) {
        if (params->maxPathDepth > 0) {
            maxDepth = params->maxPathDepth;
        } else if (params->maxPathDepth == 0) {
            maxDepth = PathIntegratorRandDetail::kUnlimitedPathDepth;
        }
        if (params->russianRouletteMinDepth >= 0) {
            rrMinDepth = params->russianRouletteMinDepth;
        }
        brdfDebugFlags = params->brdfDebugFlags;
    }

    Vec3 radiance = vecMake3(0.0f, 0.0f, 0.0f);
    Vec3 throughput = vecMake3(1.0f, 1.0f, 1.0f);
    Vec3 currentOrigin = rayOrigin;
    Vec3 currentDir = rayDir;
    MeshHit currentHit = hit;
    float etaMedium = PathIntegratorRandDetail::kAirIor;

    for (int depth = 0; depth < maxDepth; ++depth) {
        const Vec3 position = vecEvalRay(currentOrigin, currentDir, currentHit.t);
        const Vec3 normal = currentHit.normal;
        const Vec3 wo = vecScale3(currentDir, -1.0f);
        const MaterialGpu sourceMaterial = lightFetchMaterial(scene, currentHit.triangleIndex);
        const MaterialGpu material = pathIntegratorRandResolveMaterial(sourceMaterial, currentHit.uv, scene);

        radiance = vecAdd3(radiance, vecMul3(throughput, pathIntegratorRandEmission(material)));

        radiance = vecAdd3(
            radiance,
            vecMul3(
                throughput,
                pathIntegratorRandEvaluateEnvironmentNee(
                    position,
                    normal,
                    wo,
                    material,
                    etaMedium,
                    PathIntegratorRandDetail::kDefaultWavelengthNm,
                    scene,
                    env,
                    params,
                    rng)));

        radiance = vecAdd3(
            radiance,
            vecMul3(
                throughput,
                pathIntegratorRandEvaluateEmissiveNee(
                    position,
                    normal,
                    wo,
                    material,
                    etaMedium,
                    PathIntegratorRandDetail::kDefaultWavelengthNm,
                    scene,
                    rng)));

        const BrdfContext ctx{
            normal,
            wo,
            material,
            etaMedium,
            PathIntegratorRandDetail::kDefaultWavelengthNm,
            brdfDebugFlags};

        float u1 = 0.0f;
        float u2 = 0.0f;
        rand02(rng, u1, u2);
        const BrdfSampleResult sample = brdfSample(ctx, u1, u2);
        if (!sample.valid || sample.pdf <= PathIntegratorRandDetail::kMinPdf) {
            break;
        }

        const Vec3 bsdfValue = brdfEval(ctx, sample.direction);
        throughput = brdfApplyThroughput(throughput, ctx, sample, bsdfValue);
        throughput = pathIntegratorRandApplySubsurfaceTierB(throughput, material, normal, sample, rng);

        if ((brdfDebugFlags & BrdfDebugFlags::kTintGlassPaths) != 0) {
            if (sample.transmitted) {
                throughput = vecMul3(throughput, vecMake3(0.2f, 1.0f, 0.2f));
            } else {
                throughput = vecMul3(throughput, vecMake3(1.0f, 0.2f, 0.2f));
            }
        }

        const float throughputLuminance = brdfThroughputLuminance(throughput);
        if (!isfinite(throughputLuminance) || throughputLuminance <= 0.0f || throughputLuminance > 1.0e8f) {
            break;
        }

        if (depth >= rrMinDepth) {
            const float survivalProb = vecMin2(
                PathIntegratorRandDetail::kMaxRussianRouletteProb,
                vecMax2(throughputLuminance, 0.05f));
            if (rand01(rng) > survivalProb) {
                break;
            }
            throughput = vecScale3(throughput, 1.0f / survivalProb);
        }

        etaMedium = sample.nextMediumEta;

        const float continuationBias = vecMax2(
            PathIntegratorRandDetail::kShadowBias,
            PathIntegratorRandDetail::kRelativeShadowBias * currentHit.t);

        const float biasSign = vecDot3(normal, sample.direction) >= 0.0f ? 1.0f : -1.0f;
        currentOrigin = vecAdd3(position, vecScale3(normal, biasSign * continuationBias));
        currentDir = sample.direction;
        currentHit = meshAccelTraceRay(
            currentOrigin,
            currentDir,
            scene,
            continuationBias,
            PathIntegratorRandDetail::kRayTMax);
        if (!currentHit.hit) {
            const Vec3 envRadiance = lightEvalEnvironmentOrBackground(env, params, currentDir);
            const float lightPdf = lightPdfEnvironmentOrBackground(env, params, currentDir);
            const float misWeight = misBalanceWeight(sample.pdf, lightPdf);
            radiance = vecAdd3(
                radiance,
                vecMul3(throughput, vecScale3(envRadiance, misWeight)));
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
