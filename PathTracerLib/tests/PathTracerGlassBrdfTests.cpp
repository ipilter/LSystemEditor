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

MaterialGpu makeMaterial(float transmission)
{
    MaterialGpu material{};
    material.transmission = transmission;
    material.thin = 0.0f;
    material.ior = 1.5f;
    material.r = 1.0f;
    material.g = 1.0f;
    material.b = 1.0f;
    return material;
}

BrdfContext makeContext(const MaterialGpu& material)
{
    const Vec3 normal = vecMake3(0.0f, 0.0f, 1.0f);
    const Vec3 wo = vecNormalize3(vecMake3(0.2f, 0.1f, 1.0f));
    return BrdfContext{normal, wo, material, 1.0f, 550.0f};
}

void testTransmissiveSample_ThroughputNearOne()
{
    const MaterialGpu material = makeMaterial(1.0f);
    const BrdfContext ctx = makeContext(material);

    const PrincipledBrdf brdf{};
    const BrdfSampleResult sample = brdf.sample(ctx, 0.6f, 0.5f);
    expectTrue(sample.valid, "transmitted sample valid");
    expectTrue(sample.transmitted, "sample marked transmitted");

    const Vec3 bsdfValue = brdf.eval(ctx, sample.direction);
    const Vec3 throughput = brdfApplyThroughput(vecMake3(1.0f, 1.0f, 1.0f), ctx, sample, bsdfValue);
    expectNear(throughput.x, 1.0f, 0.15f, "transmitted throughput near unity");
}

void testTransmissiveSample_Reflected_ThroughputNearOne()
{
    const MaterialGpu material = makeMaterial(1.0f);
    const BrdfContext ctx = makeContext(material);

    const PrincipledBrdf brdf{};
    const BrdfSampleResult sample = brdf.sample(ctx, 0.99f, 0.5f);
    expectTrue(sample.valid, "reflected sample valid");
    expectTrue(!sample.transmitted, "sample marked reflected");

    const Vec3 bsdfValue = brdf.eval(ctx, sample.direction);
    const Vec3 throughput = brdfApplyThroughput(vecMake3(1.0f, 1.0f, 1.0f), ctx, sample, bsdfValue);
    expectNear(throughput.x, 1.0f, 0.15f, "reflected throughput near unity");
}

void testRefractedDirection_NonZero()
{
    const MaterialGpu material = makeMaterial(1.0f);
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

} // namespace

int main()
{
    testRefractedDirection_NonZero();
    testTransmissiveSample_ThroughputNearOne();
    testTransmissiveSample_Reflected_ThroughputNearOne();

    if (gFailures != 0) {
        std::cerr << gFailures << " test(s) failed.\n";
        return 1;
    }

    std::cout << "All glass BRDF tests passed.\n";
    return 0;
}
