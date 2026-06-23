#include "Brdf/BrdfDispatch.h"
#include "Brdf/BrdfBase.h"
#include "Brdf/PrincipledBrdf.h"
#include "Medium/MediumProperties.h"
#include "Medium/VolumeCore.h"
#include "MeshAccel/MeshAccelTypes.h"
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
    float sigmaSr = 0.0f,
    float sigmaSg = 0.0f,
    float sigmaSb = 0.0f,
    float sigmaAr = 0.0f,
    float sigmaAg = 0.0f,
    float sigmaAb = 0.0f,
    float mediumG = 0.0f,
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
    material.sigmaSr = sigmaSr;
    material.sigmaSg = sigmaSg;
    material.sigmaSb = sigmaSb;
    material.sigmaAr = sigmaAr;
    material.sigmaAg = sigmaAg;
    material.sigmaAb = sigmaAb;
    material.mediumG = mediumG;
    material.specular = specular;
    material.abbeNumber = 58.0f;
    return material;
}

void testMetallicSample()
{
    const MaterialGpu material = makeMaterial(0.9f, 0.9f, 0.9f, 0.15f, 1.0f);
    const BrdfContext ctx = makeContext(material);
    const BrdfSampleResult sample = brdfSampleReflect(ctx, 0.25f, 0.75f);
    expectTrue(sample.valid, "metallic sample valid");
    expectTrue(vecDot3(ctx.normal, sample.direction) > 0.0f, "metallic reflection in upper hemisphere");
    expectTrue(materialIsMetallicSurface(material), "metallic classification");
    expectTrue(!materialUsesVolumeTransport(material), "metallic never uses volume transport");
}

