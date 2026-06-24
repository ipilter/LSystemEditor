#include "Brdf/BrdfDispatch.h"
#include "Brdf/BrdfBase.h"
#include "Brdf/BrdfLobe.h"
#include "Brdf/OrenNayarDiffuseBrdf.h"
#include "Bssrdf/BssrdfCore.h"
#include "Material/MaterialParams.h"
#include "Material/MaterialType.h"
#include "Medium/MediumProperties.h"
#include "Medium/VolumeCore.h"
#include "MeshAccel/MeshAccelTypes.h"
#include "Path/PathState.h"
#include "Spectral/SpectralCore.h"
#include "Spectral/SpectralState.h"

#include <cmath>
#include <iostream>

namespace {

int gFailures = 0;

void expectTrue(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++gFailures;
    }
}

void expectNear(float actual, float expected, float tolerance, const char* message)
{
    if (std::fabs(actual - expected) > tolerance) {
        std::cerr << "FAIL: " << message << " (actual=" << actual << ", expected=" << expected << ")\n";
        ++gFailures;
    }
}

BrdfContext makeContext(const MaterialGpu& material, float etaMedium = 1.0f)
{
    const Vec3 normal = vecMake3(0.0f, 0.0f, 1.0f);
    const Vec3 wo = vecNormalize3(vecMake3(0.2f, 0.1f, 1.0f));
    return BrdfContext{normal, wo, material, etaMedium, 550.0f, 0};
}

MaterialGpu makeMaterial(
    float r,
    float g,
    float b,
    float roughness = 0.5f,
    float metallic = 0.0f,
    float ior = 1.5f,
    float emission = 0.0f,
    float diffuseRoughness = -1.0f,
    uint32_t materialType = static_cast<uint32_t>(MaterialType::Opaque),
    float subsurface = 0.0f,
    float subsurfaceRadius = 1.0f,
    float specular = 1.0f)
{
    MaterialGpu material{};
    material.r = r;
    material.g = g;
    material.b = b;
    material.roughness = roughness;
    material.metallic = metallic;
    material.ior = ior;
    material.emission = emission;
    material.diffuseRoughness = diffuseRoughness;
    material.materialType = materialType;
    material.subsurface = subsurface;
    material.subsurfaceRadiusR = subsurfaceRadius;
    material.subsurfaceRadiusG = subsurfaceRadius;
    material.subsurfaceRadiusB = subsurfaceRadius;
    material.specular = specular;
    material.abbeNumber = 58.0f;
    return material;
}

void testDiffuseSample()
{
    const MaterialGpu material = makeMaterial(0.72f, 0.68f, 0.58f, 0.85f);
    const BrdfContext ctx = makeContext(material);
    const BrdfSampleResult sample = brdfSampleReflect(ctx, 0.25f, 0.75f);
    expectTrue(sample.valid, "diffuse sample valid");
    expectTrue(vecDot3(ctx.normal, sample.direction) > 0.0f, "diffuse reflection in upper hemisphere");
}

void testDiffuseThroughput()
{
    const MaterialGpu material = makeMaterial(0.72f, 0.68f, 0.58f, 0.85f);
    const BrdfContext ctx = makeContext(material);
    const OrenNayarDiffuseBrdf brdf{};

    for (float u1 = 0.1f; u1 < 0.9f; u1 += 0.2f) {
        for (float u2 = 0.1f; u2 < 0.9f; u2 += 0.2f) {
            const BrdfSampleResult sample = brdf.sample(ctx, u1, u2);
            if (!sample.valid) {
                continue;
            }
            const Vec3 bsdfValue = brdf.eval(ctx, sample.direction);
            const float cosTheta = vecMax2(0.0f, vecDot3(ctx.normal, sample.direction));
            const float scale = cosTheta * bsdfValue.x / sample.pdf;
            expectTrue(scale > 0.1f && scale < 1.5f, "diffuse throughput in reasonable range");
        }
    }
}

void testSurfaceLobeWeights()
{
    const MaterialGpu material = makeMaterial(1.0f, 1.0f, 1.0f, 0.2f, 0.0f);
    const BrdfLobeWeights weights = computeSurfaceLobeWeights(material);
    expectNear(weights.diffuse, 1.0f, 0.01f, "v1 uses diffuse-only lobe");
    expectNear(weights.specular, 0.0f, 0.01f, "specular lobe stubbed off");
}

void testDiffuseSurfaceDefaultsToOpaquePath()
{
    const MaterialGpu material = makeMaterial(0.9f, 0.9f, 0.9f, 0.9f);
    expectTrue(materialIsOpaqueType(material), "default material type is opaque");
}

