#pragma once

#include "Geometry/MathCore.h"
#include "MeshAccel/MeshAccelTypes.h"

#include <cmath>

#if defined(__CUDACC__)
#define BSDF_CORE_FN __host__ __device__ inline
#else
#define BSDF_CORE_FN inline
#endif

namespace BsdfCoreDetail {

constexpr float kPi = 3.14159265f;
constexpr float kInvPi = 1.0f / kPi;
constexpr float kMinPdf = 1.0e-8f;
constexpr float kMinAlpha = 1.0e-4f;

} // namespace BsdfCoreDetail

enum class BsdfLobe : int
{
    None = 0,
    Diffuse = 1,
    Reflect = 2,
    Transmit = 3,
};

struct BsdfSampleResult
{
    Vec3 direction{};
    float pdf = 0.0f;
    BsdfLobe lobe = BsdfLobe::None;
    bool valid = false;
};

BSDF_CORE_FN void bsdfBuildBasis(Vec3 normal, Vec3& tangent, Vec3& bitangent)
{
    const Vec3 up = fabsf(normal.y) < 0.999f ? vecMake3(0.0f, 1.0f, 0.0f) : vecMake3(1.0f, 0.0f, 0.0f);
    tangent = vecNormalize3(vecCross3(up, normal));
    bitangent = vecNormalize3(vecCross3(normal, tangent));
}

BSDF_CORE_FN Vec3 bsdfLocalToWorld(Vec3 local, Vec3 normal, Vec3 tangent, Vec3 bitangent)
{
    return vecNormalize3(vecAdd3(
        vecAdd3(vecScale3(tangent, local.x), vecScale3(bitangent, local.y)),
        vecScale3(normal, local.z)));
}

BSDF_CORE_FN Vec3 bsdfWorldToLocal(Vec3 world, Vec3 normal, Vec3 tangent, Vec3 bitangent)
{
    return vecMake3(vecDot3(world, tangent), vecDot3(world, bitangent), vecDot3(world, normal));
}

BSDF_CORE_FN Vec3 bsdfSampleCosineHemisphere(float u1, float u2)
{
    const float phi = 2.0f * BsdfCoreDetail::kPi * u1;
    const float r = sqrtf(u2);
    const float x = r * cosf(phi);
    const float y = r * sinf(phi);
    const float z = sqrtf(vecMax2(0.0f, 1.0f - u2));
    return vecMake3(x, y, z);
}

BSDF_CORE_FN float bsdfCosineHemispherePdf(float cosTheta)
{
    return vecMax2(0.0f, cosTheta) * BsdfCoreDetail::kInvPi;
}

BSDF_CORE_FN float bsdfFresnelSchlick(float cosTheta, float ior)
{
    float r0 = (1.0f - ior) / (1.0f + ior);
    r0 = r0 * r0;
    const float oneMinusCos = 1.0f - vecMax2(0.0f, cosTheta);
    const float oneMinusCos2 = oneMinusCos * oneMinusCos;
    const float oneMinusCos5 = oneMinusCos2 * oneMinusCos2 * oneMinusCos;
    return r0 + (1.0f - r0) * oneMinusCos5;
}

BSDF_CORE_FN float bsdfFresnelDielectric(float cosThetaI, float etaI, float etaT)
{
    cosThetaI = vecClamp(cosThetaI, -1.0f, 1.0f);
    bool entering = cosThetaI > 0.0f;
    if (!entering) {
        const float temp = etaI;
        etaI = etaT;
        etaT = temp;
        cosThetaI = -cosThetaI;
    }

    const float sinThetaI = sqrtf(vecMax2(0.0f, 1.0f - cosThetaI * cosThetaI));
    const float sinThetaT = (etaI / etaT) * sinThetaI;
    if (sinThetaT >= 1.0f) {
        return 1.0f;
    }

    const float cosThetaT = sqrtf(vecMax2(0.0f, 1.0f - sinThetaT * sinThetaT));
    const float rParallel =
        ((etaT * cosThetaI) - (etaI * cosThetaT)) / ((etaT * cosThetaI) + (etaI * cosThetaT));
    const float rPerpendicular =
        ((etaI * cosThetaI) - (etaT * cosThetaT)) / ((etaI * cosThetaI) + (etaT * cosThetaT));
    return 0.5f * (rParallel * rParallel + rPerpendicular * rPerpendicular);
}

BSDF_CORE_FN bool bsdfRefract(Vec3 incident, Vec3 normal, float eta, Vec3& refracted)
{
    const float cosThetaI = vecDot3(incident, normal);
    const float sin2ThetaI = vecMax2(0.0f, 1.0f - cosThetaI * cosThetaI);
    const float sin2ThetaT = eta * eta * sin2ThetaI;
    if (sin2ThetaT > 1.0f) {
        return false;
    }
    const float cosThetaT = sqrtf(vecMax2(0.0f, 1.0f - sin2ThetaT));
    refracted = vecNormalize3(vecSub3(
        vecScale3(incident, eta),
        vecScale3(normal, eta * cosThetaI + cosThetaT)));
    return true;
}

