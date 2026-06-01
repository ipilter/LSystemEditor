#include "PathTracer.h"

#include "AppLog.h"
#include "CameraGpu.h"
#include "PathTracerCuda.h"
#include "QmcGpuResources.h"
#include "SdfAccel/SdfAccelBoundsMesh.h"
#include "SdfAccel/SdfAccelScene.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <cuda_gl_interop.h>
#include <cuda_runtime.h>
#include <vector_types.h>

#include <atomic>
#include <cstddef>
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

struct PathTracerDetail::PathTracerImpl
{
    AccumulatorData acc;
    CameraGpu* d_camera = nullptr;
    CameraGpu hostCamera{};
    std::mutex cameraMutex;
    std::atomic<bool> cameraDirty{true};
    cudaGraphicsResource_t pboResources[2] = {nullptr, nullptr};
    cudaStream_t stream = nullptr;
    cudaEvent_t sampleCompleteEvent = nullptr;
    QmcGpuResources qmc;

    std::thread worker;
    std::atomic<bool> running{false};
    std::atomic<bool> resetRequested{false};
    std::atomic<bool> configured{false};

    std::mutex workerMutex;
    std::condition_variable workerCv;

    std::atomic<int> maxSamplesPerPixel{1024};
    std::atomic<int> previewStepsPerLevel{0};
    std::atomic<int> sampleCount{0};
    std::atomic<int> lastSamplingStride{1};
    std::atomic<int> visualMode{static_cast<int>(SdfVisualMode::StepCount)};

    SdfAccelScene accelScene;
    SdfMarchParamsGpu hostMarchParams{};
    SdfMarchParamsGpu* d_marchParams = nullptr;
    SdfAccelBoundsMesh boundsMesh;

    CameraGpu lastSampleCamera{};
    std::mutex lastSampleCameraMutex;
};

namespace {

constexpr int kMaxSamplesUpperBound = 1'000'000;
constexpr int kMaxPreviewStepsPerLevel = 128;

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

int largestPowerOfTwoAtMost(int value)
{
    if (value < 1) {
        return 1;
    }

    int power = 1;
    while (power * 2 <= value) {
        power *= 2;
    }
    return power;
}

int maxStartingStride(int width, int height)
{
    const int maxDim = width > height ? width : height;
    int stride = largestPowerOfTwoAtMost(maxDim);
    if (stride > 32) {
        stride = 32;
    }
    return stride >= 1 ? stride : 1;
}

int countStrideLevels(int maxStride)
{
    int numLevels = 0;
    for (int stride = maxStride; stride >= 1; stride /= 2) {
        ++numLevels;
    }
    return numLevels;
}

int strideAtLevel(int maxStride, int levelIndex)
{
    int stride = maxStride;
    for (int i = 0; i < levelIndex; ++i) {
        stride /= 2;
    }
    return stride >= 1 ? stride : 1;
}

int samplingStride(int globalIter, int previewSteps, int width, int height)
{
    if (previewSteps <= 0 || width <= 0 || height <= 0 || globalIter >= previewSteps) {
        return 1;
    }

    const int maxStride = maxStartingStride(width, height);
    const int numLevels = countStrideLevels(maxStride);
    const int levelIndex = globalIter < numLevels - 1 ? globalIter : numLevels - 1;
    return strideAtLevel(maxStride, levelIndex);
}

int displayStrideFallback(int previewSteps, int width, int height)
{
    if (previewSteps <= 0 || width <= 0 || height <= 0) {
        return 1;
    }
    return maxStartingStride(width, height);
}

int displayStrideForCopy(const PathTracerDetail::PathTracerImpl* impl)
{
    if (impl == nullptr || impl->acc.width <= 0 || impl->acc.height <= 0) {
        return 1;
    }

    if (impl->sampleCount.load() > 0) {
        const int stride = impl->lastSamplingStride.load();
        return stride >= 1 ? stride : 1;
    }

    return displayStrideFallback(
        impl->previewStepsPerLevel.load(),
        impl->acc.width,
        impl->acc.height);
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

    cudaFree(nullptr);

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
            impl->hostMarchParams.backgroundR,
            impl->hostMarchParams.backgroundG,
            impl->hostMarchParams.backgroundB,
            impl->stream)) {
        if (outError != nullptr) {
            *outError = QStringLiteral("clear accumulator kernel launch failed");
        }
        return false;
    }

    return true;
}

