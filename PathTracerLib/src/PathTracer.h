#pragma once

#include "CameraGpu.h"
#include "PhysicalCamera.h"
#include "RenderTypes.h"
#include "MeshAccel/MeshAccelBoundsMesh.h"
#include "MeshAccel/MeshSceneContent.h"
#include "Procedural/ProceduralTypes.h"

#include <QColor>
#include <QRect>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include <QString>

namespace PathTracerDetail {
struct PathTracerImpl;
}

/// CUDA path tracer. Writes RGBA8 frames into caller-owned OpenGL PBOs.
///
/// Threading: an internal std::thread runs sample accumulation. PBO publish (CUDA–GL interop)
/// must run on the thread with the active OpenGL context via publishDisplayFrame().
class PathTracer
{
public:
    using FrameReadyCallback = std::function<void()>;

    PathTracer();
    ~PathTracer();

    PathTracer(const PathTracer&) = delete;
    PathTracer& operator=(const PathTracer&) = delete;

    void setFrameReadyCallback(FrameReadyCallback callback);

    /// Requires an active OpenGL context on the calling thread (cudaGraphicsGLRegisterBuffer).
    /// pbo0 and pbo1 are OpenGL buffer object names owned by the caller.
    bool configure(
        int width,
        int height,
        uint32_t pbo0,
        uint32_t pbo1,
        const std::vector<ProceduralInstance>& proceduralInstances = {},
        const MeshSceneBuildParams& meshParams = {});

    void start();
    void stop();
    void resetAccumulation();
    bool isRunning() const;

    /// Stops rendering and unregisters CUDA resources for the configured PBOs.
    /// Call before deleting the OpenGL buffer objects on the host side.
    void releaseOutputSurfaces();

    /// Requires an active OpenGL context. Enqueues copy-to-PBO on the display stream (non-blocking).
    bool beginDisplayPublish(int slot);
    bool isDisplayPublishReady() const;
    bool hasDisplayPublishInFlight() const;
    /// Returns the PBO slot that finished publishing, or -1 if not ready.
    int finishDisplayPublish();

    /// Synchronous publish for one-off refresh (e.g. overlay toggle). Blocks until GPU copy completes.
    bool publishDisplayFrame(int slot);

    /// Full-resolution sample iterations only (0 = unlimited). Total kernel launches = preview + max.
    void setMaxSamplesPerPixel(int max);
    int maxSamplesPerPixel() const;

    void setMinSamples(int min);
    int minSamples() const;

    void setRelativeErrorThreshold(float threshold);
    float relativeErrorThreshold() const;

    void setDebugOverlayMode(int mode);
    int debugOverlayMode() const;

    /// 0 = disabled (no preview passes). N > 0 runs N dense low-res preview passes (1/2^N .. 1/2)
    /// with shallow path tracing before full-resolution accumulation.
    void setPreviewStepsPerLevel(int steps);
    int previewStepsPerLevel() const;

    int currentSampleCount() const;

    int currentActivePixelCount() const;

    int sampleBudgetTotalIterations() const;
    bool isSampleBudgetExhausted() const;

    void setCamera(const CameraGpu& camera);

    CameraGpu lastSampleCamera() const;

    void setClearColor(const QColor& color);

    void setPhysicalCamera(float fStop, float shutterSpeedSeconds, float iso);
    PhysicalCamera suggestedPhysicalCamera() const;

    bool computeSuggestedCameraFromAccumulator(PhysicalCamera* out) const;

    void setEnvironmentHdrPath(const QString& path);
    QString environmentHdrPath() const;

    void setEnvironmentIntensity(float intensity);
    float environmentIntensity() const;

    /// Minimum path depth before Russian roulette termination (0 = from first bounce).
    void setRussianRouletteMinDepth(int depth);
    int russianRouletteMinDepth() const;

    void rebuildMeshBoundsMesh(const QColor& boundsColor);
    const MeshAccelBoundsMesh& meshBoundsMesh() const;

    bool rebuildMeshScene(
        const std::vector<ProceduralInstance>& proceduralInstances = {},
        const MeshSceneBuildParams& meshParams = {});

    void setRegionRenderEnabled(bool enabled);
    bool regionRenderEnabled() const;

    void setRenderRegion(int minX, int minY, int maxX, int maxY);
    QRect renderRegion() const;

    void resetRegionAccumulation();

    bool exportMeshSceneWavefrontObj(const QString& objFilePath, QString* errorMessage = nullptr) const;

private:
    void renderLoop();
    void notifyWorker();
    void invokeFrameReadyCallback();

    std::unique_ptr<PathTracerDetail::PathTracerImpl> m_impl;
    std::mutex m_callbackMutex;
    FrameReadyCallback m_frameReadyCallback;
};
