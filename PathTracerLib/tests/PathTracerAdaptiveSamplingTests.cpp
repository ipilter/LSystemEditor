#include "PathTracerAdaptiveSampling.h"

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

void testPixelConvergenceThreshold()
{
    expectTrue(
        isAdaptivePixelConverged(16, 1.0f, 0.0001f, 16, 0.02f),
        "low variance should converge at min samples");
    expectTrue(
        !isAdaptivePixelConverged(16, 1.0f, 0.25f, 16, 0.02f),
        "high variance should not converge");
    expectTrue(
        !isAdaptivePixelConverged(8, 1.0f, 0.0001f, 16, 0.02f),
        "should not converge before min samples");
}

void testSampleLuminance()
{
    expectTrue(sampleLuminance(1.0f, 0.0f, 0.0f) > 0.2f, "red contributes luminance");
    expectTrue(sampleLuminance(0.0f, 1.0f, 0.0f) > sampleLuminance(1.0f, 0.0f, 0.0f), "green weighted higher");
}

void testAdaptiveBudgetExhausted()
{
    expectTrue(isAdaptiveBudgetExhausted(0), "zero active pixels exhausted");
    expectTrue(!isAdaptiveBudgetExhausted(1), "one active pixel not exhausted");
}

void testDarkPixelDoesNotConvergeEarly()
{
    expectTrue(
        !isAdaptivePixelConverged(32, 0.0f, 0.0f, 32, 0.005f),
        "black pixel with zero variance should not converge");
    expectTrue(
        !isAdaptivePixelConverged(64, 1.0e-6f, 1.0e-8f, 32, 0.02f),
        "near-black pixel should not converge");
}

} // namespace

int main()
{
    testPixelConvergenceThreshold();
    testSampleLuminance();
    testAdaptiveBudgetExhausted();
    testDarkPixelDoesNotConvergeEarly();

    if (gFailures == 0) {
        std::cout << "All PathTracer adaptive sampling tests passed.\n";
        return 0;
    }

    std::cerr << gFailures << " test(s) failed.\n";
    return 1;
}
