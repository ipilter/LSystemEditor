#include "OpenGLViewportWidget.h"

#include "AppLog.h"
#include "AppSettings.h"
#include "MeshAccel/MeshSceneContent.h"
#include "SceneModel.h"

#include <QFocusEvent>
#include <QKeyEvent>
#include <QMetaObject>
#include <QMouseEvent>
#include <QSurfaceFormat>
#include <QTimer>
#include <QWheelEvent>

#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <algorithm>
#include <utility>

namespace {

constexpr const char* kVertexShader = R"(#version 460 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;

uniform mat4 uViewProj;

out vec2 vTexCoord;

void main()
{
    gl_Position = uViewProj * vec4(aPos, 0.0, 1.0);
    vTexCoord = aTexCoord;
}
)";

constexpr const char* kFragmentShader = R"(#version 460 core
uniform sampler2D uTexture;

in vec2 vTexCoord;

out vec4 fragColor;

void main()
{
    fragColor = texture(uTexture, vTexCoord);
}
)";

constexpr float kQuadVertices[] = {
    -1.0f, -1.0f, 0.0f, 1.0f,
     1.0f, -1.0f, 1.0f, 1.0f,
    -1.0f,  1.0f, 0.0f, 0.0f,
     1.0f,  1.0f, 1.0f, 0.0f,
};

constexpr unsigned int kQuadIndices[] = {
    0, 1, 2,
    2, 1, 3,
};

constexpr int kMaxDisplayRefreshHz = 60;
constexpr int kMinDisplayRefreshIntervalMs = 1000 / kMaxDisplayRefreshHz;
constexpr float kWheelZoomBase = 1.001f;

void letterboxViewportRect(int widgetW, int widgetH, int renderW, int renderH, int& outX, int& outY, int& outW, int& outH)
{
    if (widgetW <= 0 || widgetH <= 0 || renderW <= 0 || renderH <= 0) {
        outX = 0;
        outY = 0;
        outW = widgetW;
        outH = widgetH;
        return;
    }

    const float widgetAspect = static_cast<float>(widgetW) / static_cast<float>(widgetH);
    const float renderAspect = static_cast<float>(renderW) / static_cast<float>(renderH);

    if (widgetAspect > renderAspect) {
        outH = widgetH;
        outW = static_cast<int>(static_cast<float>(widgetH) * renderAspect + 0.5f);
        outX = (widgetW - outW) / 2;
        outY = 0;
    } else {
        outW = widgetW;
        outH = static_cast<int>(static_cast<float>(widgetW) / renderAspect + 0.5f);
        outX = 0;
        outY = (widgetH - outH) / 2;
    }
}

std::pair<float, float> widgetToLetterboxNdc(
    float x, float y, int widgetW, int widgetH, int renderW, int renderH)
{
    int viewportX = 0;
    int viewportY = 0;
    int viewportW = widgetW;
    int viewportH = widgetH;
    letterboxViewportRect(widgetW, widgetH, renderW, renderH, viewportX, viewportY, viewportW, viewportH);
    if (viewportW <= 0 || viewportH <= 0) {
        return {0.0f, 0.0f};
    }

    return QuadViewCamera2D::widgetToNdc(
        x - static_cast<float>(viewportX),
        y - static_cast<float>(viewportY),
        viewportW,
        viewportH);
}

MeshSceneBuildParams meshSceneBuildParamsForModel(const SceneModel* model)
{
    MeshSceneBuildParams params{};
    if (model != nullptr) {
        params.creaseAngleDeg = model->creaseAngleDeg();
    }
    return params;
}

} // namespace

