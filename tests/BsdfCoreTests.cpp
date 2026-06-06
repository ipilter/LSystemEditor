#include "Sampling/BsdfCore.h"
#include "Sampling/MisCore.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <random>

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
    if (std::abs(actual - expected) > tolerance) {
        std::cerr << "FAIL: " << message << " (actual=" << actual << " expected=" << expected << ")\n";
        ++gFailures;
    }
}

MaterialGpu makeWhiteDiffuse()
{
    MaterialGpu material{};
    material.r = 1.0f;
    material.g = 1.0f;
    material.b = 1.0f;
    material.roughness = 1.0f;
    material.metallic = 0.0f;
    material.transmission = 0.0f;
    return material;
}

void testDiffuseReciprocity()
{
    const MaterialGpu material = makeWhiteDiffuse();
    const Vec3 normal = vecMake3(0.0f, 1.0f, 0.0f);
    const Vec3 wi = vecNormalize3(vecMake3(0.3f, 0.8f, 0.2f));
    const Vec3 wo = vecNormalize3(vecMake3(-0.4f, 0.7f, 0.1f));

    const Vec3 fwd = bsdfEval(normal, wi, wo, material);
    const Vec3 rev = bsdfEval(normal, wo, wi, material);
    expectNear(fwd.x, rev.x, 1.0e-5f, "diffuse reciprocity x");
    expectNear(fwd.y, rev.y, 1.0e-5f, "diffuse reciprocity y");
    expectNear(fwd.z, rev.z, 1.0e-5f, "diffuse reciprocity z");
}

void testDiffuseEnergy()
{
    const MaterialGpu material = makeWhiteDiffuse();
    const Vec3 normal = vecMake3(0.0f, 1.0f, 0.0f);
    const Vec3 wo = vecMake3(0.0f, 1.0f, 0.0f);

    std::mt19937 rng(17);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    double energy = 0.0;
    constexpr int sampleCount = 4096;
    for (int i = 0; i < sampleCount; ++i) {
        const float u1 = dist(rng);
        const float u2 = dist(rng);
        const Vec3 local = bsdfSampleCosineHemisphere(u1, u2);
        Vec3 tangent{};
        Vec3 bitangent{};
        bsdfBuildBasis(normal, tangent, bitangent);
        const Vec3 wi = bsdfLocalToWorld(local, normal, tangent, bitangent);
        const Vec3 value = bsdfEval(normal, wi, wo, material);
        const float pdf = bsdfPdfDiffuse(normal, wi);
        const float cosTheta = vecMax2(0.0f, vecDot3(normal, wi));
        energy += static_cast<double>(value.x) * cosTheta / static_cast<double>(pdf);
    }

    energy /= static_cast<double>(sampleCount);
    expectNear(static_cast<float>(energy), 1.0f, 0.08f, "white diffuse energy conservation");
}

void testMisWeights()
{
    expectNear(misBalanceWeight(0.5f, 0.5f), 0.5f, 1.0e-5f, "mis equal weights");
    expectNear(misBalanceWeight(1.0f, 0.0f), 1.0f, 1.0e-5f, "mis single strategy");
    expectTrue(misBalanceWeight(0.0f, 1.0f) == 0.0f, "mis zero pdfA");
}

} // namespace

int main()
{
    testDiffuseReciprocity();
    testDiffuseEnergy();
    testMisWeights();

    if (gFailures != 0) {
        std::cerr << gFailures << " test failure(s)\n";
        return EXIT_FAILURE;
    }

    std::cout << "All BsdfCore tests passed\n";
    return EXIT_SUCCESS;
}