bool uploadMarchParams(PathTracerDetail::PathTracerImpl* impl, cudaStream_t stream, QString* outError)
{
    if (impl == nullptr || impl->d_marchParams == nullptr) {
        return true;
    }

    const cudaError_t copyError = cudaMemcpyAsync(
        impl->d_marchParams,
        &impl->hostMarchParams,
        sizeof(SdfMarchParamsGpu),
        cudaMemcpyHostToDevice,
        stream);
    if (copyError != cudaSuccess) {
        if (outError != nullptr) {
            *outError = cudaErrorString(copyError);
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

CameraGpu defaultCameraGpu()
{
    CameraGpu camera{};
    camera.position = make_float3(0.0f, 0.0f, 0.0f);
    camera.orientation = make_float4(1.0f, 0.0f, 0.0f, 0.0f);
    camera.fovY = 1.04719755f; // 60 degrees
    camera.aspect = 1.0f;
    camera.nearPlane = 0.1f;
    camera.farPlane = 1000.0f;
    return camera;
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

    impl->hostCamera = defaultCameraGpu();
    impl->cameraDirty.store(true);
    return true;
}

bool uploadCameraIfDirty(PathTracerDetail::PathTracerImpl* impl, cudaStream_t stream, QString* outError)
{
    if (impl == nullptr || impl->d_camera == nullptr) {
        if (outError != nullptr) {
            *outError = QStringLiteral("camera not configured");
        }
        return false;
    }

    CameraGpu cameraCopy{};
    {
        std::lock_guard<std::mutex> lock(impl->cameraMutex);
        cameraCopy = impl->hostCamera;
    }

    if (impl->acc.width > 0 && impl->acc.height > 0) {
        cameraCopy.aspect =
            static_cast<float>(impl->acc.width) / static_cast<float>(impl->acc.height);
    }

    if (!impl->cameraDirty.load()) {
        return true;
    }

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

CameraGpu sampleCameraForFrame(PathTracerDetail::PathTracerImpl* impl)
{
    CameraGpu camera{};
    if (impl == nullptr) {
        return camera;
    }

    {
        std::lock_guard<std::mutex> lock(impl->cameraMutex);
        camera = impl->hostCamera;
    }

    if (impl->acc.width > 0 && impl->acc.height > 0) {
        camera.aspect = static_cast<float>(impl->acc.width) / static_cast<float>(impl->acc.height);
    }

    return camera;
}

void storeLastSampleCamera(PathTracerDetail::PathTracerImpl* impl, const CameraGpu& camera)
{
    if (impl == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(impl->lastSampleCameraMutex);
    impl->lastSampleCamera = camera;
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
    const int maxSamples = impl->maxSamplesPerPixel.load();
    if (maxSamples == 0) {
        return true;
    }
    const int previewSteps = impl->previewStepsPerLevel.load();
    return impl->sampleCount.load() < previewSteps + maxSamples;
}

void checkCudaGlDeviceAffinity()
{
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

    int currentCudaDevice = -1;
    if (cudaGetDevice(&currentCudaDevice) != cudaSuccess) {
        AppLog::instance().warning(QStringLiteral("cudaGetDevice failed during CUDA-GL affinity check"));
        return;
    }

    if (currentCudaDevice != glCudaDevices[0]) {
        AppLog::instance().warning(
            QStringLiteral("CUDA device (%1) may not match OpenGL device (%2)")
                .arg(currentCudaDevice)
                .arg(glCudaDevices[0]));
    }
}

void freeMarchParams(PathTracerDetail::PathTracerImpl* impl)
{
    if (impl == nullptr || impl->d_marchParams == nullptr) {
        return;
    }

    cudaFree(impl->d_marchParams);
    impl->d_marchParams = nullptr;
}

bool initMarchParams(PathTracerDetail::PathTracerImpl* impl, cudaStream_t stream, QString* outError)
{
    if (impl == nullptr) {
        if (outError != nullptr) {
            *outError = QStringLiteral("invalid path tracer impl");
        }
        return false;
    }

    freeMarchParams(impl);

    const float backgroundR = impl->hostMarchParams.backgroundR;
    const float backgroundG = impl->hostMarchParams.backgroundG;
    const float backgroundB = impl->hostMarchParams.backgroundB;

    impl->hostMarchParams = SdfMarchParamsGpu{};
    impl->hostMarchParams.maxDistance = 100.0f;
    impl->hostMarchParams.surfaceEpsilon = 1.0e-4f;
    impl->hostMarchParams.normalEpsilon = 1.0e-4f;
    impl->hostMarchParams.maxSteps = 256;
    impl->hostMarchParams.refineIterations = 10;
    impl->hostMarchParams.backgroundR = backgroundR;
    impl->hostMarchParams.backgroundG = backgroundG;
    impl->hostMarchParams.backgroundB = backgroundB;

    const cudaError_t allocError = cudaMalloc(&impl->d_marchParams, sizeof(SdfMarchParamsGpu));
    if (allocError != cudaSuccess) {
        if (outError != nullptr) {
            *outError = cudaErrorString(allocError);
        }
        return false;
    }

    const cudaError_t copyError = cudaMemcpyAsync(
        impl->d_marchParams,
        &impl->hostMarchParams,
        sizeof(SdfMarchParamsGpu),
        cudaMemcpyHostToDevice,
        stream);
    if (copyError != cudaSuccess) {
        freeMarchParams(impl);
        if (outError != nullptr) {
            *outError = cudaErrorString(copyError);
        }
        return false;
    }

    return true;
}

void releaseAccelScene(PathTracerDetail::PathTracerImpl* impl)
{
    if (impl == nullptr) {
        return;
    }

    freeMarchParams(impl);
    impl->accelScene.release();
    impl->boundsMesh = SdfAccelBoundsMesh{};
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
    freeQmcGpuResources(&m_impl->qmc);
    freeAccumulator(&m_impl->acc);
    releaseAccelScene(m_impl.get());
    destroySampleEvent(m_impl.get());
    destroyStream(m_impl.get());
}

void PathTracer::setFrameReadyCallback(FrameReadyCallback callback)
{
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_frameReadyCallback = std::move(callback);
}

bool PathTracer::configure(int width, int height, uint32_t pbo0, uint32_t pbo1)
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
    freeQmcGpuResources(&m_impl->qmc);
    freeAccumulator(&m_impl->acc);
    releaseAccelScene(m_impl.get());

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

    checkCudaGlDeviceAffinity();

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

    if (!initCameraGpu(m_impl.get(), &error)) {
        AppLog::instance().error(
            QStringLiteral("PathTracer configure: camera allocation failed: %1").arg(error));
        unregisterPboResources(m_impl.get());
        freeAccumulator(&m_impl->acc);
        return false;
    }

    if (!initQmcGpuResources(&m_impl->qmc, width, height, m_impl->stream, &error)) {
        AppLog::instance().error(
            QStringLiteral("PathTracer configure: QMC sampler init failed: %1").arg(error));
        unregisterPboResources(m_impl.get());
        freeCameraGpu(m_impl.get());
        freeAccumulator(&m_impl->acc);
        releaseAccelScene(m_impl.get());
        return false;
    }

    m_impl->accelScene.setDefaultLayout();
    if (!m_impl->accelScene.build()) {
        AppLog::instance().error(QStringLiteral("PathTracer configure: SDF accel scene build failed"));
        unregisterPboResources(m_impl.get());
        freeQmcGpuResources(&m_impl->qmc);
        freeCameraGpu(m_impl.get());
        freeAccumulator(&m_impl->acc);
        releaseAccelScene(m_impl.get());
        return false;
    }

    if (!m_impl->accelScene.allocate()) {
        AppLog::instance().error(QStringLiteral("PathTracer configure: SDF accel scene allocation failed"));
        unregisterPboResources(m_impl.get());
        freeQmcGpuResources(&m_impl->qmc);
        freeCameraGpu(m_impl.get());
        freeAccumulator(&m_impl->acc);
        releaseAccelScene(m_impl.get());
        return false;
    }

    if (!m_impl->accelScene.upload(m_impl->stream)) {
        AppLog::instance().error(QStringLiteral("PathTracer configure: SDF accel scene upload failed"));
        unregisterPboResources(m_impl.get());
        freeQmcGpuResources(&m_impl->qmc);
        freeCameraGpu(m_impl.get());
        freeAccumulator(&m_impl->acc);
        releaseAccelScene(m_impl.get());
        return false;
    }

    if (!initMarchParams(m_impl.get(), m_impl->stream, &error)) {
        AppLog::instance().error(
            QStringLiteral("PathTracer configure: SDF march params upload failed: %1").arg(error));
        unregisterPboResources(m_impl.get());
        freeQmcGpuResources(&m_impl->qmc);
        freeCameraGpu(m_impl.get());
        freeAccumulator(&m_impl->acc);
        releaseAccelScene(m_impl.get());
        return false;
    }

    if (!clearAccumulator(m_impl.get(), &error)) {
        AppLog::instance().error(
            QStringLiteral("PathTracer configure: accumulator clear failed: %1").arg(error));
        unregisterPboResources(m_impl.get());
        freeQmcGpuResources(&m_impl->qmc);
        freeCameraGpu(m_impl.get());
        freeAccumulator(&m_impl->acc);
        releaseAccelScene(m_impl.get());
        return false;
    }

    const cudaError_t syncError = cudaStreamSynchronize(m_impl->stream);
    if (syncError != cudaSuccess) {
        AppLog::instance().error(
            QStringLiteral("PathTracer configure: stream sync failed: %1").arg(cudaErrorString(syncError)));
        unregisterPboResources(m_impl.get());
        freeQmcGpuResources(&m_impl->qmc);
        freeCameraGpu(m_impl.get());
        freeAccumulator(&m_impl->acc);
        releaseAccelScene(m_impl.get());
        return false;
    }

    AppLog::instance().info(
        QStringLiteral("PathTracer: Cranley-Patterson scrambled Sobol QMC sampler active"));
    m_impl->resetRequested.store(false);
    m_impl->sampleCount.store(0);
    m_impl->lastSamplingStride.store(
        displayStrideFallback(
            m_impl->previewStepsPerLevel.load(),
            width,
            height));
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

    if (m_impl->acc.width <= 0 || m_impl->acc.height <= 0) {
        return false;
    }

    cudaGraphicsResource_t resource = m_impl->pboResources[slot];
    if (resource == nullptr || m_impl->sampleCompleteEvent == nullptr) {
        return false;
    }

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

    const int stride = displayStrideForCopy(m_impl.get());

    const bool copied = pathTracerCopyToPbo(
        m_impl->acc.d_buffer,
        static_cast<uchar4*>(devicePointer),
        m_impl->acc.width,
        m_impl->acc.height,
        stride,
        m_impl->stream);

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
    m_impl->previewStepsPerLevel.store(clampPreviewStepsPerLevel(steps));
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

void PathTracer::setCamera(const CameraGpu& camera)
{
    CameraGpu stored = camera;
    if (m_impl->acc.width > 0 && m_impl->acc.height > 0) {
        stored.aspect =
            static_cast<float>(m_impl->acc.width) / static_cast<float>(m_impl->acc.height);
    }

    {
        std::lock_guard<std::mutex> lock(m_impl->cameraMutex);
        m_impl->hostCamera = stored;
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

void PathTracer::setVisualMode(SdfVisualMode mode)
{
    m_impl->visualMode.store(static_cast<int>(mode));
    notifyWorker();
}

SdfVisualMode PathTracer::visualMode() const
{
    return static_cast<SdfVisualMode>(m_impl->visualMode.load());
}

void PathTracer::setClearColor(const QColor& color)
{
    if (!color.isValid()) {
        return;
    }

    m_impl->hostMarchParams.backgroundR = static_cast<float>(color.redF());
    m_impl->hostMarchParams.backgroundG = static_cast<float>(color.greenF());
    m_impl->hostMarchParams.backgroundB = static_cast<float>(color.blueF());

    if (m_impl->stream == nullptr) {
        return;
    }

    QString error;
    if (!uploadMarchParams(m_impl.get(), m_impl->stream, &error)) {
        AppLog::instance().error(
            QStringLiteral("PathTracer clear color upload failed: %1").arg(error));
        return;
    }

    if (m_impl->configured.load()) {
        if (!clearAccumulator(m_impl.get(), &error)) {
            AppLog::instance().error(
                QStringLiteral("PathTracer accumulator clear failed: %1").arg(error));
        }
    }
}

void PathTracer::rebuildAccelBoundsMesh(const QColor& aabbColor, const QColor& octreeColor)
{
    m_impl->boundsMesh = sdfAccelBuildBoundsMesh(m_impl->accelScene, aabbColor, octreeColor);
}

const SdfAccelBoundsMesh& PathTracer::accelBoundsMesh() const
{
    return m_impl->boundsMesh;
}

void PathTracer::notifyWorker()
{
    m_impl->workerCv.notify_all();
}

void PathTracer::renderLoop()
{
    while (m_impl->running.load()) {
        if (m_impl->resetRequested.exchange(false)) {
            if (m_impl->acc.d_buffer != nullptr && m_impl->acc.d_samples != nullptr) {
                QString clearError;
                if (!clearAccumulator(m_impl.get(), &clearError)) {
                    AppLog::instance().error(
                        QStringLiteral("PathTracer accumulator reset failed: %1").arg(clearError));
                }
            }
            refreshPixelScramble(&m_impl->qmc, m_impl->stream);
            m_impl->sampleCount.store(0);
            m_impl->lastSamplingStride.store(
                displayStrideFallback(
                    m_impl->previewStepsPerLevel.load(),
                    m_impl->acc.width,
                    m_impl->acc.height));
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

        const int stride = samplingStride(
            m_impl->sampleCount.load(),
            m_impl->previewStepsPerLevel.load(),
            m_impl->acc.width,
            m_impl->acc.height);
        m_impl->lastSamplingStride.store(stride);

        QString cameraError;
        if (!uploadCameraIfDirty(m_impl.get(), m_impl->stream, &cameraError)) {
            AppLog::instance().error(
                QStringLiteral("PathTracer camera upload failed: %1").arg(cameraError));
            continue;
        }

        if (!m_impl->accelScene.upload(m_impl->stream)) {
            AppLog::instance().error(QStringLiteral("PathTracer SDF scene upload failed"));
            continue;
        }

        const CameraGpu sampleCamera = sampleCameraForFrame(m_impl.get());
        storeLastSampleCamera(m_impl.get(), sampleCamera);

        if (!pathTracerSample(
                m_impl->acc.d_buffer,
                m_impl->acc.d_samples,
                m_impl->acc.width,
                m_impl->acc.height,
                stride,
                m_impl->d_camera,
                m_impl->accelScene.deviceScene(),
                m_impl->d_marchParams,
                static_cast<int>(m_impl->visualMode.load()),
                m_impl->qmc.sobolMatrices,
                m_impl->qmc.pixelScramble,
                kMaxSobolDimensions,
                m_impl->stream)) {
            AppLog::instance().error(QStringLiteral("PathTracer sample kernel launch failed"));
            continue;
        }

        m_impl->sampleCount.fetch_add(1);

        const cudaError_t recordError =
            cudaEventRecord(m_impl->sampleCompleteEvent, m_impl->stream);
        if (recordError != cudaSuccess) {
            AppLog::instance().error(
                QStringLiteral("CUDA event record failed: %1").arg(cudaErrorString(recordError)));
            continue;
        }

        const cudaError_t syncError = cudaStreamSynchronize(m_impl->stream);
        if (syncError != cudaSuccess) {
            AppLog::instance().error(
                QStringLiteral("CUDA stream sync after sample failed: %1").arg(cudaErrorString(syncError)));
            continue;
        }

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
