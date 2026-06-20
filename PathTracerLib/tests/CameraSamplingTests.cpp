#include "PhysicalCamera.h"

#include <glm/glm.hpp>

#include <cmath>
#include <cstdlib>
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

void testDefaultFocusPointOneMeterAhead()
{
    PhysicalCamera camera{};
    expectTrue(camera.focusValid(), "default camera should have valid focus");
    const glm::vec3 expected = camera.position() + camera.forward() * PhysicalCamera::kDefaultFocusDistance;
    expectNear(camera.focusPoint().x, expected.x, 1.0e-2f, "default focus X");
    expectNear(camera.focusPoint().y, expected.y, 1.0e-2f, "default focus Y");
    expectNear(camera.focusPoint().z, expected.z, 1.0e-2f, "default focus Z");
    expectNear(camera.computeFocusDistance(), PhysicalCamera::kDefaultFocusDistance, 1.0e-2f, "default focus distance");
}

void testPinholeWhenFocusCleared()
{
    PhysicalCamera camera{};
    camera.clearFocusPoint();

    const CameraGpu gpu = camera.toGpu();
    expectTrue(gpu.focusValid == 0, "cleared focus should disable DoF");
    expectNear(gpu.apertureRadius, 0.0f, 1.0e-6f, "cleared focus should have zero aperture");
}

void testApertureRadiusScalesWithFStop()
{
    PhysicalCamera wide{};
    wide.fStop = 1.4f;
    wide.setDefaultFocusPoint();

    PhysicalCamera narrow = wide;
    narrow.fStop = 11.0f;

    expectTrue(wide.apertureRadius() > narrow.apertureRadius(), "lower f-number should have wider aperture");
}

void testApertureRadiusScalesWithFocalLength()
{
    PhysicalCamera wide{};
    wide.setFocalLengthMm(50.0f);
    wide.fStop = 2.8f;
    wide.setDefaultFocusPoint();

    PhysicalCamera tele = wide;
    tele.setFocalLengthMm(200.0f);

    expectTrue(tele.apertureRadius() > wide.apertureRadius(), "longer focal length should have wider pupil at same f-stop");
}

void testPupilRadiusAt24mmF28()
{
    PhysicalCamera camera{};
    camera.setFocalLengthMm(24.0f);
    camera.fStop = 2.8f;
    camera.setDefaultFocusPoint();

    const float expectedRadius = 24.0f / (2.0f * 2.8f);
    expectNear(camera.apertureRadius(), expectedRadius, 1.0e-4f, "24 mm f/2.8 pupil radius in world mm");
}

void testFilmDistanceMatchesFocalLength()
{
    PhysicalCamera camera{};
    camera.setFocalLengthMm(50.0f);
    camera.setDefaultFocusPoint();

    const CameraGpu gpu = camera.toGpu();
    expectNear(gpu.nearPlane, 50.0f, 1.0e-4f, "film distance equals focal length in world mm");
}

void testFocalLengthRoundTrip()
{
    PhysicalCamera camera{};
    camera.setFocalLengthMm(50.0f);
    expectNear(camera.focalLengthMm(), 50.0f, 1.0e-4f, "focal length round-trip");
}

void testFocalLengthFovConversion()
{
    const float fovY = PhysicalCamera::focalLengthMmToFovY(24.0f);
    expectNear(glm::degrees(fovY), 52.94f, 0.2f, "24 mm vertical FOV on Z7 sensor");
}

void testFocalLengthClamping()
{
    PhysicalCamera camera{};
    camera.setFocalLengthMm(5.0f);
    expectNear(camera.focalLengthMm(), PhysicalCamera::kMinFocalLengthMm, 1.0e-4f, "clamp min focal length");

    camera.setFocalLengthMm(2000.0f);
    expectNear(camera.focalLengthMm(), PhysicalCamera::kMaxFocalLengthMm, 1.0e-4f, "clamp max focal length");
}

void testPrimaryRayCenterMatchesForward()
{
    PhysicalCamera camera{};
    camera.setFocusPoint(camera.position() + camera.forward() * 2000.0f);

    glm::vec3 ro{};
    glm::vec3 rd{};
    camera.primaryRay(0.5f, 0.5f, ro, rd);

    expectNear(ro.x, camera.position().x, 1.0e-3f, "center ray origin X");
    expectNear(ro.y, camera.position().y, 1.0e-3f, "center ray origin Y");
    expectNear(ro.z, camera.position().z, 1.0e-3f, "center ray origin Z");
    expectNear(rd.x, camera.forward().x, 1.0e-4f, "center ray direction X");
    expectNear(rd.y, camera.forward().y, 1.0e-4f, "center ray direction Y");
    expectNear(rd.z, camera.forward().z, 1.0e-4f, "center ray direction Z");
}

