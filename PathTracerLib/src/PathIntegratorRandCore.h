#pragma once

#include "Brdf/BrdfDispatch.h"
#include "Brdf/BrdfDebug.h"
#include "Geometry/MathCore.h"
#include "Medium/MediumProperties.h"
#include "Medium/VolumeCore.h"
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
#define PATH_INTEGRATOR_RAND_FN __device__ __forceinline__
#else
#define PATH_INTEGRATOR_RAND_FN inline
#endif

namespace PathIntegratorRandDetail {

constexpr float kRayTMax = SceneUnits::kDefaultRayTMaxMm;
constexpr float kMinPdf = 1.0e-8f;
constexpr float kAirIor = 1.0f;
constexpr float kMediumEtaEpsilon = 1.0e-4f;
constexpr int kRussianRouletteStartDepth = 3;
constexpr float kMaxRussianRouletteProb = 0.95f;
constexpr int kDefaultMaxPathDepth = 32;
constexpr int kUnlimitedPathDepth = 256;
constexpr int kMaxMediumScatters = 16;

} // namespace PathIntegratorRandDetail

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

PATH_INTEGRATOR_RAND_FN float pathIntegratorRandEvaluateEnvironmentNee(
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
    if (brdfSkipsEnvironmentNee(material)) {
        return 0.0f;
    }

    float u1 = 0.0f;
    float u2 = 0.0f;
    rand02(rng, u1, u2);

    float lightPdf = 0.0f;
    const Vec3 wi = lightSampleEnvironmentOrBackground(env, params, u1, u2, lightPdf);
    if (lightPdf <= PathIntegratorRandDetail::kMinPdf) {
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

PATH_INTEGRATOR_RAND_FN float pathIntegratorRandEvaluateEmissiveNee(
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
    if (dist2 <= PathIntegratorRandDetail::kMinPdf) {
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
    if (lightPdf <= PathIntegratorRandDetail::kMinPdf) {
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

PATH_INTEGRATOR_RAND_FN bool pathIntegratorRandTraceMediumSegment(
    PathSpectralState& spectral,
    Vec3& currentOrigin,
    Vec3& currentDir,
    MeshHit& currentHit,
    float& etaMedium,
    const MediumProperties& medium,
    const MeshAccelSceneGpu* scene,
    const EnvironmentMapGpu* env,
    const RenderParamsGpu* params,
    float& radiance,
    curandState* rng)
{
    const float sigmaA_lambda = mediumSigmaAAtWavelength(medium.sigmaA, spectral.wavelengthNm);
    const float sigmaS_lambda = spectralGlassAbsorptionAtWavelength(
        medium.sigmaS.x, medium.sigmaS.y, medium.sigmaS.z, spectral.wavelengthNm);
    const float sigmaT_lambda = sigmaA_lambda + sigmaS_lambda;
    const float scatterAlbedo = sigmaT_lambda > VolumeDetail::kMinSigmaT
        ? sigmaS_lambda / sigmaT_lambda
        : 0.0f;
    const float continuationBias = SceneUnits::rayEpsilonMm(
        0.0f,
        scene != nullptr ? scene->sceneExtentMm : 0.0f);

    for (int scatterIndex = 0; scatterIndex < PathIntegratorRandDetail::kMaxMediumScatters; ++scatterIndex) {
        const MeshHit exitHit = meshAccelTraceRay(
            currentOrigin,
            currentDir,
            scene,
            continuationBias,
            PathIntegratorRandDetail::kRayTMax);
        const float tExit = exitHit.hit ? exitHit.t : PathIntegratorRandDetail::kRayTMax;

        if (sigmaT_lambda < VolumeDetail::kMinSigmaT) {
            spectral.throughput *= mediumTransmittanceAtWavelength(sigmaA_lambda, tExit);
            if (!exitHit.hit) {
                const float envRadiance = lightEvalEnvironmentSpectral(
                    env, params, currentDir, spectral.wavelengthNm);
                radiance += spectral.throughput * envRadiance;
                return false;
            }

            currentHit = exitHit;
            etaMedium = mediumIorAtWavelength(medium, spectral.wavelengthNm);
            const Vec3 exitPosition = vecEvalRay(currentOrigin, currentDir, tExit);
            const float biasSign = vecDot3(exitHit.normal, currentDir) >= 0.0f ? -1.0f : 1.0f;
            currentOrigin = vecAdd3(exitPosition, vecScale3(exitHit.normal, biasSign * continuationBias));
            currentHit.t = 0.0f;
            return true;
        }

        const float uFlight = rand01(rng);
        const float tScatter = mediumSampleFreeFlight(uFlight, sigmaT_lambda);

        if (tScatter >= tExit) {
            spectral.throughput *= mediumTransmittanceAtWavelength(sigmaA_lambda, tExit);
            if (!exitHit.hit) {
                const float envRadiance = lightEvalEnvironmentSpectral(
                    env, params, currentDir, spectral.wavelengthNm);
                radiance += spectral.throughput * envRadiance;
                return false;
            }

            currentHit = exitHit;
            etaMedium = mediumIorAtWavelength(medium, spectral.wavelengthNm);
            const Vec3 exitPosition = vecEvalRay(currentOrigin, currentDir, tExit);
            const float biasSign = vecDot3(exitHit.normal, currentDir) >= 0.0f ? -1.0f : 1.0f;
            currentOrigin = vecAdd3(exitPosition, vecScale3(exitHit.normal, biasSign * continuationBias));
            currentHit.t = 0.0f;
            return true;
        }

        currentOrigin = vecEvalRay(currentOrigin, currentDir, tScatter);
        spectral.throughput *= mediumTransmittanceAtWavelength(sigmaA_lambda, tScatter);
        spectral.throughput *= scatterAlbedo;

        float uHg1 = 0.0f;
        float uHg2 = 0.0f;
        rand02(rng, uHg1, uHg2);
        currentDir = henyeyGreensteinSampleDirection(vecScale3(currentDir, -1.0f), medium.mediumG, uHg1, uHg2);
    }

    return false;
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
    PathSpectralState spectral{};
    spectralSampleWavelength(rand01(rng), spectral.wavelengthNm, spectral.wavelengthPdf);
    spectral.throughput = 1.0f;

    if (!hit.hit) {
        const float envRadiance = lightEvalEnvironmentSpectral(env, params, rayDir, spectral.wavelengthNm);
        return spectralToRgb(envRadiance, spectral.wavelengthNm, spectral.wavelengthPdf);
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

    float radiance = 0.0f;
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

        radiance += spectral.throughput * lightEmissiveRadianceSpectral(material, spectral.wavelengthNm);

        radiance += pathIntegratorRandEvaluateEnvironmentNee(
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

        radiance += pathIntegratorRandEvaluateEmissiveNee(
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
        const bool opaquePath = materialIsMetallicSurface(material)
            || mediumIsOpaque(mediumFromMaterial(material));
        const bool volumePath = !opaquePath && materialUsesVolumeTransport(material);

        if (opaquePath) {
            sample = brdfSampleReflect(ctx, u1, u2);
        } else {
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
        }

        if (!sample.valid || sample.pdf <= PathIntegratorRandDetail::kMinPdf) {
            break;
        }

        const bool enteringMedium = sample.transmitted
            && sample.nextMediumEta > PathIntegratorRandDetail::kAirIor + PathIntegratorRandDetail::kMediumEtaEpsilon;

        if (sample.transmitted && volumePath && enteringMedium) {
            spectral.throughput = brdfApplyInterfaceThroughputScalar(
                spectral.throughput, fresnelReflectance, false);
            etaMedium = sample.nextMediumEta;
            const float continuationBias = SceneUnits::rayEpsilonMm(
                currentHit.t,
                scene != nullptr ? scene->sceneExtentMm : 0.0f);
            const float biasSign = vecDot3(normal, sample.direction) >= 0.0f ? 1.0f : -1.0f;
            currentOrigin = vecAdd3(position, vecScale3(normal, biasSign * continuationBias));
            currentDir = sample.direction;

            const MediumProperties mediumProps = mediumFromMaterial(material);
            const bool continueSurface = pathIntegratorRandTraceMediumSegment(
                spectral,
                currentOrigin,
                currentDir,
                currentHit,
                etaMedium,
                mediumProps,
                scene,
                env,
                params,
                radiance,
                rng);
            if (!continueSurface) {
                break;
            }

            if ((brdfDebugFlags & BrdfDebugFlags::kTintGlassPaths) != 0) {
                spectral.throughput *= 0.47f;
            }
        } else {
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

            const float continuationBias = SceneUnits::rayEpsilonMm(
                currentHit.t,
                scene != nullptr ? scene->sceneExtentMm : 0.0f);

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
                const float envRadiance = lightEvalEnvironmentSpectral(
                    env, params, currentDir, spectral.wavelengthNm);
                const float lightPdf = lightPdfEnvironmentOrBackground(env, params, currentDir);
                const float misWeight = misBalanceWeight(sample.pdf, lightPdf);
                radiance += spectral.throughput * envRadiance * misWeight;
                break;
            }
        }

        const float throughputLuminance = brdfThroughputLuminanceScalar(
            spectral.throughput, spectral.wavelengthNm);
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
            spectral.throughput /= survivalProb;
        }
    }

    return spectralToRgb(radiance, spectral.wavelengthNm, spectral.wavelengthPdf);
}

PATH_INTEGRATOR_RAND_FN Vec3 tracePathRand(
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
        rayOrigin, rayDir, scene, primaryEpsilon, PathIntegratorRandDetail::kRayTMax);
    return tracePathRandFromHit(hit, rayOrigin, rayDir, scene, params, env, rng);
}

#undef PATH_INTEGRATOR_RAND_FN
