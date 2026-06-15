#include "CameraGpu.h"
#include "PathTracer.h"
#include "PhysicalCamera.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

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

CameraGpu cameraLookingPositiveX()
{
    const glm::quat orientation = glm::angleAxis(-glm::half_pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));

    CameraGpu camera{};
    camera.position = make_float3(0.0f, 0.0f, 0.0f);
    camera.orientation = make_float4(orientation.w, orientation.x, orientation.y, orientation.z);
    camera.fovY = 1.04719755f;
    camera.aspect = 1.0f;
    camera.nearPlane = 0.1f;
    camera.farPlane = 1000.0f;
    return camera;
}

glm::vec3 forwardFromGpuCamera(const CameraGpu& camera)
{
    PhysicalCamera physical{};
    physical.applyGpuGeometry(camera);
    return physical.forward();
}

void testCameraLookingPositiveXForwardAlongPositiveX()
{
    const CameraGpu camera = cameraLookingPositiveX();
    const glm::vec3 forward = forwardFromGpuCamera(camera);

    expectNear(forward.x, 1.0f, 1.0e-4f, "camera forward X should be +1");
    expectNear(forward.y, 0.0f, 1.0e-4f, "camera forward Y should be 0");
    expectNear(forward.z, 0.0f, 1.0e-4f, "camera forward Z should be 0");
}

void testPathTracerLastSampleCameraMatchesSetCamera()
{
    const CameraGpu camera = cameraLookingPositiveX();

    PathTracer tracer;
    tracer.setCamera(camera);

    const CameraGpu stored = tracer.lastSampleCamera();
    const glm::vec3 expectedForward = forwardFromGpuCamera(camera);
    const glm::vec3 storedForward = forwardFromGpuCamera(stored);

    expectNear(storedForward.x, expectedForward.x, 1.0e-4f, "stored camera forward X");
    expectNear(storedForward.y, expectedForward.y, 1.0e-4f, "stored camera forward Y");
    expectNear(storedForward.z, expectedForward.z, 1.0e-4f, "stored camera forward Z");
    expectNear(stored.position.x, camera.position.x, 1.0e-6f, "stored camera position X");
    expectNear(stored.position.y, camera.position.y, 1.0e-6f, "stored camera position Y");
    expectNear(stored.position.z, camera.position.z, 1.0e-6f, "stored camera position Z");
}

} // namespace

int main()
{
    testCameraLookingPositiveXForwardAlongPositiveX();
    testPathTracerLastSampleCameraMatchesSetCamera();

    if (gFailures == 0) {
        std::cout << "All PathTracer camera direction tests passed.\n";
        return EXIT_SUCCESS;
    }

    std::cerr << gFailures << " test(s) failed.\n";
    return EXIT_FAILURE;
}
