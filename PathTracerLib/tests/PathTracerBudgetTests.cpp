#include "PathTracerSampleBudget.h"

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

void testDefaultBudgetStopsAt1025()
{
    constexpr int previewSteps = 1;
    constexpr int maxSamples = 1024;
    const int total = sampleBudgetTotalIterations(previewSteps, maxSamples);
    expectTrue(total == 1025, "default budget total should be preview + max (1025)");

    expectTrue(canTakeSampleAtIteration(1024, previewSteps, maxSamples), "iteration 1024 should continue");
    expectTrue(!canTakeSampleAtIteration(1025, previewSteps, maxSamples), "iteration 1025 should stop");
    expectTrue(isSampleBudgetExhausted(1025, previewSteps, maxSamples), "iteration 1025 exhausted");
    expectTrue(!isSampleBudgetExhausted(1024, previewSteps, maxSamples), "iteration 1024 not exhausted");
}

void testUnlimitedSamples()
{
    expectTrue(sampleBudgetTotalIterations(1, 0) == -1, "unlimited returns -1 total");
    expectTrue(canTakeSampleAtIteration(1'000'000, 1, 0), "unlimited never stops");
    expectTrue(!isSampleBudgetExhausted(1'000'000, 1, 0), "unlimited never exhausted");
}

void testPreviewOnlyBudget()
{
    constexpr int previewSteps = 4;
    constexpr int maxSamples = 256;
    expectTrue(sampleBudgetTotalIterations(previewSteps, maxSamples) == 260, "preview adds to budget");

    expectTrue(canTakeSampleAtIteration(259, previewSteps, maxSamples), "259 still accumulating");
    expectTrue(!canTakeSampleAtIteration(260, previewSteps, maxSamples), "260 stops");
}

void testIncreasingMaxResumes()
{
    constexpr int previewSteps = 1;
    const int sampleCount = 1025;
    expectTrue(isSampleBudgetExhausted(sampleCount, previewSteps, 1024), "exhausted at old max");
    expectTrue(
        canTakeSampleAtIteration(sampleCount, previewSteps, 4096),
        "raising max above current count resumes accumulation");
}

void testAdaptiveBudgetExhaustedWhenNoActivePixels()
{
    constexpr int previewSteps = 1;
    expectTrue(!isAdaptiveSampleBudgetExhausted(100, previewSteps, 0), "preview phase not exhausted");
    expectTrue(!isAdaptiveSampleBudgetExhausted(100, previewSteps, 1), "preview phase not exhausted at iter 1");
    expectTrue(isAdaptiveSampleBudgetExhausted(0, previewSteps, 10), "full-res exhausted when active count is 0");
    expectTrue(!isAdaptiveSampleBudgetExhausted(5, previewSteps, 10), "full-res not exhausted with active pixels");
}

} // namespace

int main()
{
    testDefaultBudgetStopsAt1025();
    testUnlimitedSamples();
    testPreviewOnlyBudget();
    testIncreasingMaxResumes();
    testAdaptiveBudgetExhaustedWhenNoActivePixels();

    if (gFailures == 0) {
        std::cout << "All PathTracer budget tests passed.\n";
        return 0;
    }

    std::cerr << gFailures << " test(s) failed.\n";
    return 1;
}
