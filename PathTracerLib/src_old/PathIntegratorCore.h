#pragma once

#include "Brdf/BrdfDispatch.h"
#include "Brdf/BrdfDebug.h"
#include "Bssrdf/BssrdfCore.h"
#include "Geometry/MathCore.h"
#include "Medium/MediumProperties.h"
#include "MeshAccel/MaterialType.h"
#include "MeshAccel/MeshAccelIntersectCore.h"
#include "MeshAccel/MeshAccelTypes.h"
#include "RenderTypes.h"
#include "Sampling/LightSamplingCore.h"
#include "Sampling/MisCore.h"
#include "Sampling/RandCore.h"
#include "SceneUnits.h"
#include "Spectral/SpectralCore.h"
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
constexpr float kAirIor = BrdfDetail::kAirIor;
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

PATH_INTEGRATOR_CORE_FN float pathIntegratorEvaluateEnvironmentNee(
    Vec3 position,
    Vec3 normal,
    Vec3 wo,
    const MaterialGpu& material,
    float etaMedium,
    float wavelengthNm,
    float throughput,
    const MeshAccelSceneGpu* scene,
    const EnvironmentMapGpu* env,
    const RenderParamsGpu* params,
    curandState* rng,
    float hitDistanceMm,
    uint32_t sourceTriangleIndex)
{
    if (brdfSkipsEnvironmentNee(material, etaMedium)) {
        return 0.0f;
    }

    float u1 = 0.0f;
    float u2 = 0.0f;
    rand02(rng, u1, u2);

    float lightPdf = 0.0f;
    const Vec3 wi = lightSampleEnvironmentOrBackground(env, params, u1, u2, lightPdf);
    if (lightPdf <= PathIntegratorCoreDetail::kMinPdf) {
        return 0.0f;
    }

    if (lightIsOccluded(position, normal, wi, scene, hitDistanceMm, sourceTriangleIndex)) {
        return 0.0f;
    }

    const float cosTheta = vecMax2(0.0f, vecDot3(normal, wi));
    if (cosTheta <= 0.0f) {
        return 0.0f;
    }

    const BrdfContext ctx{normal, wo, material, etaMedium, wavelengthNm};
    const float lightRadiance = lightEvalEnvironmentSpectral(env, params, wi, wavelengthNm);
    const float bsdf = brdfEvalSpectral(ctx, wi);
    const float bsdfPdfValue = brdfPdf(ctx, wi);
    const float misWeight = misBalanceWeight(lightPdf, bsdfPdfValue);
    const float scale = misWeight * cosTheta / lightPdf;

    return throughput * bsdf * lightRadiance * scale;
}

PATH_INTEGRATOR_CORE_FN float pathIntegratorEvaluateEmissiveNee(
    Vec3 position,
    Vec3 normal,
    Vec3 wo,
    const MaterialGpu& material,
    float etaMedium,
    float wavelengthNm,
    float throughput,
    const MeshAccelSceneGpu* scene,
    curandState* rng,
    float hitDistanceMm,
    uint32_t sourceTriangleIndex)
{
    if (scene == nullptr || scene->emissiveTriangleCount == 0) {
        return 0.0f;
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
        return 0.0f;
    }

    const Vec3 toLight = vecSub3(lightPosition, position);
    const float dist2 = vecDot3(toLight, toLight);
    if (dist2 <= PathIntegratorCoreDetail::kMinPdf) {
        return 0.0f;
    }

    const Vec3 wi = vecNormalize3(toLight);
    const float cosTheta = vecMax2(0.0f, vecDot3(normal, wi));
    const float cosLight = vecMax2(0.0f, vecDot3(lightNormal, vecScale3(wi, -1.0f)));
    if (cosTheta <= 0.0f || cosLight <= 0.0f) {
        return 0.0f;
    }

    if (lightIsOccludedFrom(position, wi, scene, hitDistanceMm, sourceTriangleIndex)) {
        return 0.0f;
    }

    const float lightPdf = areaPdf * dist2 / cosLight;
    if (lightPdf <= PathIntegratorCoreDetail::kMinPdf) {
        return 0.0f;
    }

    const BrdfContext ctx{normal, wo, material, etaMedium, wavelengthNm};
    const float bsdf = brdfEvalSpectral(ctx, wi);
    const float bsdfPdfValue = brdfPdf(ctx, wi);
    const float misWeight = misBalanceWeight(lightPdf, bsdfPdfValue);
    const float scale = misWeight * cosTheta / lightPdf;
    const float lightSpectral = spectralEnvironmentRadianceAtWavelength(lightRadiance, wavelengthNm);

    return throughput * bsdf * lightSpectral * scale;
}

