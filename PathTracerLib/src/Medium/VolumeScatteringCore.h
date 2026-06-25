#pragma once

#include "Bssrdf/BssrdfCore.h"
#include "Material/MaterialParams.h"
#include "Medium/MediumProperties.h"
#include "Medium/VolumeCore.h"
#include "MeshAccel/MeshAccelIntersectCore.h"
#include "Path/PathState.h"
#include "RenderTypes.h"
#include "Sampling/LightSamplingCore.h"
#include "Sampling/MisCore.h"
#include "Sampling/RandCore.h"
#include "SceneUnits.h"
#include "Spectral/SpectralCore.h"

#if defined(__CUDACC__)
#define VOLUME_SCATTERING_FN __device__ __forceinline__
#else
#define VOLUME_SCATTERING_FN inline
#endif

namespace VolumeScatteringDetail {

constexpr float kMinPdf = 1.0e-8f;
constexpr int kDefaultMaxSubsurfaceScatters = 8;
constexpr int kMinMaxSubsurfaceScatters = 1;
constexpr int kMaxMaxSubsurfaceScatters = 128;
constexpr float kRayTMax = SceneUnits::kDefaultRayTMaxMm;

VOLUME_SCATTERING_FN int volumeMaxSubsurfaceScatters(const RenderParamsGpu* params)
{
    if (params == nullptr) {
        return VolumeScatteringDetail::kDefaultMaxSubsurfaceScatters;
    }
    int cap = params->maxSubsurfaceScatters;
    if (cap < VolumeScatteringDetail::kMinMaxSubsurfaceScatters) {
        cap = VolumeScatteringDetail::kMinMaxSubsurfaceScatters;
    }
    if (cap > VolumeScatteringDetail::kMaxMaxSubsurfaceScatters) {
        cap = VolumeScatteringDetail::kMaxMaxSubsurfaceScatters;
    }
    return cap;
}

} // namespace VolumeScatteringDetail

VOLUME_SCATTERING_FN bool volumeTransportEnabled(const MaterialGpu& material)
{
    return materialHasParticipatingMedium(material);
}

#if defined(__CUDACC__)

VOLUME_SCATTERING_FN float volumeInteriorEnvironmentNee(
    Vec3 position,
    Vec3 incomingDir,
    const MaterialGpu& material,
    float walkThroughput,
    float wavelengthNm,
    const MeshAccelSceneGpu* scene,
    const EnvironmentMapGpu* env,
    const RenderParamsGpu* params,
    curandState* rng)
{
    float u1 = 0.0f;
    float u2 = 0.0f;
    rand02(rng, u1, u2);

    float lightPdf = 0.0f;
    const Vec3 wi = lightSampleEnvironmentOrBackground(env, params, u1, u2, lightPdf);
    if (lightPdf <= VolumeScatteringDetail::kMinPdf) {
        return 0.0f;
    }

    if (lightIsOccludedFrom(position, wi, scene, 0.0f, UINT32_MAX)) {
        return 0.0f;
    }

    const PhysicalMediumCoeffs coeffs = materialToPhysicalMedium(material, wavelengthNm);
    const float sigmaT = mediumSigmaTAtWavelength(coeffs.sigmaA, coeffs.sigmaS, wavelengthNm);
    const float scatterAlbedo = mediumScatterAlbedoAtWavelength(coeffs.sigmaA, coeffs.sigmaS, wavelengthNm);
    if (sigmaT <= VolumeScatteringDetail::kMinPdf || scatterAlbedo <= VolumeScatteringDetail::kMinPdf) {
        return 0.0f;
    }

    const float cosTheta = vecDot3(incomingDir, wi);
    const float phase = henyeyGreensteinEval(cosTheta, material.mediumG);
    const float envRadiance = lightEvalEnvironmentSpectral(env, params, wi, wavelengthNm);
    const float misWeight = misBalanceWeight(lightPdf, phase);
    return walkThroughput * scatterAlbedo * phase * envRadiance * misWeight / lightPdf;
}

VOLUME_SCATTERING_FN float volumeInteriorEmissiveNee(
    Vec3 position,
    Vec3 incomingDir,
    const MaterialGpu& material,
    float walkThroughput,
    float wavelengthNm,
    const MeshAccelSceneGpu* scene,
    curandState* rng)
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
    if (dist2 <= VolumeScatteringDetail::kMinPdf) {
        return 0.0f;
    }

    const Vec3 wi = vecNormalize3(toLight);
    const float cosLight = vecMax2(0.0f, vecDot3(lightNormal, vecScale3(wi, -1.0f)));
    if (cosLight <= 0.0f) {
        return 0.0f;
    }

    if (lightIsOccludedFromBefore(position, wi, sqrtf(dist2), scene, 0.0f, UINT32_MAX)) {
        return 0.0f;
    }

    const float lightPdf = areaPdf * dist2 / cosLight;
    if (lightPdf <= VolumeScatteringDetail::kMinPdf) {
        return 0.0f;
    }

    const PhysicalMediumCoeffs coeffs = materialToPhysicalMedium(material, wavelengthNm);
    const float scatterAlbedo = mediumScatterAlbedoAtWavelength(coeffs.sigmaA, coeffs.sigmaS, wavelengthNm);
    const float cosTheta = vecDot3(incomingDir, wi);
    const float phase = henyeyGreensteinEval(cosTheta, material.mediumG);
    const float lightScalar = spectralRgbToScalar(
        lightRadiance.x, lightRadiance.y, lightRadiance.z, wavelengthNm);
    const float misWeight = misBalanceWeight(lightPdf, phase);
    return walkThroughput * scatterAlbedo * phase * lightScalar * misWeight / lightPdf;
}