BSDF_CORE_FN float bsdfGgxD(float cosThetaM, float alpha)
{
    const float cos2 = cosThetaM * cosThetaM;
    const float tan2 = vecMax2(0.0f, (1.0f - cos2) / cos2);
    const float denom = BsdfCoreDetail::kPi * cos2 * cos2 * (alpha * alpha + tan2) * (alpha * alpha + tan2);
    if (denom <= 0.0f) {
        return 0.0f;
    }
    return (alpha * alpha) / denom;
}

BSDF_CORE_FN float bsdfGgxG1(float cosTheta, float alpha)
{
    const float cos2 = cosTheta * cosTheta;
    const float tan2 = vecMax2(0.0f, (1.0f - cos2) / cos2);
    return 2.0f / (1.0f + sqrtf(1.0f + alpha * alpha * tan2));
}

BSDF_CORE_FN float bsdfGgxLambda(float cosTheta, float alpha)
{
    const float cos2 = cosTheta * cosTheta;
    const float tan2 = vecMax2(0.0f, (1.0f - cos2) / cos2);
    return (-1.0f + sqrtf(1.0f + alpha * alpha * tan2)) * 0.5f;
}

BSDF_CORE_FN float bsdfGgxSmithG(float cosThetaI, float cosThetaO, float alpha)
{
    return 1.0f / (1.0f + bsdfGgxLambda(cosThetaI, alpha) + bsdfGgxLambda(cosThetaO, alpha));
}

BSDF_CORE_FN Vec3 bsdfBaseColor(const MaterialGpu& material)
{
    return vecMake3(material.r, material.g, material.b);
}

BSDF_CORE_FN float bsdfAlpha(const MaterialGpu& material)
{
    const float roughness = vecClamp(material.roughness, 0.0f, 1.0f);
    const float alpha = roughness * roughness;
    return vecMax2(alpha, BsdfCoreDetail::kMinAlpha);
}

BSDF_CORE_FN Vec3 bsdfEvalDiffuse(
    Vec3 normal,
    Vec3 wi,
    Vec3 wo,
    const MaterialGpu& material)
{
    const float cosWi = vecMax2(0.0f, vecDot3(normal, wi));
    const float cosWo = vecMax2(0.0f, vecDot3(normal, wo));
    if (cosWi <= 0.0f || cosWo <= 0.0f) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    const Vec3 baseColor = bsdfBaseColor(material);
    const float diffuseWeight = (1.0f - material.metallic) * (1.0f - material.transmission);
    return vecScale3(baseColor, diffuseWeight * BsdfCoreDetail::kInvPi);
}

BSDF_CORE_FN Vec3 bsdfEvalSpecular(
    Vec3 normal,
    Vec3 wi,
    Vec3 wo,
    const MaterialGpu& material)
{
    const float cosWi = vecMax2(0.0f, vecDot3(normal, wi));
    const float cosWo = vecMax2(0.0f, vecDot3(normal, wo));
    if (cosWi <= 0.0f || cosWo <= 0.0f) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    const Vec3 baseColor = bsdfBaseColor(material);
    const Vec3 specColor = vecLerp3(vecMake3(0.04f, 0.04f, 0.04f), baseColor, material.metallic);
    const Vec3 halfVec = vecNormalize3(vecAdd3(wi, wo));
    const float cosThetaM = vecMax2(0.0f, vecDot3(normal, halfVec));
    const float alpha = bsdfAlpha(material);
    const float D = bsdfGgxD(cosThetaM, alpha);
    const float G = bsdfGgxSmithG(cosWi, cosWo, alpha);
    const float cosThetaH = vecMax2(0.0f, vecDot3(halfVec, wi));
    const float F = bsdfFresnelSchlick(cosThetaH, material.metallic > 0.5f ? 2.5f : 1.5f);
    const float denom = vecMax2(4.0f * cosWi * cosWo, 1.0e-6f);
    const float scale = (D * G * F) / denom;
    return vecMake3(specColor.x * scale, specColor.y * scale, specColor.z * scale);
}

BSDF_CORE_FN Vec3 bsdfEval(
    Vec3 normal,
    Vec3 wi,
    Vec3 wo,
    const MaterialGpu& material)
{
    const Vec3 diffuse = bsdfEvalDiffuse(normal, wi, wo, material);
    const Vec3 specular = bsdfEvalSpecular(normal, wi, wo, material);
    return vecAdd3(diffuse, specular);
}

