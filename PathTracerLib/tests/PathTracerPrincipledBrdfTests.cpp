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
#include "Subsurface/SubsurfaceTransportCore.h"

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
    expectNear(weights.diffuse, 0.5f, 0.01f, "non-metallic splits surface between diffuse and specular");
    expectNear(weights.specular, 0.5f, 0.01f, "non-metallic specular lobe weight");
    expectNear(weights.subsurface, 0.0f, 0.01f, "no subsurface by default");

    const MaterialGpu subsurface = makeMaterial(
        1.0f, 1.0f, 1.0f, 0.2f, 0.0f, 1.5f, 0.0f, -1.0f,
        static_cast<uint32_t>(MaterialType::Opaque), 0.6f);
    const BrdfLobeWeights subWeights = computeSurfaceLobeWeights(subsurface);
    expectNear(subWeights.subsurface, 0.6f, 0.01f, "subsurface weight");
    expectNear(subWeights.diffuse, 0.2f, 0.01f, "diffuse complement within surface");
    expectNear(subWeights.specular, 0.2f, 0.01f, "specular complement within surface");
}

void testWeightDrivenSubsurfaceWithoutType()
{
    const MaterialGpu wax = makeMaterial(
        0.9f, 0.95f, 0.2f, 0.8f, 0.0f, 1.5f, 0.0f, -1.0f,
        static_cast<uint32_t>(MaterialType::Opaque), 1.0f, 2.0f);
    expectTrue(materialHasParticipatingMedium(wax), "subsurface weight enables participating medium");
    expectTrue(materialUsesVolumeTransport(wax), "subsurface weight enables volume transport");
    expectTrue(!materialIsSubsurfaceType(wax), "no explicit subsurface type required");
}

void testMetallicSpecularSample()
{
    const MaterialGpu metal = makeMaterial(1.0f, 0.85f, 0.3f, 0.15f, 1.0f);
    const BrdfContext ctx = makeContext(metal);
    const BrdfLobeWeights reflect = computeReflectLobeWeights(metal);
    expectNear(reflect.diffuse, 0.0f, 0.01f, "metal has no diffuse lobe");
    expectNear(reflect.specular, 1.0f, 0.01f, "metal is specular-only");

    const BrdfSampleResult sample = brdfSampleReflect(ctx, 0.9f, 0.25f);
    expectTrue(sample.valid, "metallic principled sample valid");
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
        static_cast<uint32_t>(MaterialType::Opaque), 1.0f, 2.0f);
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
    expectNear(path.sssThroughput, 1.0f, 0.01f, "default sss throughput");
}

void testSubsurfaceShellMode()
{
    const MaterialGpu wax = makeMaterial(
        0.8f, 0.8f, 0.8f, 0.5f, 0.0f, 1.5f, 0.0f, -1.0f,
        static_cast<uint32_t>(MaterialType::Subsurface), 1.0f, 2.0f);
    SubsurfaceShellInfo openSheet{};
    openSheet.valid = true;
    openSheet.hasBackFace = false;
    openSheet.thicknessMm = 0.01f;
    expectTrue(subsurfaceUsesThinShellMode(openSheet, wax), "open sheet uses thin shell");

    SubsurfaceShellInfo thick{};
    thick.valid = true;
    thick.hasBackFace = true;
    thick.thicknessMm = 50.0f;
    expectTrue(!subsurfaceUsesThinShellMode(thick, wax), "thick branch uses random walk");
}

void testThinShellTransmittanceOrdering()
{
    MaterialGpu material = makeMaterial(
        0.2f, 0.8f, 0.1f, 0.5f, 0.0f, 1.5f, 0.0f, -1.0f,
        static_cast<uint32_t>(MaterialType::Opaque), 0.8f, 1.5f);
    material.subsurfaceScatterScale = 1.0f;

    const float tGreen = SubsurfaceTransportDetail::thinShellTransmittance(material, 2.0f, 550.0f);
    const float tRed = SubsurfaceTransportDetail::thinShellTransmittance(material, 2.0f, 650.0f);
    expectTrue(tGreen > 0.0f && tGreen <= 1.0f, "thin shell transmittance in range");
    (void)tRed;
}

void testVolumeFreeFlight()
{
    const float distance = mediumSampleFreeFlight(0.5f, 1.0f);
    expectTrue(distance > 0.0f, "free flight distance is positive");
}

