#pragma once

#include "Geometry/MathCore.h"
#include "MeshAccel/MeshAccelTypes.h"

#include <cmath>

#if defined(__CUDACC__)
#define BRDF_BASE_FN __host__ __device__ inline
#else
#define BRDF_BASE_FN inline
#endif

namespace BrdfDetail {

constexpr float kPi = 3.14159265f;
constexpr float kInvPi = 1.0f / kPi;
constexpr float kMinPdf = 1.0e-8f;

} // namespace BrdfDetail

struct BrdfSampleResult
{
    Vec3 direction{};
    float pdf = 0.0f;
    bool valid = false;
    /** @brief True when the sampled path transmits through the surface (glass refraction). */
    bool transmitted = false;
    /** @brief IOR of the medium the ray travels in after this bounce. */
    float nextMediumEta = 1.0f;
};

struct BrdfContext
{
    Vec3 normal{};
    Vec3 wo{};
    MaterialGpu material{};
    /** @brief IOR of the medium the ray is currently in (1.0 = air). */
    float etaMedium = 1.0f;
    /** @brief Hero wavelength for this path in nanometers. */
    float wavelengthNm = 550.0f;
};

template<typename Derived>
struct BrdfBase
{
    BRDF_BASE_FN Vec3 eval(const BrdfContext& ctx, Vec3 wi) const
    {
        return static_cast<const Derived*>(this)->evalImpl(ctx, wi);
    }

    BRDF_BASE_FN BrdfSampleResult sample(const BrdfContext& ctx, float u1, float u2) const
    {
        return static_cast<const Derived*>(this)->sampleImpl(ctx, u1, u2);
    }

    BRDF_BASE_FN float pdf(const BrdfContext& ctx, Vec3 wi) const
    {
        return static_cast<const Derived*>(this)->pdfImpl(ctx, wi);
    }

    BRDF_BASE_FN float luminance(Vec3 color) const
    {
        return 0.2126f * color.x + 0.7152f * color.y + 0.0722f * color.z;
    }
};

BRDF_BASE_FN void brdfBuildBasis(Vec3 normal, Vec3& tangent, Vec3& bitangent)
{
    const Vec3 up = fabsf(normal.y) < 0.999f ? vecMake3(0.0f, 1.0f, 0.0f) : vecMake3(1.0f, 0.0f, 0.0f);
    tangent = vecNormalize3(vecCross3(up, normal));
    bitangent = vecNormalize3(vecCross3(normal, tangent));
}

BRDF_BASE_FN Vec3 brdfLocalToWorld(Vec3 local, Vec3 normal, Vec3 tangent, Vec3 bitangent)
{
    return vecNormalize3(vecAdd3(
        vecAdd3(vecScale3(tangent, local.x), vecScale3(bitangent, local.y)),
        vecScale3(normal, local.z)));
}

BRDF_BASE_FN Vec3 brdfSampleCosineHemisphereLocal(float u1, float u2)
{
    const float phi = 2.0f * BrdfDetail::kPi * u1;
    const float r = sqrtf(u2);
    const float x = r * cosf(phi);
    const float y = r * sinf(phi);
    const float z = sqrtf(vecMax2(0.0f, 1.0f - u2));
    return vecMake3(x, y, z);
}

BRDF_BASE_FN float brdfCosineHemispherePdf(float cosTheta)
{
    return vecMax2(0.0f, cosTheta) * BrdfDetail::kInvPi;
}

BRDF_BASE_FN Vec3 brdfBaseColor(const MaterialGpu& material)
{
    return vecMake3(material.r, material.g, material.b);
}

BRDF_BASE_FN float brdfAlphaFromRoughness(float roughness)
{
    const float clamped = vecMax2(roughness, 0.04f);
    return clamped * clamped;
}

BRDF_BASE_FN Vec3 brdfReflect3(Vec3 v, Vec3 n)
{
    return vecSub3(v, vecScale3(n, 2.0f * vecDot3(n, v)));
}