BSDF_CORE_FN float bsdfPdfDiffuse(Vec3 normal, Vec3 wo)
{
    return bsdfCosineHemispherePdf(vecMax2(0.0f, vecDot3(normal, wo)));
}

BSDF_CORE_FN float bsdfPdfSpecular(Vec3 normal, Vec3 wi, Vec3 wo, const MaterialGpu& material)
{
    const Vec3 halfVec = vecNormalize3(vecAdd3(wi, wo));
    const float cosThetaM = vecMax2(0.0f, vecDot3(normal, halfVec));
    const float cosThetaO = vecMax2(0.0f, vecDot3(normal, wo));
    if (cosThetaM <= 0.0f || cosThetaO <= 0.0f) {
        return 0.0f;
    }
    const float alpha = bsdfAlpha(material);
    const float D = bsdfGgxD(cosThetaM, alpha);
    const float cosThetaH = vecMax2(0.0f, vecDot3(halfVec, wi));
    return D * cosThetaM / vecMax2(4.0f * cosThetaH * cosThetaO, 1.0e-6f);
}

BSDF_CORE_FN float bsdfPdf(
    Vec3 normal,
    Vec3 wi,
    Vec3 wo,
    const MaterialGpu& material,
    float diffuseWeight,
    float specWeight)
{
    const float totalWeight = diffuseWeight + specWeight;
    if (totalWeight <= 0.0f) {
        return 0.0f;
    }
    const float pdfDiffuse = bsdfPdfDiffuse(normal, wo);
    const float pdfSpec = bsdfPdfSpecular(normal, wi, wo, material);
    return (diffuseWeight * pdfDiffuse + specWeight * pdfSpec) / totalWeight;
}

BSDF_CORE_FN BsdfSampleResult bsdfSample(
    Vec3 normal,
    Vec3 wi,
    const MaterialGpu& material,
    float uLobe,
    float u1,
    float u2)
{
    BsdfSampleResult result{};
    const float diffuseWeight = (1.0f - material.metallic) * (1.0f - material.transmission);
    const float specWeight = vecMax2(0.05f, 1.0f - diffuseWeight);
    const float totalWeight = diffuseWeight + specWeight;
    const bool sampleDiffuse = uLobe < (diffuseWeight / totalWeight);

    Vec3 tangent{};
    Vec3 bitangent{};
    bsdfBuildBasis(normal, tangent, bitangent);

    if (sampleDiffuse) {
        const Vec3 local = bsdfSampleCosineHemisphere(u1, u2);
        result.direction = bsdfLocalToWorld(local, normal, tangent, bitangent);
        result.pdf = bsdfPdfDiffuse(normal, result.direction) * (diffuseWeight / totalWeight);
        result.lobe = BsdfLobe::Diffuse;
        result.valid = vecDot3(normal, result.direction) > 0.0f;
        return result;
    }

    const float alpha = bsdfAlpha(material);
    const float phi = 2.0f * BsdfCoreDetail::kPi * u1;
    const float cosTheta = sqrtf((1.0f - u2) / (1.0f + (alpha * alpha - 1.0f) * u2));
    const float sinTheta = sqrtf(vecMax2(0.0f, 1.0f - cosTheta * cosTheta));
    const Vec3 localHalf = vecMake3(sinTheta * cosf(phi), sinTheta * sinf(phi), cosTheta);
    const Vec3 halfVec = bsdfLocalToWorld(localHalf, normal, tangent, bitangent);
    result.direction = vecNormalize3(vecSub3(vecScale3(halfVec, 2.0f * vecDot3(wi, halfVec)), wi));
    result.pdf = bsdfPdfSpecular(normal, wi, result.direction, material) * (specWeight / totalWeight);
    result.lobe = BsdfLobe::Reflect;
    result.valid = vecDot3(normal, result.direction) > 0.0f && result.pdf > BsdfCoreDetail::kMinPdf;
    return result;
}

BSDF_CORE_FN BsdfSampleResult bsdfSampleTransmission(
    Vec3 normal,
    Vec3 wi,
    const MaterialGpu& material,
    float eta,
    float u1,
    float u2)
{
    BsdfSampleResult result{};
    if (material.transmission <= 0.0f) {
        return result;
    }

    Vec3 refracted{};
    if (!bsdfRefract(wi, normal, eta, refracted)) {
        return result;
    }

    result.direction = refracted;
    const float cosTheta = vecAbs(vecDot3(normal, refracted));
    result.pdf = bsdfCosineHemispherePdf(cosTheta);
    result.lobe = BsdfLobe::Transmit;
    result.valid = result.pdf > BsdfCoreDetail::kMinPdf;
    return result;
}

BSDF_CORE_FN float bsdfLuminance(Vec3 color)
{
    return 0.2126f * color.x + 0.7152f * color.y + 0.0722f * color.z;
}

#undef BSDF_CORE_FN
