#pragma once

#include "Bssrdf/BssrdfCore.h"
#include "Material/MaterialParams.h"
#include "Medium/VolumeCore.h"
#include "Medium/VolumeScatteringCore.h"
#include "MeshAccel/MeshAccelIntersectCore.h"
#include "Path/PathState.h"
#include "SceneUnits.h"
#include "Spectral/SpectralCore.h"

#if defined(__CUDACC__)
#define SUBSURFACE_TRANSPORT_FN __device__ __forceinline__
#else
#define SUBSURFACE_TRANSPORT_FN inline
#endif

namespace SubsurfaceTransportDetail {

constexpr float kMinPdf = 1.0e-8f;

SUBSURFACE_TRANSPORT_FN float thinShellTransmittance(
    const MaterialGpu& material,
    float thicknessMm,
    float wavelengthNm)
{
    const PhysicalMediumCoeffs coeffs = materialToPhysicalMedium(material, wavelengthNm);
    const float sigmaA = mediumSigmaAAtWavelength(coeffs.sigmaA, wavelengthNm);
    return expf(-sigmaA * thicknessMm);
}

SUBSURFACE_TRANSPORT_FN float thinShellScatterTint(
    const MaterialGpu& material,
    float wavelengthNm)
{
    const PhysicalMediumCoeffs coeffs = materialToPhysicalMedium(material, wavelengthNm);
    return mediumScatterAlbedoAtWavelength(coeffs.sigmaA, coeffs.sigmaS, wavelengthNm);
}

#if defined(__CUDACC__)

SUBSURFACE_TRANSPORT_FN SubsurfaceTransportResult subsurfaceTransportThinShell(
    Vec3 position,
    Vec3 normal,
    const MaterialGpu& material,
    const SubsurfaceShellInfo& shell,
    float sssThroughput,
    float wavelengthNm,
    const MeshAccelSceneGpu* scene,
    float hitDistanceMm,
    uint32_t sourceTriangleIndex)
{
    SubsurfaceTransportResult result{};
    result.sssThroughput = sssThroughput;
    if (!shell.valid || scene == nullptr) {
        return result;
    }

    const float thickness = shell.hasBackFace
        ? shell.thicknessMm
        : vecMax2(subsurfaceMeanEffectiveRadius(material), BssrdfDetail::kMinShellMm);
    const float transmittance = thinShellTransmittance(material, thickness, wavelengthNm);
    const float tint = thinShellScatterTint(material, wavelengthNm);
    result.sssRadiance = 0.0f;
    result.sssThroughput = sssThroughput * transmittance * tint;

    const float epsilon = SceneUnits::rayEpsilonMm(
        hitDistanceMm,
        scene->sceneExtentMm);

    if (shell.hasBackFace) {
        result.exitPosition = shell.exitPosition;
        result.exitNormal = shell.exitNormal;
        const Vec3 backOrigin = vecAdd3(
            result.exitPosition,
            vecScale3(result.exitNormal, epsilon));
        result.exitHit = meshAccelTraceRay(
            backOrigin,
            vecScale3(result.exitNormal, -1.0f),
            scene,
            epsilon,
            SceneUnits::kDefaultRayTMaxMm);
        if (!result.exitHit.hit) {
            result.exitHit.hit = true;
            result.exitHit.t = 0.0f;
            result.exitHit.normal = result.exitNormal;
            result.exitHit.triangleIndex = shell.exitTriangleIndex;
        }
    } else {
        result.exitPosition = vecAdd3(position, vecScale3(normal, -epsilon));
        result.exitNormal = vecScale3(normal, -1.0f);
        result.exitHit.hit = true;
        result.exitHit.t = 0.0f;
        result.exitHit.normal = result.exitNormal;
        result.exitHit.triangleIndex = sourceTriangleIndex;
    }

    result.exitWo = vecScale3(result.exitNormal, -1.0f);
    result.valid = result.sssThroughput > kMinPdf;
    return result;
}

SUBSURFACE_TRANSPORT_FN SubsurfaceTransportResult subsurfaceTransport(
    Vec3 position,
    Vec3 normal,
    const MaterialGpu& material,
    float sssThroughput,
    float wavelengthNm,
    const MeshAccelSceneGpu* scene,
    const EnvironmentMapGpu* env,
    const RenderParamsGpu* params,
    curandState* rng,
    float hitDistanceMm,
    uint32_t sourceTriangleIndex)
{
    const SubsurfaceShellInfo shell = subsurfaceProbeShell(
        position, normal, scene, sourceTriangleIndex, hitDistanceMm);

    if (subsurfaceUsesThinShellMode(shell, material)) {
        return subsurfaceTransportThinShell(
            position,
            normal,
            material,
            shell,
            sssThroughput,
            wavelengthNm,
            scene,
            hitDistanceMm,
            sourceTriangleIndex);
    }

    return volumeSubsurfaceRandomWalk(
        position,
        normal,
        material,
        shell,
        sssThroughput,
        wavelengthNm,
        scene,
        env,
        params,
        rng,
        hitDistanceMm,
        sourceTriangleIndex);
}

#endif

} // namespace SubsurfaceTransportDetail

#undef SUBSURFACE_TRANSPORT_FN
