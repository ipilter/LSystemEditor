#include "PathTracer.h"

#include "AppLog.h"
#include "CameraGpu.h"
#include "EnvironmentMap.h"
#include "PhysicalCamera.h"
#include "PathTracerSampleBudget.h"
#include "MeshAccel/MeshAccelBoundsMesh.h"
#include "MeshAccel/MeshAccelScene.h"
#include "MeshAccel/MeshSceneContent.h"
#include "PathTracerCuda.h"
#include "PathTracerPreviewLevels.h"
#include "RenderTypes.h"
#include "Spectral/SpectralState.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <cuda_gl_interop.h>
#include <cuda_runtime.h>
#include <vector_types.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include <QString>

struct AccumulatorData
{
    float4* d_buffer = nullptr;
    uint32_t* d_samples = nullptr;
    int width = 0;
    int height = 0;
};

struct PreviewLevelData
{
    float4* d_buffer = nullptr;
    int width = 0;
    int height = 0;
    int downscale = 1;
};

struct PathTracerDetail::PathTracerImpl
{
    AccumulatorData acc;
    std::vector<PreviewLevelData> previewLevels;
    CameraGpu* d_camera = nullptr;
    PhysicalCamera hostCamera{};
    mutable std::mutex cameraMutex;
    std::atomic<bool> cameraDirty{true};
    cudaGraphicsResource_t pboResources[2] = {nullptr, nullptr};
    cudaStream_t stream = nullptr;
    cudaEvent_t sampleCompleteEvent = nullptr;

    std::thread worker;
    std::atomic<bool> running{false};
    std::atomic<bool> resetRequested{false};
    std::atomic<bool> configured{false};

    std::mutex workerMutex;
    std::condition_variable workerCv;

    std::atomic<int> maxSamplesPerPixel{8};
    std::atomic<int> previewStepsPerLevel{0};
    std::atomic<int> sampleCount{0};
    std::atomic<int> finestCompletedPreview{-1};
    std::atomic<bool> displayBuffersValid{false};

    MeshAccelScene meshScene;
    MeshAccelBoundsMesh boundsMesh;

    EnvironmentMap environmentMap;
    SpectralStateHost spectralState;
    RenderParamsGpu hostRenderParams{};
    RenderParamsGpu* d_renderParams = nullptr;
    bool deviceParamsArePreview = false;
    int cudaDeviceId = 0;
    PhysicalCamera suggestedPhysicalCamera{};
    QString environmentHdrPath;
    std::atomic<bool> environmentDirty{true};
    std::atomic<bool> renderParamsDirty{true};

    CameraGpu lastSampleCamera{};
    std::mutex lastSampleCameraMutex;

    std::mutex meshSceneMutex;
    std::mutex streamMutex;
};

