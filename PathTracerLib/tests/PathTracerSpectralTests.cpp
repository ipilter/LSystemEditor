#include "Sampling/LightSamplingCore.h"
#include "Medium/VolumeCore.h"
#include "MeshAccel/MeshAccelTypes.h"
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

    const float sigmaAt550 = spectralGlassAbsorptionAtWavelength(0.1f, 0.05f, 0.02f, 550.0f);
    const float sigmaAt650 = spectralGlassAbsorptionAtWavelength(0.1f, 0.05f, 0.02f, 650.0f);
    expectTrue(
        std::fabs(sigmaAt550 - 0.1f) > 1.0e-4f || std::fabs(sigmaAt650 - 0.02f) > 1.0e-4f,
        "sigma(lambda) differs from raw channel at non-reference wavelengths");
}

void testGlassIorUsesAbbe()
{
    MaterialGpu glass{};
    glass.ior = 1.52f;
    glass.abbeNumber = 58.0f;
    const float iorRed = spectralGlassIor(glass, 650.0f);
    const float iorBlue = spectralGlassIor(glass, 450.0f);
    expectTrue(iorBlue > iorRed, "spectralGlassIor blue exceeds red with Abbe dispersion");
}

void testVolumeTransmittanceOrdering()
{
    const Vec3 sigmaA = vecMake3(0.5f, 0.1f, 0.02f);
    const float distance = 2.0f;
    const float sigmaA_red = mediumSigmaAAtWavelength(sigmaA, 650.0f);
    const float sigmaA_green = mediumSigmaAAtWavelength(sigmaA, 550.0f);
    const float sigmaA_blue = mediumSigmaAAtWavelength(sigmaA, 450.0f);
    const float tRed = mediumTransmittanceAtWavelength(sigmaA_red, distance);
    const float tGreen = mediumTransmittanceAtWavelength(sigmaA_green, distance);
    const float tBlue = mediumTransmittanceAtWavelength(sigmaA_blue, distance);
    expectTrue(tRed < tGreen && tGreen < tBlue, "leaf-like sigmaA: red absorbs most, blue least");
}

void testScatterAlbedoUsesSeparateCoeffs()
{
    const Vec3 sigmaA = vecMake3(0.4f, 0.1f, 0.05f);
    const Vec3 sigmaS = vecMake3(0.1f, 0.3f, 0.2f);
    const Vec3 sigmaT = vecMake3(
        sigmaA.x + sigmaS.x,
        sigmaA.y + sigmaS.y,
        sigmaA.z + sigmaS.z);
    const float lambdaNm = 550.0f;
    const float correct = mediumScatterAlbedoAtWavelength(sigmaA, sigmaS, lambdaNm);
    const float wrong = spectralGlassAbsorptionAtWavelength(sigmaS.x, sigmaS.y, sigmaS.z, lambdaNm)
        / vecMax2(
            spectralGlassAbsorptionAtWavelength(sigmaT.x, sigmaT.y, sigmaT.z, lambdaNm),
            1.0e-8f);
    expectTrue(std::fabs(correct - wrong) > 1.0e-4f, "scatter albedo uses sigmaA+sigmaS at lambda, not rgb2spec(sigmaT)");
}

void testEnvironmentSpectral()
{
    RenderParamsGpu params{};
    params.backgroundR = 0.2f;
    params.backgroundG = 0.6f;
    params.backgroundB = 0.1f;
    params.environmentIntensity = 2.0f;

    const float spectral = lightEvalEnvironmentSpectral(
        nullptr, &params, vecMake3(0.0f, 1.0f, 0.0f), 550.0f);
    expectTrue(spectral > 0.0f, "spectral environment radiance positive at hero wavelength");
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
    testGlassIorUsesAbbe();
    testVolumeTransmittanceOrdering();
    testScatterAlbedoUsesSeparateCoeffs();
    testEnvironmentSpectral();

    if (gFailures != 0) {
        std::cerr << gFailures << " test(s) failed.\n";
        return 1;
    }

    std::cout << "All spectral tests passed.\n";
    return 0;
}
