#include "PathTracer.h"

#include "AppLog.h"
#include "CameraGpu.h"
#include "EnvironmentMap.h"
#include "PhysicalCamera.h"
#include "PathTracerSampleBudget.h"
#include "PathTracerAdaptiveSampling.h"
#include "MeshAccel/MeshAccelBoundsMesh.h"
#include "MeshAccel/MeshAccelScene.h"
#include "MeshAccel/MeshAccelIntersectCore.h"
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

#include <QRect>

struct AccumulatorData
{
    float4* d_buffer = nullptr;
    uint32_t* d_samples = nullptr;
    float* d_lumMean = nullptr;
    float* d_m2 = nullptr;
    uint8_t* d_converged = nullptr;
    int* d_activeIndices = nullptr;
    int* d_activeScratch = nullptr;
    int* d_activeCount = nullptr;
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
    cudaStream_t sampleStream = nullptr;
    cudaStream_t displayStream = nullptr;
    cudaEvent_t sampleCompleteEvent = nullptr;
    cudaEvent_t displayReadyEvent = nullptr;
    std::atomic<bool> displayPublishInFlight{false};
    std::atomic<int> displayPublishSlot{-1};

    std::thread worker;
    std::atomic<bool> running{false};
    std::atomic<bool> resetRequested{false};
    std::atomic<bool> regionResetRequested{false};
    std::atomic<bool> configured{false};

    std::mutex workerMutex;
    std::condition_variable workerCv;

    std::atomic<int> maxSamplesPerPixel{8};
    std::atomic<int> minSamples{16};
    std::atomic<float> relativeErrorThreshold{0.02f};
    std::atomic<int> debugOverlayMode{0};
    std::atomic<int> brdfDebugFlags{0};
    std::atomic<int> activePixelCount{0};
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
    std::atomic<bool> regionEnabled{false};
    std::atomic<int> regionMinX{0};
    std::atomic<int> regionMinY{0};
    std::atomic<int> regionMaxX{0};
    std::atomic<int> regionMaxY{0};
    std::atomic<bool> renderParamsDirty{true};

    CameraGpu lastSampleCamera{};
    std::mutex lastSampleCameraMutex;

    std::mutex meshSceneMutex;
    std::mutex streamMutex;
};

