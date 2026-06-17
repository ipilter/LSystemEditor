#include "Sampling/LightSamplingCore.h"
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

void testWavelengthSamplingRange()
{
    float lambdaNm = 0.0f;
    float pdf = 0.0f;
    spectralSampleWavelength(0.0f, lambdaNm, pdf);
    expectNear(lambdaNm, SpectralDetail::kLambdaMin, 1.0e-4f, "u=0 maps to lambda min");
    spectralSampleWavelength(1.0f, lambdaNm, pdf);
    expectNear(lambdaNm, SpectralDetail::kLambdaMax, 1.0e-4f, "u=1 maps to lambda max");
    expectNear(pdf, 1.0f / SpectralDetail::kLambdaRange, 1.0e-6f, "uniform wavelength pdf");
}

void testGlassAbsorptionScalar()
{
    const float sigma = spectralGlassAbsorptionAtWavelength(0.1f, 0.05f, 0.02f, 589.3f);
    expectTrue(sigma > 0.0f, "absorption scalar positive for non-zero coeffs");
}

void testGlassIorDispersionOrdering()
{
    const float iorRed = spectralIorAtWavelength(1.52f, 58.0f, 650.0f);
    const float iorBlue = spectralIorAtWavelength(1.52f, 58.0f, 450.0f);
    expectTrue(iorBlue > iorRed, "blue IOR exceeds red IOR for normal dispersion");
}

void testEnvironmentUsesDirectRgb()
{
    RenderParamsGpu params{};
    params.backgroundR = 0.2f;
    params.backgroundG = 0.6f;
    params.backgroundB = 0.1f;
    params.environmentIntensity = 2.0f;

    const Vec3 rgb = lightEvalEnvironmentOrBackground(nullptr, &params, vecMake3(0.0f, 1.0f, 0.0f));
    expectNear(rgb.x, 0.4f, 1.0e-4f, "background R uses direct RGB");
    expectNear(rgb.y, 1.2f, 1.0e-4f, "background G uses direct RGB");
    expectNear(rgb.z, 0.2f, 1.0e-4f, "background B uses direct RGB");
}

} // namespace

int main()
{
    std::string initError;
    if (!spectralInitHostFromCoeffFile(PATHTRACER_RGB2SPEC_COEFF_PATH, &initError)) {
        std::cerr << "Failed to load rgb2spec coefficients: " << initError << '\n';
        return 1;
    }

    testWavelengthSamplingRange();
    testGlassAbsorptionScalar();
    testGlassIorDispersionOrdering();
    testEnvironmentUsesDirectRgb();

    if (gFailures != 0) {
        std::cerr << gFailures << " test(s) failed.\n";
        return 1;
    }

    std::cout << "All spectral tests passed.\n";
    return 0;
}