OpenGLViewportWidget::OpenGLViewportWidget(QWidget* parent)
    : QOpenGLWidget(parent)
    , m_clearColor(QColor(10, 10, 10))
    , m_cameraResetThrottle(250, this)
    , m_cameraMotionStopDebounce(200, this)
{
    QSurfaceFormat format;
    format.setVersion(4, 6);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    setFormat(format);

    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    m_cameraResetThrottle.setMode(DebounceTimer::Mode::Throttle);
    m_cameraMotionStopDebounce.setMode(DebounceTimer::Mode::Debounce);
    connect(
        &m_cameraResetThrottle,
        &DebounceTimer::triggered,
        this,
        &OpenGLViewportWidget::onCameraMotionThrottledReset);
    connect(
        &m_cameraMotionStopDebounce,
        &DebounceTimer::triggered,
        this,
        &OpenGLViewportWidget::onCameraMotionStopped);
    connect(
        &AppSettings::instance(),
        &AppSettings::cameraDynamicsSettingsChanged,
        this,
        [this](const CameraDynamicsSettings&) { applyCameraDynamicsSettings(); });

    connect(&m_cameraTick, &QTimer::timeout, this, &OpenGLViewportWidget::updateCameraDynamics);
    applyCameraDynamicsSettings();
    m_cameraTick.start();
    m_cameraTickTimer.start();

    m_pathTracer.setFrameReadyCallback([this]() {
        m_hasNewFrame.store(true);
        if (!m_frameCallbackQueued.exchange(true)) {
            QMetaObject::invokeMethod(this, "dispatchFrameUpdate", Qt::QueuedConnection);
        }
        if (!m_iterationCallbackQueued.exchange(true)) {
            QMetaObject::invokeMethod(this, "notifyIterationChanged", Qt::QueuedConnection);
        }
    });
}

OpenGLViewportWidget::~OpenGLViewportWidget()
{
    makeCurrent();
    releaseGlResources();
    doneCurrent();
}

void OpenGLViewportWidget::setClearColor(const QColor& color)
{
    if (!color.isValid() || m_clearColor == color) {
        return;
    }

    m_clearColor = color;
    m_pathTracer.setClearColor(color);
    if (m_glInitialized && m_pathTracer.isRunning()) {
        m_pathTracer.resetAccumulation();
    }
    if (m_glInitialized) {
        update();
    }
}

void OpenGLViewportWidget::setSceneModel(SceneModel* model)
{
    if (m_model == model) {
        return;
    }

    if (m_model != nullptr) {
        disconnect(m_model, nullptr, this, nullptr);
    }

    m_model = model;

    if (m_model == nullptr) {
        return;
    }

    connect(m_model, &SceneModel::renderSizeChanged, this, [this]() {
        if (!m_glInitialized) {
            return;
        }
        makeCurrent();
        recreateGpuBuffers();
        doneCurrent();
        update();
    });

    connect(m_model, &SceneModel::maxSamplesPerPixelChanged, this, [this](int max) {
        const int current = m_pathTracer.currentSampleCount();
        m_pathTracer.setMaxSamplesPerPixel(max);

        if (m_renderPaused) {
            emitRenderState();
            return;
        }

        if (max == 0) {
            ensureRenderWorkerRunning();
            emitRenderState();
            return;
        }

        const int previewSteps = m_model->previewStepsPerLevel();
        if (current >= previewSteps + max) {
            restartRender();
            return;
        }

        ensureRenderWorkerRunning();
        emitRenderState();
    });

    connect(m_model, &SceneModel::previewStepsPerLevelChanged, this, [this](int steps) {
        m_pathTracer.setPreviewStepsPerLevel(steps);
        emitRenderState();
    });

    connect(m_model, &SceneModel::russianRouletteMinDepthChanged, this, [this](int depth) {
        m_pathTracer.setRussianRouletteMinDepth(depth);
    });

    connect(m_model, &SceneModel::boundsOverlayModeChanged, this, [this](MeshAccelBoundsOverlayMode) {
        update();
    });

    connect(m_model, &SceneModel::accelBvhColorChanged, this, [this](const QColor&) {
        rebuildBoundsOverlay();
        update();
    });

    connect(m_model, &SceneModel::sceneChanged, this, [this]() {
        if (!m_glInitialized || m_model == nullptr) {
            return;
        }
        makeCurrent();
        if (!m_pathTracer.rebuildMeshScene(
                m_model->proceduralInstances(), meshSceneBuildParamsForModel(m_model))) {
            doneCurrent();
            return;
        }
        rebuildBoundsOverlay();
        restartRender();
        doneCurrent();
        update();
    });

    if (m_glInitialized) {
        makeCurrent();
        recreateGpuBuffers();
        doneCurrent();
        update();
    }
}