VOLUME_SCATTERING_FN float volumeInteriorDirectNee(
    Vec3 position,
    Vec3 incomingDir,
    const MaterialGpu& material,
    float walkThroughput,
    float wavelengthNm,
    const MeshAccelSceneGpu* scene,
    const EnvironmentMapGpu* env,
    const RenderParamsGpu* params,
    curandState* rng)
{
    float accumulated = volumeInteriorEnvironmentNee(
        position, incomingDir, material, walkThroughput, wavelengthNm, scene, env, params, rng);
    accumulated += volumeInteriorEmissiveNee(
        position, incomingDir, material, walkThroughput, wavelengthNm, scene, rng);
    return accumulated;
}

VOLUME_SCATTERING_FN SubsurfaceTransportResult volumeSubsurfaceRandomWalk(
    Vec3 position,
    Vec3 normal,
    const MaterialGpu& material,
    const SubsurfaceShellInfo& shell,
    float sssThroughput,
    float wavelengthNm,
    const MeshAccelSceneGpu* scene,
    const EnvironmentMapGpu* env,
    const RenderParamsGpu* params,
    curandState* rng,
    float hitDistanceMm,
    uint32_t sourceTriangleIndex)
{
    SubsurfaceTransportResult result{};
    (void)sourceTriangleIndex;
    if (!shell.valid || scene == nullptr) {
        return result;
    }

    const PhysicalMediumCoeffs coeffs = materialToPhysicalMedium(material, wavelengthNm);
    const float sigmaT = mediumSigmaTAtWavelength(coeffs.sigmaA, coeffs.sigmaS, wavelengthNm);
    const float albedo = mediumScatterAlbedoAtWavelength(coeffs.sigmaA, coeffs.sigmaS, wavelengthNm);

    const float epsilon = SceneUnits::rayEpsilonMm(hitDistanceMm, scene->sceneExtentMm);
    const Vec3 interiorDir = vecScale3(normal, -1.0f);
    Vec3 walkPos = vecAdd3(position, vecScale3(normal, -epsilon));
    Vec3 walkDir = interiorDir;
    float remainingMm = shell.hasBackFace ? shell.thicknessMm : subsurfaceMeanEffectiveRadius(material);
    float throughput = sssThroughput;
    float accumulated = 0.0f;

    const int maxScatters = VolumeScatteringDetail::volumeMaxSubsurfaceScatters(params);
    for (int scatter = 0; scatter < maxScatters; ++scatter) {
        const float uFlight = rand01(rng);
        const float freeFlight = mediumSampleFreeFlight(uFlight, sigmaT);
        if (freeFlight >= remainingMm) {
            const float boundaryT = expf(-sigmaT * remainingMm);
            throughput *= boundaryT;
            walkPos = vecAdd3(walkPos, vecScale3(walkDir, remainingMm));
            break;
        }

        remainingMm -= freeFlight;
        walkPos = vecAdd3(walkPos, vecScale3(walkDir, freeFlight));
        throughput *= albedo;

        accumulated += volumeInteriorDirectNee(
            walkPos,
            walkDir,
            material,
            throughput,
            wavelengthNm,
            scene,
            env,
            params,
            rng);

        float u1 = 0.0f;
        float u2 = 0.0f;
        rand02(rng, u1, u2);
        walkDir = henyeyGreensteinSampleDirection(walkDir, material.mediumG, u1, u2);

        if (remainingMm <= epsilon) {
            break;
        }

        if (scatter >= 3) {
            const float survival = vecMax2(0.1f, vecMin2(0.95f, throughput));
            if (rand01(rng) > survival) {
                break;
            }
            throughput /= survival;
        }
    }

    result.sssRadiance = accumulated;
    result.sssThroughput = throughput;
    result.exitPosition = shell.hasBackFace
        ? shell.exitPosition
        : vecAdd3(position, vecScale3(normal, -epsilon));
    result.exitNormal = shell.hasBackFace ? shell.exitNormal : vecScale3(normal, -1.0f);
    result.exitWo = vecScale3(result.exitNormal, -1.0f);

    const Vec3 backOrigin = vecAdd3(result.exitPosition, vecScale3(result.exitNormal, epsilon));
    result.exitHit = meshAccelTraceRay(
        backOrigin,
        vecScale3(result.exitNormal, -1.0f),
        scene,
        epsilon,
        VolumeScatteringDetail::kRayTMax);
    if (!result.exitHit.hit) {
        result.exitHit.hit = true;
        result.exitHit.t = 0.0f;
        result.exitHit.normal = result.exitNormal;
        result.exitHit.triangleIndex = shell.exitTriangleIndex;
    }

    result.valid = result.sssThroughput > VolumeScatteringDetail::kMinPdf;
    return result;
}

#endif

#undef VOLUME_SCATTERING_FN
