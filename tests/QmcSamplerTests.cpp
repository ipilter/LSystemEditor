#include "NloptSobolReference.h"
#include "QmcSamplerCore.h"
#include "SobolTables.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {

int gFailures = 0;

void expectTrue(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++gFailures;
    }
}

void expectNear(double actual, double expected, double tolerance, const char* message)
{
    if (std::abs(actual - expected) > tolerance) {
        std::cerr << "FAIL: " << message << " (actual=" << actual << " expected=" << expected << ")\n";
        ++gFailures;
    }
}

std::vector<uint32_t> buildTestMatrices()
{
    std::vector<uint32_t> matrices(
        static_cast<std::size_t>(kMaxSobolDimensions) * static_cast<std::size_t>(kSobolBits),
        0u);
    if (!buildSobolMatricesHost(matrices.data(), kMaxSobolDimensions)) {
        std::cerr << "FAIL: buildSobolMatricesHost returned false\n";
        ++gFailures;
    }
    return matrices;
}

void testSobolDim0KnownValues(const std::vector<uint32_t>& matrices)
{
    const struct
    {
        int index;
        float expected;
    } kCases[] = {
        {1, 0.5f},
        {2, 0.25f},
        {3, 0.75f},
        {4, 0.125f},
    };

    for (const auto& testCase : kCases) {
        SampleContext ctx{};
        ctx.sampleIndex = testCase.index;
        ctx.dimension = 0;
        ctx.scramble = 0;
        const float value = qmcNext1D(ctx, matrices.data(), kMaxSobolDimensions);
        expectNear(value, testCase.expected, 1.0e-7, "SobolDim0KnownValues");
    }
}

void testSobolTablesMatchNlopt(const std::vector<uint32_t>& matrices)
{
    NloptSobolReference::SobolData reference;
    if (!NloptSobolReference::init(reference, 3u)) {
        std::cerr << "FAIL: SobolTablesMatchNlopt nlopt init failed\n";
        ++gFailures;
        return;
    }

    for (int dimension = 0; dimension < 3; ++dimension) {
        for (int bit = 0; bit < kSobolBits; ++bit) {
            const uint32_t ours =
                matrices[static_cast<std::size_t>(bit) * static_cast<std::size_t>(kMaxSobolDimensions) +
                         static_cast<std::size_t>(dimension)];
            const uint32_t expected = reference.m[bit][dimension];
            if (dimension == 0) {
                const uint32_t vanDerCorput = 1u << (31 - bit);
                if (ours != vanDerCorput) {
                    std::cerr << "FAIL: SobolTablesMatchNlopt dim=0 bit=" << bit << " ours=" << ours
                              << " expected=" << vanDerCorput << '\n';
                    ++gFailures;
                    return;
                }
                continue;
            }

            if (ours != expected) {
                std::cerr << "FAIL: SobolTablesMatchNlopt dim=" << dimension << " bit=" << bit
                          << " ours=" << ours << " expected=" << expected << '\n';
                ++gFailures;
                return;
            }
        }
    }
}

void testPerPixelMeanConverges(const std::vector<uint32_t>& matrices)
{
    constexpr int kWidth = 32;
    constexpr int kHeight = 32;
    constexpr int kSamples = 1024;
    constexpr unsigned int kGlobalSeed = 1u;

    std::vector<double> means;
    means.reserve(static_cast<std::size_t>(kWidth * kHeight));

    for (int pixelIndex = 0; pixelIndex < kWidth * kHeight; ++pixelIndex) {
        SampleContext ctx{};
        ctx.pixelIndex = pixelIndex;
        ctx.dimension = 0;
        ctx.scramble = hashPixelScramble(pixelIndex, kGlobalSeed);

        double sum = 0.0;
        for (int sample = 0; sample < kSamples; ++sample) {
            ctx.sampleIndex = sample;
            sum += qmcNext1D(ctx, matrices.data(), kMaxSobolDimensions);
        }

        const double mean = sum / static_cast<double>(kSamples);
        means.push_back(mean);
        expectTrue(mean >= 0.48 && mean <= 0.52, "PerPixelMeanConverges pixel in range");
    }

    double avg = 0.0;
    for (double mean : means) {
        avg += mean;
    }
    avg /= static_cast<double>(means.size());

    double variance = 0.0;
    for (double mean : means) {
        const double delta = mean - avg;
        variance += delta * delta;
    }
    const double stdev = std::sqrt(variance / static_cast<double>(means.size()));
    expectTrue(stdev < 0.01, "PerPixelMeanConverges global stdev");
}

double runningMeanErrorAtN(const std::vector<uint32_t>& matrices, int n, bool useQmc, std::mt19937& rng)
{
    SampleContext ctx{};
    ctx.pixelIndex = 0;
    ctx.dimension = 0;
    ctx.scramble = hashPixelScramble(0, 1u);

    double running = 0.0;
    for (int i = 0; i < n; ++i) {
        double sample = 0.0;
        if (useQmc) {
            ctx.sampleIndex = i;
            sample = qmcNext1D(ctx, matrices.data(), kMaxSobolDimensions);
        } else {
            std::uniform_real_distribution<double> dist(0.0, 1.0);
            sample = dist(rng);
        }
        running += sample;
    }

    return std::abs((running / static_cast<double>(n)) - 0.5);
}

void testQmcBeatsRandomVariance(const std::vector<uint32_t>& matrices)
{
    std::mt19937 rng(12345u);
    const int checkpoints[] = {64, 128, 256};

    for (int n : checkpoints) {
        const double qmcError = runningMeanErrorAtN(matrices, n, true, rng);
        const double randomError = runningMeanErrorAtN(matrices, n, false, rng);
        if (qmcError * 3.0 >= randomError) {
            std::cerr << "FAIL: QmcBeatsRandomVariance at N=" << n << " qmcError=" << qmcError
                      << " randomError=" << randomError << '\n';
            ++gFailures;
        }
    }
}

} // namespace

int main()
{
    const std::vector<uint32_t> matrices = buildTestMatrices();

    testSobolDim0KnownValues(matrices);
    testSobolTablesMatchNlopt(matrices);
    testPerPixelMeanConverges(matrices);
    testQmcBeatsRandomVariance(matrices);

    if (gFailures == 0) {
        std::cout << "All QMC sampler tests passed.\n";
        return EXIT_SUCCESS;
    }

    std::cerr << gFailures << " test failure(s).\n";
    return EXIT_FAILURE;
}