void OpenGLViewportWidget::setEnvironmentHdrPath(const QString& path)
{
    m_pathTracer.setEnvironmentHdrPath(path);
}

void OpenGLViewportWidget::setEnvironmentIntensity(float intensity)
{
    m_pathTracer.setEnvironmentIntensity(intensity);
    if (m_glInitialized) {
        update();
    }
}

void OpenGLViewportWidget::setPhysicalCamera(float fStop, float shutterSpeedSeconds, float iso)
{
    m_pathTracer.setPhysicalCamera(fStop, shutterSpeedSeconds, iso);
    if (m_glInitialized && m_textureAllocated) {
        m_hasNewFrame.store(true);
        update();
    }
}

PhysicalCamera OpenGLViewportWidget::suggestedPhysicalCamera() const
{
    return m_pathTracer.suggestedPhysicalCamera();
}

bool OpenGLViewportWidget::computeSuggestedPhysicalCameraFromAccumulator(PhysicalCamera* out) const
{
    return m_pathTracer.computeSuggestedCameraFromAccumulator(out);
}

void OpenGLViewportWidget::initializeGL()
{
    initializeOpenGLFunctions();

    const GLuint vertexShader = compileShader(GL_VERTEX_SHADER, kVertexShader);
    const GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, kFragmentShader);
    m_program = linkProgram(vertexShader, fragmentShader);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    if (m_program == 0) {
        AppLog::instance().error(QStringLiteral("OpenGL shader program failed to link"));
    } else {
        AppLog::instance().info(QStringLiteral("OpenGL shaders compiled and linked"));
    }

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadVertices), kQuadVertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kQuadIndices), kQuadIndices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<const void*>(2 * sizeof(float)));

    glBindVertexArray(0);

    m_boundsOverlay.initialize(this);
    m_originGizmo.initialize(this);
    m_glInitialized = true;

    if (m_model != nullptr) {
        recreateGpuBuffers();
    }
}

void OpenGLViewportWidget::resizeGL(int w, int h)
{
    Q_UNUSED(w);
    Q_UNUSED(h);
    update();
}

void OpenGLViewportWidget::paintGL()
{
    const float r = static_cast<float>(m_clearColor.redF());
    const float g = static_cast<float>(m_clearColor.greenF());
    const float b = static_cast<float>(m_clearColor.blueF());
    const float a = static_cast<float>(m_clearColor.alphaF());

    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);

    if (m_model == nullptr || m_program == 0) {
        return;
    }

    if (m_hasNewFrame.exchange(false)) {
        const int slot = m_displaySlot;
        if (m_pathTracer.publishDisplayFrame(slot)) {
            uploadDisplayTexture(slot, !m_textureAllocated);
            m_textureAllocated = true;
            m_displaySlot = 1 - slot;
        }
    }

    if (m_textureAllocated && m_texture != 0) {
        const int renderW = m_model->renderSize().width();
        const int renderH = m_model->renderSize().height();

        int viewportX = 0;
        int viewportY = 0;
        int viewportW = width();
        int viewportH = height();
        if (renderW > 0 && renderH > 0) {
            letterboxViewportRect(width(), height(), renderW, renderH, viewportX, viewportY, viewportW, viewportH);
        }

        glViewport(viewportX, viewportY, viewportW, viewportH);
        glEnable(GL_SCISSOR_TEST);
        glScissor(viewportX, viewportY, viewportW, viewportH);

        glUseProgram(m_program);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_texture);
        glUniform1i(glGetUniformLocation(m_program, "uTexture"), 0);

        const glm::mat4 viewProj = m_quadView.viewProjMatrix(1.0f, 1.0f);
        glUniformMatrix4fv(glGetUniformLocation(m_program, "uViewProj"), 1, GL_FALSE, glm::value_ptr(viewProj));

        glBindVertexArray(m_vao);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);

        glDisable(GL_SCISSOR_TEST);
        glViewport(0, 0, width(), height());
    }

    drawSceneOverlays();
}

