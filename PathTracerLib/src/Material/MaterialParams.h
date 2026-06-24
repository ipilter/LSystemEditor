#pragma once

#include "Geometry/MathCore.h"
#include "Material/MaterialType.h"
#include "MeshAccel/MeshAccelTypes.h"
#include "Spectral/SpectralCore.h"

#if defined(__CUDACC__)
#define MATERIAL_PARAMS_FN __host__ __device__ inline
#else
#define MATERIAL_PARAMS_FN inline
#endif

namespace MaterialParamsDetail {

constexpr float kMinSigmaT = 1.0e-6f;
constexpr float kMinMeanFreePathMm = 0.001f;

} // namespace MaterialParamsDetail

/**
 * Artist-friendly material parameters mapped to physical volume coefficients.
 *
 * albedo rgb           -> alpha = sigma_s / (sigma_s + sigma_a)
 * subsurfaceRadius rgb -> sigma_t = 1 / mean_free_path
 * subsurface weight    -> scales effective sigma_t
 */
struct PhysicalMediumCoeffs
{
    Vec3 sigmaA{};
    Vec3 sigmaS{};
    Vec3 sigmaT{};
    Vec3 albedo{};
    float mediumG = 0.0f;
};

MATERIAL_PARAMS_FN float materialAlbedoChannel(float rgb, float sigmaS, float sigmaT)
{
    return sigmaT > MaterialParamsDetail::kMinSigmaT ? sigmaS / sigmaT : rgb;
}

MATERIAL_PARAMS_FN PhysicalMediumCoeffs materialToPhysicalMedium(
    const MaterialGpu& material,
    float wavelengthNm)
{
    PhysicalMediumCoeffs coeffs{};
    coeffs.mediumG = material.mediumG;

    const float sub = vecMin2(1.0f, vecMax2(0.0f, material.subsurface));
    const float weightSum = vecMax2(material.r + material.g + material.b, 1.0e-6f);

    const float explicitSigmaTr = vecMax2(0.0f, material.sigmaAr) + vecMax2(0.0f, material.sigmaSr);
    const float explicitSigmaTg = vecMax2(0.0f, material.sigmaAg) + vecMax2(0.0f, material.sigmaSg);
    const float explicitSigmaTb = vecMax2(0.0f, material.sigmaAb) + vecMax2(0.0f, material.sigmaSb);
    const float maxExplicitSigmaT = vecMax2(vecMax2(explicitSigmaTr, explicitSigmaTg), explicitSigmaTb);

    if (maxExplicitSigmaT > MaterialTypeDetail::kClearSigmaThreshold) {
        coeffs.sigmaA = vecMake3(
            vecMax2(0.0f, material.sigmaAr),
            vecMax2(0.0f, material.sigmaAg),
            vecMax2(0.0f, material.sigmaAb));
        coeffs.sigmaS = vecMake3(
            vecMax2(0.0f, material.sigmaSr),
            vecMax2(0.0f, material.sigmaSg),
            vecMax2(0.0f, material.sigmaSb));
    } else if (sub > 1.0e-6f) {
        const float radiusR = vecMax2(material.subsurfaceRadiusR * sub, MaterialParamsDetail::kMinMeanFreePathMm);
        const float radiusG = vecMax2(material.subsurfaceRadiusG * sub, MaterialParamsDetail::kMinMeanFreePathMm);
        const float radiusB = vecMax2(material.subsurfaceRadiusB * sub, MaterialParamsDetail::kMinMeanFreePathMm);

        coeffs.sigmaT = vecMake3(1.0f / radiusR, 1.0f / radiusG, 1.0f / radiusB);

        const float alphaR = material.r / weightSum;
        const float alphaG = material.g / weightSum;
        const float alphaB = material.b / weightSum;

        coeffs.sigmaS = vecMake3(
            alphaR * coeffs.sigmaT.x,
            alphaG * coeffs.sigmaT.y,
            alphaB * coeffs.sigmaT.z);
        coeffs.sigmaA = vecSub3(coeffs.sigmaT, coeffs.sigmaS);
    } else {
        return coeffs;
    }

    coeffs.sigmaT = vecMake3(
        coeffs.sigmaA.x + coeffs.sigmaS.x,
        coeffs.sigmaA.y + coeffs.sigmaS.y,
        coeffs.sigmaA.z + coeffs.sigmaS.z);
    coeffs.albedo = vecMake3(
        materialAlbedoChannel(material.r, coeffs.sigmaS.x, coeffs.sigmaT.x),
        materialAlbedoChannel(material.g, coeffs.sigmaS.y, coeffs.sigmaT.y),
        materialAlbedoChannel(material.b, coeffs.sigmaS.z, coeffs.sigmaT.z));

    (void)wavelengthNm;
    return coeffs;
}

MATERIAL_PARAMS_FN float mediumSigmaTAtWavelength(const PhysicalMediumCoeffs& coeffs, float wavelengthNm)
{
    const float sigmaA = spectralGlassAbsorptionAtWavelength(
        coeffs.sigmaA.x, coeffs.sigmaA.y, coeffs.sigmaA.z, wavelengthNm);
    const float sigmaS = spectralGlassAbsorptionAtWavelength(
        coeffs.sigmaS.x, coeffs.sigmaS.y, coeffs.sigmaS.z, wavelengthNm);
    return vecMax2(sigmaA + sigmaS, MaterialParamsDetail::kMinSigmaT);
}

MATERIAL_PARAMS_FN float mediumScatterAlbedoAtWavelength(const PhysicalMediumCoeffs& coeffs, float wavelengthNm)
{
    const float sigmaA = spectralGlassAbsorptionAtWavelength(
        coeffs.sigmaA.x, coeffs.sigmaA.y, coeffs.sigmaA.z, wavelengthNm);
    const float sigmaS = spectralGlassAbsorptionAtWavelength(
        coeffs.sigmaS.x, coeffs.sigmaS.y, coeffs.sigmaS.z, wavelengthNm);
    const float sigmaT = sigmaA + sigmaS;
    return sigmaT > MaterialParamsDetail::kMinSigmaT ? sigmaS / sigmaT : 0.0f;
}

#undef MATERIAL_PARAMS_FN
