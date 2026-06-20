#include "Brdf/BrdfDispatch.h"
#include "Brdf/BrdfBase.h"
#include "Brdf/BrdfDebug.h"
#include "Brdf/PrincipledBrdf.h"
#include "MeshAccel/MeshAccelTypes.h"

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
    return BrdfContext{normal, wo, material, etaMedium, 550.0f, BrdfDebugFlags::kNone};
}

BrdfContext makeContextFromInside(const MaterialGpu& material, float etaMedium)
{
    const Vec3 normal = vecMake3(0.0f, 0.0f, 1.0f);
    const Vec3 wo = vecNormalize3(vecMake3(0.2f, 0.1f, -1.0f));
    return BrdfContext{normal, wo, material, etaMedium, 550.0f, BrdfDebugFlags::kNone};
}

MaterialGpu makeMaterial(
    float r,
    float g,
    float b,
    float roughness = 0.5f,
    float metallic = 0.0f,
    float transmission = 0.0f,
    float thin = 0.0f,
    float ior = 1.5f,
    float subsurface = 0.0f,
    float emission = 0.0f,
    float diffuseRoughness = -1.0f,
    float scatterRadiusR = 0.0f,
    float scatterRadiusG = 0.0f,
    float scatterRadiusB = 0.0f,
    float specular = 1.0f)
{
    MaterialGpu material{};
    material.r = r;
    material.g = g;
    material.b = b;
    material.roughness = roughness;
    material.metallic = metallic;
    material.transmission = transmission;
    material.thin = thin;
    material.ior = ior;
    material.subsurface = subsurface;
    material.emission = emission;
    material.diffuseRoughness = diffuseRoughness;
    material.scatterRadiusR = scatterRadiusR;
    material.scatterRadiusG = scatterRadiusG;
    material.scatterRadiusB = scatterRadiusB;
    material.specular = specular;
    return material;
}