namespace {

constexpr int kMaxSamplesUpperBound = 1'000'000;
constexpr int kMaxPreviewStepsPerLevel = 128;

bool hasDisplayableContent(const PathTracerDetail::PathTracerImpl& impl)
{
    const int count = impl.sampleCount.load();
    const int previewCount = impl.previewStepsPerLevel.load();
    const int finest = impl.finestCompletedPreview.load();
    if (count <= 0) {
        return false;
    }
    if (previewCount > 0 && count <= previewCount) {
        return finest >= 0;
    }
    return true;
}

int clampMaxSamples(int value)
{
    if (value < 0) {
        return 0;
    }
    if (value > kMaxSamplesUpperBound) {
        return kMaxSamplesUpperBound;
    }
    return value;
}

int clampPreviewStepsPerLevel(int value)
{
    if (value < 0) {
        return 0;
    }
    if (value > kMaxPreviewStepsPerLevel) {
        return kMaxPreviewStepsPerLevel;
    }
    return value;
}

QString cudaErrorString(cudaError_t error)
{
    return QString::fromUtf8(cudaGetErrorString(error));
}

bool registerPboResource(cudaGraphicsResource_t* out, uint32_t pbo, QString* outError)
{
    if (out == nullptr || pbo == 0) {
        if (outError != nullptr) {
            *outError = QStringLiteral("invalid PBO id");
        }
        return false;
    }

    cudaGraphicsResource_t resource = nullptr;
    const cudaError_t error = cudaGraphicsGLRegisterBuffer(
        &resource,
        static_cast<unsigned int>(pbo),
        cudaGraphicsRegisterFlagsWriteDiscard);

    if (error != cudaSuccess) {
        if (outError != nullptr) {
            *outError = cudaErrorString(error);
        }
        return false;
    }

    *out = resource;
    return true;
}

void unregisterPboResource(cudaGraphicsResource_t* resource)
{
    if (resource == nullptr || *resource == nullptr) {
        return;
    }

    cudaGraphicsUnregisterResource(*resource);
    *resource = nullptr;
}

bool mapPboResource(cudaGraphicsResource_t resource, cudaStream_t stream, void** outPtr, QString* outError)
{
    if (resource == nullptr || outPtr == nullptr) {
        if (outError != nullptr) {
            *outError = QStringLiteral("invalid map arguments");
        }
        return false;
    }

    cudaError_t error = cudaGraphicsMapResources(1, &resource, stream);
    if (error != cudaSuccess) {
        if (outError != nullptr) {
            *outError = cudaErrorString(error);
        }
        return false;
    }

    void* devicePointer = nullptr;
    std::size_t mappedSize = 0;
    error = cudaGraphicsResourceGetMappedPointer(&devicePointer, &mappedSize, resource);
    if (error != cudaSuccess) {
        cudaGraphicsUnmapResources(1, &resource, stream);
        if (outError != nullptr) {
            *outError = cudaErrorString(error);
        }
        return false;
    }

    *outPtr = devicePointer;
    return true;
}

bool unmapPboResource(cudaGraphicsResource_t resource, cudaStream_t stream, QString* outError)
{
    if (resource == nullptr) {
        if (outError != nullptr) {
            *outError = QStringLiteral("invalid PBO resource");
        }
        return false;
    }

    const cudaError_t error = cudaGraphicsUnmapResources(1, &resource, stream);
    if (error != cudaSuccess && outError != nullptr) {
        *outError = cudaErrorString(error);
    }
    return error == cudaSuccess;
}

void freeAccumulator(AccumulatorData* acc)
{
    if (acc == nullptr) {
        return;
    }

    if (acc->d_buffer != nullptr) {
        cudaFree(acc->d_buffer);
        acc->d_buffer = nullptr;
    }
    if (acc->d_samples != nullptr) {
        cudaFree(acc->d_samples);
        acc->d_samples = nullptr;
    }
    acc->width = 0;
    acc->height = 0;
}

bool initAccumulator(AccumulatorData* acc, int width, int height, QString* outError)
{
    if (acc == nullptr || width <= 0 || height <= 0) {
        if (outError != nullptr) {
            *outError = QStringLiteral("invalid accumulator dimensions");
        }
        return false;
    }

    freeAccumulator(acc);

    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    cudaError_t error = cudaMalloc(&acc->d_buffer, pixelCount * sizeof(float4));
    if (error != cudaSuccess) {
        freeAccumulator(acc);
        if (outError != nullptr) {
            *outError = cudaErrorString(error);
        }
        return false;
    }

    error = cudaMalloc(&acc->d_samples, pixelCount * sizeof(uint32_t));
    if (error != cudaSuccess) {
        freeAccumulator(acc);
        if (outError != nullptr) {
            *outError = cudaErrorString(error);
        }
        return false;
    }

    error = cudaMemset(acc->d_samples, 0, pixelCount * sizeof(uint32_t));
    if (error != cudaSuccess) {
        freeAccumulator(acc);
        if (outError != nullptr) {
            *outError = cudaErrorString(error);
        }
        return false;
    }

    acc->width = width;
    acc->height = height;
    return true;
}

void freePreviewBuffers(std::vector<PreviewLevelData>* levels)
{
    if (levels == nullptr) {
        return;
    }

    for (PreviewLevelData& level : *levels) {
        if (level.d_buffer != nullptr) {
            cudaFree(level.d_buffer);
            level.d_buffer = nullptr;
        }
        level.width = 0;
        level.height = 0;
        level.downscale = 1;
    }
    levels->clear();
}

bool initPreviewBuffers(
    std::vector<PreviewLevelData>* levels,
    int fullWidth,
    int fullHeight,
    int previewLevelCount,
    QString* outError)
{
    if (levels == nullptr || fullWidth <= 0 || fullHeight <= 0) {
        if (outError != nullptr) {
            *outError = QStringLiteral("invalid preview buffer dimensions");
        }
        return false;
    }

    freePreviewBuffers(levels);

    if (previewLevelCount <= 0) {
        return true;
    }

    const std::vector<PreviewLevelDimensions> dimensions =
        buildPreviewLevelDimensions(fullWidth, fullHeight, previewLevelCount);
    levels->reserve(dimensions.size());

    for (const PreviewLevelDimensions& dim : dimensions) {
        PreviewLevelData level{};
        level.width = dim.width;
        level.height = dim.height;
        level.downscale = dim.downscale;

        const std::size_t pixelCount =
            static_cast<std::size_t>(level.width) * static_cast<std::size_t>(level.height);
        cudaError_t error = cudaMalloc(&level.d_buffer, pixelCount * sizeof(float4));
        if (error != cudaSuccess) {
            freePreviewBuffers(levels);
            if (outError != nullptr) {
                *outError = cudaErrorString(error);
            }
            return false;
        }

        levels->push_back(level);
    }

    return true;
}

bool clearPreviewBuffers(PathTracerDetail::PathTracerImpl* impl, QString* outError)
{
    if (impl == nullptr || impl->stream == nullptr) {
        return true;
    }

    for (const PreviewLevelData& level : impl->previewLevels) {
        if (level.d_buffer == nullptr || level.width <= 0 || level.height <= 0) {
            continue;
        }

        const std::size_t byteCount =
            static_cast<std::size_t>(level.width) * static_cast<std::size_t>(level.height) * sizeof(float4);
        const cudaError_t error = cudaMemsetAsync(level.d_buffer, 0, byteCount, impl->stream);
        if (error != cudaSuccess) {
            if (outError != nullptr) {
                *outError = cudaErrorString(error);
            }
            return false;
        }
    }

    return true;
}

bool clearAccumulator(PathTracerDetail::PathTracerImpl* impl, QString* outError)
{
    if (impl == nullptr || impl->acc.d_buffer == nullptr || impl->acc.d_samples == nullptr ||
        impl->acc.width <= 0 || impl->acc.height <= 0 || impl->stream == nullptr) {
        return true;
    }

    if (!pathTracerClearAccumulator(
            impl->acc.d_buffer,
            impl->acc.d_samples,
            impl->acc.width,
            impl->acc.height,
            impl->stream)) {
        if (outError != nullptr) {
            *outError = QStringLiteral("clear accumulator kernel launch failed");
        }
        return false;
    }

    return true;
}

void freeCameraGpu(PathTracerDetail::PathTracerImpl* impl)
{
    if (impl == nullptr || impl->d_camera == nullptr) {
        return;
    }

    cudaFree(impl->d_camera);
    impl->d_camera = nullptr;
}

void freeRenderParamsGpu(PathTracerDetail::PathTracerImpl* impl)
{
    if (impl == nullptr) {
        return;
    }

    if (impl->d_renderParams != nullptr) {
        cudaFree(impl->d_renderParams);
        impl->d_renderParams = nullptr;
    }
    impl->deviceParamsArePreview = false;
}

void freeSpectralState(PathTracerDetail::PathTracerImpl* impl)
{
    if (impl == nullptr) {
        return;
    }
    impl->spectralState.freeDevice();
}

bool initSpectralState(PathTracerDetail::PathTracerImpl* impl, QString* outError)
{
    if (impl == nullptr) {
        if (outError != nullptr) {
            *outError = QStringLiteral("invalid path tracer impl");
        }
        return false;
    }

    freeSpectralState(impl);

    std::string loadError;
    if (!impl->spectralState.loadCoeffFile(PATHTRACER_RGB2SPEC_COEFF_PATH, &loadError)) {
        if (outError != nullptr) {
            *outError = QString::fromStdString(loadError);
        }
        return false;
    }

    if (!impl->spectralState.uploadToDevice(&loadError)) {
        if (outError != nullptr) {
            *outError = QString::fromStdString(loadError);
        }
        freeSpectralState(impl);
        return false;
    }

    spectralSetHostModel(impl->spectralState.hostModel());
    return true;
}

bool initRenderParamsGpu(PathTracerDetail::PathTracerImpl* impl, QString* outError)
{
    if (impl == nullptr) {
        if (outError != nullptr) {
            *outError = QStringLiteral("invalid path tracer impl");
        }
        return false;
    }

    freeRenderParamsGpu(impl);

    cudaError_t error = cudaMalloc(&impl->d_renderParams, sizeof(RenderParamsGpu));
    if (error != cudaSuccess) {
        freeRenderParamsGpu(impl);
        if (outError != nullptr) {
            *outError = cudaErrorString(error);
        }
        return false;
    }

    impl->hostRenderParams = RenderParamsGpu{};
    impl->deviceParamsArePreview = false;
    impl->renderParamsDirty.store(true);
    return true;
}

bool syncRenderParamsToDevice(
    PathTracerDetail::PathTracerImpl* impl,
    cudaStream_t stream,
    bool previewPass,
    QString* outError)
{
    if (impl == nullptr || impl->d_renderParams == nullptr) {
        if (outError != nullptr) {
            *outError = QStringLiteral("render params not configured");
        }
        return false;
    }

    if (previewPass) {
        if (impl->deviceParamsArePreview) {
            return true;
        }
    } else if (!impl->renderParamsDirty.load() && !impl->deviceParamsArePreview) {
        return true;
    }

    RenderParamsGpu deviceParams = impl->hostRenderParams;
    if (previewPass) {
        deviceParams.maxPathDepth = kPreviewMaxPathDepth;
    }

    const cudaError_t error = cudaMemcpyAsync(
        impl->d_renderParams,
        &deviceParams,
        sizeof(RenderParamsGpu),
        cudaMemcpyHostToDevice,
        stream);
    if (error != cudaSuccess) {
        if (outError != nullptr) {
            *outError = cudaErrorString(error);
        }
        return false;
    }

    impl->deviceParamsArePreview = previewPass;
    if (!previewPass) {
        impl->renderParamsDirty.store(false);
    }
    return true;
}

bool reloadEnvironmentMap(PathTracerDetail::PathTracerImpl* impl, cudaStream_t stream, QString* outError)
{
    if (impl == nullptr) {
        return false;
    }

    if (impl->environmentHdrPath.isEmpty()) {
        impl->environmentMap.clear();
        impl->environmentMap.upload(stream);
        impl->environmentDirty.store(false);
        impl->suggestedPhysicalCamera = PhysicalCamera{};
        return true;
    }

    QString loadError;
    if (!impl->environmentMap.loadFromHdr(impl->environmentHdrPath, &loadError)) {
        if (outError != nullptr) {
            *outError = loadError;
        }
        impl->environmentMap.clear();
        impl->environmentMap.upload(stream);
        impl->environmentDirty.store(false);
        impl->suggestedPhysicalCamera = PhysicalCamera{};
        return false;
    }

    if (!impl->environmentMap.upload(stream)) {
        if (outError != nullptr) {
            *outError = QStringLiteral("environment map upload failed");
        }
        return false;
    }

    impl->suggestedPhysicalCamera = impl->environmentMap.suggestPhysicalCamera();
    impl->environmentDirty.store(false);
    return true;
}

bool uploadEnvironmentIfDirty(PathTracerDetail::PathTracerImpl* impl, cudaStream_t stream, QString* outError)
{
    if (impl == nullptr) {
        return false;
    }

    if (!impl->environmentDirty.load()) {
        return true;
    }

    return reloadEnvironmentMap(impl, stream, outError);
}

bool initCameraGpu(PathTracerDetail::PathTracerImpl* impl, QString* outError)
{
    if (impl == nullptr) {
        if (outError != nullptr) {
            *outError = QStringLiteral("invalid path tracer impl");
        }
        return false;
    }

    freeCameraGpu(impl);

    cudaError_t error = cudaMalloc(&impl->d_camera, sizeof(CameraGpu));
    if (error != cudaSuccess) {
        if (outError != nullptr) {
            *outError = cudaErrorString(error);
        }
        return false;
    }

    impl->hostCamera = PhysicalCamera{};
    impl->cameraDirty.store(true);
    return true;
}

CameraGpu buildCameraGpu(const PathTracerDetail::PathTracerImpl* impl)
{
    CameraGpu camera{};
    if (impl == nullptr) {
        return camera;
    }

    {
        std::lock_guard<std::mutex> lock(impl->cameraMutex);
        camera = impl->hostCamera.toGpu();
    }

    if (impl->acc.width > 0 && impl->acc.height > 0) {
        camera.aspect = static_cast<float>(impl->acc.width) / static_cast<float>(impl->acc.height);
    }

    return camera;
}

bool uploadCameraIfDirty(PathTracerDetail::PathTracerImpl* impl, cudaStream_t stream, QString* outError)
{
    if (impl == nullptr || impl->d_camera == nullptr) {
        if (outError != nullptr) {
            *outError = QStringLiteral("camera not configured");
        }
        return false;
    }

    if (!impl->cameraDirty.load()) {
        return true;
    }

    const CameraGpu cameraCopy = buildCameraGpu(impl);
    const cudaError_t error =
        cudaMemcpyAsync(impl->d_camera, &cameraCopy, sizeof(CameraGpu), cudaMemcpyHostToDevice, stream);
    if (error != cudaSuccess) {
        if (outError != nullptr) {
            *outError = cudaErrorString(error);
        }
        return false;
    }

    impl->cameraDirty.store(false);
    return true;
}

void unregisterPboResources(PathTracerDetail::PathTracerImpl* impl)
{
    if (impl == nullptr) {
        return;
    }

    for (int i = 0; i < 2; ++i) {
        unregisterPboResource(&impl->pboResources[i]);
    }
}

void destroyStream(PathTracerDetail::PathTracerImpl* impl)
{
    if (impl == nullptr || impl->stream == nullptr) {
        return;
    }

    cudaStreamDestroy(impl->stream);
    impl->stream = nullptr;
}

void destroySampleEvent(PathTracerDetail::PathTracerImpl* impl)
{
    if (impl == nullptr || impl->sampleCompleteEvent == nullptr) {
        return;
    }

    cudaEventDestroy(impl->sampleCompleteEvent);
    impl->sampleCompleteEvent = nullptr;
}

bool ensureSampleEvent(PathTracerDetail::PathTracerImpl* impl, QString* outError)
{
    if (impl == nullptr) {
        return false;
    }

    if (impl->sampleCompleteEvent != nullptr) {
        return true;
    }

    const cudaError_t error = cudaEventCreate(&impl->sampleCompleteEvent);
    if (error != cudaSuccess && outError != nullptr) {
        *outError = cudaErrorString(error);
    }
    return error == cudaSuccess;
}

bool canTakeSample(const PathTracerDetail::PathTracerImpl* impl)
{
    return canTakeSampleAtIteration(
        impl->sampleCount.load(),
        impl->previewStepsPerLevel.load(),
        impl->maxSamplesPerPixel.load());
}

void checkCudaGlDeviceAffinity(PathTracerDetail::PathTracerImpl* impl)
{
    if (impl == nullptr) {
        return;
    }

    unsigned int cudaDeviceCount = 0;
    int glCudaDevices[1] = {-1};
    const cudaError_t error =
        cudaGLGetDevices(&cudaDeviceCount, glCudaDevices, 1, cudaGLDeviceListAll);
    if (error != cudaSuccess) {
        AppLog::instance().warning(
            QStringLiteral("CUDA-GL device query failed: %1").arg(cudaErrorString(error)));
        return;
    }

    if (cudaDeviceCount == 0) {
        AppLog::instance().warning(
            QStringLiteral("No CUDA devices associated with the current OpenGL context"));
        return;
    }

    impl->cudaDeviceId = glCudaDevices[0];

    int currentCudaDevice = -1;
    if (cudaGetDevice(&currentCudaDevice) != cudaSuccess) {
        AppLog::instance().warning(QStringLiteral("cudaGetDevice failed during CUDA-GL affinity check"));
        return;
    }

    if (currentCudaDevice != impl->cudaDeviceId) {
        AppLog::instance().warning(
            QStringLiteral("CUDA device (%1) may not match OpenGL device (%2)")
                .arg(currentCudaDevice)
                .arg(impl->cudaDeviceId));
    }
}

void ensureCudaContext()
{
    cudaFree(nullptr);
}

void releaseMeshScene(PathTracerDetail::PathTracerImpl* impl)
{
    if (impl == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> meshSceneLock(impl->meshSceneMutex);
    impl->meshScene.release();
    impl->boundsMesh = MeshAccelBoundsMesh{};
}

enum class SampleWaitResult
{
    Completed,
    ResetDuringWait,
    Stopped,
    Error
};

SampleWaitResult waitForSampleEvent(
    cudaEvent_t event,
    const std::atomic<bool>& running,
    const std::atomic<bool>& resetRequested,
    QString* outError)
{
    bool resetSeen = false;
    for (;;) {
        if (!running.load()) {
            return SampleWaitResult::Stopped;
        }
        if (resetRequested.load()) {
            resetSeen = true;
        }

        const cudaError_t queryError = cudaEventQuery(event);
        if (queryError == cudaSuccess) {
            return resetSeen ? SampleWaitResult::ResetDuringWait : SampleWaitResult::Completed;
        }
        if (queryError != cudaErrorNotReady) {
            if (outError != nullptr) {
                *outError = cudaErrorString(queryError);
            }
            return SampleWaitResult::Error;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

#ifdef PATHTRACER_PROFILE

struct RenderPipelineTiming
{
    std::atomic<int> completedSamples{0};
    std::atomic<uint64_t> totalGpuWaitUs{0};
    std::atomic<uint64_t> totalPublishMutexWaitUs{0};
    std::atomic<uint64_t> publishMutexWaitSamples{0};
    std::chrono::steady_clock::time_point resetRequestTime{};
    std::atomic<bool> resetTimingActive{false};
};

RenderPipelineTiming g_renderPipelineTiming;

constexpr int kPipelineTimingLogInterval = 128;

void logPipelineTimingIfDue()
{
    const int count = g_renderPipelineTiming.completedSamples.load();
    if (count <= 0 || count % kPipelineTimingLogInterval != 0) {
        return;
    }

    const uint64_t gpuSamples = static_cast<uint64_t>(count);
    const uint64_t avgGpuWaitUs = g_renderPipelineTiming.totalGpuWaitUs.load() / gpuSamples;
    const uint64_t publishSamples = g_renderPipelineTiming.publishMutexWaitSamples.load();
    const uint64_t avgPublishMutexWaitUs =
        publishSamples > 0 ? g_renderPipelineTiming.totalPublishMutexWaitUs.load() / publishSamples : 0;

    AppLog::instance().info(
        QStringLiteral("Render pipeline timing (last %1 samples): avg GPU wait %2 ms, avg publish mutex wait %3 ms")
            .arg(kPipelineTimingLogInterval)
            .arg(static_cast<double>(avgGpuWaitUs) / 1000.0, 0, 'f', 2)
            .arg(static_cast<double>(avgPublishMutexWaitUs) / 1000.0, 0, 'f', 2));
}

#endif // PATHTRACER_PROFILE

bool buildAndUploadMeshScene(
    PathTracerDetail::PathTracerImpl* impl,
    const std::vector<ProceduralInstance>& proceduralInstances,
    const MeshSceneBuildParams& meshParams,
    QString* outError)
{
    if (impl == nullptr || impl->stream == nullptr) {
        if (outError != nullptr) {
            *outError = QStringLiteral("PathTracer not initialized");
        }
        return false;
    }

    std::lock_guard<std::mutex> meshSceneLock(impl->meshSceneMutex);

    if (!meshSceneBuild(proceduralInstances, impl->meshScene, meshParams)) {
        if (outError != nullptr) {
            *outError = QStringLiteral("Manifold mesh scene build failed");
        }
        return false;
    }

    if (!impl->meshScene.allocate()) {
        if (outError != nullptr) {
            *outError = QStringLiteral("Mesh accel scene allocation failed");
        }
        return false;
    }

    if (!impl->meshScene.upload(impl->stream)) {
        if (outError != nullptr) {
            *outError = QStringLiteral("Mesh accel scene upload failed");
        }
        return false;
    }

    return true;
}

} // namespace

PathTracer::PathTracer()
    : m_impl(std::make_unique<PathTracerDetail::PathTracerImpl>())
{
}

PathTracer::~PathTracer()
{
    stop();
    unregisterPboResources(m_impl.get());
    freeCameraGpu(m_impl.get());
    freeRenderParamsGpu(m_impl.get());
    freeSpectralState(m_impl.get());
    m_impl->environmentMap.release();
    freeAccumulator(&m_impl->acc);
    freePreviewBuffers(&m_impl->previewLevels);
    releaseMeshScene(m_impl.get());
    destroySampleEvent(m_impl.get());
    destroyStream(m_impl.get());
}

void PathTracer::setFrameReadyCallback(FrameReadyCallback callback)
{
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_frameReadyCallback = std::move(callback);
}

bool PathTracer::configure(
    int width,
    int height,
    uint32_t pbo0,
    uint32_t pbo1,
    const std::vector<ProceduralInstance>& proceduralInstances,
    const MeshSceneBuildParams& meshParams)
{
    if (width <= 0 || height <= 0 || pbo0 == 0 || pbo1 == 0) {
        AppLog::instance().error(QStringLiteral("PathTracer configure: invalid dimensions or PBO ids"));
        return false;
    }

    if (m_impl->running.load()) {
        stop();
    }

    unregisterPboResources(m_impl.get());
    freeCameraGpu(m_impl.get());
    freeRenderParamsGpu(m_impl.get());
    freeSpectralState(m_impl.get());
    m_impl->environmentMap.release();
    freeAccumulator(&m_impl->acc);
    freePreviewBuffers(&m_impl->previewLevels);
    releaseMeshScene(m_impl.get());

    if (m_impl->stream == nullptr) {
        const cudaError_t streamError = cudaStreamCreate(&m_impl->stream);
        if (streamError != cudaSuccess) {
            AppLog::instance().error(
                QStringLiteral("PathTracer configure: stream creation failed: %1").arg(cudaErrorString(streamError)));
            return false;
        }
    }

    QString error;
    if (!ensureSampleEvent(m_impl.get(), &error)) {
        AppLog::instance().error(
            QStringLiteral("PathTracer configure: sample event creation failed: %1").arg(error));
        return false;
    }

    ensureCudaContext();
    checkCudaGlDeviceAffinity(m_impl.get());
    cudaSetDevice(m_impl->cudaDeviceId);

    if (!registerPboResource(&m_impl->pboResources[0], pbo0, &error)) {
        AppLog::instance().error(
            QStringLiteral("PathTracer configure: PBO 0 registration failed: %1").arg(error));
        unregisterPboResources(m_impl.get());
        return false;
    }

    if (!registerPboResource(&m_impl->pboResources[1], pbo1, &error)) {
        AppLog::instance().error(
            QStringLiteral("PathTracer configure: PBO 1 registration failed: %1").arg(error));
        unregisterPboResources(m_impl.get());
        return false;
    }

    if (!initAccumulator(&m_impl->acc, width, height, &error)) {
        AppLog::instance().error(
            QStringLiteral("PathTracer configure: accumulator allocation failed: %1").arg(error));
        unregisterPboResources(m_impl.get());
        return false;
    }

    if (!initPreviewBuffers(
            &m_impl->previewLevels,
            width,
            height,
            m_impl->previewStepsPerLevel.load(),
            &error)) {
        AppLog::instance().error(
            QStringLiteral("PathTracer configure: preview buffer allocation failed: %1").arg(error));
        unregisterPboResources(m_impl.get());
        freeAccumulator(&m_impl->acc);
        return false;
    }

    if (!initCameraGpu(m_impl.get(), &error)) {
        AppLog::instance().error(
            QStringLiteral("PathTracer configure: camera allocation failed: %1").arg(error));
        unregisterPboResources(m_impl.get());
        freeAccumulator(&m_impl->acc);
        freePreviewBuffers(&m_impl->previewLevels);
        return false;
    }

    if (!initRenderParamsGpu(m_impl.get(), &error)) {
        AppLog::instance().error(
            QStringLiteral("PathTracer configure: render params allocation failed: %1").arg(error));
        unregisterPboResources(m_impl.get());
        freeCameraGpu(m_impl.get());
        freeAccumulator(&m_impl->acc);
        return false;
    }

    if (!initSpectralState(m_impl.get(), &error)) {
        AppLog::instance().error(
            QStringLiteral("PathTracer configure: spectral state init failed: %1").arg(error));
        unregisterPboResources(m_impl.get());
        freeCameraGpu(m_impl.get());
        freeRenderParamsGpu(m_impl.get());
        freeAccumulator(&m_impl->acc);
        return false;
    }

    if (!reloadEnvironmentMap(m_impl.get(), m_impl->stream, &error)) {
        if (!m_impl->environmentHdrPath.isEmpty()) {
            AppLog::instance().warning(
                QStringLiteral("PathTracer configure: environment map load failed: %1").arg(error));
        }
    }

    QString accelError;
    if (!buildAndUploadMeshScene(m_impl.get(), proceduralInstances, meshParams, &accelError)) {
        AppLog::instance().error(
            QStringLiteral("PathTracer configure: %1").arg(accelError));
        unregisterPboResources(m_impl.get());
        freeCameraGpu(m_impl.get());
        freeAccumulator(&m_impl->acc);
        releaseMeshScene(m_impl.get());
        return false;
    }

    if (!clearAccumulator(m_impl.get(), &error)) {
        AppLog::instance().error(
            QStringLiteral("PathTracer configure: accumulator clear failed: %1").arg(error));
        unregisterPboResources(m_impl.get());
        freeCameraGpu(m_impl.get());
        freeAccumulator(&m_impl->acc);
        freePreviewBuffers(&m_impl->previewLevels);
        releaseMeshScene(m_impl.get());
        return false;
    }

    if (!clearPreviewBuffers(m_impl.get(), &error)) {
        AppLog::instance().error(
            QStringLiteral("PathTracer configure: preview buffer clear failed: %1").arg(error));
        unregisterPboResources(m_impl.get());
        freeCameraGpu(m_impl.get());
        freeAccumulator(&m_impl->acc);
        freePreviewBuffers(&m_impl->previewLevels);
        releaseMeshScene(m_impl.get());
        return false;
    }

    const cudaError_t syncError = cudaStreamSynchronize(m_impl->stream);
    if (syncError != cudaSuccess) {
        AppLog::instance().error(
            QStringLiteral("PathTracer configure: stream sync failed: %1").arg(cudaErrorString(syncError)));
        unregisterPboResources(m_impl.get());
        freeCameraGpu(m_impl.get());
        freeAccumulator(&m_impl->acc);
        freePreviewBuffers(&m_impl->previewLevels);
        releaseMeshScene(m_impl.get());
        return false;
    }

    m_impl->resetRequested.store(false);
    m_impl->sampleCount.store(0);
    m_impl->finestCompletedPreview.store(-1);
    m_impl->displayBuffersValid.store(false);
    m_impl->configured.store(true);
    notifyWorker();
    return true;
}

void PathTracer::start()
{
    if (m_impl->running.load() || !m_impl->configured.load()) {
        return;
    }

    m_impl->running.store(true);
    m_impl->worker = std::thread([this]() { renderLoop(); });
}

void PathTracer::stop()
{
    if (!m_impl->running.load()) {
        return;
    }

    m_impl->running.store(false);
    notifyWorker();
    if (m_impl->worker.joinable()) {
        m_impl->worker.join();
    }
}

void PathTracer::resetAccumulation()
{
#ifdef PATHTRACER_PROFILE
    g_renderPipelineTiming.resetRequestTime = std::chrono::steady_clock::now();
    g_renderPipelineTiming.resetTimingActive.store(true);
#endif
    m_impl->resetRequested.store(true);
    notifyWorker();
}

bool PathTracer::isRunning() const
{
    return m_impl->running.load();
}

void PathTracer::releaseOutputSurfaces()
{
    stop();
    unregisterPboResources(m_impl.get());
    m_impl->configured.store(false);
}

bool PathTracer::publishDisplayFrame(int slot)
{
    if (slot < 0 || slot > 1 || !m_impl->configured.load()) {
        return false;
    }

    if (!m_impl->displayBuffersValid.load() || !hasDisplayableContent(*m_impl)) {
        return false;
    }

    if (m_impl->acc.width <= 0 || m_impl->acc.height <= 0) {
        return false;
    }

    cudaGraphicsResource_t resource = m_impl->pboResources[slot];
    if (resource == nullptr || m_impl->sampleCompleteEvent == nullptr) {
        return false;
    }

    const auto mutexWaitStart = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> streamLock(m_impl->streamMutex);
    const auto mutexWaitEnd = std::chrono::steady_clock::now();
    const auto mutexWaitUs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(mutexWaitEnd - mutexWaitStart).count());
#ifdef PATHTRACER_PROFILE
    g_renderPipelineTiming.totalPublishMutexWaitUs.fetch_add(mutexWaitUs);
    g_renderPipelineTiming.publishMutexWaitSamples.fetch_add(1);
#endif

    QString error;
    const cudaError_t waitError =
        cudaStreamWaitEvent(m_impl->stream, m_impl->sampleCompleteEvent, 0);
    if (waitError != cudaSuccess) {
        AppLog::instance().error(
            QStringLiteral("CUDA stream wait on sample event failed: %1").arg(cudaErrorString(waitError)));
        return false;
    }

    void* devicePointer = nullptr;
    if (!mapPboResource(resource, m_impl->stream, &devicePointer, &error)) {
        AppLog::instance().error(QStringLiteral("CUDA-GL PBO map failed: %1").arg(error));
        return false;
    }

    const int previewCount = m_impl->previewStepsPerLevel.load();
    const int iter = m_impl->sampleCount.load();
    const int finest = m_impl->finestCompletedPreview.load();

    const bool showPreview =
        previewCount > 0 &&
        finest >= 0 &&
        finest < static_cast<int>(m_impl->previewLevels.size()) &&
        iter <= previewCount;

    float exposure = 1.0f;
    {
        std::lock_guard<std::mutex> lock(m_impl->cameraMutex);
        exposure = m_impl->hostCamera.exposureMultiplier();
    }

    bool copied = false;
    if (showPreview) {
        const PreviewLevelData& level = m_impl->previewLevels[static_cast<std::size_t>(finest)];
        copied = pathTracerUpsamplePreviewToPbo(
            level.d_buffer,
            level.width,
            level.height,
            level.downscale,
            static_cast<uchar4*>(devicePointer),
            m_impl->acc.width,
            m_impl->acc.height,
            m_impl->d_renderParams,
            exposure,
            m_impl->stream);
    } else {
        copied = pathTracerCopyToPbo(
            m_impl->acc.d_buffer,
            m_impl->acc.d_samples,
            static_cast<uchar4*>(devicePointer),
            m_impl->acc.width,
            m_impl->acc.height,
            m_impl->d_renderParams,
            exposure,
            m_impl->stream);
    }

    if (!copied) {
        unmapPboResource(resource, m_impl->stream, nullptr);
        AppLog::instance().error(QStringLiteral("copy-to-PBO kernel launch failed"));
        return false;
    }

    if (!unmapPboResource(resource, m_impl->stream, &error)) {
        AppLog::instance().error(QStringLiteral("CUDA-GL PBO unmap failed: %1").arg(error));
        return false;
    }

    const cudaError_t syncError = cudaStreamSynchronize(m_impl->stream);
    if (syncError != cudaSuccess) {
        AppLog::instance().error(
            QStringLiteral("CUDA stream sync after PBO publish failed: %1").arg(cudaErrorString(syncError)));
        return false;
    }

    return true;
}

void PathTracer::setMaxSamplesPerPixel(int max)
{
    m_impl->maxSamplesPerPixel.store(clampMaxSamples(max));
    notifyWorker();
}

int PathTracer::maxSamplesPerPixel() const
{
    return m_impl->maxSamplesPerPixel.load();
}

void PathTracer::setPreviewStepsPerLevel(int steps)
{
    const int clamped = clampPreviewStepsPerLevel(steps);
    m_impl->previewStepsPerLevel.store(clamped);

    if (m_impl->configured.load() && m_impl->acc.width > 0 && m_impl->acc.height > 0) {
        QString error;
        std::lock_guard<std::mutex> streamLock(m_impl->streamMutex);
        if (!initPreviewBuffers(
                &m_impl->previewLevels,
                m_impl->acc.width,
                m_impl->acc.height,
                clamped,
                &error)) {
            AppLog::instance().warning(
                QStringLiteral("PathTracer preview buffer resize failed: %1").arg(error));
        } else if (!clearPreviewBuffers(m_impl.get(), &error)) {
            AppLog::instance().warning(
                QStringLiteral("PathTracer preview buffer clear failed: %1").arg(error));
        } else {
            const cudaError_t syncError = cudaStreamSynchronize(m_impl->stream);
            if (syncError != cudaSuccess) {
                AppLog::instance().warning(
                    QStringLiteral("PathTracer preview buffer sync failed: %1")
                        .arg(cudaErrorString(syncError)));
            }
        }
    }

    notifyWorker();
}

int PathTracer::previewStepsPerLevel() const
{
    return m_impl->previewStepsPerLevel.load();
}

int PathTracer::currentSampleCount() const
{
    return m_impl->sampleCount.load();
}

int PathTracer::sampleBudgetTotalIterations() const
{
    return ::sampleBudgetTotalIterations(
        m_impl->previewStepsPerLevel.load(),
        m_impl->maxSamplesPerPixel.load());
}

bool PathTracer::isSampleBudgetExhausted() const
{
    return ::isSampleBudgetExhausted(
        m_impl->sampleCount.load(),
        m_impl->previewStepsPerLevel.load(),
        m_impl->maxSamplesPerPixel.load());
}

void PathTracer::setCamera(const CameraGpu& camera)
{
    CameraGpu stored = camera;
    if (m_impl->acc.width > 0 && m_impl->acc.height > 0) {
        stored.aspect =
            static_cast<float>(m_impl->acc.width) / static_cast<float>(m_impl->acc.height);
    }

    {
        std::lock_guard<std::mutex> lock(m_impl->cameraMutex);
        m_impl->hostCamera.applyGpuGeometry(stored);
    }
    {
        std::lock_guard<std::mutex> lock(m_impl->lastSampleCameraMutex);
        m_impl->lastSampleCamera = stored;
    }
    m_impl->cameraDirty.store(true);
    notifyWorker();
}

CameraGpu PathTracer::lastSampleCamera() const
{
    std::lock_guard<std::mutex> lock(m_impl->lastSampleCameraMutex);
    return m_impl->lastSampleCamera;
}

void PathTracer::setClearColor(const QColor& color)
{
    if (!color.isValid()) {
        return;
    }

    m_impl->hostRenderParams.backgroundR = static_cast<float>(color.redF());
    m_impl->hostRenderParams.backgroundG = static_cast<float>(color.greenF());
    m_impl->hostRenderParams.backgroundB = static_cast<float>(color.blueF());
    m_impl->renderParamsDirty.store(true);

    if (m_impl->configured.load()) {
        resetAccumulation();
    }
}

void PathTracer::setEnvironmentIntensity(float intensity)
{
    if (intensity < 0.0f) {
        intensity = 0.0f;
    }
    if (intensity > 100.0f) {
        intensity = 100.0f;
    }

    if (m_impl->hostRenderParams.environmentIntensity == intensity) {
        return;
    }

    m_impl->hostRenderParams.environmentIntensity = intensity;
    m_impl->renderParamsDirty.store(true);

    if (m_impl->configured.load()) {
        resetAccumulation();
    }
}

float PathTracer::environmentIntensity() const
{
    return m_impl->hostRenderParams.environmentIntensity;
}

void PathTracer::setRussianRouletteMinDepth(int depth)
{
    if (depth < 0) {
        depth = 0;
    }
    if (depth > 256) {
        depth = 256;
    }

    if (m_impl->hostRenderParams.russianRouletteMinDepth == depth) {
        return;
    }

    m_impl->hostRenderParams.russianRouletteMinDepth = depth;
    m_impl->renderParamsDirty.store(true);

    if (m_impl->configured.load()) {
        resetAccumulation();
    }
}

int PathTracer::russianRouletteMinDepth() const
{
    return m_impl->hostRenderParams.russianRouletteMinDepth;
}

void PathTracer::setPhysicalCamera(float fStop, float shutterSpeedSeconds, float iso)
{
    std::lock_guard<std::mutex> lock(m_impl->cameraMutex);
    m_impl->hostCamera.fStop = PhysicalCamera::clampFStop(fStop);
    m_impl->hostCamera.shutterSpeedSeconds = PhysicalCamera::clampShutterSpeedSeconds(shutterSpeedSeconds);
    m_impl->hostCamera.iso = PhysicalCamera::clampIso(iso);
}

PhysicalCamera PathTracer::suggestedPhysicalCamera() const
{
    return m_impl->suggestedPhysicalCamera;
}

bool PathTracer::computeSuggestedCameraFromAccumulator(PhysicalCamera* out) const
{
    if (out == nullptr || m_impl == nullptr || !m_impl->configured.load()) {
        return false;
    }

    if (m_impl->acc.d_buffer == nullptr || m_impl->acc.d_samples == nullptr || m_impl->acc.width <= 0 ||
        m_impl->acc.height <= 0) {
        return false;
    }

    const int width = m_impl->acc.width;
    const int height = m_impl->acc.height;
    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

    std::vector<float4> hostBuffer(pixelCount);
    std::vector<uint32_t> hostCounts(pixelCount);

    {
        std::lock_guard<std::mutex> streamLock(m_impl->streamMutex);
        if (m_impl->sampleCompleteEvent != nullptr) {
            const cudaError_t waitError =
                cudaStreamWaitEvent(m_impl->stream, m_impl->sampleCompleteEvent, 0);
            if (waitError != cudaSuccess) {
                return false;
            }
        }

        const cudaError_t bufferError = cudaMemcpy(
            hostBuffer.data(),
            m_impl->acc.d_buffer,
            pixelCount * sizeof(float4),
            cudaMemcpyDeviceToHost);
        const cudaError_t countsError = cudaMemcpy(
            hostCounts.data(),
            m_impl->acc.d_samples,
            pixelCount * sizeof(uint32_t),
            cudaMemcpyDeviceToHost);
        if (bufferError != cudaSuccess || countsError != cudaSuccess) {
            return false;
        }
    }

    constexpr int kSampleStride = 8;
    std::vector<float> luminances;
    luminances.reserve(pixelCount / static_cast<std::size_t>(kSampleStride * kSampleStride) + 1);

    for (int y = 0; y < height; y += kSampleStride) {
        for (int x = 0; x < width; x += kSampleStride) {
            const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                static_cast<std::size_t>(x);
            if (hostCounts[index] == 0) {
                continue;
            }

            const float4 value = hostBuffer[index];
            const float luminance = 0.2126f * value.x + 0.7152f * value.y + 0.0722f * value.z;
            if (luminance > 1.0e-6f) {
                luminances.push_back(luminance);
            }
        }
    }

    if (luminances.empty()) {
        return false;
    }

    std::vector<float> sorted = luminances;
    const std::size_t p99Index = static_cast<std::size_t>(0.99f * static_cast<float>(sorted.size() - 1));
    std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(p99Index), sorted.end());
    const float luminanceCap = sorted[p99Index];

    double logSum = 0.0;
    int validCount = 0;
    for (const float luminance : luminances) {
        if (luminance > luminanceCap) {
            continue;
        }
        logSum += std::log(static_cast<double>(luminance));
        ++validCount;
    }

    if (validCount == 0) {
        return false;
    }

    const float geometricMean = static_cast<float>(std::exp(logSum / static_cast<double>(validCount)));
    *out = PhysicalCamera::forAverageLuminance(geometricMean);
    return true;
}

void PathTracer::setEnvironmentHdrPath(const QString& path)
{
    const QString normalized = path.trimmed();
    if (m_impl->environmentHdrPath == normalized) {
        return;
    }

    m_impl->environmentHdrPath = normalized;
    m_impl->environmentDirty.store(true);

    if (m_impl->configured.load() && m_impl->stream != nullptr) {
        QString error;
        std::lock_guard<std::mutex> streamLock(m_impl->streamMutex);
        if (!reloadEnvironmentMap(m_impl.get(), m_impl->stream, &error)) {
            if (!normalized.isEmpty()) {
                AppLog::instance().warning(
                    QStringLiteral("Environment map load failed: %1").arg(error));
            }
            cudaStreamSynchronize(m_impl->stream);
        }
        resetAccumulation();
    }
}

QString PathTracer::environmentHdrPath() const
{
    return m_impl->environmentHdrPath;
}

void PathTracer::rebuildMeshBoundsMesh(const QColor& boundsColor)
{
    m_impl->boundsMesh = meshAccelBuildBoundsMesh(m_impl->meshScene, boundsColor);
}

const MeshAccelBoundsMesh& PathTracer::meshBoundsMesh() const
{
    return m_impl->boundsMesh;
}

bool PathTracer::rebuildMeshScene(
    const std::vector<ProceduralInstance>& proceduralInstances,
    const MeshSceneBuildParams& meshParams)
{
    if (!m_impl->configured.load()) {
        return false;
    }

    QString error;
    if (!buildAndUploadMeshScene(m_impl.get(), proceduralInstances, meshParams, &error)) {
        AppLog::instance().error(QStringLiteral("PathTracer rebuild mesh scene: %1").arg(error));
        return false;
    }

    std::lock_guard<std::mutex> streamLock(m_impl->streamMutex);
    const cudaError_t syncError = cudaStreamSynchronize(m_impl->stream);
    if (syncError != cudaSuccess) {
        AppLog::instance().error(
            QStringLiteral("PathTracer rebuild mesh scene: stream sync failed: %1")
                .arg(cudaErrorString(syncError)));
        return false;
    }

    return true;
}

bool PathTracer::exportMeshSceneWavefrontObj(const QString& objFilePath, QString* errorMessage) const
{
    std::lock_guard<std::mutex> meshSceneLock(m_impl->meshSceneMutex);
    return m_impl->meshScene.exportWavefrontObj(objFilePath, errorMessage);
}

void PathTracer::notifyWorker()
{
    m_impl->workerCv.notify_all();
}

void PathTracer::renderLoop()
{
    cudaSetDevice(m_impl->cudaDeviceId);

    while (m_impl->running.load()) {
        if (m_impl->resetRequested.exchange(false)) {
            m_impl->displayBuffersValid.store(false);

            bool clearLaunched = false;
            {
                std::lock_guard<std::mutex> streamLock(m_impl->streamMutex);
                if (m_impl->acc.d_buffer != nullptr && m_impl->acc.d_samples != nullptr) {
                    QString clearError;
                    if (!clearAccumulator(m_impl.get(), &clearError)) {
                        AppLog::instance().error(
                            QStringLiteral("PathTracer accumulator reset failed: %1").arg(clearError));
                    } else if (!clearPreviewBuffers(m_impl.get(), &clearError)) {
                        AppLog::instance().error(
                            QStringLiteral("PathTracer preview buffer reset failed: %1").arg(clearError));
                    } else {
                        clearLaunched = true;
                    }
                }
            }

            if (clearLaunched) {
                const cudaError_t syncError = cudaStreamSynchronize(m_impl->stream);
                if (syncError != cudaSuccess) {
                    AppLog::instance().error(
                        QStringLiteral("CUDA stream sync after accumulator reset failed: %1")
                            .arg(cudaErrorString(syncError)));
                }
            }

            m_impl->sampleCount.store(0);
            m_impl->finestCompletedPreview.store(-1);

#ifdef PATHTRACER_PROFILE
            if (g_renderPipelineTiming.resetTimingActive.exchange(false)) {
                const auto resetDone = std::chrono::steady_clock::now();
                const auto resetLatencyMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    resetDone - g_renderPipelineTiming.resetRequestTime);
                AppLog::instance().info(
                    QStringLiteral("Render pipeline timing: reset latency %1 ms").arg(resetLatencyMs.count()));
            }
#endif
        }

        if (m_impl->acc.width <= 0 || m_impl->acc.height <= 0) {
            continue;
        }

        {
            std::unique_lock<std::mutex> lock(m_impl->workerMutex);
            m_impl->workerCv.wait(lock, [this]() {
                if (!m_impl->running.load()) {
                    return true;
                }
                if (m_impl->resetRequested.load()) {
                    return true;
                }
                return canTakeSample(m_impl.get());
            });
            if (!m_impl->running.load()) {
                break;
            }
        }

        if (!canTakeSample(m_impl.get())) {
            continue;
        }

        const int previewCount = m_impl->previewStepsPerLevel.load();
        const int iter = m_impl->sampleCount.load();
        const bool previewPass = previewCount > 0 && iter < previewCount &&
            iter < static_cast<int>(m_impl->previewLevels.size());

        QString cameraError;
        bool sampleLaunched = false;
        {
            std::lock_guard<std::mutex> streamLock(m_impl->streamMutex);

            if (m_impl->meshScene.isDeviceDirty()) {
                std::lock_guard<std::mutex> meshSceneLock(m_impl->meshSceneMutex);
                if (!m_impl->meshScene.upload(m_impl->stream)) {
                    AppLog::instance().error(QStringLiteral("PathTracer mesh scene upload failed"));
                    continue;
                }
            }

            if (!uploadCameraIfDirty(m_impl.get(), m_impl->stream, &cameraError)) {
                AppLog::instance().error(
                    QStringLiteral("PathTracer camera upload failed: %1").arg(cameraError));
                continue;
            }

            QString paramsError;
            if (!syncRenderParamsToDevice(m_impl.get(), m_impl->stream, previewPass, &paramsError)) {
                AppLog::instance().error(
                    QStringLiteral("PathTracer render params upload failed: %1").arg(paramsError));
                continue;
            }

            QString envError;
            if (!uploadEnvironmentIfDirty(m_impl.get(), m_impl->stream, &envError)) {
                AppLog::instance().error(
                    QStringLiteral("PathTracer environment upload failed: %1").arg(envError));
                continue;
            }

            const unsigned int globalSeed = static_cast<unsigned int>(iter) + 1u;
            const MeshAccelSceneGpu* deviceScene = m_impl->meshScene.deviceScene();
            const EnvironmentMapGpu* deviceEnv = m_impl->environmentMap.deviceMap();

            if (previewPass) {
                const PreviewLevelData& level = m_impl->previewLevels[static_cast<std::size_t>(iter)];
                sampleLaunched = pathTracerPreviewSample(
                    level.d_buffer,
                    level.width,
                    level.height,
                    m_impl->d_camera,
                    deviceScene,
                    deviceEnv,
                    m_impl->d_renderParams,
                    globalSeed,
                    m_impl->stream);
                if (!sampleLaunched) {
                    AppLog::instance().error(QStringLiteral("PathTracer preview sample kernel launch failed"));
                }
            } else {
                sampleLaunched = pathTracerSample(
                    m_impl->acc.d_buffer,
                    m_impl->acc.d_samples,
                    m_impl->acc.width,
                    m_impl->acc.height,
                    m_impl->d_camera,
                    deviceScene,
                    deviceEnv,
                    m_impl->d_renderParams,
                    globalSeed,
                    m_impl->stream);
                if (!sampleLaunched) {
                    AppLog::instance().error(QStringLiteral("PathTracer sample kernel launch failed"));
                }
            }

            if (sampleLaunched) {
                const cudaError_t recordError =
                    cudaEventRecord(m_impl->sampleCompleteEvent, m_impl->stream);
                if (recordError != cudaSuccess) {
                    AppLog::instance().error(
                        QStringLiteral("CUDA event record failed: %1").arg(cudaErrorString(recordError)));
                    sampleLaunched = false;
                }
            }
        }

        if (!sampleLaunched) {
            continue;
        }

        const auto gpuWaitStart = std::chrono::steady_clock::now();
        QString waitError;
        const SampleWaitResult waitResult = waitForSampleEvent(
            m_impl->sampleCompleteEvent,
            m_impl->running,
            m_impl->resetRequested,
            &waitError);
        const auto gpuWaitEnd = std::chrono::steady_clock::now();
        const auto gpuWaitUs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(gpuWaitEnd - gpuWaitStart).count());

        if (waitResult == SampleWaitResult::Error) {
            AppLog::instance().error(
                QStringLiteral("CUDA event wait failed: %1").arg(waitError));
            continue;
        }

        if (waitResult == SampleWaitResult::Stopped) {
            break;
        }

        if (waitResult == SampleWaitResult::ResetDuringWait) {
            continue;
        }

#ifdef PATHTRACER_PROFILE
        g_renderPipelineTiming.totalGpuWaitUs.fetch_add(gpuWaitUs);
#endif
        if (previewCount > 0 && iter < previewCount) {
            m_impl->finestCompletedPreview.store(iter);
        }
        m_impl->sampleCount.fetch_add(1);
        m_impl->displayBuffersValid.store(true);
#ifdef PATHTRACER_PROFILE
        g_renderPipelineTiming.completedSamples.fetch_add(1);
        logPipelineTimingIfDue();
#endif

        FrameReadyCallback callback;
        {
            std::lock_guard<std::mutex> lock(m_callbackMutex);
            callback = m_frameReadyCallback;
        }
        if (callback) {
            callback();
        }
    }
}
