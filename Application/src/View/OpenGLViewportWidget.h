#pragma once

#include "CameraDynamicsController.h"
#include "DebounceTimer.h"
#include "PhysicalCamera.h"
#include "QuadViewCamera2D.h"
#include "PathTracer.h"
#include "RenderAccumulationState.h"
#include "MeshAccelBoundsOverlay.h"
#include "OriginGizmoOverlay.h"
#include "RegionRenderOverlay.h"
#include "ViewportInputState.h"

#include <QColor>
#include <QElapsedTimer>
#include <QOpenGLFunctions_4_5_Core>
#include <QOpenGLWidget>
#include <QPoint>
#include <QRect>
#include <QTimer>
#include <QtGui/qopengl.h>

#include <atomic>
#include <optional>

#include <glm/glm.hpp>

class SceneModel;

class OpenGLViewportWidget : public QOpenGLWidget, protected QOpenGLFunctions_4_5_Core
{
    Q_OBJECT

public:
    explicit OpenGLViewportWidget(QWidget* parent = nullptr);
    ~OpenGLViewportWidget() override;

    void setClearColor(const QColor& color);
    void setSceneModel(SceneModel* model);
    void setEnvironmentHdrPath(const QString& path);
    void setEnvironmentIntensity(float intensity);
    void setPhysicalCamera(float fStop, float shutterSpeedSeconds, float iso);
    PhysicalCamera suggestedPhysicalCamera() const;
    bool computeSuggestedPhysicalCameraFromAccumulator(PhysicalCamera* out) const;

    void restartRender(bool regionOnlyReset = false);
    void pauseRender();
    void setRegionDefineMode(bool active);
    bool regionDefineMode() const;
    void applyRegionRenderSettings(bool resetActiveRegion);
    bool exportSceneWavefrontObj(const QString& objFilePath, QString* errorMessage = nullptr) const;

signals:
    void iterationChanged(int sampleCount);
    void renderStateChanged(RenderAccumulationState state, int sampleCount, int budgetTotal, int activePixelCount);
    void regionDefineModeChanged(bool active);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private slots:
    void dispatchFrameUpdate();
    void notifyIterationChanged();
    void updateCameraDynamics();
    void onCameraMotionThrottledReset();
    void onCameraMotionStopped();
    void applyCameraDynamicsSettings();

private:
    void recreateGpuBuffers();
    void uploadDisplayTexture(int slot, bool initialUpload);
    void releaseGlResources();
    void syncCameraToPathTracer();
    void syncCameraLive();
    void resetAccumulationForCamera();
    void onCameraChanged();
    void rebuildBoundsOverlay();
    void refreshDisplayImage();
    void drawSceneOverlays();
    void drawImageSpaceLayers(int viewportX, int viewportY, int viewportW, int viewportH);
    void drawRegionOverlayLines(const glm::mat4& viewProj, const QRect& regionPx);
    bool isWidgetPosInLetterbox(const QPoint& widgetPos, int* outViewportX, int* outViewportY, int* outViewportW, int* outViewportH) const;
    std::optional<QPoint> widgetPosToImagePixel(const QPoint& widgetPos) const;
    void finalizeRegionDefinition(const QPoint& imagePixel);
    void cancelRegionDefinition();
    void ensureRenderWorkerRunning();
    void emitRenderState();
    GLuint compileShader(GLenum type, const char* source);
    GLuint linkProgram(GLuint vertexShader, GLuint fragmentShader);

    SceneModel* m_model = nullptr;
    PathTracer m_pathTracer;
    PhysicalCamera m_camera;
    QuadViewCamera2D m_quadView;
    CameraDynamicsController m_cameraDynamics;
    ViewportInputState m_inputState;
    MeshAccelBoundsOverlay m_boundsOverlay;
    OriginGizmoOverlay m_originGizmo;
    RegionRenderOverlay m_regionOverlay;

    QColor m_clearColor;
    bool m_looking = false;
    bool m_quadPanning = false;
    bool m_regionDefining = false;
    bool m_regionDefineHasAnchor = false;
    QPoint m_regionDefineAnchor;
    QPoint m_regionDefinePreview;
    QPoint m_lastMousePos;
    glm::vec2 m_pendingMouseAngularInput{0.0f};
    float m_mouseSensitivity = 0.15f;

    QTimer m_cameraTick;
    QElapsedTimer m_cameraTickTimer;
    DebounceTimer m_cameraResetThrottle;
    DebounceTimer m_cameraMotionStopDebounce;
    GLuint m_texture = 0;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_ebo = 0;
    GLuint m_program = 0;
    bool m_glInitialized = false;
    bool m_textureAllocated = false;
    int m_displaySlot = 0;
    bool m_renderPaused = false;

    std::atomic<bool> m_hasNewFrame{false};
    std::atomic<bool> m_frameCallbackQueued{false};
    std::atomic<bool> m_iterationCallbackQueued{false};
    QElapsedTimer m_displayRefreshTimer;

    GLuint m_pbos[2] = {0, 0};
};