void OpenGLViewportWidget::rebuildBoundsOverlay()
{
    if (m_model == nullptr || !m_glInitialized) {
        return;
    }

    m_pathTracer.rebuildMeshBoundsMesh(m_model->accelBvhColor());
    m_boundsOverlay.rebuild(this, m_pathTracer.meshBoundsMesh());
}

void OpenGLViewportWidget::drawSceneOverlays()
{
    if (m_model == nullptr || !m_quadView.isDefault()) {
        return;
    }

    const int renderW = m_model->renderSize().width();
    const int renderH = m_model->renderSize().height();
    if (renderW <= 0 || renderH <= 0) {
        return;
    }

    int viewportX = 0;
    int viewportY = 0;
    int viewportW = width();
    int viewportH = height();
    letterboxViewportRect(width(), height(), renderW, renderH, viewportX, viewportY, viewportW, viewportH);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glViewport(viewportX, viewportY, viewportW, viewportH);
    glEnable(GL_SCISSOR_TEST);
    glScissor(viewportX, viewportY, viewportW, viewportH);
    glClear(GL_DEPTH_BUFFER_BIT);

    const CameraGpu sampleCamera = m_pathTracer.lastSampleCamera();
    const glm::mat4 viewProj =
        PhysicalCamera::projMatrixFromGpu(sampleCamera, renderW, renderH)
        * PhysicalCamera::viewMatrixFromGpu(sampleCamera);

    m_originGizmo.draw(this, viewProj);

    if (m_model->boundsOverlayMode() != MeshAccelBoundsOverlayMode::Off) {
        m_boundsOverlay.draw(
            this,
            viewProj,
            m_model->boundsOverlayMode(),
            m_model->accelBvhColor());
    }

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glViewport(0, 0, width(), height());
}

void OpenGLViewportWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        return;
    }

    setFocus();
    m_lastMousePos = event->pos();

    if (event->modifiers() & Qt::ControlModifier) {
        m_quadPanning = true;
        event->accept();
        return;
    }

    m_looking = true;
    event->accept();
}

void OpenGLViewportWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_quadPanning) {
        const QPoint delta = event->pos() - m_lastMousePos;
        m_lastMousePos = event->pos();

        if (m_model != nullptr && width() > 0 && height() > 0) {
            const int renderW = m_model->renderSize().width();
            const int renderH = m_model->renderSize().height();
            int viewportX = 0;
            int viewportY = 0;
            int viewportW = width();
            int viewportH = height();
            letterboxViewportRect(width(), height(), renderW, renderH, viewportX, viewportY, viewportW, viewportH);
            if (viewportW > 0 && viewportH > 0) {
                const float ndcDeltaX = -2.0f * static_cast<float>(delta.x()) / static_cast<float>(viewportW);
                const float ndcDeltaY = 2.0f * static_cast<float>(delta.y()) / static_cast<float>(viewportH);
                m_quadView.pan(ndcDeltaX, ndcDeltaY);
                update();
            }
        }
        event->accept();
        return;
    }

    if (!m_looking) {
        return;
    }

    const QPoint delta = event->pos() - m_lastMousePos;
    m_lastMousePos = event->pos();

    m_pendingMouseAngularInput.x += static_cast<float>(-delta.y()) * m_mouseSensitivity;
    m_pendingMouseAngularInput.y += static_cast<float>(-delta.x()) * m_mouseSensitivity;
    event->accept();
}

void OpenGLViewportWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_looking = false;
        m_quadPanning = false;
        event->accept();
    }
}

