#include "MeshAccel/MeshAccelTypes.h"
#include "Sampling/LightSamplingCore.h"
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

void testRenderParamsDefaults()
{
    const RenderParamsGpu params{};
    expectTrue(params.maxPathDepth == 8, "default max path depth");
    expectTrue(params.sunIntensity == 1.0f, "default sun intensity");
}

void testSunPdfIntegrates()
{
    RenderParamsGpu params{};
    params.sunDiskSizeDeg = 1.0f;

    const float angularRadius = params.sunDiskSizeDeg * 0.5f * 3.14159265f / 180.0f;
    const float cosThetaMax = std::cos(angularRadius);
    const float capSolidAngle = 2.0f * 3.14159265f * (1.0f - cosThetaMax);

    std::mt19937 rng(3);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    double integral = 0.0;
    constexpr int sampleCount = 4096;
    for (int i = 0; i < sampleCount; ++i) {
        float pdf = 0.0f;
        const Vec3 wi = lightSampleSunDirection(&params, dist(rng), dist(rng), pdf);
        const float evaluatedPdf = lightPdfSunDirection(&params, wi);
        expectNear(evaluatedPdf, pdf, 1.0e-5f, "sun sample and eval pdf match");
        if (evaluatedPdf > 0.0f) {
            integral += static_cast<double>(evaluatedPdf);
        }
    }

    integral = integral * static_cast<double>(capSolidAngle) / static_cast<double>(sampleCount);
    expectNear(static_cast<float>(integral), 1.0f, 0.05f, "sun solid-angle pdf integrates to 1");
}

void testMisCombinesToOne()
{
    const float pdfLight = 0.4f;
    const float pdfBsdf = 0.6f;
    const float wLight = misBalanceWeight(pdfLight, pdfBsdf);
    const float wBsdf = misBalanceWeight(pdfBsdf, pdfLight);
    expectNear(wLight + wBsdf, 1.0f, 1.0e-5f, "mis weights sum to 1");
}

} // namespace

int main()
{
    testRenderParamsDefaults();
    testSunPdfIntegrates();
    testMisCombinesToOne();

    if (gFailures != 0) {
        std::cerr << gFailures << " test failure(s)\n";
        return EXIT_FAILURE;
    }

    std::cout << "All PathIntegrator tests passed\n";
    return EXIT_SUCCESS;
}