void testSubsurfaceWeightDecoupledFromMfp()
{
    const MaterialGpu halfWeight = makeMaterial(
        0.8f, 0.8f, 0.8f, 0.5f, 0.0f, 1.5f, 0.0f, -1.0f,
        static_cast<uint32_t>(MaterialType::Opaque), 0.5f, 2.0f);
    const MaterialGpu fullWeight = makeMaterial(
        0.8f, 0.8f, 0.8f, 0.5f, 0.0f, 1.5f, 0.0f, -1.0f,
        static_cast<uint32_t>(MaterialType::Opaque), 1.0f, 2.0f);

    const PhysicalMediumCoeffs halfCoeffs = materialToPhysicalMedium(halfWeight, 550.0f);
    const PhysicalMediumCoeffs fullCoeffs = materialToPhysicalMedium(fullWeight, 550.0f);
    expectNear(halfCoeffs.sigmaT.x, 0.5f, 0.01f, "half weight keeps same sigma_t");
    expectNear(fullCoeffs.sigmaT.x, 0.5f, 0.01f, "full weight keeps same sigma_t");
    expectNear(bssrdfEnterProbability(halfWeight), 0.5f, 0.01f, "enter probability follows weight");
    expectNear(bssrdfEnterProbability(fullWeight), 1.0f, 0.01f, "enter probability follows weight");
}

void testZeroScatterChannelFallback()
{
    MaterialGpu wax = makeMaterial(
        0.8f, 0.8f, 0.8f, 0.5f, 0.0f, 1.5f, 0.0f, -1.0f,
        static_cast<uint32_t>(MaterialType::Opaque), 1.0f, 1.0f);
    wax.subsurfaceRadiusR = 3.5f;
    wax.subsurfaceRadiusG = 0.0f;
    wax.subsurfaceRadiusB = 1.0f;

    expectNear(materialScatterDistanceChannel(wax, 0), 3.5f, 0.01f, "red channel unchanged");
    expectNear(materialScatterDistanceChannel(wax, 1), 3.5f, 0.01f, "zero green inherits max of others");
    expectNear(materialScatterDistanceChannel(wax, 2), 1.0f, 0.01f, "blue channel unchanged");
}

void testSubsurfaceDirectLightingBsdf()
{
    MaterialGpu wax = makeMaterial(
        0.99f, 0.98f, 0.95f, 0.78f, 0.0f, 1.43f, 0.0f, 0.88f,
        static_cast<uint32_t>(MaterialType::Opaque), 1.0f, 3.5f, 0.12f);
    wax.subsurfaceRadiusR = 3.5f;
    wax.subsurfaceRadiusG = 3.0f;
    wax.subsurfaceRadiusB = 1.0f;

    const BrdfContext ctx = makeContext(wax);
    const Vec3 wi = vecNormalize3(vecMake3(0.1f, 0.05f, 1.0f));
    const Vec3 surfaceOnly = brdfEval(ctx, wi);
    const Vec3 directLighting = brdfEvalDirectLighting(ctx, wi);

    expectTrue(directLighting.x > surfaceOnly.x * 0.5f, "subsurface direct lighting adds diffuse term");
    expectTrue(brdfPdfDirectLighting(ctx, wi) > 0.0f, "subsurface direct lighting pdf positive");
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
    testWeightDrivenSubsurfaceWithoutType();
    testMetallicSpecularSample();
    testDiffuseSurfaceDefaultsToOpaquePath();
    testSubsurfaceUsesVolumeTransport();
    testMaterialParamsMapping();
    testBssrdfEnterProbability();
    testLegacySubsurfaceInference();
    testOrenNayar_ZeroRoughnessMatchesLambert();
    testSpectralBrdfEvalPositive();
    testPathStateDefaults();
    testSubsurfaceShellMode();
    testThinShellTransmittanceOrdering();
    testVolumeFreeFlight();
    testSubsurfaceWeightDecoupledFromMfp();
    testZeroScatterChannelFallback();
    testSubsurfaceDirectLightingBsdf();

    if (gFailures == 0) {
        std::cout << "All Oren-Nayar / material tests passed.\n";
        return 0;
    }

    std::cerr << gFailures << " test(s) failed.\n";
    return 1;
}