void OpenGLViewportWidget::wheelEvent(QWheelEvent* event)
{
    if (!(event->modifiers() & Qt::ControlModifier)
        || !m_textureAllocated
        || m_texture == 0
        || m_model == nullptr) {
        event->ignore();
        return;
    }

    const int renderW = m_model->renderSize().width();
    const int renderH = m_model->renderSize().height();
    if (renderW <= 0 || renderH <= 0 || width() <= 0 || height() <= 0) {
        event->ignore();
        return;
    }

    const auto [cursorNdcX, cursorNdcY] = widgetToLetterboxNdc(
        static_cast<float>(event->position().x()),
        static_cast<float>(event->position().y()),
        width(),
        height(),
        renderW,
        renderH);
    const float factor = std::pow(kWheelZoomBase, static_cast<float>(event->angleDelta().y()));
    m_quadView.zoomAt(factor, cursorNdcX, cursorNdcY, 1.0f, 1.0f);
    update();
    event->accept();
}

void OpenGLViewportWidget::keyPressEvent(QKeyEvent* event)
{
    if (m_inputState.handleKeyPress(event)) {
        event->accept();
        return;
    }

    QOpenGLWidget::keyPressEvent(event);
}

void OpenGLViewportWidget::keyReleaseEvent(QKeyEvent* event)
{
    if (m_inputState.handleKeyRelease(event)) {
        event->accept();
        return;
    }

    QOpenGLWidget::keyReleaseEvent(event);
}

void OpenGLViewportWidget::focusOutEvent(QFocusEvent* event)
{
    m_inputState.clear();
    m_pendingMouseAngularInput = glm::vec2(0.0f);
    m_cameraDynamics.reset();
    m_cameraResetThrottle.cancel();
    m_cameraMotionStopDebounce.cancel();
    m_looking = false;
    QOpenGLWidget::focusOutEvent(event);
}

void OpenGLViewportWidget::applyCameraDynamicsSettings()
{
    const CameraDynamicsSettings settings = AppSettings::instance().cameraDynamicsSettings();
    m_cameraDynamics.applySettings(settings);
    m_mouseSensitivity = settings.mouseSensitivity;
    m_cameraTick.setInterval(settings.tickIntervalMs);
    m_cameraResetThrottle.setIntervalMs(settings.motionResetThrottleMs);
    m_cameraMotionStopDebounce.setIntervalMs(settings.motionStopDebounceMs);
}

void OpenGLViewportWidget::updateCameraDynamics()
{
    const float dt = std::min(static_cast<float>(m_cameraTickTimer.elapsed()) / 1000.0f, 0.1f);
    m_cameraTickTimer.restart();
    if (dt <= 0.0f) {
        return;
    }

    glm::vec3 angularInput = m_inputState.angularInput();
    angularInput.x += m_pendingMouseAngularInput.x;
    angularInput.y += m_pendingMouseAngularInput.y;
    m_pendingMouseAngularInput = glm::vec2(0.0f);

    m_cameraDynamics.setThrustScale(m_inputState.boostHeld() ? 2.0f : 1.0f);
    m_cameraDynamics.setLinearInput(m_inputState.linearInput());
    m_cameraDynamics.setAngularInput(angularInput);

    if (m_cameraDynamics.step(m_camera, dt)) {
        syncCameraLive();
        m_cameraResetThrottle.schedule();
        m_cameraMotionStopDebounce.schedule();
    }
}

void OpenGLViewportWidget::onCameraMotionThrottledReset()
{
    resetAccumulationForCamera();
}

void OpenGLViewportWidget::onCameraMotionStopped()
{
    resetAccumulationForCamera();
}

void OpenGLViewportWidget::dispatchFrameUpdate()
{
    m_frameCallbackQueued.store(false);

    const bool prioritizeFirstFrame = m_pathTracer.currentSampleCount() <= 1;

    if (!prioritizeFirstFrame) {
        if (!m_displayRefreshTimer.isValid()) {
            m_displayRefreshTimer.start();
        } else if (m_displayRefreshTimer.elapsed() < kMinDisplayRefreshIntervalMs) {
            if (m_hasNewFrame.load() && !m_frameCallbackQueued.exchange(true)) {
                const int delayMs =
                    kMinDisplayRefreshIntervalMs - static_cast<int>(m_displayRefreshTimer.elapsed());
                QTimer::singleShot(delayMs > 0 ? delayMs : 0, this, &OpenGLViewportWidget::dispatchFrameUpdate);
            }
            return;
        }
    }

    m_displayRefreshTimer.restart();
    update();
}

