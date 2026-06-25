#pragma once

#include "Material/MaterialParams.h"
#include "Material/MaterialType.h"
#include "MeshAccel/MeshAccelIntersectCore.h"
#include "MeshAccel/MeshAccelIntersectCore.h"
#include "MeshAccel/MeshAccelTypes.h"
#include "SceneUnits.h"
#include "Sampling/LightSamplingCore.h"

struct SubsurfaceShellInfo
{
    float thicknessMm = 0.0f;
    bool hasBackFace = false;
    Vec3 exitPosition{};
    Vec3 exitNormal{};
    float exitDistanceMm = 0.0f;
    uint32_t exitTriangleIndex = 0u;
    bool valid = false;
};

struct SubsurfaceTransportResult
{
    float sssRadiance = 0.0f;
    MeshHit exitHit{};
    Vec3 exitPosition{};
    Vec3 exitNormal{};
    Vec3 exitWo{};
    float sssThroughput = 1.0f;
    bool valid = false;
};

#if defined(__CUDACC__)
#define BSSRDF_CORE_FN __host__ __device__ inline
#else
#define BSSRDF_CORE_FN inline
#endif

namespace BssrdfDetail {

constexpr float kMinShellMm = 0.01f;
constexpr float kThinFactor = 0.25f;

} // namespace BssrdfDetail

BSSRDF_CORE_FN bool bssrdfEnabled(const MaterialGpu& material)
{
    return materialHasParticipatingMedium(material);
}

BSSRDF_CORE_FN float bssrdfEnterProbability(const MaterialGpu& material)
{
    return vecMin2(1.0f, vecMax2(0.0f, material.subsurface));
}

BSSRDF_CORE_FN float subsurfaceEffectiveRadius(const MaterialGpu& material, float channelIndex)
{
    const float scatterScale = vecMax2(material.subsurfaceScatterScale, 1.0e-6f);
    const int channel = channelIndex < 0.5f ? 0 : (channelIndex < 1.5f ? 1 : 2);
    const float radius = materialScatterDistanceChannel(material, channel);
    return vecMax2(radius * scatterScale, BssrdfDetail::kMinShellMm);
}

BSSRDF_CORE_FN float subsurfaceMinEffectiveRadius(const MaterialGpu& material)
{
    const float rR = subsurfaceEffectiveRadius(material, 0.0f);
    const float rG = subsurfaceEffectiveRadius(material, 1.0f);
    const float rB = subsurfaceEffectiveRadius(material, 2.0f);
    return vecMin2(vecMin2(rR, rG), rB);
}

BSSRDF_CORE_FN float subsurfaceMeanEffectiveRadius(const MaterialGpu& material)
{
    const float rR = subsurfaceEffectiveRadius(material, 0.0f);
    const float rG = subsurfaceEffectiveRadius(material, 1.0f);
    const float rB = subsurfaceEffectiveRadius(material, 2.0f);
    return (rR + rG + rB) / 3.0f;
}

BSSRDF_CORE_FN SubsurfaceShellInfo subsurfaceProbeShell(
    Vec3 position,
    Vec3 normal,
    const MeshAccelSceneGpu* scene,
    uint32_t sourceTriangleIndex,
    float hitDistanceMm)
{
    SubsurfaceShellInfo info{};
    if (scene == nullptr) {
        return info;
    }

    const float epsilon = SceneUnits::rayEpsilonMm(
        hitDistanceMm,
        scene->sceneExtentMm);
    const Vec3 interiorDir = vecScale3(normal, -1.0f);
    const Vec3 origin = vecAdd3(position, vecScale3(normal, -epsilon));
    const MeshHit backHit = meshAccelTraceRay(
        origin,
        interiorDir,
        scene,
        epsilon,
        SceneUnits::kDefaultRayTMaxMm);

    if (!backHit.hit) {
        info.valid = true;
        info.hasBackFace = false;
        info.thicknessMm = BssrdfDetail::kMinShellMm;
        info.exitPosition = vecAdd3(position, vecScale3(normal, -BssrdfDetail::kMinShellMm));
        info.exitNormal = vecScale3(normal, -1.0f);
        return info;
    }

    info.valid = true;
    info.hasBackFace = true;
    info.thicknessMm = backHit.t;
    info.exitDistanceMm = backHit.t;
    info.exitTriangleIndex = backHit.triangleIndex;
    info.exitPosition = vecEvalRay(origin, interiorDir, backHit.t);
    info.exitNormal = vecScale3(backHit.normal, -1.0f);
    (void)sourceTriangleIndex;
    return info;
}

BSSRDF_CORE_FN bool subsurfaceUsesThinShellMode(
    const SubsurfaceShellInfo& shell,
    const MaterialGpu& material)
{
    if (!shell.valid) {
        return true;
    }
    if (!shell.hasBackFace) {
        return true;
    }
    const float minRadius = subsurfaceMinEffectiveRadius(material);
    return shell.thicknessMm < BssrdfDetail::kThinFactor * minRadius;
}

#undef BSSRDF_CORE_FN