void testDiffuseThroughput()
{
    const MaterialGpu material = makeMaterial(0.72f, 0.68f, 0.58f, 0.85f);
    const BrdfContext ctx = makeContext(material);
    const PrincipledBrdf brdf{};

    for (float u1 = 0.1f; u1 < 0.9f; u1 += 0.2f) {
        for (float u2 = 0.1f; u2 < 0.9f; u2 += 0.2f) {
            const BrdfSampleResult sample = brdf.sampleReflectImpl(ctx, u1, u2);
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

void testReflectLobeWeights()
{
    MaterialGpu material = makeMaterial(1.0f, 1.0f, 1.0f, 0.2f, 0.0f);
    const BrdfContext ctx = makeContext(material);
    const PrincipledDetail::LobeWeights weights = PrincipledDetail::computeReflectLobeWeights(ctx);
    expectNear(weights.diffuse, 0.5f, 0.01f, "dielectric has diffuse weight");
    expectNear(weights.specular, 0.5f, 0.01f, "dielectric has specular weight");

    material.metallic = 1.0f;
    const BrdfContext metalCtx = makeContext(material);
    const PrincipledDetail::LobeWeights metalWeights = PrincipledDetail::computeReflectLobeWeights(metalCtx);
    expectNear(metalWeights.diffuse, 0.0f, 0.01f, "metal kills diffuse lobe");
    expectNear(metalWeights.specular, 1.0f, 0.01f, "metal is all specular");
}

void testInterfaceFresnel()
{
    const MaterialGpu material = makeMaterial(1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.5f);
    const BrdfContext ctx = makeContext(material);
    const float fresnel = PrincipledDetail::interfaceFresnelReflectance(ctx);
    expectTrue(fresnel >= 0.0f && fresnel <= 1.0f, "fresnel in [0,1]");
    expectTrue(fresnel > 0.02f && fresnel < 0.2f, "normal incidence glass has modest fresnel");
}

void testRefractIntoGlass()
{
    const MaterialGpu material = makeMaterial(1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.5f);
    const BrdfContext ctx = makeContext(material);
    const BrdfSampleResult sample = brdfSampleRefract(ctx, 0.0f, 0.5f);
    expectTrue(sample.valid, "refract sample valid");
    expectTrue(sample.transmitted, "refract marked transmitted");
    expectNear(sample.nextMediumEta, 1.5f, 0.01f, "entering glass sets nextMediumEta to ior");
    expectTrue(vecDot3(ctx.normal, sample.direction) < 0.0f, "refracted ray exits upper hemisphere");
}

void testRefractOutOfGlass()
{
    const MaterialGpu material = makeMaterial(1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.5f);
    const Vec3 normal = vecMake3(0.0f, 0.0f, 1.0f);
    const Vec3 wo = vecNormalize3(vecMake3(0.2f, 0.1f, -1.0f));
    const BrdfContext ctx{normal, wo, material, 1.5f, 550.0f, 0};
    const BrdfSampleResult sample = brdfSampleRefract(ctx, 0.0f, 0.5f);
    expectTrue(sample.valid, "exit refraction valid with interior etaMedium");
    expectTrue(sample.transmitted, "exit refraction marked transmitted");
    expectNear(sample.nextMediumEta, 1.0f, 0.01f, "exiting glass sets nextMediumEta to air");
    expectTrue(vecDot3(ctx.normal, sample.direction) > 0.0f, "exiting ray continues into air");
}

void testDiffuseSurfaceDefaultsToOpaquePath()
{
    const MaterialGpu material = makeMaterial(
        0.9f, 0.9f, 0.9f, 0.9f, 0.0f, 1.5f, 0.0f, -1.0f,
        1000.0f, 1000.0f, 1000.0f);
    expectTrue(mediumIsOpaque(mediumFromMaterial(material)), "default sigmaS uses opaque shortcut");
    expectTrue(!materialUsesVolumeTransport(material), "surface-only dielectric skips volume path");
}

void testClearMediumClassification()
{
    const MaterialGpu glass = makeMaterial(1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.5f);
    expectTrue(materialIsClearMedium(glass), "zero sigma glass is clear");
    expectTrue(brdfSkipsEnvironmentNee(glass), "clear medium skips environment NEE");
    expectTrue(materialUsesVolumeTransport(glass), "clear dielectric still uses volume path at interface");
}

void testOpaqueMediumClassification()
{
    MaterialGpu plastic = makeMaterial(0.8f, 0.8f, 0.8f, 0.5f, 0.0f, 1.5f, 0.0f, -1.0f, 2000.0f, 2000.0f, 2000.0f);
    expectTrue(mediumIsOpaque(mediumFromMaterial(plastic)), "high sigmaS is opaque shortcut");
    expectTrue(!materialUsesVolumeTransport(plastic), "opaque dielectric skips volume transport");
}

void testOrenNayar_ZeroRoughnessMatchesLambert()
{
    const MaterialGpu material = makeMaterial(0.8f, 0.8f, 0.8f, 0.0f, 0.0f, 1.5f, 0.0f, 0.0f);
    const BrdfContext ctx = makeContext(material);
    const Vec3 wi = vecNormalize3(vecMake3(0.3f, 0.2f, 1.0f));
    const float factor = PrincipledDetail::diffuseOrenNayarFactor(ctx, wi);
    expectNear(factor, 1.0f, 0.05f, "zero diffuse roughness approximates Lambert factor");
}

void testFreeFlightMean()
{
    float sum = 0.0f;
    constexpr int samples = 10000;
    const float sigmaT = 2.0f;
    for (int i = 1; i <= samples; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(samples + 1);
        sum += mediumSampleFreeFlight(u, sigmaT);
    }
    expectNear(sum / static_cast<float>(samples), 1.0f / sigmaT, 0.02f, "free flight mean distance");
}

void testHgIsotropicPdf()
{
    expectNear(henyeyGreensteinEval(0.0f, 0.0f), 0.079577f, 0.001f, "isotropic HG eval");
    const float cosTheta = henyeyGreensteinSampleCosTheta(0.0f, 0.25f);
    expectNear(cosTheta, 0.5f, 0.01f, "isotropic HG sample uniform in cos theta");
}

void testSpectralBrdfEvalPositive()
{
    const MaterialGpu material = makeMaterial(0.72f, 0.68f, 0.58f, 0.5f);
    const BrdfContext ctx = makeContext(material);
    const Vec3 wi = vecNormalize3(vecMake3(0.3f, 0.2f, 1.0f));
    const PrincipledBrdf brdf{};
    const float spectral = brdf.evalSpectral(ctx, wi);
    expectTrue(spectral > 0.0f, "scalar hero-wavelength BRDF is positive");
}

void testGlassIorDispersionViaMaterial()
{
    MaterialGpu glass = makeMaterial(1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.52f);
    glass.abbeNumber = 40.0f;
    BrdfContext ctxRed = makeContext(glass);
    BrdfContext ctxBlue = makeContext(glass);
    ctxRed.wavelengthNm = 650.0f;
    ctxBlue.wavelengthNm = 450.0f;
    const float iorRed = PrincipledDetail::materialIor(ctxRed);
    const float iorBlue = PrincipledDetail::materialIor(ctxBlue);
    expectTrue(iorBlue > iorRed, "materialIor uses Abbe dispersion");
}

} // namespace

int main()
{
    std::string initError;
    if (!spectralInitHostFromCoeffFile(PATHTRACER_RGB2SPEC_COEFF_PATH, &initError)) {
        std::cerr << "Failed to load rgb2spec coefficients: " << initError << '\n';
        return 1;
    }

    testMetallicSample();
    testDiffuseThroughput();
    testReflectLobeWeights();
    testInterfaceFresnel();
    testRefractIntoGlass();
    testRefractOutOfGlass();
    testDiffuseSurfaceDefaultsToOpaquePath();
    testClearMediumClassification();
    testOpaqueMediumClassification();
    testOrenNayar_ZeroRoughnessMatchesLambert();
    testFreeFlightMean();
    testHgIsotropicPdf();
    testSpectralBrdfEvalPositive();
    testGlassIorDispersionViaMaterial();

    if (gFailures == 0) {
        std::cout << "All PrincipledBrdf / medium tests passed.\n";
        return 0;
    }

    std::cerr << gFailures << " test(s) failed.\n";
    return 1;
}