namespace {

constexpr int kMaxSamplesUpperBound = 1'000'000;
constexpr int kMaxPreviewStepsPerLevel = 128;
constexpr int kMinMinSamples = 1;
constexpr int kMaxMinSamples = 10'000;
constexpr float kMinRelativeErrorThreshold = 0.001f;
constexpr float kMaxRelativeErrorThreshold = 1.0f;
constexpr int kCompactActiveListInterval = kAdaptiveCompactActiveListInterval;

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

int clampMinSamples(int value)
{
    if (value < kMinMinSamples) {
        return kMinMinSamples;
    }
    if (value > kMaxMinSamples) {
        return kMaxMinSamples;
    }
    return value;
}

float clampRelativeErrorThreshold(float value)
{
    if (value < kMinRelativeErrorThreshold) {
        return kMinRelativeErrorThreshold;
    }
    if (value > kMaxRelativeErrorThreshold) {
        return kMaxRelativeErrorThreshold;
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
    if (acc->d_lumMean != nullptr) {
        cudaFree(acc->d_lumMean);
        acc->d_lumMean = nullptr;
    }
    if (acc->d_m2 != nullptr) {
        cudaFree(acc->d_m2);
        acc->d_m2 = nullptr;
    }
    if (acc->d_converged != nullptr) {
        cudaFree(acc->d_converged);
        acc->d_converged = nullptr;
    }
    if (acc->d_activeIndices != nullptr) {
        cudaFree(acc->d_activeIndices);
        acc->d_activeIndices = nullptr;
    }
    if (acc->d_activeScratch != nullptr) {
        cudaFree(acc->d_activeScratch);
        acc->d_activeScratch = nullptr;
    }
    if (acc->d_activeCount != nullptr) {
        cudaFree(acc->d_activeCount);
        acc->d_activeCount = nullptr;
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

    error = cudaMalloc(&acc->d_lumMean, pixelCount * sizeof(float));
    if (error != cudaSuccess) {
        freeAccumulator(acc);
        if (outError != nullptr) {
            *outError = cudaErrorString(error);
        }
        return false;
    }

    error = cudaMalloc(&acc->d_m2, pixelCount * sizeof(float));
    if (error != cudaSuccess) {
        freeAccumulator(acc);
        if (outError != nullptr) {
            *outError = cudaErrorString(error);
        }
        return false;
    }

    error = cudaMalloc(&acc->d_converged, pixelCount * sizeof(uint8_t));
    if (error != cudaSuccess) {
        freeAccumulator(acc);
        if (outError != nullptr) {
            *outError = cudaErrorString(error);
        }
        return false;
    }

    error = cudaMalloc(&acc->d_activeIndices, pixelCount * sizeof(int));
    if (error != cudaSuccess) {
        freeAccumulator(acc);
        if (outError != nullptr) {
            *outError = cudaErrorString(error);
        }
        return false;
    }

    error = cudaMalloc(&acc->d_activeScratch, pixelCount * sizeof(int));
    if (error != cudaSuccess) {
        freeAccumulator(acc);
        if (outError != nullptr) {
            *outError = cudaErrorString(error);
        }
        return false;
    }

    error = cudaMalloc(&acc->d_activeCount, sizeof(int));
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
    if (impl == nullptr || impl->sampleStream == nullptr) {
        return true;
    }

    for (const PreviewLevelData& level : impl->previewLevels) {
        if (level.d_buffer == nullptr || level.width <= 0 || level.height <= 0) {
            continue;
        }

        const std::size_t byteCount =
            static_cast<std::size_t>(level.width) * static_cast<std::size_t>(level.height) * sizeof(float4);
        const cudaError_t error = cudaMemsetAsync(level.d_buffer, 0, byteCount, impl->sampleStream);
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
        impl->acc.d_lumMean == nullptr || impl->acc.d_m2 == nullptr || impl->acc.d_converged == nullptr ||
        impl->acc.d_activeIndices == nullptr || impl->acc.d_activeScratch == nullptr ||
        impl->acc.d_activeCount == nullptr || impl->acc.width <= 0 || impl->acc.height <= 0 ||
        impl->sampleStream == nullptr) {
        return true;
    }

    if (!pathTracerClearAccumulator(
            impl->acc.d_buffer,
            impl->acc.d_samples,
            impl->acc.d_lumMean,
            impl->acc.d_m2,
            impl->acc.d_converged,
            impl->acc.d_activeIndices,
            impl->acc.d_activeScratch,
            impl->acc.d_activeCount,
            impl->acc.width,
            impl->acc.height,
            impl->sampleStream)) {
        if (outError != nullptr) {
            *outError = QStringLiteral("clear accumulator kernel launch failed");
        }
        return false;
    }

    const int pixelCount = impl->acc.width * impl->acc.height;
    impl->activePixelCount.store(pixelCount);
    return true;
}

bool clearRegionAccumulator(PathTracerDetail::PathTracerImpl* impl, QString* outError)
{
    if (impl == nullptr || impl->acc.d_buffer == nullptr || impl->acc.d_samples == nullptr ||
        impl->acc.d_lumMean == nullptr || impl->acc.d_m2 == nullptr || impl->acc.d_converged == nullptr ||
        impl->acc.d_activeIndices == nullptr || impl->acc.d_activeCount == nullptr || impl->acc.width <= 0 ||
        impl->acc.height <= 0 || impl->sampleStream == nullptr) {
        return true;
    }

    const int minX = impl->regionMinX.load();
    const int minY = impl->regionMinY.load();
    const int maxX = impl->regionMaxX.load();
    const int maxY = impl->regionMaxY.load();

    if (!pathTracerClearRegionAccumulator(
            impl->acc.d_buffer,
            impl->acc.d_samples,
            impl->acc.d_lumMean,
            impl->acc.d_m2,
            impl->acc.d_converged,
            impl->acc.d_activeIndices,
            impl->acc.d_activeCount,
            impl->acc.width,
            impl->acc.height,
            minX,
            minY,
            maxX,
            maxY,
            impl->sampleStream)) {
        if (outError != nullptr) {
            *outError = QStringLiteral("clear region accumulator kernel launch failed");
        }
        return false;
    }

    const int regionW = maxX - minX + 1;
    const int regionH = maxY - minY + 1;
    impl->activePixelCount.store(regionW * regionH);
    return true;
}

bool rebuildFullActiveList(PathTracerDetail::PathTracerImpl* impl, QString* outError)
{
    if (impl == nullptr || impl->acc.d_converged == nullptr || impl->acc.d_samples == nullptr ||
        impl->acc.d_activeIndices == nullptr || impl->acc.d_activeCount == nullptr || impl->acc.width <= 0 ||
        impl->acc.height <= 0 || impl->sampleStream == nullptr) {
        return true;
    }

    if (!pathTracerRebuildActiveList(
            impl->acc.d_converged,
            impl->acc.d_samples,
            impl->acc.d_activeIndices,
            impl->acc.d_activeCount,
            impl->acc.width,
            impl->acc.height,
            impl->maxSamplesPerPixel.load(),
            impl->sampleStream)) {
        if (outError != nullptr) {
            *outError = QStringLiteral("rebuild active list kernel launch failed");
        }
        return false;
    }

    int activeCount = 0;
    const cudaError_t countError = cudaMemcpyAsync(
        &activeCount,
        impl->acc.d_activeCount,
        sizeof(int),
        cudaMemcpyDeviceToHost,
        impl->sampleStream);
    if (countError != cudaSuccess) {
        if (outError != nullptr) {
            *outError = QStringLiteral("active count readback failed after rebuild");
        }
        return false;
    }

    const cudaError_t syncError = cudaStreamSynchronize(impl->sampleStream);
    if (syncError != cudaSuccess) {
        if (outError != nullptr) {
            *outError = QStringLiteral("stream sync failed after rebuild active list");
        }
        return false;
    }

    impl->activePixelCount.store(activeCount);
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
    impl->hostRenderParams.minSamples = clampMinSamples(impl->minSamples.load());
    impl->hostRenderParams.maxSamplesPerPixel = clampMaxSamples(impl->maxSamplesPerPixel.load());
    impl->hostRenderParams.relativeErrorThreshold = clampRelativeErrorThreshold(impl->relativeErrorThreshold.load());
    impl->hostRenderParams.debugOverlayMode = impl->debugOverlayMode.load();
    impl->hostRenderParams.brdfDebugFlags = impl->brdfDebugFlags.load();
    impl->deviceParamsArePreview = false;
    impl->renderParamsDirty.store(true);
    return true;
}

bool syncRenderParamsToDevice(
    PathTracerDetail::PathTracerImpl* impl,
    cudaStream_t stream,
    bool previewPass,
    QString* outError,
    bool force = false)
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
    } else if (!force && !impl->renderParamsDirty.load() && !impl->deviceParamsArePreview) {
        return true;
    }

    RenderParamsGpu deviceParams = impl->hostRenderParams;
    deviceParams.minSamples = clampMinSamples(impl->minSamples.load());
    deviceParams.maxSamplesPerPixel = clampMaxSamples(impl->maxSamplesPerPixel.load());
    deviceParams.relativeErrorThreshold = clampRelativeErrorThreshold(impl->relativeErrorThreshold.load());
    deviceParams.debugOverlayMode = impl->debugOverlayMode.load();
    deviceParams.brdfDebugFlags = impl->brdfDebugFlags.load();
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

void destroyStreams(PathTracerDetail::PathTracerImpl* impl)
{
    if (impl == nullptr) {
        return;
    }

    if (impl->sampleStream != nullptr) {
        cudaStreamDestroy(impl->sampleStream);
        impl->sampleStream = nullptr;
    }
    if (impl->displayStream != nullptr) {
        cudaStreamDestroy(impl->displayStream);
        impl->displayStream = nullptr;
    }
}

void destroySampleEvent(PathTracerDetail::PathTracerImpl* impl)
{
    if (impl == nullptr || impl->sampleCompleteEvent == nullptr) {
        return;
    }

    cudaEventDestroy(impl->sampleCompleteEvent);
    impl->sampleCompleteEvent = nullptr;
}

void destroyDisplayReadyEvent(PathTracerDetail::PathTracerImpl* impl)
{
    if (impl == nullptr || impl->displayReadyEvent == nullptr) {
        return;
    }

    cudaEventDestroy(impl->displayReadyEvent);
    impl->displayReadyEvent = nullptr;
}

bool ensureDisplayReadyEvent(PathTracerDetail::PathTracerImpl* impl, QString* outError)
{
    if (impl == nullptr) {
        return false;
    }

    if (impl->displayReadyEvent != nullptr) {
        return true;
    }

    const cudaError_t error = cudaEventCreate(&impl->displayReadyEvent);
    if (error != cudaSuccess && outError != nullptr) {
        *outError = cudaErrorString(error);
    }
    return error == cudaSuccess;
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
    const int previewSteps = impl->previewStepsPerLevel.load();
    const int sampleCount = impl->sampleCount.load();
    if (sampleCount < previewSteps) {
        return canTakeSampleAtIteration(
            sampleCount,
            previewSteps,
            impl->maxSamplesPerPixel.load());
    }
    return impl->activePixelCount.load() > 0;
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
    const std::atomic<bool>& regionResetRequested,
    QString* outError)
{
    bool resetSeen = false;
    for (;;) {
        if (!running.load()) {
            return SampleWaitResult::Stopped;
        }
        if (resetRequested.load() || regionResetRequested.load()) {
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
    if (impl == nullptr || impl->sampleStream == nullptr) {
        if (outError != nullptr) {
            *outError = QStringLiteral("PathTracer not initialized");
        }
        return false;
    }

    std::lock_guard<std::mutex> meshSceneLock(impl->meshSceneMutex);

    QString meshError;
    if (!meshSceneBuild(proceduralInstances, impl->meshScene, meshParams, &meshError)) {
        if (outError != nullptr) {
            *outError = meshError.isEmpty()
                ? QStringLiteral("Manifold mesh scene build failed")
                : meshError;
        }
        return false;
    }

    if (!impl->meshScene.allocate()) {
        if (outError != nullptr) {
            *outError = QStringLiteral("Mesh accel scene allocation failed");
        }
        return false;
    }

    if (!impl->meshScene.upload(impl->sampleStream)) {
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
    destroyStreams(m_impl.get());
    destroyDisplayReadyEvent(m_impl.get());
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

    if (m_impl->sampleStream == nullptr) {
        const cudaError_t streamError = cudaStreamCreate(&m_impl->sampleStream);
        if (streamError != cudaSuccess) {
            AppLog::instance().error(
                QStringLiteral("PathTracer configure: sample stream creation failed: %1").arg(cudaErrorString(streamError)));
            return false;
        }
    }

    if (m_impl->displayStream == nullptr) {
        const cudaError_t streamError = cudaStreamCreate(&m_impl->displayStream);
        if (streamError != cudaSuccess) {
            AppLog::instance().error(
                QStringLiteral("PathTracer configure: display stream creation failed: %1").arg(cudaErrorString(streamError)));
            return false;
        }
    }

    QString error;
    if (!ensureSampleEvent(m_impl.get(), &error)) {
        AppLog::instance().error(
            QStringLiteral("PathTracer configure: sample event creation failed: %1").arg(error));
        return false;
    }

    if (!ensureDisplayReadyEvent(m_impl.get(), &error)) {
        AppLog::instance().error(
            QStringLiteral("PathTracer configure: display event creation failed: %1").arg(error));
        return false;
    }

    m_impl->displayPublishInFlight.store(false);
    m_impl->displayPublishSlot.store(-1);

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

    if (!reloadEnvironmentMap(m_impl.get(), m_impl->sampleStream, &error)) {
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

    const cudaError_t syncError = cudaStreamSynchronize(m_impl->sampleStream);
    if (syncError != cudaSuccess) {
        AppLog::instance().error(
            QStringLiteral("PathTracer configure: sample stream sync failed: %1").arg(cudaErrorString(syncError)));
        unregisterPboResources(m_impl.get());
        freeCameraGpu(m_impl.get());
        freeAccumulator(&m_impl->acc);
        freePreviewBuffers(&m_impl->previewLevels);
        releaseMeshScene(m_impl.get());
        return false;
    }

    if (m_impl->displayStream != nullptr) {
        const cudaError_t displaySyncError = cudaStreamSynchronize(m_impl->displayStream);
        if (displaySyncError != cudaSuccess) {
            AppLog::instance().error(
                QStringLiteral("PathTracer configure: display stream sync failed: %1").arg(cudaErrorString(displaySyncError)));
            unregisterPboResources(m_impl.get());
            freeCameraGpu(m_impl.get());
            freeAccumulator(&m_impl->acc);
            freePreviewBuffers(&m_impl->previewLevels);
            releaseMeshScene(m_impl.get());
            return false;
        }
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

void PathTracer::resetRegionAccumulation()
{
    m_impl->regionResetRequested.store(true);
    notifyWorker();
}

void PathTracer::setRegionRenderEnabled(bool enabled)
{
    m_impl->regionEnabled.store(enabled);
    notifyWorker();
}

bool PathTracer::regionRenderEnabled() const
{
    return m_impl->regionEnabled.load();
}

void PathTracer::setRenderRegion(int minX, int minY, int maxX, int maxY)
{
    const int width = m_impl->acc.width;
    const int height = m_impl->acc.height;
    const int maxCoordX = (std::max)(0, width - 1);
    const int maxCoordY = (std::max)(0, height - 1);

    const int left = (std::max)(0, (std::min)(minX, maxX));
    const int right = (std::min)(maxCoordX, (std::max)(minX, maxX));
    const int top = (std::max)(0, (std::min)(minY, maxY));
    const int bottom = (std::min)(maxCoordY, (std::max)(minY, maxY));

    m_impl->regionMinX.store(left);
    m_impl->regionMinY.store(top);
    m_impl->regionMaxX.store(right);
    m_impl->regionMaxY.store(bottom);
}

QRect PathTracer::renderRegion() const
{
    return QRect(
        QPoint(m_impl->regionMinX.load(), m_impl->regionMinY.load()),
        QPoint(m_impl->regionMaxX.load(), m_impl->regionMaxY.load()));
}

bool PathTracer::isRunning() const
{
    return m_impl->running.load();
}

bool launchDisplayPublishLocked(
    PathTracerDetail::PathTracerImpl* impl,
    int slot,
    QString* outError)
{
    if (impl == nullptr || slot < 0 || slot > 1) {
        if (outError != nullptr) {
            *outError = QStringLiteral("invalid display publish slot");
        }
        return false;
    }

    const int overlayMode = impl->debugOverlayMode.load();
    const bool uvMode = overlayMode == static_cast<int>(RenderViewOverlayMode::Uv);

    if (!uvMode && (!impl->displayBuffersValid.load() || !hasDisplayableContent(*impl))) {
        return false;
    }

    if (impl->acc.width <= 0 || impl->acc.height <= 0) {
        return false;
    }

    cudaGraphicsResource_t resource = impl->pboResources[slot];
    if (resource == nullptr || impl->sampleCompleteEvent == nullptr || impl->displayStream == nullptr ||
        impl->displayReadyEvent == nullptr) {
        if (outError != nullptr) {
            *outError = QStringLiteral("display publish resources not ready");
        }
        return false;
    }

    if (!uvMode) {
        const cudaError_t waitError =
            cudaStreamWaitEvent(impl->displayStream, impl->sampleCompleteEvent, 0);
        if (waitError != cudaSuccess) {
            if (outError != nullptr) {
                *outError = cudaErrorString(waitError);
            }
            return false;
        }
    }

    QString paramsError;
    const bool forceParamsSync = impl->renderParamsDirty.load();
    if (!syncRenderParamsToDevice(impl, impl->displayStream, false, &paramsError, forceParamsSync)) {
        if (outError != nullptr) {
            *outError = paramsError;
        }
        return false;
    }

    QString cameraError;
    if (!uploadCameraIfDirty(impl, impl->displayStream, &cameraError)) {
        if (outError != nullptr) {
            *outError = cameraError;
        }
        return false;
    }

    void* devicePointer = nullptr;
    if (!mapPboResource(resource, impl->displayStream, &devicePointer, outError)) {
        return false;
    }

    const int previewCount = impl->previewStepsPerLevel.load();
    const int iter = impl->sampleCount.load();
    const int finest = impl->finestCompletedPreview.load();

    const bool showPreview =
        !uvMode &&
        !impl->regionEnabled.load() &&
        previewCount > 0 &&
        finest >= 0 &&
        finest < static_cast<int>(impl->previewLevels.size()) &&
        iter <= previewCount;

    float exposure = 1.0f;
    {
        std::lock_guard<std::mutex> lock(impl->cameraMutex);
        exposure = impl->hostCamera.exposureMultiplier();
    }

    bool copied = false;
    if (uvMode) {
        copied = pathTracerWriteUvDebugToPbo(
            impl->d_camera,
            impl->meshScene.deviceScene(),
            static_cast<uchar4*>(devicePointer),
            impl->acc.width,
            impl->acc.height,
            impl->d_renderParams,
            impl->displayStream);
    } else if (showPreview) {
        const PreviewLevelData& level = impl->previewLevels[static_cast<std::size_t>(finest)];
        copied = pathTracerUpsamplePreviewToPbo(
            level.d_buffer,
            level.width,
            level.height,
            level.downscale,
            static_cast<uchar4*>(devicePointer),
            impl->acc.width,
            impl->acc.height,
            impl->d_renderParams,
            exposure,
            impl->displayStream);
    } else {
        copied = pathTracerCopyToPbo(
            impl->acc.d_buffer,
            impl->acc.d_samples,
            impl->acc.d_converged,
            static_cast<uchar4*>(devicePointer),
            impl->acc.width,
            impl->acc.height,
            impl->d_renderParams,
            exposure,
            impl->displayStream);
    }

    if (!copied) {
        unmapPboResource(resource, impl->displayStream, nullptr);
        if (outError != nullptr) {
            *outError = QStringLiteral("copy-to-PBO kernel launch failed");
        }
        return false;
    }

    if (!unmapPboResource(resource, impl->displayStream, outError)) {
        return false;
    }

    const cudaError_t recordError = cudaEventRecord(impl->displayReadyEvent, impl->displayStream);
    if (recordError != cudaSuccess) {
        if (outError != nullptr) {
            *outError = cudaErrorString(recordError);
        }
        return false;
    }

    return true;
}

void PathTracer::releaseOutputSurfaces()
{
    stop();
    unregisterPboResources(m_impl.get());
    m_impl->configured.store(false);
}

bool PathTracer::beginDisplayPublish(int slot)
{
    if (slot < 0 || slot > 1 || !m_impl->configured.load()) {
        return false;
    }

    if (m_impl->displayPublishInFlight.load()) {
        return false;
    }

    const auto mutexWaitStart = std::chrono::steady_clock::now();
    QString error;
    {
        std::lock_guard<std::mutex> streamLock(m_impl->streamMutex);
        if (m_impl->displayPublishInFlight.load()) {
            return false;
        }

        if (!launchDisplayPublishLocked(m_impl.get(), slot, &error)) {
            if (!error.isEmpty()) {
                AppLog::instance().error(QStringLiteral("Display publish enqueue failed: %1").arg(error));
            }
            return false;
        }

        m_impl->displayPublishInFlight.store(true);
        m_impl->displayPublishSlot.store(slot);
    }
    const auto mutexWaitEnd = std::chrono::steady_clock::now();
    const auto mutexWaitUs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(mutexWaitEnd - mutexWaitStart).count());
#ifdef PATHTRACER_PROFILE
    g_renderPipelineTiming.totalPublishMutexWaitUs.fetch_add(mutexWaitUs);
    g_renderPipelineTiming.publishMutexWaitSamples.fetch_add(1);
#endif

    return true;
}

bool PathTracer::isDisplayPublishReady() const
{
    if (!m_impl->displayPublishInFlight.load() || m_impl->displayReadyEvent == nullptr) {
        return false;
    }

    const cudaError_t queryError = cudaEventQuery(m_impl->displayReadyEvent);
    return queryError == cudaSuccess;
}

bool PathTracer::hasDisplayPublishInFlight() const
{
    return m_impl->displayPublishInFlight.load();
}

int PathTracer::finishDisplayPublish()
{
    if (!m_impl->displayPublishInFlight.load()) {
        return -1;
    }

    if (!isDisplayPublishReady()) {
        return -1;
    }

    const int slot = m_impl->displayPublishSlot.load();
    m_impl->displayPublishInFlight.store(false);
    m_impl->displayPublishSlot.store(-1);
    return slot;
}

bool PathTracer::publishDisplayFrame(int slot)
{
    if (!beginDisplayPublish(slot)) {
        return false;
    }

    for (;;) {
        if (isDisplayPublishReady()) {
            return finishDisplayPublish() >= 0;
        }
        std::this_thread::yield();
    }
}

void PathTracer::setMaxSamplesPerPixel(int max)
{
    const int clamped = clampMaxSamples(max);
    m_impl->maxSamplesPerPixel.store(clamped);
    m_impl->hostRenderParams.maxSamplesPerPixel = clamped;
    m_impl->renderParamsDirty.store(true);
    notifyWorker();
}

int PathTracer::maxSamplesPerPixel() const
{
    return m_impl->maxSamplesPerPixel.load();
}

void PathTracer::setMinSamples(int min)
{
    const int clamped = clampMinSamples(min);
    m_impl->minSamples.store(clamped);
    m_impl->hostRenderParams.minSamples = clamped;
    m_impl->renderParamsDirty.store(true);
    if (m_impl->configured.load()) {
        resetAccumulation();
    }
    notifyWorker();
}

int PathTracer::minSamples() const
{
    return m_impl->minSamples.load();
}

void PathTracer::setRelativeErrorThreshold(float threshold)
{
    const float clamped = clampRelativeErrorThreshold(threshold);
    m_impl->relativeErrorThreshold.store(clamped);
    m_impl->hostRenderParams.relativeErrorThreshold = clamped;
    m_impl->renderParamsDirty.store(true);
    if (m_impl->configured.load()) {
        resetAccumulation();
    }
    notifyWorker();
}

float PathTracer::relativeErrorThreshold() const
{
    return m_impl->relativeErrorThreshold.load();
}

void PathTracer::setDebugOverlayMode(int mode)
{
    if (mode < 0) {
        mode = 0;
    }
    if (mode > 3) {
        mode = 3;
    }
    m_impl->debugOverlayMode.store(mode);
    m_impl->hostRenderParams.debugOverlayMode = mode;
    m_impl->renderParamsDirty.store(true);
    notifyWorker();
}

int PathTracer::debugOverlayMode() const
{
    return m_impl->debugOverlayMode.load();
}

void PathTracer::setBrdfDebugFlags(int flags)
{
    m_impl->brdfDebugFlags.store(flags);
    m_impl->hostRenderParams.brdfDebugFlags = flags;
    m_impl->renderParamsDirty.store(true);
    notifyWorker();
}

int PathTracer::brdfDebugFlags() const
{
    return m_impl->brdfDebugFlags.load();
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
            const cudaError_t syncError = cudaStreamSynchronize(m_impl->sampleStream);
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

int PathTracer::currentActivePixelCount() const
{
    return m_impl->activePixelCount.load();
}

int PathTracer::sampleBudgetTotalIterations() const
{
    return ::sampleBudgetTotalIterations(
        m_impl->previewStepsPerLevel.load(),
        m_impl->maxSamplesPerPixel.load());
}

bool PathTracer::isSampleBudgetExhausted() const
{
    const int previewSteps = m_impl->previewStepsPerLevel.load();
    const int sampleCount = m_impl->sampleCount.load();
    if (sampleCount < previewSteps) {
        return ::isSampleBudgetExhausted(
            sampleCount,
            previewSteps,
            m_impl->maxSamplesPerPixel.load());
    }
    return isAdaptiveSampleBudgetExhausted(
        m_impl->activePixelCount.load(),
        previewSteps,
        sampleCount);
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
    m_impl->cameraDirty.store(true);
}

void PathTracer::setFocusPoint(const glm::vec3& worldPoint)
{
    {
        std::lock_guard<std::mutex> lock(m_impl->cameraMutex);
        m_impl->hostCamera.setFocusPoint(worldPoint);
    }
    m_impl->cameraDirty.store(true);

    if (m_impl->configured.load()) {
        resetAccumulation();
    }
    notifyWorker();
}

void PathTracer::clearFocusPoint()
{
    {
        std::lock_guard<std::mutex> lock(m_impl->cameraMutex);
        m_impl->hostCamera.clearFocusPoint();
    }
    m_impl->cameraDirty.store(true);

    if (m_impl->configured.load()) {
        resetAccumulation();
    }
    notifyWorker();
}

bool PathTracer::pickSurface(const glm::vec3& ro, const glm::vec3& rd, glm::vec3* hitPoint) const
{
    if (hitPoint == nullptr || m_impl == nullptr) {
        return false;
    }

    std::lock_guard<std::mutex> meshSceneLock(m_impl->meshSceneMutex);
    const MeshAccelSceneGpu* scene = m_impl->meshScene.hostScene();
    if (scene == nullptr) {
        return false;
    }

    const glm::vec3 normalizedRd = glm::normalize(rd);
    const Vec3 roVec = vecMake3(ro.x, ro.y, ro.z);
    const Vec3 rdVec = vecMake3(normalizedRd.x, normalizedRd.y, normalizedRd.z);
    const MeshHit hit = meshAccelTraceRay(roVec, rdVec, scene, 0.001f, 1.0e30f);
    if (!hit.hit) {
        return false;
    }

    *hitPoint = ro + normalizedRd * hit.t;
    return true;
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

    cudaSetDevice(m_impl->cudaDeviceId);

    const int width = m_impl->acc.width;
    const int height = m_impl->acc.height;
    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

    std::vector<float4> hostBuffer(pixelCount);
    std::vector<uint32_t> hostCounts(pixelCount);

    {
        std::lock_guard<std::mutex> streamLock(m_impl->streamMutex);
        if (m_impl->displayPublishInFlight.load() && m_impl->displayReadyEvent != nullptr) {
            const cudaError_t displayWaitError = cudaEventSynchronize(m_impl->displayReadyEvent);
            if (displayWaitError != cudaSuccess) {
                return false;
            }
        } else if (m_impl->sampleCompleteEvent != nullptr) {
            const cudaError_t waitError =
                cudaStreamWaitEvent(m_impl->sampleStream, m_impl->sampleCompleteEvent, 0);
            if (waitError != cudaSuccess) {
                return false;
            }
            const cudaError_t syncError = cudaStreamSynchronize(m_impl->sampleStream);
            if (syncError != cudaSuccess) {
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

    constexpr int kSampleStride = 16;
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

    if (m_impl->configured.load() && m_impl->sampleStream != nullptr) {
        QString error;
        std::lock_guard<std::mutex> streamLock(m_impl->streamMutex);
        if (!reloadEnvironmentMap(m_impl.get(), m_impl->sampleStream, &error)) {
            if (!normalized.isEmpty()) {
                AppLog::instance().warning(
                    QStringLiteral("Environment map load failed: %1").arg(error));
            }
            cudaStreamSynchronize(m_impl->sampleStream);
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
    const cudaError_t syncError = cudaStreamSynchronize(m_impl->sampleStream);
    if (syncError != cudaSuccess) {
        AppLog::instance().error(
            QStringLiteral("PathTracer rebuild mesh scene: sample stream sync failed: %1")
                .arg(cudaErrorString(syncError)));
        return false;
    }

    if (m_impl->displayStream != nullptr) {
        const cudaError_t displaySyncError = cudaStreamSynchronize(m_impl->displayStream);
        if (displaySyncError != cudaSuccess) {
            AppLog::instance().error(
                QStringLiteral("PathTracer rebuild mesh scene: display stream sync failed: %1")
                    .arg(cudaErrorString(displaySyncError)));
            return false;
        }
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

void PathTracer::invokeFrameReadyCallback()
{
    FrameReadyCallback callback;
    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        callback = m_frameReadyCallback;
    }
    if (callback) {
        callback();
    }
}

void PathTracer::renderLoop()
{
    cudaSetDevice(m_impl->cudaDeviceId);

    while (m_impl->running.load()) {
        const bool fullResetRequested = m_impl->resetRequested.exchange(false);
        const bool regionOnlyResetRequested = m_impl->regionResetRequested.exchange(false);

        if (fullResetRequested || regionOnlyResetRequested) {
            bool invalidateDisplay = false;

            bool clearLaunched = false;
            {
                std::lock_guard<std::mutex> streamLock(m_impl->streamMutex);
                if (m_impl->acc.d_buffer != nullptr && m_impl->acc.d_samples != nullptr) {
                    QString clearError;
                    if (fullResetRequested) {
                        if (m_impl->regionEnabled.load()) {
                            if (!clearRegionAccumulator(m_impl.get(), &clearError)) {
                                AppLog::instance().error(
                                    QStringLiteral("PathTracer region accumulator reset failed: %1").arg(clearError));
                            } else {
                                clearLaunched = true;
                            }
                        } else if (!clearAccumulator(m_impl.get(), &clearError)) {
                            AppLog::instance().error(
                                QStringLiteral("PathTracer accumulator reset failed: %1").arg(clearError));
                        } else if (!clearPreviewBuffers(m_impl.get(), &clearError)) {
                            AppLog::instance().error(
                                QStringLiteral("PathTracer preview buffer reset failed: %1").arg(clearError));
                        } else {
                            clearLaunched = true;
                            invalidateDisplay = true;
                        }
                    } else if (regionOnlyResetRequested) {
                        if (m_impl->regionEnabled.load()) {
                            if (!clearRegionAccumulator(m_impl.get(), &clearError)) {
                                AppLog::instance().error(
                                    QStringLiteral("PathTracer region accumulator reset failed: %1").arg(clearError));
                            } else {
                                clearLaunched = true;
                            }
                        } else if (!rebuildFullActiveList(m_impl.get(), &clearError)) {
                            AppLog::instance().error(
                                QStringLiteral("PathTracer active list rebuild failed: %1").arg(clearError));
                        } else {
                            clearLaunched = true;
                        }
                    }
                }
            }

            if (clearLaunched) {
                const cudaError_t syncError = cudaStreamSynchronize(m_impl->sampleStream);
                if (syncError != cudaSuccess) {
                    AppLog::instance().error(
                        QStringLiteral("CUDA stream sync after accumulator reset failed: %1")
                            .arg(cudaErrorString(syncError)));
                } else if (!invalidateDisplay && m_impl->sampleCount.load() > 0) {
                    invokeFrameReadyCallback();
                }
            }

            if (invalidateDisplay) {
                m_impl->displayBuffersValid.store(false);
            }

            if (fullResetRequested) {
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
                if (m_impl->resetRequested.load() || m_impl->regionResetRequested.load()) {
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
        const bool previewPass = !m_impl->regionEnabled.load() && previewCount > 0 && iter < previewCount &&
            iter < static_cast<int>(m_impl->previewLevels.size());

        QString cameraError;
        bool sampleLaunched = false;
        bool pendingActiveListUpdate = false;
        bool pendingActiveIndicesSwap = false;
        int pendingNewActiveCount = 0;
        {
            std::lock_guard<std::mutex> streamLock(m_impl->streamMutex);

            if (m_impl->displayPublishInFlight.load() && m_impl->displayReadyEvent != nullptr) {
                const cudaError_t displayWaitError =
                    cudaStreamWaitEvent(m_impl->sampleStream, m_impl->displayReadyEvent, 0);
                if (displayWaitError != cudaSuccess) {
                    AppLog::instance().error(
                        QStringLiteral("CUDA stream wait on display event failed: %1")
                            .arg(cudaErrorString(displayWaitError)));
                    continue;
                }
            }

            if (m_impl->meshScene.isDeviceDirty()) {
                std::lock_guard<std::mutex> meshSceneLock(m_impl->meshSceneMutex);
                if (!m_impl->meshScene.upload(m_impl->sampleStream)) {
                    AppLog::instance().error(QStringLiteral("PathTracer mesh scene upload failed"));
                    continue;
                }
            }

            if (!uploadCameraIfDirty(m_impl.get(), m_impl->sampleStream, &cameraError)) {
                AppLog::instance().error(
                    QStringLiteral("PathTracer camera upload failed: %1").arg(cameraError));
                continue;
            }

            QString paramsError;
            if (!syncRenderParamsToDevice(m_impl.get(), m_impl->sampleStream, previewPass, &paramsError)) {
                AppLog::instance().error(
                    QStringLiteral("PathTracer render params upload failed: %1").arg(paramsError));
                continue;
            }

            QString envError;
            if (!uploadEnvironmentIfDirty(m_impl.get(), m_impl->sampleStream, &envError)) {
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
                    m_impl->sampleStream);
                if (!sampleLaunched) {
                    AppLog::instance().error(QStringLiteral("PathTracer preview sample kernel launch failed"));
                }
            } else {
                const int activeCount = m_impl->activePixelCount.load();
                const int fullResWave = iter - previewCount + 1;
                const bool runCompact =
                    activeCount > 0 &&
                    (fullResWave % kCompactActiveListInterval) == 0;

                if (activeCount > 0) {
                    sampleLaunched = pathTracerAdaptiveSample(
                        m_impl->acc.d_buffer,
                        m_impl->acc.d_samples,
                        m_impl->acc.d_lumMean,
                        m_impl->acc.d_m2,
                        m_impl->acc.d_converged,
                        m_impl->acc.d_activeIndices,
                        activeCount,
                        m_impl->acc.width,
                        m_impl->acc.height,
                        m_impl->d_camera,
                        deviceScene,
                        deviceEnv,
                        m_impl->d_renderParams,
                        globalSeed,
                        m_impl->sampleStream);
                    if (!sampleLaunched) {
                        AppLog::instance().error(QStringLiteral("PathTracer adaptive sample kernel launch failed"));
                    } else if (runCompact) {
                        const int maxSamples = m_impl->maxSamplesPerPixel.load();
                        const bool useRegionFilter = m_impl->regionEnabled.load();
                        if (!pathTracerCompactActiveList(
                                m_impl->acc.d_activeIndices,
                                m_impl->acc.d_activeScratch,
                                m_impl->acc.d_activeCount,
                                m_impl->acc.d_converged,
                                m_impl->acc.d_samples,
                                maxSamples,
                                activeCount,
                                m_impl->acc.width,
                                m_impl->acc.height,
                                useRegionFilter,
                                m_impl->regionMinX.load(),
                                m_impl->regionMinY.load(),
                                m_impl->regionMaxX.load(),
                                m_impl->regionMaxY.load(),
                                m_impl->sampleStream)) {
                            AppLog::instance().error(
                                QStringLiteral("PathTracer compact active list kernel launch failed"));
                            sampleLaunched = false;
                        } else {
                            pendingNewActiveCount = 0;
                            const cudaError_t countError = cudaMemcpyAsync(
                                &pendingNewActiveCount,
                                m_impl->acc.d_activeCount,
                                sizeof(int),
                                cudaMemcpyDeviceToHost,
                                m_impl->sampleStream);
                            if (countError != cudaSuccess) {
                                AppLog::instance().error(
                                    QStringLiteral("PathTracer active count readback failed: %1")
                                        .arg(cudaErrorString(countError)));
                                sampleLaunched = false;
                            } else {
                                pendingActiveListUpdate = true;
                                pendingActiveIndicesSwap = true;
                            }
                        }
                    }
                }
            }

            if (sampleLaunched) {
                const cudaError_t recordError =
                    cudaEventRecord(m_impl->sampleCompleteEvent, m_impl->sampleStream);
                if (recordError != cudaSuccess) {
                    AppLog::instance().error(
                        QStringLiteral("CUDA event record failed: %1").arg(cudaErrorString(recordError)));
                    sampleLaunched = false;
                    pendingActiveListUpdate = false;
                    pendingActiveIndicesSwap = false;
                }
            }
        }

        if (!sampleLaunched) {
            if (!previewPass && m_impl->activePixelCount.load() > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            continue;
        }

        const auto gpuWaitStart = std::chrono::steady_clock::now();
        QString waitError;
        const SampleWaitResult waitResult = waitForSampleEvent(
            m_impl->sampleCompleteEvent,
            m_impl->running,
            m_impl->resetRequested,
            m_impl->regionResetRequested,
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

        if (pendingActiveListUpdate) {
            m_impl->activePixelCount.store(pendingNewActiveCount);
            if (pendingActiveIndicesSwap) {
                std::swap(m_impl->acc.d_activeIndices, m_impl->acc.d_activeScratch);
            }
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
