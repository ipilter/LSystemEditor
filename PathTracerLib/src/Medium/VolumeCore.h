#pragma once

#include "Medium/MediumProperties.h"
#include "Spectral/SpectralCore.h"

#include <cmath>

#if defined(__CUDACC__)
#define VOLUME_CORE_FN __host__ __device__ inline
#else
#define VOLUME_CORE_FN inline
#endif

namespace VolumeDetail {

constexpr float kMinSigmaT = 1.0e-8f;
constexpr float kHgIsotropicEpsilon = 1.0e-4f;
constexpr float kInvFourPi = 0.07957747154594767f;

} // namespace VolumeDetail

VOLUME_CORE_FN float mediumSampleFreeFlight(float u, float sigmaT)
{
    const float safeSigmaT = vecMax2(sigmaT, VolumeDetail::kMinSigmaT);
    return -logf(vecMax2(1.0e-8f, u)) / safeSigmaT;
}

VOLUME_CORE_FN Vec3 mediumTransmittance(Vec3 sigmaA, float distance)
{
    return vecMake3(
        expf(-sigmaA.x * distance),
        expf(-sigmaA.y * distance),
        expf(-sigmaA.z * distance));
}

VOLUME_CORE_FN Vec3 mediumScatterThroughput(Vec3 sigmaS, Vec3 sigmaT)
{
    return vecMake3(
        sigmaT.x > VolumeDetail::kMinSigmaT ? sigmaS.x / sigmaT.x : 0.0f,
        sigmaT.y > VolumeDetail::kMinSigmaT ? sigmaS.y / sigmaT.y : 0.0f,
        sigmaT.z > VolumeDetail::kMinSigmaT ? sigmaS.z / sigmaT.z : 0.0f);
}

VOLUME_CORE_FN float mediumSigmaTAtWavelength(Vec3 sigmaA, Vec3 sigmaS, float lambdaNm)
{
    const float absorption = spectralGlassAbsorptionAtWavelength(sigmaA.x, sigmaA.y, sigmaA.z, lambdaNm);
    const float scattering = spectralGlassAbsorptionAtWavelength(sigmaS.x, sigmaS.y, sigmaS.z, lambdaNm);
    return absorption + scattering;
}

VOLUME_CORE_FN float mediumSigmaAAtWavelength(Vec3 sigmaA, float lambdaNm)
{
    return spectralGlassAbsorptionAtWavelength(sigmaA.x, sigmaA.y, sigmaA.z, lambdaNm);
}

VOLUME_CORE_FN float mediumTransmittanceAtWavelength(float sigmaA_lambda, float distance)
{
    return expf(-sigmaA_lambda * distance);
}

VOLUME_CORE_FN float mediumScatterAlbedoAtWavelength(Vec3 sigmaA, Vec3 sigmaS, float lambdaNm)
{
    const float sigmaA_lambda = spectralGlassAbsorptionAtWavelength(
        sigmaA.x, sigmaA.y, sigmaA.z, lambdaNm);
    const float sigmaS_lambda = spectralGlassAbsorptionAtWavelength(
        sigmaS.x, sigmaS.y, sigmaS.z, lambdaNm);
    const float sigmaT_lambda = sigmaA_lambda + sigmaS_lambda;
    return sigmaT_lambda > VolumeDetail::kMinSigmaT ? sigmaS_lambda / sigmaT_lambda : 0.0f;
}

VOLUME_CORE_FN float henyeyGreensteinEval(float cosTheta, float g)
{
    if (fabsf(g) < VolumeDetail::kHgIsotropicEpsilon) {
        return VolumeDetail::kInvFourPi;
    }

    const float g2 = g * g;
    const float denom = 1.0f + g2 - 2.0f * g * cosTheta;
    const float inv = 1.0f / sqrtf(vecMax2(denom, 1.0e-8f));
    const float scale = (1.0f - g2) * inv * inv * inv;
    return scale * VolumeDetail::kInvFourPi;
}

VOLUME_CORE_FN float henyeyGreensteinSampleCosTheta(float g, float u)
{
    if (fabsf(g) < VolumeDetail::kHgIsotropicEpsilon) {
        return 1.0f - 2.0f * u;
    }

    if (fabsf(g) > 0.999f) {
        const float g2 = g * g;
        const float term = (1.0f - g2) / vecMax2(1.0f - g + 2.0f * g * u, 1.0e-8f);
        const float cosTheta = (1.0f + g2 - term * term) / (2.0f * g);
        return vecMax2(-1.0f, vecMin2(1.0f, cosTheta));
    }

    const float g2 = g * g;
    const float term = (1.0f - g2) / (1.0f - g + 2.0f * g * u);
    const float cosTheta = (1.0f + g2 - term * term) / (2.0f * g);
    return vecMax2(-1.0f, vecMin2(1.0f, cosTheta));
}

VOLUME_CORE_FN Vec3 henyeyGreensteinSampleDirection(
    Vec3 incomingDir,
    float g,
    float u1,
    float u2)
{
    const float cosTheta = henyeyGreensteinSampleCosTheta(g, u1);
    const float sinTheta = sqrtf(vecMax2(0.0f, 1.0f - cosTheta * cosTheta));
    const float phi = 2.0f * 3.14159265f * u2;
    const float cosPhi = cosf(phi);
    const float sinPhi = sinf(phi);

    Vec3 tangent{};
    Vec3 bitangent{};
    const Vec3 wo = vecNormalize3(incomingDir);
    const Vec3 up = fabsf(wo.y) < 0.999f ? vecMake3(0.0f, 1.0f, 0.0f) : vecMake3(1.0f, 0.0f, 0.0f);
    tangent = vecNormalize3(vecCross3(up, wo));
    bitangent = vecNormalize3(vecCross3(wo, tangent));

    const Vec3 local = vecMake3(sinTheta * cosPhi, sinTheta * sinPhi, cosTheta);
    return vecNormalize3(vecAdd3(
        vecAdd3(vecScale3(tangent, local.x), vecScale3(bitangent, local.y)),
        vecScale3(wo, local.z)));
}

#undef VOLUME_CORE_FN
