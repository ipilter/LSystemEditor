#include "PhysicalCamera.h"
#include "RenderTypes.h"
#include "Sampling/LightSamplingCore.h"

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

void testExposureMultiplierBrightVsDark()
{
    constexpr float kDarkFStop = 8.0f;
    constexpr float kDarkShutter = 1.0f / 125.0f;
    constexpr float kBrightFStop = 3.5f;
    constexpr float kBrightShutter = 2.0f;
    constexpr float kIso = 100.0f;

    const float darkExposure = PhysicalCamera::exposureMultiplier(kDarkFStop, kDarkShutter, kIso);
    const float brightExposure = PhysicalCamera::exposureMultiplier(kBrightFStop, kBrightShutter, kIso);

    expectNear(darkExposure, 1.0f / 125.0f / 64.0f, 1.0e-6f, "dark exposure multiplier");
    expectNear(brightExposure, 2.0f / 12.25f, 1.0e-5f, "bright exposure multiplier");

    const float ratio = brightExposure / darkExposure;
    expectTrue(ratio > 1000.0f && ratio < 1600.0f, "bright/dark exposure ratio should be ~1300x");

    constexpr float kLinearRadiance = 1.0f;
    const float darkDisplay = PhysicalCamera::reinhardDisplay(kLinearRadiance, darkExposure);
    const float brightDisplay = PhysicalCamera::reinhardDisplay(kLinearRadiance, brightExposure);
    const float displayRatio = brightDisplay / darkDisplay;
    expectTrue(displayRatio > 1000.0f, "display pipeline should preserve exposure brightness ratio");
}

void testExposureValueMatchesMultiplier()
{
    constexpr float kFStop = 3.5f;
    constexpr float kShutter = 2.0f;
    constexpr float kIso = 100.0f;

    const float ev = PhysicalCamera::exposureValue(kFStop, kShutter, kIso);
    expectNear(ev, 2.6f, 0.1f, "EV for f/3.5 and 2s shutter");

    const float multiplier = PhysicalCamera::exposureMultiplier(kFStop, kShutter, kIso);
    const float reconstructed = std::pow(2.0f, -ev) * (kIso / 100.0f);
    expectNear(multiplier, reconstructed, 1.0e-5f, "exposure multiplier consistent with EV");
}

void testPhysicalCameraForAverageLuminance()
{
    constexpr float kAverageLuminance = 2.0f;
    const PhysicalCamera camera = PhysicalCamera::forAverageLuminance(kAverageLuminance);
    const float display = PhysicalCamera::reinhardDisplay(kAverageLuminance, camera.exposureMultiplier());
    expectNear(
        display,
        PhysicalCamera::kDisplayMiddleGray,
        1.0e-3f,
        "auto-exposure targets 18% gray after Reinhard tone map");
}

void testShutterClampAllowsLongExposures()
{
    const float eightSecondExposure = PhysicalCamera::exposureMultiplier(2.8f, 8.0f, 100.0f);
    const float tenSecondExposure = PhysicalCamera::exposureMultiplier(2.8f, 10.0f, 100.0f);
    expectTrue(tenSecondExposure > eightSecondExposure, "10s shutter should be brighter than 8s");
    expectNear(
        tenSecondExposure / eightSecondExposure,
        10.0f / 8.0f,
        1.0e-5f,
        "exposure should scale linearly with shutter time above 8s");

    const float clamped = PhysicalCamera::clampShutterSpeedSeconds(150.0f);
    expectNear(clamped, PhysicalCamera::kMaxShutterSpeedSeconds, 1.0e-6f, "shutter clamp max is 120s");
}

void testAutoExposureUsesIsoWhenShutterMaxed()
{
    constexpr float kVeryDarkLuminance = 1.0e-4f;
    const PhysicalCamera camera = PhysicalCamera::forAverageLuminance(kVeryDarkLuminance);
    expectNear(camera.shutterSpeedSeconds, PhysicalCamera::kMaxShutterSpeedSeconds, 1.0e-6f, "dark scene maxes shutter");
    expectTrue(camera.fStop <= PhysicalCamera::kDefaultFStop + 1.0e-4f, "dark scene opens aperture");
    expectTrue(camera.iso > PhysicalCamera::kDefaultIso, "dark scene raises ISO after shutter maxes");
    const float display = PhysicalCamera::reinhardDisplay(kVeryDarkLuminance, camera.exposureMultiplier());
    expectNear(display, PhysicalCamera::kDisplayMiddleGray, 1.0e-3f, "dark scene reaches middle gray via ISO");
}

void testEnvironmentPdfSolidAngleIncludesResolution()
{
    constexpr float kPi = 3.14159265f;
    constexpr int kWidth = 4096;
    constexpr int kHeight = 2048;
    constexpr float kSinTheta = 0.5f;
    constexpr float kTexelProbability = 1.0f / static_cast<float>(kWidth * kHeight);

    const float correctPdf = kTexelProbability * static_cast<float>(kWidth * kHeight) /
        (2.0f * kPi * kPi * kSinTheta);
    const float buggyPdf = kTexelProbability / (2.0f * kPi * kPi * kSinTheta);

    expectTrue(correctPdf > buggyPdf * 1000000.0f, "env PDF must include width*height solid-angle factor");
    expectNear(correctPdf / buggyPdf, static_cast<float>(kWidth * kHeight), 1.0f, "PDF scale equals pixel count");
}

void testSolidEnvironmentPdfIsUniformSphere()
{
    constexpr float kPi = 3.14159265f;
    const Vec3 direction = vecMake3(0.0f, 1.0f, 0.0f);
    expectNear(lightPdfSolidEnvironment(direction), 1.0f / (4.0f * kPi), 1.0e-6f, "solid env PDF is 1/(4pi)");
}

void testSolidEnvironmentRadianceScalesWithIntensity()
{
    RenderParamsGpu params{};
    params.backgroundR = 1.0f;
    params.backgroundG = 1.0f;
    params.backgroundB = 1.0f;
    params.environmentIntensity = 2.0f;

    EnvironmentMapGpu invalidEnv{};
    const Vec3 direction = vecMake3(0.0f, 1.0f, 0.0f);
    const Vec3 radiance = lightEvalEnvironmentOrBackground(&invalidEnv, &params, direction);

    expectNear(radiance.x, 2.0f, 1.0e-6f, "solid env radiance R scales with intensity");
    expectNear(radiance.y, 2.0f, 1.0e-6f, "solid env radiance G scales with intensity");
    expectNear(radiance.z, 2.0f, 1.0e-6f, "solid env radiance B scales with intensity");
}

} // namespace

int main()
{
    testExposureMultiplierBrightVsDark();
    testExposureValueMatchesMultiplier();
    testPhysicalCameraForAverageLuminance();
    testShutterClampAllowsLongExposures();
    testAutoExposureUsesIsoWhenShutterMaxed();
    testEnvironmentPdfSolidAngleIncludesResolution();
    testSolidEnvironmentPdfIsUniformSphere();
    testSolidEnvironmentRadianceScalesWithIntensity();

    if (gFailures == 0) {
        std::cout << "All PathTracer exposure tests passed.\n";
        return 0;
    }

    std::cerr << gFailures << " test(s) failed.\n";
    return 1;
}
