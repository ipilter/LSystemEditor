#include "CameraGpu.h"
#include "PathTracer.h"

#include <cuda_runtime.h>

#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions_4_5_Core>
#include <QSurfaceFormat>
#include <QString>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace {

int gFailures = 0;

void expectTrue(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++gFailures;
    }
}

void expectNearByte(unsigned char actual, unsigned char expected, const char* message)
{
    const int delta = static_cast<int>(actual) - static_cast<int>(expected);
    if (delta < -1 || delta > 1) {
        std::cerr << "FAIL: " << message << " (got " << static_cast<int>(actual)
                  << ", expected " << static_cast<int>(expected) << " +/- 1)\n";
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

struct OffscreenGlContext
{
    QOffscreenSurface surface;
    QOpenGLContext context;

    bool initialize(QString* outError)
    {
        QSurfaceFormat format;
        format.setVersion(4, 5);
        format.setProfile(QSurfaceFormat::CoreProfile);
        QSurfaceFormat::setDefaultFormat(format);

        surface.setFormat(format);
        surface.create();
        if (!surface.isValid()) {
            if (outError != nullptr) {
                *outError = QStringLiteral("offscreen surface creation failed");
            }
            return false;
        }

        context.setFormat(surface.format());
        if (!context.create()) {
            if (outError != nullptr) {
                *outError = QStringLiteral("OpenGL context creation failed");
            }
            return false;
        }

        if (!context.makeCurrent(&surface)) {
            if (outError != nullptr) {
                *outError = QStringLiteral("OpenGL context makeCurrent failed");
            }
            return false;
        }

        return true;
    }

    void release()
    {
        context.doneCurrent();
    }
};

struct PboPair
{
    GLuint ids[2] = {0, 0};

    bool create(QOpenGLFunctions_4_5_Core* gl, int width, int height)
    {
        if (gl == nullptr || width <= 0 || height <= 0) {
            return false;
        }

        const GLsizeiptr byteSize = static_cast<GLsizeiptr>(width) * static_cast<GLsizeiptr>(height) * 4;
        gl->glGenBuffers(2, ids);
        for (int i = 0; i < 2; ++i) {
            gl->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, ids[i]);
            gl->glBufferData(GL_PIXEL_UNPACK_BUFFER, byteSize, nullptr, GL_DYNAMIC_DRAW);
        }
        gl->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        return ids[0] != 0 && ids[1] != 0;
    }

    void destroy(QOpenGLFunctions_4_5_Core* gl)
    {
        if (gl == nullptr || (ids[0] == 0 && ids[1] == 0)) {
            return;
        }

        gl->glDeleteBuffers(2, ids);
        ids[0] = 0;
        ids[1] = 0;
    }
};

bool readPboPixel(QOpenGLFunctions_4_5_Core* gl, GLuint pbo, std::array<unsigned char, 4>& rgba)
{
    if (gl == nullptr || pbo == 0) {
        return false;
    }

    gl->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
    gl->glGetBufferSubData(GL_PIXEL_UNPACK_BUFFER, 0, 4, rgba.data());
    gl->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    return true;
}

bool waitForSamples(PathTracer& tracer, int minSamples, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (tracer.currentSampleCount() >= minSamples) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return tracer.currentSampleCount() >= minSamples;
}

bool hasCudaDevice()
{
    int deviceCount = 0;
    return cudaGetDeviceCount(&deviceCount) == cudaSuccess && deviceCount > 0;
}

void testEmptySceneCameraPositiveXEncodesRedInPbo()
{
    OffscreenGlContext glContext;
    QString glError;
    expectTrue(glContext.initialize(&glError), "offscreen OpenGL context should initialize");
    if (!glError.isEmpty() && gFailures > 0) {
        std::cerr << "  " << glError.toStdString() << '\n';
        return;
    }

    QOpenGLFunctions_4_5_Core gl;
    expectTrue(gl.initializeOpenGLFunctions(), "OpenGL 4.5 functions should initialize");
    if (gFailures > 0) {
        glContext.release();
        return;
    }

    constexpr int kWidth = 1;
    constexpr int kHeight = 1;

    PboPair pbos;
    expectTrue(pbos.create(&gl, kWidth, kHeight), "PBO pair should be created");
    if (gFailures > 0) {
        glContext.release();
        return;
    }

    PathTracer tracer;
    expectTrue(
        tracer.configure(kWidth, kHeight, pbos.ids[0], pbos.ids[1]),
        "PathTracer configure should succeed for 1x1 empty scene");
    if (gFailures > 0) {
        pbos.destroy(&gl);
        glContext.release();
        return;
    }

    tracer.setMaxSamplesPerPixel(1);
    tracer.setPreviewStepsPerLevel(0);
    tracer.setCamera(cameraLookingPositiveX());
    tracer.start();

    expectTrue(
        waitForSamples(tracer, 1, std::chrono::milliseconds(5000)),
        "PathTracer should accumulate at least one sample");
    expectTrue(tracer.publishDisplayFrame(0), "publishDisplayFrame should succeed");

    std::array<unsigned char, 4> rgba{};
    const bool pixelRead = readPboPixel(&gl, pbos.ids[0], rgba);
    expectTrue(pixelRead, "PBO pixel should be readable");
    if (pixelRead) {
        expectNearByte(rgba[0], 255, "red channel should encode +X (255)");
        expectNearByte(rgba[1], 127, "green channel should encode 0.0 direction (127)");
        expectNearByte(rgba[2], 127, "blue channel should encode 0.0 direction (127)");
        expectNearByte(rgba[3], 255, "alpha channel should be opaque (255)");
        expectTrue(rgba[0] > rgba[1] && rgba[0] > rgba[2], "pixel should be red-dominant");
    }

    tracer.stop();
    tracer.releaseOutputSurfaces();
    pbos.destroy(&gl);
    glContext.release();
}

} // namespace

int main(int argc, char* argv[])
{
    if (!hasCudaDevice()) {
        std::cout << "SKIP: no CUDA device available.\n";
        return EXIT_SUCCESS;
    }

    QGuiApplication app(argc, argv);

    testEmptySceneCameraPositiveXEncodesRedInPbo();

    if (gFailures == 0) {
        std::cout << "All PathTracer camera direction tests passed.\n";
        return EXIT_SUCCESS;
    }

    std::cerr << gFailures << " test(s) failed.\n";
    return EXIT_FAILURE;
}