void testTransmissiveSample_ThroughputNearOne()
{
    const MaterialGpu material = makeMaterial(1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    const BrdfContext ctx = makeContext(material);
    const PrincipledBrdf brdf{};

    const BrdfSampleResult sample = brdf.sample(ctx, 0.6f, 0.5f);
    expectTrue(sample.valid, "transmissive sample valid");
    expectTrue(sample.transmitted, "transmissive sample marked transmitted");

    const Vec3 bsdfValue = brdf.eval(ctx, sample.direction);
    const Vec3 throughput = brdfApplyThroughput(vecMake3(1.0f, 1.0f, 1.0f), ctx, sample, bsdfValue);
    expectNear(throughput.x, 1.0f, 0.15f, "transmissive throughput near unity");
}

void testTransmissiveSample_Reflected_ThroughputNearOne()
{
    const MaterialGpu material = makeMaterial(1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    const BrdfContext ctx = makeContext(material);
    const PrincipledBrdf brdf{};

    const BrdfSampleResult sample = brdf.sample(ctx, 0.99f, 0.5f);
    expectTrue(sample.valid, "reflected transmissive sample valid");
    expectTrue(!sample.transmitted, "reflected transmissive sample not transmitted");

    const Vec3 bsdfValue = brdf.eval(ctx, sample.direction);
    const Vec3 throughput = brdfApplyThroughput(vecMake3(1.0f, 1.0f, 1.0f), ctx, sample, bsdfValue);
    expectNear(throughput.x, 1.0f, 0.15f, "reflected transmissive throughput near unity");
}

void testThinTransmissiveSample()
{
    const MaterialGpu material = makeMaterial(0.2f, 0.8f, 0.1f, 0.5f, 0.0f, 1.0f, 1.0f);
    const BrdfContext ctx = makeContext(material);
    const PrincipledBrdf brdf{};

    const BrdfSampleResult sample = brdf.sample(ctx, 0.6f, 0.5f);
    expectTrue(sample.valid, "thin transmissive sample valid");
    expectTrue(sample.transmitted, "thin transmissive sample marked transmitted");
    expectNear(vecDot3(sample.direction, vecScale3(ctx.wo, -1.0f)), 1.0f, 0.01f, "thin transmission flips side");
}

void testSubsurfaceSample()
{
    const MaterialGpu material = makeMaterial(
        0.9f, 0.85f, 0.7f, 0.85f, 0.0f, 0.0f, 0.0f, 1.5f, 0.8f, 0.0f, 0.85f, 0.05f, 0.04f, 0.02f);
    const BrdfContext ctx = makeContext(material);
    const PrincipledBrdf brdf{};

    const BrdfSampleResult sample = brdf.sample(ctx, 0.95f, 0.25f);
    expectTrue(sample.valid, "subsurface sample valid");
    expectTrue(vecDot3(ctx.normal, sample.direction) > 0.0f, "subsurface stays in upper hemisphere");
    expectTrue(sample.subsurfaceScatter, "subsurface sample flagged");
}

void testMetallicSample()
{
    const MaterialGpu material = makeMaterial(0.9f, 0.9f, 0.9f, 0.15f, 1.0f);
    const BrdfContext ctx = makeContext(material);
    const PrincipledBrdf brdf{};

    const BrdfSampleResult sample = brdf.sample(ctx, 0.25f, 0.75f);
    expectTrue(sample.valid, "metallic sample valid");
    expectTrue(vecDot3(ctx.normal, sample.direction) > 0.0f, "metallic reflection in upper hemisphere");
}

void testDiffuseThroughput()
{
    const MaterialGpu material = makeMaterial(0.72f, 0.68f, 0.58f, 0.85f);
    const BrdfContext ctx = makeContext(material);
    const PrincipledBrdf brdf{};

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

void testBrdfSkipsEnvironmentNee_FullyTransmissive()
{
    const MaterialGpu transmissive = makeMaterial(1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f);
    const MaterialGpu opaque = makeMaterial(0.72f, 0.68f, 0.58f);
    expectTrue(brdfSkipsEnvironmentNee(transmissive), "fully transmissive skips environment NEE");
    expectTrue(!brdfSkipsEnvironmentNee(opaque), "opaque material uses environment NEE");
}

void testLobeWeights_FromParameters()
{
    MaterialGpu material = makeMaterial(1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f);
    const BrdfContext ctx = makeContext(material);
    const PrincipledDetail::LobeWeights weights = PrincipledDetail::computeLobeWeights(ctx);
    expectTrue(weights.transmit >= 0.99f, "high transmission increases transmit lobe weight");
    expectTrue(weights.specular < 0.01f, "specular lobe weight drops when fully transmissive");
}

void testRefractedDirection_NonZero()
{
    const MaterialGpu material = makeMaterial(1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    const BrdfContext ctx = makeContext(material);
    const PrincipledBrdf brdf{};

    Vec3 refracted{};
    float etaI = 0.0f;
    float etaT = 0.0f;
    float nextMediumEta = 1.0f;
    expectTrue(
        PrincipledDetail::refractDirection(ctx, refracted, etaI, etaT, nextMediumEta),
        "Snell refraction succeeds");
    const Vec3 value = brdf.eval(ctx, refracted);
    expectTrue(value.x > 0.0f, "refracted eval > 0");
}

void testSampleTransmit_ForcedRefract()
{
    const MaterialGpu material = makeMaterial(1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.5f);
    const BrdfContext ctx = makeContext(material);
    const PrincipledBrdf brdf{};

    const BrdfSampleResult sample = brdf.sampleTransmit(ctx, 0.0f, 0.5f);
    expectTrue(sample.valid, "forced refract sample valid");
    expectTrue(sample.transmitted, "forced refract marked transmitted");
    expectNear(sample.nextMediumEta, 1.5f, 0.01f, "entering glass sets nextMediumEta to ior");
    expectTrue(vecDot3(ctx.normal, sample.direction) < 0.0f, "refracted ray exits upper hemisphere");
}

void testSampleTransmit_ForcedReflect()
{
    const MaterialGpu material = makeMaterial(1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.5f);
    const BrdfContext ctx = makeContext(material);
    const PrincipledBrdf brdf{};

    const BrdfSampleResult sample = brdf.sampleTransmit(ctx, 1.0f, 0.5f);
    expectTrue(sample.valid, "forced reflect sample valid");
    expectTrue(!sample.transmitted, "forced reflect not transmitted");
    expectNear(sample.nextMediumEta, 1.0f, 0.01f, "reflected ray keeps current medium eta");
    expectTrue(vecDot3(ctx.normal, sample.direction) > 0.0f, "reflected ray stays in upper hemisphere");
}

void testDebugFlags_DisableRefract()
{
    const MaterialGpu material = makeMaterial(1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.5f);
    BrdfContext ctx = makeContext(material);
    ctx.debugFlags = BrdfDebugFlags::kDisableRefract;
    const PrincipledBrdf brdf{};

    const BrdfSampleResult sample = brdf.sampleTransmit(ctx, 0.0f, 0.5f);
    expectTrue(sample.valid, "disable refract sample valid");
    expectTrue(!sample.transmitted, "disable refract always reflects");
    expectTrue(vecDot3(ctx.normal, sample.direction) > 0.0f, "disable refract stays reflected");
}

void testDebugFlags_DisableReflect()
{
    const MaterialGpu material = makeMaterial(1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.5f);
    BrdfContext ctx = makeContext(material);
    ctx.debugFlags = BrdfDebugFlags::kDisableReflect;
    const PrincipledBrdf brdf{};

    const BrdfSampleResult sample = brdf.sampleTransmit(ctx, 1.0f, 0.5f);
    expectTrue(sample.valid, "disable reflect sample valid");
    expectTrue(sample.transmitted, "disable reflect always refracts");
    expectNear(sample.nextMediumEta, 1.5f, 0.01f, "disable reflect enters glass medium");
}

void testDebugFlags_ForceTransmitLobeOnly()
{
    const MaterialGpu material = makeMaterial(1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.5f);
    BrdfContext ctx = makeContext(material);
    ctx.debugFlags = BrdfDebugFlags::kForceTransmitLobeOnly;
    const PrincipledBrdf brdf{};

    const BrdfSampleResult sample = brdf.sample(ctx, 0.5f, 0.5f);
    expectTrue(sample.valid, "force transmit lobe sample valid");
    expectTrue(
        sample.transmitted || vecDot3(ctx.normal, sample.direction) > 0.0f,
        "force transmit lobe produces transmit-lobe behavior");
}

void testGlassMediumEta_EnterAndExit()
{
    const MaterialGpu material = makeMaterial(1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.5f);
    const PrincipledBrdf brdf{};

    const BrdfContext enterCtx = makeContext(material, 1.0f);
    const BrdfSampleResult enterSample = brdf.sampleTransmit(enterCtx, 0.0f, 0.5f);
    expectTrue(enterSample.transmitted, "enter glass transmits");
    expectNear(enterSample.nextMediumEta, 1.5f, 0.01f, "enter glass eta becomes ior");

    const BrdfContext exitCtx = makeContextFromInside(material, enterSample.nextMediumEta);
    const BrdfSampleResult exitSample = brdf.sampleTransmit(exitCtx, 0.0f, 0.5f);
    expectTrue(exitSample.transmitted, "exit glass transmits");
    expectNear(exitSample.nextMediumEta, 1.0f, 0.01f, "exit glass eta returns to air");
}

void testIntegratorGlassState_ConsistentWithSample()
{
    const MaterialGpu material = makeMaterial(1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.5f);
    const BrdfContext ctx = makeContext(material);
    const PrincipledBrdf brdf{};

    const BrdfSampleResult refract = brdf.sampleTransmit(ctx, 0.0f, 0.5f);
    expectTrue(refract.transmitted, "refract uses direction below normal for entry");
    expectNear(refract.nextMediumEta, 1.5f, 0.01f, "integrator copies nextMediumEta after refract");
    expectTrue(
        vecDot3(ctx.normal, refract.direction) < 0.0f,
        "refract bias sign offsets origin into transmitted hemisphere");

    const BrdfSampleResult reflect = brdf.sampleTransmit(ctx, 1.0f, 0.5f);
    expectTrue(!reflect.transmitted, "reflect keeps ray in current medium");
    expectNear(reflect.nextMediumEta, ctx.etaMedium, 0.01f, "integrator keeps etaMedium after reflect");
    expectTrue(
        vecDot3(ctx.normal, reflect.direction) > 0.0f,
        "reflect bias sign offsets origin into reflected hemisphere");
}

void testOrenNayar_ZeroRoughnessMatchesLambert()
{
    const MaterialGpu material = makeMaterial(0.8f, 0.8f, 0.8f, 0.5f, 0.0f, 0.0f, 0.0f, 1.5f, 0.0f, 0.0f, 0.0f);
    const BrdfContext ctx = makeContext(material);
    const Vec3 wi = vecNormalize3(vecMake3(0.3f, 0.2f, 1.0f));
    const float factor = PrincipledDetail::diffuseOrenNayarFactor(ctx, wi);
    expectNear(factor, 1.0f, 0.05f, "zero diffuse roughness approximates Lambert factor");
}

void testOrenNayar_HighRoughnessBroadensLobe()
{
    const MaterialGpu smooth = makeMaterial(0.8f, 0.8f, 0.8f, 0.1f, 0.0f, 0.0f, 0.0f, 1.5f, 0.0f, 0.0f, 0.0f);
    const MaterialGpu rough = makeMaterial(0.8f, 0.8f, 0.8f, 0.1f, 0.0f, 0.0f, 0.0f, 1.5f, 0.0f, 0.0f, 0.95f);
    const BrdfContext smoothCtx = makeContext(smooth);
    const BrdfContext roughCtx = makeContext(rough);
    const Vec3 wi = vecNormalize3(vecMake3(0.5f, 0.1f, 1.0f));
    const float smoothFactor = PrincipledDetail::diffuseOrenNayarFactor(smoothCtx, wi);
    const float roughFactor = PrincipledDetail::diffuseOrenNayarFactor(roughCtx, wi);
    expectNear(smoothFactor, 1.0f, 0.05f, "smooth diffuse stays near Lambert");
    expectTrue(fabsf(roughFactor - smoothFactor) > 0.05f, "rough diffuse changes lobe shape");
}

void testMetallic_NoDiffuseLeak()
{
    MaterialGpu material = makeMaterial(0.9f, 0.7f, 0.2f, 0.2f, 1.0f);
    const BrdfContext ctx = makeContext(material);
    const PrincipledDetail::LobeWeights weights = PrincipledDetail::computeLobeWeights(ctx);
    expectTrue(weights.diffuse < 0.01f, "metallic zeroes diffuse lobe");
    expectTrue(weights.transmit < 0.01f, "metallic zeroes transmission lobe");
    expectTrue(weights.subsurface < 0.01f, "metallic zeroes subsurface lobe");
}

void testMetallic_EnergyCompensationIncreasesRoughSpecular()
{
    const MaterialGpu material = makeMaterial(0.9f, 0.9f, 0.9f, 0.85f, 1.0f);
    const BrdfContext ctx = makeContext(material);
    const PrincipledBrdf brdf{};
    const BrdfSampleResult sample = brdf.sample(ctx, 0.3f, 0.6f);
    expectTrue(sample.valid, "rough metallic sample valid");
    const Vec3 bsdfValue = brdf.eval(ctx, sample.direction);
    expectTrue(bsdfValue.x > 0.01f, "rough metallic specular remains visible with compensation");
}

void testNestedGlass_EnterExitCycle()
{
    const MaterialGpu material = makeMaterial(1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.33f);
    const PrincipledBrdf brdf{};

    const BrdfContext airCtx = makeContext(material, 1.0f);
    const BrdfSampleResult enter = brdf.sampleTransmit(airCtx, 0.0f, 0.5f);
    expectTrue(enter.transmitted, "enter water from air");
    expectNear(enter.nextMediumEta, 1.33f, 0.01f, "inside water eta");

    const BrdfContext waterCtx = makeContextFromInside(material, enter.nextMediumEta);
    const BrdfSampleResult exit = brdf.sampleTransmit(waterCtx, 0.0f, 0.5f);
    expectTrue(exit.transmitted, "exit water to air");
    expectNear(exit.nextMediumEta, 1.0f, 0.01f, "back in air eta");

    const BrdfContext reenterCtx = makeContext(material, exit.nextMediumEta);
    const BrdfSampleResult reenter = brdf.sampleTransmit(reenterCtx, 0.0f, 0.5f);
    expectTrue(reenter.transmitted, "re-enter glass pane");
    expectNear(reenter.nextMediumEta, 1.33f, 0.01f, "nested glass eta tracking");
}

void testThinTransmission_TintedThroughput()
{
    const MaterialGpu material = makeMaterial(0.2f, 0.8f, 0.1f, 0.5f, 0.0f, 1.0f, 1.0f);
    const BrdfContext ctx = makeContext(material);
    const PrincipledBrdf brdf{};
    const BrdfSampleResult sample = brdf.sample(ctx, 0.1f, 0.5f);
    expectTrue(sample.valid && sample.transmitted, "thin tinted transmission sample");
    const Vec3 bsdfValue = brdf.eval(ctx, sample.direction);
    const Vec3 throughput = brdfApplyThroughput(vecMake3(1.0f, 1.0f, 1.0f), ctx, sample, bsdfValue);
    expectTrue(throughput.y > throughput.x, "thin shell tints throughput green");
}

void testRoughGlass_SamplesNonDeltaDirection()
{
    const MaterialGpu material = makeMaterial(1.0f, 1.0f, 1.0f, 0.35f, 0.0f, 1.0f, 0.0f, 1.5f);
    const BrdfContext ctx = makeContext(material);
    const PrincipledBrdf brdf{};
    const BrdfSampleResult sample = brdf.sampleTransmit(ctx, 0.05f, 0.37f);
    expectTrue(sample.valid, "rough glass refract sample valid");
    expectTrue(sample.transmitted, "rough glass transmits");
}

} // namespace

int main()
{
    testTransmissiveSample_ThroughputNearOne();
    testTransmissiveSample_Reflected_ThroughputNearOne();
    testThinTransmissiveSample();
    testSubsurfaceSample();
    testMetallicSample();
    testDiffuseThroughput();
    testBrdfSkipsEnvironmentNee_FullyTransmissive();
    testLobeWeights_FromParameters();
    testRefractedDirection_NonZero();
    testSampleTransmit_ForcedRefract();
    testSampleTransmit_ForcedReflect();
    testDebugFlags_DisableRefract();
    testDebugFlags_DisableReflect();
    testDebugFlags_ForceTransmitLobeOnly();
    testGlassMediumEta_EnterAndExit();
    testIntegratorGlassState_ConsistentWithSample();
    testOrenNayar_ZeroRoughnessMatchesLambert();
    testOrenNayar_HighRoughnessBroadensLobe();
    testMetallic_NoDiffuseLeak();
    testMetallic_EnergyCompensationIncreasesRoughSpecular();
    testNestedGlass_EnterExitCycle();
    testThinTransmission_TintedThroughput();
    testRoughGlass_SamplesNonDeltaDirection();

    if (gFailures != 0) {
        std::cerr << gFailures << " test(s) failed.\n";
        return 1;
    }

    std::cout << "All principled BRDF tests passed.\n";
    return 0;
}
