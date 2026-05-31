#pragma once

#include "CameraGpu.h"
#include "Sdf/SdfTypes.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>

namespace PathTracerDetail {
struct PathTracerImpl;
}

/// Portable CUDA path tracer. Writes tonemapped RGBA8 frames into caller-owned OpenGL PBOs.
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
    bool configure(int width, int height, uint32_t pbo0, uint32_t pbo1);

    void start();
    void stop();
    void resetAccumulation();
    bool isRunning() const;

    /// Stops rendering and unregisters CUDA resources for the configured PBOs.
    /// Call before deleting the OpenGL buffer objects on the host side.
    void releaseOutputSurfaces();

    /// Requires an active OpenGL context. Maps pbo[slot], copies accumulator, unmaps.
    bool publishDisplayFrame(int slot);

    /// Full-resolution sample iterations only (0 = unlimited). Total kernel launches = preview + max.
    void setMaxSamplesPerPixel(int max);
    int maxSamplesPerPixel() const;

    /// 0 = disabled (stride 1 always). N > 0 runs N coarse pyramid iterations before full-res samples.
    void setPreviewStepsPerLevel(int steps);
    int previewStepsPerLevel() const;

    int currentSampleCount() const;

    void setCamera(const CameraGpu& camera);

    void setVisualMode(SdfVisualMode mode);
    SdfVisualMode visualMode() const;

private:
    void renderLoop();
    void notifyWorker();

    std::unique_ptr<PathTracerDetail::PathTracerImpl> m_impl;
    std::mutex m_callbackMutex;
    FrameReadyCallback m_frameReadyCallback;
};