void testIndependentPixelJitterOffsets()
{
    const float jitterU = 0.25f;
    const float jitterV = 0.75f;
    const int width = 100;
    const int height = 50;
    const int x = 10;
    const int y = 20;

    const float u = (static_cast<float>(x) + jitterU) / static_cast<float>(width);
    const float v = (static_cast<float>(y) + jitterV) / static_cast<float>(height);

    expectTrue(std::fabs(u - v) > 1.0e-4f, "independent jitter should produce different u and v");
    expectNear(u, 0.1025f, 1.0e-6f, "jittered u");
    expectNear(v, 0.415f, 1.0e-6f, "jittered v");
}

void testToGpuIncludesDofFieldsWhenFocusValid()
{
    PhysicalCamera camera{};
    camera.fStop = 2.8f;
    camera.setDefaultFocusPoint();

    const CameraGpu gpu = camera.toGpu();
    expectTrue(gpu.focusValid != 0, "gpu camera should mark focus valid");
    expectTrue(gpu.apertureRadius > 0.0f, "gpu camera should have positive aperture radius");
    expectTrue(gpu.focusDistance > 0.0f, "gpu camera should have positive focus distance");
    expectNear(gpu.focusDistance, camera.computeFocusDistance(), 1.0e-3f, "gpu focus distance matches host");
}

void testFocusDistanceAlongForwardAxis()
{
    PhysicalCamera camera{};
    const glm::vec3 focus = camera.position() + camera.forward() * 3500.0f + camera.right() * 250.0f;
    camera.setFocusPoint(focus);

    expectNear(camera.computeFocusDistance(), 3500.0f, 1.0e-2f, "focus distance projects onto forward axis");
}

void testFocusDistanceUnchangedAfterRotation()
{
    PhysicalCamera camera{};
    camera.setDefaultFocusPoint();
    const float initialDistance = camera.computeFocusDistance();

    camera.addEulerDelta(glm::radians(90.0f), glm::radians(45.0f), 0.0f);

    expectNear(camera.computeFocusDistance(), initialDistance, 1.0e-2f, "focus distance after rotation");
    const CameraGpu gpu = camera.toGpu();
    expectNear(gpu.focusDistance, initialDistance, 1.0e-2f, "gpu focus distance after rotation");
}

void testRefreshFocusDistanceFromPointAfterTranslation()
{
    PhysicalCamera camera{};
    const glm::vec3 pinnedPoint = camera.position() + camera.forward() * 2500.0f;
    camera.setFocusPoint(pinnedPoint);
    expectNear(camera.computeFocusDistance(), 2500.0f, 1.0e-2f, "initial pinned focus distance");

    camera.moveForward(500.0f);
    camera.refreshFocusDistanceFromPoint();

    expectNear(camera.computeFocusDistance(), 2000.0f, 1.0e-2f, "pinned focus distance after forward move");
    expectNear(camera.focusPoint().x, pinnedPoint.x, 1.0e-2f, "pinned focus point X after move");
}

void testRefreshFocusDistanceFromPointAfterRotation()
{
    PhysicalCamera camera{};
    const glm::vec3 pinnedPoint = camera.position() + camera.forward() * 1800.0f + camera.right() * 400.0f;
    camera.setFocusPoint(pinnedPoint);
    expectNear(camera.computeFocusDistance(), 1800.0f, 1.0e-2f, "initial projected focus distance");

    camera.addEulerDelta(glm::radians(20.0f), glm::radians(-10.0f), 0.0f);
    camera.refreshFocusDistanceFromPoint();

    const glm::vec3 delta = pinnedPoint - camera.position();
    const float expectedDistance = glm::dot(delta, camera.forward());
    expectNear(camera.computeFocusDistance(), expectedDistance, 1.0e-2f, "refreshed focus distance after rotation");
    expectNear(camera.focusPoint().x, pinnedPoint.x, 1.0e-2f, "pinned focus point unchanged after rotation");
}

} // namespace

int main()
{
    testDefaultFocusPointOneMeterAhead();
    testPinholeWhenFocusCleared();
    testApertureRadiusScalesWithFStop();
    testApertureRadiusScalesWithFocalLength();
    testPupilRadiusAt24mmF28();
    testFilmDistanceMatchesFocalLength();
    testFocalLengthRoundTrip();
    testFocalLengthFovConversion();
    testFocalLengthClamping();
    testPrimaryRayCenterMatchesForward();
    testIndependentPixelJitterOffsets();
    testToGpuIncludesDofFieldsWhenFocusValid();
    testFocusDistanceAlongForwardAxis();
    testFocusDistanceUnchangedAfterRotation();
    testRefreshFocusDistanceFromPointAfterTranslation();
    testRefreshFocusDistanceFromPointAfterRotation();

    if (gFailures == 0) {
        std::cout << "All camera sampling tests passed.\n";
        return EXIT_SUCCESS;
    }

    std::cerr << gFailures << " test(s) failed.\n";
    return EXIT_FAILURE;
}