PATH_INTEGRATOR_CORE_FN bool pathIntegratorContinueAfterTrace(
    PathSpectralState& spectral,
    float& radiance,
    const BrdfContext& ctx,
    const BrdfSampleResult& sample,
    float fresnelReflectance,
    bool opaquePath,
    bool choseReflect,
    int brdfDebugFlags,
    Vec3 position,
    Vec3 normal,
    float& etaMedium,
    Vec3& currentOrigin,
    Vec3& currentDir,
    MeshHit& currentHit,
    const MeshAccelSceneGpu* scene,
    const EnvironmentMapGpu* env,
    const RenderParamsGpu* params,
    float continuationBias)
{
    if (sample.transmitted && !opaquePath) {
        spectral.throughput = brdfApplyInterfaceThroughputScalar(
            spectral.throughput, fresnelReflectance, false);
    } else {
        const float bsdfValue = brdfEvalSpectral(ctx, sample.direction);
        spectral.throughput = brdfApplyThroughputScalar(
            spectral.throughput, ctx, sample, bsdfValue);
    }

    if ((brdfDebugFlags & BrdfDebugFlags::kTintGlassPaths) != 0) {
        spectral.throughput *= 0.47f;
    }

    etaMedium = sample.nextMediumEta;

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
        const float envRadiance = lightEvalEnvironmentSpectral(
            env, params, currentDir, ctx.wavelengthNm);
        const float lightPdf = lightPdfEnvironmentOrBackground(env, params, currentDir);
        const float misWeight = misBalanceWeight(sample.pdf, lightPdf);
        radiance += spectral.throughput * envRadiance * misWeight;
        return false;
    }
    return true;
}