void OpenGLViewportWidget::notifyIterationChanged()
{
    m_iterationCallbackQueued.store(false);
    emit iterationChanged(m_pathTracer.currentSampleCount());
    emitRenderState();
}

void OpenGLViewportWidget::ensureRenderWorkerRunning()
{
    if (m_renderPaused || m_model == nullptr) {
        return;
    }

    if (!m_pathTracer.isRunning()) {
        m_pathTracer.start();
    }
}

void OpenGLViewportWidget::emitRenderState()
{
    if (m_model == nullptr) {
        return;
    }

    const RenderAccumulationState state = renderAccumulationState(
        m_pathTracer.isRunning(),
        m_renderPaused,
        m_pathTracer.currentSampleCount(),
        m_model->previewStepsPerLevel(),
        m_model->maxSamplesPerPixel());

    emit renderStateChanged(
        state,
        m_pathTracer.currentSampleCount(),
        m_pathTracer.sampleBudgetTotalIterations());
}

void OpenGLViewportWidget::restartRender()
{
    m_renderPaused = false;
    m_hasNewFrame.store(false);
    m_frameCallbackQueued.store(false);
    m_pathTracer.resetAccumulation();
    if (!m_pathTracer.isRunning()) {
        m_pathTracer.start();
    }
    emit iterationChanged(0);
    emitRenderState();
    update();
}

void OpenGLViewportWidget::pauseRender()
{
    m_pathTracer.stop();
    m_renderPaused = true;
    emitRenderState();
}

bool OpenGLViewportWidget::exportSceneWavefrontObj(const QString& objFilePath, QString* errorMessage) const
{
    return m_pathTracer.exportMeshSceneWavefrontObj(objFilePath, errorMessage);
}

void OpenGLViewportWidget::syncCameraToPathTracer()
{
    m_pathTracer.setCamera(m_camera.toGpu());
}

void OpenGLViewportWidget::syncCameraLive()
{
    syncCameraToPathTracer();
    update();
}

void OpenGLViewportWidget::resetAccumulationForCamera()
{
    m_renderPaused = false;
    m_hasNewFrame.store(false);
    m_frameCallbackQueued.store(false);
    m_pathTracer.resetAccumulation();
    ensureRenderWorkerRunning();
    emit iterationChanged(0);
    emitRenderState();
    update();
}

void OpenGLViewportWidget::onCameraChanged()
{
    syncCameraToPathTracer();
    restartRender();
    update();
}

