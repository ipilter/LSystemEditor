#pragma once

#include "Geometry/MathCore.h"
#include "MeshAccel/MaterialType.h"
#include "MeshAccel/MeshAccelTypes.h"
#include "Spectral/SpectralCore.h"

#if defined(__CUDACC__)
#define MEDIUM_PROPERTIES_FN __host__ __device__ inline
#else
#define MEDIUM_PROPERTIES_FN inline
#endif

namespace MediumDetail {

constexpr float kOpaqueSigmaS = 1000.0f;
constexpr float kClearSigmaThreshold = 1.0e-4f;

} // namespace MediumDetail

struct MediumProperties
{
    Vec3 sigmaA{};
    Vec3 sigmaS{};
    /** @brief Henyey-Greenstein anisotropy in [-1, 1]. */
    float mediumG = 0.0f;
    float ior = 1.5f;
    float abbeNumber = 58.0f;
};

MEDIUM_PROPERTIES_FN MediumProperties mediumFromMaterial(const MaterialGpu& material)
{
    MediumProperties medium{};
    medium.sigmaA = vecMake3(
        vecMax2(0.0f, material.sigmaAr),
        vecMax2(0.0f, material.sigmaAg),
        vecMax2(0.0f, material.sigmaAb));
    medium.sigmaS = vecMake3(
        vecMax2(0.0f, material.sigmaSr),
        vecMax2(0.0f, material.sigmaSg),
        vecMax2(0.0f, material.sigmaSb));
    medium.mediumG = material.mediumG;
    medium.ior = material.ior;
    medium.abbeNumber = material.abbeNumber;
    return medium;
}

MEDIUM_PROPERTIES_FN float mediumIorAtWavelength(const MediumProperties& medium, float lambdaNm)
{
    MaterialGpu material{};
    material.ior = medium.ior;
    material.abbeNumber = medium.abbeNumber;
    return spectralGlassIor(material, lambdaNm);
}

MEDIUM_PROPERTIES_FN Vec3 mediumSigmaT(const MediumProperties& medium)
{
    return vecMake3(
        medium.sigmaA.x + medium.sigmaS.x,
        medium.sigmaA.y + medium.sigmaS.y,
        medium.sigmaA.z + medium.sigmaS.z);
}

MEDIUM_PROPERTIES_FN float mediumMaxSigmaS(const MediumProperties& medium)
{
    return vecMax2(vecMax2(medium.sigmaS.x, medium.sigmaS.y), medium.sigmaS.z);
}

MEDIUM_PROPERTIES_FN float mediumMaxSigmaT(const MediumProperties& medium)
{
    const Vec3 sigmaT = mediumSigmaT(medium);
    return vecMax2(vecMax2(sigmaT.x, sigmaT.y), sigmaT.z);
}

MEDIUM_PROPERTIES_FN bool materialIsMetallicSurface(const MaterialGpu& material)
{
    return material.metallic > 0.5f;
}

MEDIUM_PROPERTIES_FN bool mediumIsOpaque(const MediumProperties& medium)
{
    return mediumMaxSigmaS(medium) >= MediumDetail::kOpaqueSigmaS;
}

MEDIUM_PROPERTIES_FN bool mediumIsClear(const MediumProperties& medium)
{
    const float maxSigmaS = mediumMaxSigmaS(medium);
    const float maxSigmaA = vecMax2(
        vecMax2(medium.sigmaA.x, medium.sigmaA.y),
        medium.sigmaA.z);
    return maxSigmaS < MediumDetail::kClearSigmaThreshold
        && maxSigmaA < MediumDetail::kClearSigmaThreshold;
}

MEDIUM_PROPERTIES_FN bool materialIsOpaque(const MaterialGpu& material)
{
    return materialIsOpaqueType(material);
}

MEDIUM_PROPERTIES_FN bool materialUsesVolumeTransport(const MaterialGpu& material)
{
    (void)material;
    return false;
}

MEDIUM_PROPERTIES_FN bool materialIsClearMedium(const MaterialGpu& material)
{
    if (materialIsMetallicSurface(material)) {
        return false;
    }
    if (material.materialType != 0u) {
        return materialIsGlassType(material);
    }
    const MediumProperties medium = mediumFromMaterial(material);
    if (!mediumIsClear(medium)) {
        return false;
    }
    return material.roughness < 0.05f;
}

#undef MEDIUM_PROPERTIES_FN
