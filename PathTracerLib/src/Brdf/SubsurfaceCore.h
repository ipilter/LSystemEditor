#pragma once

#include "BrdfBase.h"

#include <cmath>

#if defined(__CUDACC__)
#define SUBSURFACE_CORE_FN __host__ __device__ inline
#else
#define SUBSURFACE_CORE_FN inline
#endif

namespace SubsurfaceDetail {

constexpr float kMinRadius = 1.0e-5f;

} // namespace SubsurfaceDetail

/** @brief Burley normalized diffusion: sample radial distance (Christophe Burley, 2015). */
SUBSURFACE_CORE_FN float subsurfaceSampleBurleyRadius(float u, float d)
{
    const float safeD = vecMax2(d, SubsurfaceDetail::kMinRadius);
    const float r = safeD * sqrtf(-logf(vecMax2(1.0e-8f, 1.0f - u * (1.0f - expf(-1.0f / safeD)))));
    return r;
}

/** @brief Burley profile value at radial distance r for diffusion length d. */
SUBSURFACE_CORE_FN float subsurfaceBurleyProfile(float r, float d)
{
    const float safeD = vecMax2(d, SubsurfaceDetail::kMinRadius);
    return (expf(-r / safeD) + expf(-r / (3.0f * safeD))) / (4.0f * BrdfDetail::kPi * safeD);
}

SUBSURFACE_CORE_FN float subsurfaceBurleyPdfRadius(float r, float d)
{
    const float safeD = vecMax2(d, SubsurfaceDetail::kMinRadius);
    const float term0 = expf(-r / safeD);
    const float term1 = expf(-r / (3.0f * safeD));
    const float norm = 1.0f - expf(-1.0f / safeD);
    return vecMax2(0.0f, (term0 + term1) / vecMax2(2.0f * safeD * norm, 1.0e-8f));
}

SUBSURFACE_CORE_FN Vec3 subsurfaceChannelWeights(const MaterialGpu& material)
{
    const Vec3 radius = brdfScatterRadius(material);
    const float sum = vecMax2(radius.x + radius.y + radius.z, SubsurfaceDetail::kMinRadius);
    return vecMake3(radius.x / sum, radius.y / sum, radius.z / sum);
}

SUBSURFACE_CORE_FN float subsurfaceEffectiveRadius(const MaterialGpu& material, float uChannel)
{
    const Vec3 radius = brdfScatterRadius(material);
    const float maxRadius = brdfMaxScatterRadius(material);
    if (maxRadius <= SubsurfaceDetail::kMinRadius) {
        return SubsurfaceDetail::kMinRadius;
    }
    if (uChannel < radius.x / (radius.x + radius.y + radius.z + SubsurfaceDetail::kMinRadius)) {
        return vecMax2(radius.x, SubsurfaceDetail::kMinRadius);
    }
    if (uChannel < (radius.x + radius.y) / (radius.x + radius.y + radius.z + SubsurfaceDetail::kMinRadius)) {
        return vecMax2(radius.y, SubsurfaceDetail::kMinRadius);
    }
    return vecMax2(radius.z, SubsurfaceDetail::kMinRadius);
}

SUBSURFACE_CORE_FN Vec3 subsurfaceEvalBurley(
    const MaterialGpu& material,
    float diskDistance,
    float cosThetaI,
    float cosThetaO)
{
    const Vec3 radius = brdfScatterRadius(material);
    const Vec3 weights = subsurfaceChannelWeights(material);
    const float profileR = subsurfaceBurleyProfile(diskDistance, vecMax2(radius.x, SubsurfaceDetail::kMinRadius));
    const float profileG = subsurfaceBurleyProfile(diskDistance, vecMax2(radius.y, SubsurfaceDetail::kMinRadius));
    const float profileB = subsurfaceBurleyProfile(diskDistance, vecMax2(radius.z, SubsurfaceDetail::kMinRadius));
    const float diffuse = vecMax2(cosThetaI, 0.0f) * vecMax2(cosThetaO, 0.0f);
    return vecMake3(
        weights.x * profileR * diffuse,
        weights.y * profileG * diffuse,
        weights.z * profileB * diffuse);
}

SUBSURFACE_CORE_FN void subsurfaceSampleExitOffset(
    const BrdfContext& ctx,
    float u1,
    float u2,
    float u3,
    Vec3& outOffset,
    float& outDiskDistance,
    float& outPdf)
{
    Vec3 tangent{};
    Vec3 bitangent{};
    brdfBuildBasis(ctx.normal, tangent, bitangent);

    const float effectiveRadius = subsurfaceEffectiveRadius(ctx.material, u3);
    const float r = subsurfaceSampleBurleyRadius(u1, effectiveRadius);
    const float phi = 2.0f * BrdfDetail::kPi * u2;
    const Vec3 tangentOffset = vecAdd3(
        vecScale3(tangent, r * cosf(phi)),
        vecScale3(bitangent, r * sinf(phi)));
    outOffset = tangentOffset;
    outDiskDistance = r;
    outPdf = subsurfaceBurleyPdfRadius(r, effectiveRadius) / vecMax2(2.0f * BrdfDetail::kPi * r, 1.0e-8f);
}

SUBSURFACE_CORE_FN Vec3 subsurfaceInternalStepDirection(Vec3 normal, float u1, float u2)
{
    Vec3 tangent{};
    Vec3 bitangent{};
    brdfBuildBasis(normal, tangent, bitangent);
    const float cosTheta = 1.0f - 2.0f * u1;
    const float sinTheta = sqrtf(vecMax2(0.0f, 1.0f - cosTheta * cosTheta));
    const float phi = 2.0f * BrdfDetail::kPi * u2;
    const Vec3 local = vecMake3(sinTheta * cosf(phi), sinTheta * sinf(phi), cosTheta);
    return brdfLocalToWorld(local, normal, tangent, bitangent);
}

#undef SUBSURFACE_CORE_FN
