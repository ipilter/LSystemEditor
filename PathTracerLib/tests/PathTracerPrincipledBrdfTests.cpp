#include "Brdf/BrdfDispatch.h"
#include "Brdf/BrdfBase.h"
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

BrdfContext makeContext(const MaterialGpu& material)
{
    const Vec3 normal = vecMake3(0.0f, 0.0f, 1.0f);
    const Vec3 wo = vecNormalize3(vecMake3(0.2f, 0.1f, 1.0f));
    return BrdfContext{normal, wo, material, 1.0f, 550.0f};
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
    float subsurface = 0.0f)
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
    const MaterialGpu material = makeMaterial(0.9f, 0.85f, 0.7f, 0.85f, 0.0f, 0.0f, 0.0f, 1.5f, 0.8f);
    const BrdfContext ctx = makeContext(material);
    const PrincipledBrdf brdf{};

    const BrdfSampleResult sample = brdf.sample(ctx, 0.95f, 0.25f);
    expectTrue(sample.valid, "subsurface sample valid");
    expectTrue(vecDot3(ctx.normal, sample.direction) > 0.0f, "subsurface stays in upper hemisphere");
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

    if (gFailures != 0) {
        std::cerr << gFailures << " test(s) failed.\n";
        return 1;
    }

    std::cout << "All principled BRDF tests passed.\n";
    return 0;
}