void OpenGLViewportWidget::recreateGpuBuffers()
{
    if (m_model == nullptr) {
        return;
    }

    m_pathTracer.releaseOutputSurfaces();

    m_hasNewFrame.store(false);
    m_frameCallbackQueued.store(false);
    m_iterationCallbackQueued.store(false);
    m_displaySlot = 0;

    if (m_texture != 0) {
        glDeleteTextures(1, &m_texture);
        m_texture = 0;
    }
    m_textureAllocated = false;

    if (m_pbos[0] != 0 || m_pbos[1] != 0) {
        glDeleteBuffers(2, m_pbos);
        m_pbos[0] = 0;
        m_pbos[1] = 0;
    }
    m_model->setPboIds(0, 0);

    const int w = m_model->renderSize().width();
    const int h = m_model->renderSize().height();
    const std::size_t byteSize = static_cast<std::size_t>(m_model->bufferByteSize());
    if (w <= 0 || h <= 0 || byteSize == 0) {
        AppLog::instance().error(QStringLiteral("Invalid render buffer size %1x%2").arg(w).arg(h));
        return;
    }

    glGenBuffers(2, m_pbos);
    for (int i = 0; i < 2; ++i) {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pbos[i]);
        glBufferData(GL_PIXEL_UNPACK_BUFFER, static_cast<GLsizeiptr>(byteSize), nullptr, GL_DYNAMIC_DRAW);
    }
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    if (!m_pathTracer.configure(
            w,
            h,
            m_pbos[0],
            m_pbos[1],
            m_model->proceduralInstances(),
            meshSceneBuildParamsForModel(m_model))) {
        AppLog::instance().error(QStringLiteral("PathTracer configure failed for %1x%2").arg(w).arg(h));
        return;
    }

    m_pathTracer.setClearColor(m_clearColor);
    m_pathTracer.setEnvironmentIntensity(m_model->environmentIntensity());

    rebuildBoundsOverlay();

    m_pathTracer.setMaxSamplesPerPixel(m_model->maxSamplesPerPixel());
    m_pathTracer.setPreviewStepsPerLevel(m_model->previewStepsPerLevel());
    m_pathTracer.setRussianRouletteMinDepth(m_model->russianRouletteMinDepth());
    m_pathTracer.setEnvironmentHdrPath(m_model->environmentHdrPath());
    m_pathTracer.setPhysicalCamera(m_model->fStop(), m_model->shutterSpeedSeconds(), m_model->iso());

    m_model->setPboIds(m_pbos[0], m_pbos[1]);

    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    m_camera.setAspect(w, h);
    syncCameraToPathTracer();

    if (!m_renderPaused) {
        m_pathTracer.start();
    }
    AppLog::instance().info(QStringLiteral("Render buffers configured %1x%2").arg(w).arg(h));
}

void OpenGLViewportWidget::uploadDisplayTexture(int slot, bool initialUpload)
{
    if (m_model == nullptr || m_texture == 0 || slot < 0 || slot > 1 || m_pbos[slot] == 0) {
        return;
    }

    const int w = m_model->renderSize().width();
    const int h = m_model->renderSize().height();

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pbos[slot]);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    if (initialUpload) {
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_RGBA8,
                     w,
                     h,
                     0,
                     GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     nullptr);
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void OpenGLViewportWidget::releaseGlResources()
{
    m_pathTracer.releaseOutputSurfaces();
    m_boundsOverlay.release(this);
    m_originGizmo.release(this);

    if (m_texture != 0) {
        glDeleteTextures(1, &m_texture);
        m_texture = 0;
    }
    m_textureAllocated = false;

    if (m_pbos[0] != 0 || m_pbos[1] != 0) {
        glDeleteBuffers(2, m_pbos);
        m_pbos[0] = 0;
        m_pbos[1] = 0;
    }
    if (m_model != nullptr) {
        m_model->setPboIds(0, 0);
    }
    if (m_vao != 0) {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_vbo != 0) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_ebo != 0) {
        glDeleteBuffers(1, &m_ebo);
        m_ebo = 0;
    }
    if (m_program != 0) {
        glDeleteProgram(m_program);
        m_program = 0;
    }
    m_glInitialized = false;
}

GLuint OpenGLViewportWidget::compileShader(GLenum type, const char* source)
{
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) {
        GLint logLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
        QByteArray log;
        log.resize(logLength);
        glGetShaderInfoLog(shader, logLength, nullptr, log.data());

        const QString typeName =
            type == GL_VERTEX_SHADER ? QStringLiteral("vertex") : QStringLiteral("fragment");
        AppLog::instance().error(
            QStringLiteral("Shader compile failed (%1): %2").arg(typeName, QString::fromUtf8(log)));
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

GLuint OpenGLViewportWidget::linkProgram(GLuint vertexShader, GLuint fragmentShader)
{
    if (vertexShader == 0 || fragmentShader == 0) {
        return 0;
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        GLint logLength = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
        QByteArray log;
        log.resize(logLength);
        glGetProgramInfoLog(program, logLength, nullptr, log.data());

        AppLog::instance().error(QStringLiteral("Shader link failed: %1").arg(QString::fromUtf8(log)));
        glDeleteProgram(program);
        return 0;
    }

    return program;
}
