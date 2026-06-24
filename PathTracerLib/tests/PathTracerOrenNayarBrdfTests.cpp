#include "Brdf/BrdfDispatch.h"
#include "Brdf/BrdfBase.h"
#include "Brdf/BrdfLobe.h"
#include "Brdf/OrenNayarDiffuseBrdf.h"
#include "Material/MaterialType.h"
#include "MeshAccel/MeshAccelTypes.h"
#include "Path/PathState.h"

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
    return BrdfContext{normal, wo, material, 1.0f, 550.0f, 0};
}

MaterialGpu makeMaterial(
    float r,
    float g,
    float b,
    float roughness = 0.5f,
    float diffuseRoughness = -1.0f)
{
    MaterialGpu material{};
    material.r = r;
    material.g = g;
    material.b = b;
    material.roughness = roughness;
    material.diffuseRoughness = diffuseRoughness;
    material.ior = 1.5f;
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
    const MaterialGpu material = makeMaterial(1.0f, 1.0f, 1.0f, 0.2f);
    const BrdfLobeWeights weights = computeSurfaceLobeWeights(material);
    expectNear(weights.diffuse, 1.0f, 0.01f, "v1 uses diffuse-only lobe");
    expectNear(weights.specular, 0.0f, 0.01f, "specular lobe stubbed off");
}

void testOrenNayar_ZeroRoughnessMatchesLambert()
{
    const MaterialGpu material = makeMaterial(0.8f, 0.8f, 0.8f, 0.0f, 0.0f);
    const BrdfContext ctx = makeContext(material);
    const Vec3 wi = vecNormalize3(vecMake3(0.3f, 0.2f, 1.0f));
    const float factor = OrenNayarDetail::diffuseOrenNayarFactor(ctx, wi);
    expectNear(factor, 1.0f, 0.05f, "zero diffuse roughness approximates Lambert factor");
}

void testBrdfEvalPositive()
{
    const MaterialGpu material = makeMaterial(0.72f, 0.68f, 0.58f, 0.5f);
    const BrdfContext ctx = makeContext(material);
    const Vec3 wi = vecNormalize3(vecMake3(0.3f, 0.2f, 1.0f));
    const Vec3 value = brdfEval(ctx, wi);
    expectTrue(value.x > 0.0f && value.y > 0.0f && value.z > 0.0f, "RGB BRDF is positive");
}

void testPathStateDefaults()
{
    PathState path{};
    expectNear(path.throughput.x, 1.0f, 0.01f, "default throughput");
}

} // namespace

int main()
{
    testDiffuseSample();
    testDiffuseThroughput();
    testSurfaceLobeWeights();
    testOrenNayar_ZeroRoughnessMatchesLambert();
    testBrdfEvalPositive();
    testPathStateDefaults();

    if (gFailures == 0) {
        std::cout << "All Oren-Nayar BRDF tests passed.\n";
        return 0;
    }

    std::cerr << gFailures << " test(s) failed.\n";
    return 1;
}
