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
constexpr float kAirIor = 1.0f;

} // namespace BrdfDetail

struct BrdfSampleResult
{
    Vec3 direction{};
    float pdf = 0.0f;
    bool valid = false;
    bool transmitted = false;
    float nextMediumEta = 1.0f;
};

struct BrdfContext
{
    Vec3 normal{};
    Vec3 wo{};
    MaterialGpu material{};
    float etaMedium = 1.0f;
    float wavelengthNm = 550.0f;
    int debugFlags = 0;
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

BRDF_BASE_FN float brdfDiffuseRoughness(const MaterialGpu& material)
{
    const float value = material.diffuseRoughness < 0.0f ? material.roughness : material.diffuseRoughness;
    return vecMax2(0.0f, vecMin2(1.0f, value));
}

BRDF_BASE_FN float brdfOrenNayarSigma(float diffuseRoughness)
{
    return diffuseRoughness * 0.5f * BrdfDetail::kPi;
}

BRDF_BASE_FN float brdfOrenNayarFactor(
    float sigma,
    float cosThetaI,
    float cosThetaO,
    float dotWoWi)
{
    if (sigma <= 1.0e-4f) {
        return 1.0f;
    }

    const float sigma2 = sigma * sigma;
    const float A = 1.0f - 0.5f * sigma2 / (sigma2 + 0.33f);
    const float B = 0.45f * sigma2 / (sigma2 + 0.09f);

    const float sinThetaI = sqrtf(vecMax2(0.0f, 1.0f - cosThetaI * cosThetaI));
    const float sinThetaO = sqrtf(vecMax2(0.0f, 1.0f - cosThetaO * cosThetaO));

    float sinAlpha = 0.0f;
    float sinBeta = 0.0f;
    if (cosThetaI > cosThetaO) {
        sinAlpha = sinThetaI;
        sinBeta = sinThetaO;
    } else {
        sinAlpha = sinThetaO;
        sinBeta = sinThetaI;
    }

    float cosPhi = 1.0f;
    const float d = dotWoWi - cosThetaI * cosThetaO;
    if (d <= 0.0f) {
        const float denom = vecMax2(sinAlpha * sinBeta, 1.0e-8f);
        cosPhi = vecMax2(-1.0f, vecMin2(1.0f, d / denom));
    } else {
        sinAlpha = 0.0f;
        sinBeta = 0.0f;
    }

    const float tanBeta = sinBeta / vecMax2(cosThetaI, cosThetaO);
    return A + B * vecMax2(0.0f, cosPhi) * sinAlpha * tanBeta;
}

#undef BRDF_BASE_FN
