#pragma once

#include "Geometry/MathCore.h"
#include "MeshAccel/MeshAccelTypes.h"
#include "Spectral/SpectralCore.h"

#include <cmath>

#if defined(__CUDACC__)
#define BSSRDF_CORE_FN __host__ __device__ inline
#else
#define BSSRDF_CORE_FN inline
#endif

namespace BssrdfDetail {

constexpr float kPi = 3.14159265f;
constexpr float kMinRadiusMm = 0.001f;

} // namespace BssrdfDetail

struct BssrdfExitSample
{
    Vec3 offsetPosition{};
    Vec3 traceDirection{};
    float pdf = 0.0f;
    bool valid = false;
};

BSSRDF_CORE_FN float bssrdfEffectiveRadius(const MaterialGpu& material, float wavelengthNm)
{
    const float sub = vecMin2(1.0f, vecMax2(0.0f, material.subsurface));
    if (sub <= 0.0f) {
        return 0.0f;
    }

    const float rR = material.subsurfaceRadiusR * sub;
    const float rG = material.subsurfaceRadiusG * sub;
    const float rB = material.subsurfaceRadiusB * sub;
    const float weightSum = material.r + material.g + material.b;
    if (weightSum <= 1.0e-6f) {
        return vecMax2(vecMax2(rR, rG), rB);
    }

    return (rR * material.r + rG * material.g + rB * material.b) / weightSum;
}

BSSRDF_CORE_FN float bssrdfSampleRadius(float u, float radiusMm)
{
    const float r = vecMax2(radiusMm, BssrdfDetail::kMinRadiusMm);
    const float uClamped = vecMin2(vecMax2(u, 1.0e-6f), 1.0f - 1.0e-6f);
    return r * sqrtf(-logf(1.0f - uClamped));
}

BSSRDF_CORE_FN void bssrdfBuildTangentFrame(Vec3 normal, Vec3& tangent, Vec3& bitangent)
{
    const Vec3 up = vecAbs(normal.z) < 0.999f
        ? vecMake3(0.0f, 0.0f, 1.0f)
        : vecMake3(0.0f, 1.0f, 0.0f);
    tangent = vecNormalize3(vecCross3(up, normal));
    bitangent = vecCross3(normal, tangent);
}

BSSRDF_CORE_FN BssrdfExitSample bssrdfSampleExitPoint(
    Vec3 position,
    Vec3 normal,
    const MaterialGpu& material,
    float wavelengthNm,
    float u1,
    float u2)
{
    BssrdfExitSample result{};
    const float radius = bssrdfEffectiveRadius(material, wavelengthNm);
    if (radius <= BssrdfDetail::kMinRadiusMm) {
        return result;
    }

    const float sampledR = bssrdfSampleRadius(u1, radius);
    const float phi = 2.0f * BssrdfDetail::kPi * u2;
    Vec3 tangent{};
    Vec3 bitangent{};
    bssrdfBuildTangentFrame(normal, tangent, bitangent);

    const Vec3 offset = vecAdd3(
        vecAdd3(position, vecScale3(tangent, sampledR * cosf(phi))),
        vecScale3(bitangent, sampledR * sinf(phi)));

    result.offsetPosition = offset;
    result.traceDirection = vecScale3(normal, -1.0f);
    const float diskArea = BssrdfDetail::kPi * radius * radius;
    result.pdf = diskArea > 1.0e-8f ? 1.0f / diskArea : 0.0f;
    result.valid = result.pdf > 0.0f;
    return result;
}

#undef BSSRDF_CORE_FN