void testSubsurfaceUsesVolumeTransport()
{
    const MaterialGpu wax = makeMaterial(
        0.9f, 0.95f, 0.2f, 0.8f, 0.0f, 1.5f, 0.0f, -1.0f,
        static_cast<uint32_t>(MaterialType::Subsurface), 1.0f, 2.0f);
    expectTrue(materialIsSubsurfaceType(wax), "typed wax is subsurface");
    expectTrue(materialUsesVolumeTransport(wax), "subsurface uses random-walk volume transport");
}

void testMaterialParamsMapping()
{
    const MaterialGpu wax = makeMaterial(
        0.8f, 0.6f, 0.4f, 0.5f, 0.0f, 1.5f, 0.0f, -1.0f,
        static_cast<uint32_t>(MaterialType::Subsurface), 1.0f, 2.0f);
    const PhysicalMediumCoeffs coeffs = materialToPhysicalMedium(wax, 550.0f);
    expectNear(coeffs.sigmaT.x, 0.5f, 0.01f, "sigma_t = 1 / radius");
    expectNear(coeffs.sigmaS.x / coeffs.sigmaT.x, 0.8f / (0.8f + 0.6f + 0.4f), 0.05f, "albedo maps to sigma_s/sigma_t");
}

void testBssrdfEnterProbability()
{
    const MaterialGpu wax = makeMaterial(
        0.8f, 0.8f, 0.8f, 0.5f, 0.0f, 1.5f, 0.0f, -1.0f,
        static_cast<uint32_t>(MaterialType::Subsurface), 0.5f, 2.0f);
    expectNear(bssrdfEnterProbability(wax), 0.5f, 0.01f, "enter probability follows subsurface weight");
}

void testLegacySubsurfaceInference()
{
    MaterialGpu legacy{};
    legacy.sigmaSr = 0.3f;
    legacy.sigmaSg = 0.25f;
    legacy.sigmaSb = 0.15f;
    legacy.sigmaAr = 0.02f;
    legacy.sigmaAg = 0.015f;
    legacy.sigmaAb = 0.01f;
    legacy.ior = 1.5f;
    applyLegacySubsurfaceInference(legacy);
    expectTrue(materialIsSubsurfaceType(legacy), "legacy sigma infers subsurface type");
    expectNear(legacy.subsurfaceRadiusR, 1.0f / 0.32f, 0.01f, "legacy inference sets radius from sigmaT");
}

void testOrenNayar_ZeroRoughnessMatchesLambert()
{
    const MaterialGpu material = makeMaterial(0.8f, 0.8f, 0.8f, 0.0f, 0.0f, 1.5f, 0.0f, 0.0f);
    const BrdfContext ctx = makeContext(material);
    const Vec3 wi = vecNormalize3(vecMake3(0.3f, 0.2f, 1.0f));
    const float factor = OrenNayarDetail::diffuseOrenNayarFactor(ctx, wi);
    expectNear(factor, 1.0f, 0.05f, "zero diffuse roughness approximates Lambert factor");
}

void testSpectralBrdfEvalPositive()
{
    const MaterialGpu material = makeMaterial(0.72f, 0.68f, 0.58f, 0.5f);
    const BrdfContext ctx = makeContext(material);
    const Vec3 wi = vecNormalize3(vecMake3(0.3f, 0.2f, 1.0f));
    const OrenNayarDiffuseBrdf brdf{};
    const float spectral = brdf.evalSpectral(ctx, wi);
    expectTrue(spectral > 0.0f, "scalar hero-wavelength BRDF is positive");
}

void testPathStateDefaults()
{
    PathState path{};
    expectNear(path.wavelengthNm, 550.0f, 0.01f, "default hero wavelength");
    expectTrue(!path.insideVolume, "path starts outside volume");
}

void testVolumeFreeFlight()
{
    const float distance = mediumSampleFreeFlight(0.5f, 1.0f);
    expectTrue(distance > 0.0f, "free flight distance is positive");
}

} // namespace

int main()
{
    std::string initError;
    if (!spectralInitHostFromCoeffFile(PATHTRACER_RGB2SPEC_COEFF_PATH, &initError)) {
        std::cerr << "Failed to load rgb2spec coefficients: " << initError << '\n';
        return 1;
    }

    testDiffuseSample();
    testDiffuseThroughput();
    testSurfaceLobeWeights();
    testDiffuseSurfaceDefaultsToOpaquePath();
    testSubsurfaceUsesVolumeTransport();
    testMaterialParamsMapping();
    testBssrdfEnterProbability();
    testLegacySubsurfaceInference();
    testOrenNayar_ZeroRoughnessMatchesLambert();
    testSpectralBrdfEvalPositive();
    testPathStateDefaults();
    testVolumeFreeFlight();

    if (gFailures == 0) {
        std::cout << "All Oren-Nayar / material tests passed.\n";
        return 0;
    }

    std::cerr << gFailures << " test(s) failed.\n";
    return 1;
}