PATH_INTEGRATOR_CORE_FN bool pathIntegratorTryBssrdfBounce(
    PathSpectralState& spectral,
    float& radiance,
    Vec3 position,
    Vec3 normal,
    const MaterialGpu& material,
    float etaMedium,
    const MeshAccelSceneGpu* scene,
    const EnvironmentMapGpu* env,
    const RenderParamsGpu* params,
    curandState* rng,
    Vec3& currentOrigin,
    Vec3& currentDir,
    MeshHit& currentHit)
{
    if (!materialIsSubsurfaceType(material) || material.subsurface <= 1.0e-6f) {
        return false;
    }

    const float pBssrdf = material.subsurface / vecMax2(material.subsurface + material.specular, 1.0e-6f);
    if (rand01(rng) >= pBssrdf) {
        return false;
    }

    float u1 = 0.0f;
    float u2 = 0.0f;
    rand02(rng, u1, u2);
    const BssrdfExitSample exitSample = bssrdfSampleExitPoint(
        position, normal, material, spectral.wavelengthNm, u1, u2);
    if (!exitSample.valid) {
        return false;
    }

    const float continuationBias = SceneUnits::rayEpsilonMm(
        currentHit.t,
        scene != nullptr ? scene->sceneExtentMm : 0.0f);
    const MeshHit exitHit = meshAccelTraceRay(
        exitSample.offsetPosition,
        exitSample.traceDirection,
        scene,
        continuationBias,
        PathIntegratorCoreDetail::kRayTMax);
    if (!exitHit.hit) {
        return false;
    }

    const Vec3 exitPosition = vecEvalRay(
        exitSample.offsetPosition, exitSample.traceDirection, exitHit.t);
    const Vec3 exitNormal = exitHit.normal;
    const Vec3 exitWo = vecScale3(exitSample.traceDirection, -1.0f);
    const MaterialGpu exitSourceMaterial = lightFetchMaterial(scene, exitHit.triangleIndex);
    const MaterialGpu exitMaterial = pathIntegratorResolveMaterial(
        exitSourceMaterial, exitHit.uv, scene);

    const float bssrdfWeight = material.subsurface / vecMax2(pBssrdf, 1.0e-6f);
    spectral.throughput *= bssrdfWeight / vecMax2(exitSample.pdf, PathIntegratorCoreDetail::kMinPdf);

    radiance += pathIntegratorEvaluateEnvironmentNee(
        exitPosition,
        exitNormal,
        exitWo,
        exitMaterial,
        etaMedium,
        spectral.wavelengthNm,
        spectral.throughput,
        scene,
        env,
        params,
        rng,
        exitHit.t,
        exitHit.triangleIndex);

    radiance += pathIntegratorEvaluateEmissiveNee(
        exitPosition,
        exitNormal,
        exitWo,
        exitMaterial,
        etaMedium,
        spectral.wavelengthNm,
        spectral.throughput,
        scene,
        rng,
        exitHit.t,
        exitHit.triangleIndex);

    const BrdfContext exitCtx{
        exitNormal,
        exitWo,
        exitMaterial,
        etaMedium,
        spectral.wavelengthNm};

    float uReflect1 = 0.0f;
    float uReflect2 = 0.0f;
    rand02(rng, uReflect1, uReflect2);
    const BrdfSampleResult exitSampleReflect = brdfSampleReflect(exitCtx, uReflect1, uReflect2);
    if (!exitSampleReflect.valid || exitSampleReflect.pdf <= PathIntegratorCoreDetail::kMinPdf) {
        currentHit.hit = false;
        return true;
    }

    const float bsdfValue = brdfEvalSpectral(exitCtx, exitSampleReflect.direction);
    spectral.throughput = brdfApplyThroughputScalar(
        spectral.throughput, exitCtx, exitSampleReflect, bsdfValue);

    const float biasSign = vecDot3(exitNormal, exitSampleReflect.direction) >= 0.0f ? 1.0f : -1.0f;
    currentOrigin = vecAdd3(
        exitPosition,
        vecScale3(exitNormal, biasSign * continuationBias));
    currentDir = exitSampleReflect.direction;
    currentHit = meshAccelTraceRay(
        currentOrigin,
        currentDir,
        scene,
        continuationBias,
        PathIntegratorCoreDetail::kRayTMax);
    if (!currentHit.hit) {
        const float envRadiance = lightEvalEnvironmentSpectral(
            env, params, currentDir, spectral.wavelengthNm);
        const float lightPdf = lightPdfEnvironmentOrBackground(env, params, currentDir);
        const float misWeight = misBalanceWeight(exitSampleReflect.pdf, lightPdf);
        radiance += spectral.throughput * envRadiance * misWeight;
        return true;
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
    PathSpectralState spectral{};
    spectralSampleWavelength(rand01(rng), spectral.wavelengthNm, spectral.wavelengthPdf);
    spectral.throughput = 1.0f;

    if (!hit.hit) {
        const float envRadiance = lightEvalEnvironmentSpectral(env, params, rayDir, spectral.wavelengthNm);
        return spectralToRgb(envRadiance, spectral.wavelengthNm, spectral.wavelengthPdf);
    }

    int maxDepth = PathIntegratorCoreDetail::kDefaultMaxPathDepth;
    int rrMinDepth = PathIntegratorCoreDetail::kRussianRouletteStartDepth;
    int brdfDebugFlags = BrdfDebugFlags::kNone;
    if (params != nullptr) {
        if (params->maxPathDepth > 0) {
            maxDepth = params->maxPathDepth;
        } else if (params->maxPathDepth == 0) {
            maxDepth = PathIntegratorCoreDetail::kUnlimitedPathDepth;
        }
        if (params->russianRouletteMinDepth >= 0) {
            rrMinDepth = params->russianRouletteMinDepth;
        }
        brdfDebugFlags = params->brdfDebugFlags;
    }

    float radiance = 0.0f;
    Vec3 currentOrigin = rayOrigin;
    Vec3 currentDir = rayDir;
    MeshHit currentHit = hit;
    float etaMedium = PathIntegratorCoreDetail::kAirIor;

    for (int depth = 0; depth < maxDepth; ++depth) {
        const Vec3 position = vecEvalRay(currentOrigin, currentDir, currentHit.t);
        const Vec3 normal = currentHit.normal;
        const Vec3 wo = vecScale3(currentDir, -1.0f);
        const MaterialGpu sourceMaterial = lightFetchMaterial(scene, currentHit.triangleIndex);
        const MaterialGpu material = pathIntegratorResolveMaterial(sourceMaterial, currentHit.uv, scene);
        const MaterialType type = materialTypeOf(material);

        radiance += spectral.throughput * lightEmissiveRadianceSpectral(material, spectral.wavelengthNm);

        radiance += pathIntegratorEvaluateEnvironmentNee(
            position,
            normal,
            wo,
            material,
            etaMedium,
            spectral.wavelengthNm,
            spectral.throughput,
            scene,
            env,
            params,
            rng,
            currentHit.t,
            currentHit.triangleIndex);

        radiance += pathIntegratorEvaluateEmissiveNee(
            position,
            normal,
            wo,
            material,
            etaMedium,
            spectral.wavelengthNm,
            spectral.throughput,
            scene,
            rng,
            currentHit.t,
            currentHit.triangleIndex);

        if (pathIntegratorTryBssrdfBounce(
                spectral,
                radiance,
                position,
                normal,
                material,
                etaMedium,
                scene,
                env,
                params,
                rng,
                currentOrigin,
                currentDir,
                currentHit)) {
            if (!currentHit.hit) {
                break;
            }

            const float throughputLuminance = brdfThroughputLuminanceScalar(
                spectral.throughput, spectral.wavelengthNm);
            if (!isfinite(throughputLuminance)
                || throughputLuminance <= 0.0f
                || throughputLuminance > 1.0e8f) {
                break;
            }

            if (depth >= rrMinDepth) {
                const float survivalProb = vecMin2(
                    PathIntegratorCoreDetail::kMaxRussianRouletteProb,
                    vecMax2(throughputLuminance, 0.05f));
                if (rand01(rng) > survivalProb) {
                    break;
                }
                spectral.throughput /= survivalProb;
            }
            continue;
        }

        const BrdfContext ctx{
            normal,
            wo,
            material,
            etaMedium,
            spectral.wavelengthNm,
            brdfDebugFlags};

        float u1 = 0.0f;
        float u2 = 0.0f;
        rand02(rng, u1, u2);

        BrdfSampleResult sample{};
        float fresnelReflectance = 0.0f;
        bool choseReflect = true;
        const bool opaquePath = materialIsMetallicSurface(material) || materialIsOpaqueType(material);
        const bool glassPath = type == MaterialType::Glass;

        if (opaquePath) {
            sample = brdfSampleReflect(ctx, u1, u2);
        } else if (glassPath || materialIsSubsurfaceType(material)) {
            fresnelReflectance = PrincipledDetail::interfaceFresnelReflectance(ctx);
            const float uInterface = rand01(rng);
            choseReflect = uInterface < fresnelReflectance;
            if ((brdfDebugFlags & BrdfDebugFlags::kDisableRefract) != 0) {
                choseReflect = true;
            }
            if ((brdfDebugFlags & BrdfDebugFlags::kDisableReflect) != 0) {
                choseReflect = false;
            }
            if ((brdfDebugFlags & BrdfDebugFlags::kForceTransmitLobeOnly) != 0) {
                choseReflect = false;
            }

            if (choseReflect) {
                sample = brdfSampleReflect(ctx, u1, u2);
            } else {
                sample = brdfSampleRefract(ctx, u1, u2);
                if (!sample.valid) {
                    if ((brdfDebugFlags & BrdfDebugFlags::kDisableTirFallback) != 0) {
                        break;
                    }
                    sample = brdfSampleReflect(ctx, u1, u2);
                    choseReflect = true;
                }
            }
        } else {
            sample = brdfSampleReflect(ctx, u1, u2);
        }

        if (!sample.valid || sample.pdf <= PathIntegratorCoreDetail::kMinPdf) {
            break;
        }

        const float continuationBias = SceneUnits::rayEpsilonMm(
            currentHit.t,
            scene != nullptr ? scene->sceneExtentMm : 0.0f);

        if (!pathIntegratorContinueAfterTrace(
                spectral,
                radiance,
                ctx,
                sample,
                fresnelReflectance,
                opaquePath,
                choseReflect,
                brdfDebugFlags,
                position,
                normal,
                etaMedium,
                currentOrigin,
                currentDir,
                currentHit,
                scene,
                env,
                params,
                continuationBias)) {
            break;
        }

        const float throughputLuminance = brdfThroughputLuminanceScalar(
            spectral.throughput, spectral.wavelengthNm);
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
            spectral.throughput /= survivalProb;
        }
    }

    return spectralToRgb(radiance, spectral.wavelengthNm, spectral.wavelengthPdf);
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