BRDF_BASE_FN float brdfGGXD(float cosThetaH, float alpha)
{
    const float alpha2 = alpha * alpha;
    const float cos2 = cosThetaH * cosThetaH;
    const float denom = cos2 * (alpha2 - 1.0f) + 1.0f;
    return alpha2 / (BrdfDetail::kPi * denom * denom);
}

BRDF_BASE_FN float brdfSmithG1(float cosTheta, float alpha)
{
    const float alpha2 = alpha * alpha;
    const float cos2 = vecMax2(cosTheta * cosTheta, 1.0e-8f);
    const float tan2 = (1.0f - cos2) / cos2;
    return 2.0f / (1.0f + sqrtf(1.0f + alpha2 * tan2));
}

BRDF_BASE_FN Vec3 brdfSchlickF(float cosTheta, Vec3 f0)
{
    const float t = vecMax2(0.0f, vecMin2(1.0f, 1.0f - cosTheta));
    const float t2 = t * t;
    const float t5 = t2 * t2 * t;
    return vecAdd3(vecScale3(f0, 1.0f - t5), vecMake3(t5, t5, t5));
}

BRDF_BASE_FN Vec3 brdfSampleGGXHalfLocal(float alpha, float u1, float u2, float& pdf)
{
    const float phi = 2.0f * BrdfDetail::kPi * u1;
    const float alpha2 = alpha * alpha;
    const float cosTheta = sqrtf((1.0f - u2) / (1.0f + (alpha2 - 1.0f) * u2));
    const float sinTheta = sqrtf(vecMax2(0.0f, 1.0f - cosTheta * cosTheta));
    const float cosPhi = cosf(phi);
    const float sinPhi = sinf(phi);
    pdf = (alpha2 * cosTheta) / (BrdfDetail::kPi * (cosTheta * cosTheta * (alpha2 - 1.0f) + 1.0f) * (cosTheta * cosTheta * (alpha2 - 1.0f) + 1.0f));
    return vecMake3(sinTheta * cosPhi, sinTheta * sinPhi, cosTheta);
}

BRDF_BASE_FN float brdfDielectricFresnel(float cosThetaI, float etaI, float etaT)
{
    float cosI = vecMax2(-1.0f, vecMin2(1.0f, cosThetaI));
    float eta = etaI / vecMax2(etaT, 1.0e-8f);
    if (cosI < 0.0f) {
        cosI = -cosI;
        eta = 1.0f / eta;
    }

    const float sin2t = eta * eta * (1.0f - cosI * cosI);
    if (sin2t >= 1.0f) {
        return 1.0f;
    }

    const float cosT = sqrtf(vecMax2(0.0f, 1.0f - sin2t));
    const float rParallel = (eta * cosI - cosT) / vecMax2(eta * cosI + cosT, 1.0e-8f);
    const float rPerpendicular = (cosI - eta * cosT) / vecMax2(cosI + eta * cosT, 1.0e-8f);
    return 0.5f * (rParallel * rParallel + rPerpendicular * rPerpendicular);
}

BRDF_BASE_FN bool brdfRefractRelative(Vec3 wo, Vec3 normal, float etaI, float etaT, Vec3& outWi)
{
    float cosThetaO = vecDot3(normal, wo);
    Vec3 n = normal;
    float etaRel = etaI / vecMax2(etaT, 1.0e-8f);
    if (cosThetaO > 0.0f) {
        n = vecScale3(normal, -1.0f);
        etaRel = 1.0f / etaRel;
        cosThetaO = -cosThetaO;
    }

    const float sin2t = etaRel * etaRel * (1.0f - cosThetaO * cosThetaO);
    if (sin2t >= 1.0f) {
        return false;
    }

    const float cosT = sqrtf(vecMax2(0.0f, 1.0f - sin2t));
    outWi = vecNormalize3(vecSub3(
        vecScale3(wo, -etaRel),
        vecScale3(n, etaRel * cosThetaO + cosT)));
    return true;
}

BRDF_BASE_FN bool brdfRefract3(Vec3 wo, Vec3 normal, float eta, Vec3& outWi)
{
    return brdfRefractRelative(wo, normal, 1.0f, eta, outWi);
}

#undef BRDF_BASE_FN
