#pragma once

#include "Geometry/MathCore.h"
#include "MeshAccel/MeshAccelTypes.h"

#if defined(__CUDACC__)
#define MATERIAL_TYPE_FN __host__ __device__ inline
#else
#define MATERIAL_TYPE_FN inline
#endif

namespace MaterialTypeDetail {

constexpr float kLegacyOpaqueSigmaS = 1000.0f;
constexpr float kClearSigmaThreshold = 1.0e-4f;

} // namespace MaterialTypeDetail

MATERIAL_TYPE_FN MaterialType materialTypeEnum(const MaterialGpu& material)
{
    switch (material.materialType) {
    case static_cast<uint32_t>(MaterialType::Glass):
        return MaterialType::Glass;
    case static_cast<uint32_t>(MaterialType::Subsurface):
        return MaterialType::Subsurface;
    case static_cast<uint32_t>(MaterialType::Emissive):
        return MaterialType::Emissive;
    default:
        return MaterialType::Opaque;
    }
}

MATERIAL_TYPE_FN MaterialType inferLegacyMaterialType(const MaterialGpu& material)
{
    const float maxSigmaS = vecMax2(
        vecMax2(material.sigmaSr, material.sigmaSg),
        material.sigmaSb);
    const float maxSigmaA = vecMax2(
        vecMax2(material.sigmaAr, material.sigmaAg),
        material.sigmaAb);

    if (maxSigmaS >= MaterialTypeDetail::kLegacyOpaqueSigmaS) {
        return MaterialType::Opaque;
    }
    if (maxSigmaS > MaterialTypeDetail::kClearSigmaThreshold
        || maxSigmaA > MaterialTypeDetail::kClearSigmaThreshold) {
        return MaterialType::Subsurface;
    }
    if (maxSigmaS < MaterialTypeDetail::kClearSigmaThreshold
        && maxSigmaA < MaterialTypeDetail::kClearSigmaThreshold
        && material.ior > 1.0f + 1.0e-6f
        && material.roughness < 0.05f
        && material.metallic < 0.5f) {
        return MaterialType::Glass;
    }
    return MaterialType::Opaque;
}

MATERIAL_TYPE_FN MaterialType materialTypeOf(const MaterialGpu& material)
{
    if (material.metallic > 0.5f) {
        return MaterialType::Opaque;
    }
    if (material.materialType != 0u) {
        return materialTypeEnum(material);
    }
    return inferLegacyMaterialType(material);
}

MATERIAL_TYPE_FN bool materialIsOpaqueType(const MaterialGpu& material)
{
    const MaterialType type = materialTypeOf(material);
    return type == MaterialType::Opaque || type == MaterialType::Emissive;
}

MATERIAL_TYPE_FN bool materialIsGlassType(const MaterialGpu& material)
{
    return materialTypeOf(material) == MaterialType::Glass;
}

MATERIAL_TYPE_FN bool materialIsSubsurfaceType(const MaterialGpu& material)
{
    return materialTypeOf(material) == MaterialType::Subsurface;
}

MATERIAL_TYPE_FN bool materialHasParticipatingMedium(const MaterialGpu& material)
{
    if (material.subsurface > 1.0e-6f) {
        return true;
    }
    const float maxSigmaT = vecMax2(
        vecMax2(material.sigmaAr + material.sigmaSr, material.sigmaAg + material.sigmaSg),
        material.sigmaAb + material.sigmaSb);
    return maxSigmaT > MaterialTypeDetail::kClearSigmaThreshold;
}

MATERIAL_TYPE_FN void applyLegacySubsurfaceInference(MaterialGpu& material)
{
    if (material.materialType != 0u || material.subsurface > 0.0f) {
        return;
    }
    const MaterialType inferred = inferLegacyMaterialType(material);
    if (inferred != MaterialType::Subsurface) {
        return;
    }

    const float sigmaTr = vecMax2(0.0f, material.sigmaAr) + vecMax2(0.0f, material.sigmaSr);
    const float sigmaTg = vecMax2(0.0f, material.sigmaAg) + vecMax2(0.0f, material.sigmaSg);
    const float sigmaTb = vecMax2(0.0f, material.sigmaAb) + vecMax2(0.0f, material.sigmaSb);
    const float maxSigmaT = vecMax2(vecMax2(sigmaTr, sigmaTg), sigmaTb);
    const float fallbackRadius = maxSigmaT > MaterialTypeDetail::kClearSigmaThreshold
        ? 1.0f / maxSigmaT
        : 1.0f;

    material.materialType = static_cast<uint32_t>(MaterialType::Subsurface);
    material.subsurface = 1.0f;
    material.subsurfaceRadiusR = sigmaTr > MaterialTypeDetail::kClearSigmaThreshold
        ? 1.0f / sigmaTr
        : fallbackRadius;
    material.subsurfaceRadiusG = sigmaTg > MaterialTypeDetail::kClearSigmaThreshold
        ? 1.0f / sigmaTg
        : fallbackRadius;
    material.subsurfaceRadiusB = sigmaTb > MaterialTypeDetail::kClearSigmaThreshold
        ? 1.0f / sigmaTb
        : fallbackRadius;
}

#undef MATERIAL_TYPE_FN
