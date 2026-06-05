#pragma once

#include "Camera3D.h"
#include "PathTracer.h"
#include "MeshAccelBoundsOverlay.h"
#include "OriginGizmoOverlay.h"

#include <QColor>
#include <QOpenGLFunctions_4_5_Core>
#include <QOpenGLWidget>
#include <QPoint>
#include <QtGui/qopengl.h>

#include <atomic>

class SceneModel;

class OpenGLViewportWidget : public QOpenGLWidget, protected QOpenGLFunctions_4_5_Core
{
    Q_OBJECT

public:
    explicit OpenGLViewportWidget(QWidget* parent = nullptr);
    ~OpenGLViewportWidget() override;

    void setClearColor(const QColor& color);
    void setSceneModel(SceneModel* model);

    void restartRender();
    void pauseRender();

signals:
    void iterationChanged(int sampleCount);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void dispatchFrameUpdate();
    void notifyIterationChanged();

private:
    void recreateGpuBuffers();
    void uploadDisplayTexture(int slot, bool initialUpload);
    void releaseGlResources();
    void syncCameraToPathTracer();
    void onCameraChanged();
    void rebuildBoundsOverlay();
    void drawSceneOverlays();
    GLuint compileShader(GLenum type, const char* source);
    GLuint linkProgram(GLuint vertexShader, GLuint fragmentShader);

    SceneModel* m_model = nullptr;
    PathTracer m_pathTracer;
    Camera3D m_camera;
    MeshAccelBoundsOverlay m_boundsOverlay;
    OriginGizmoOverlay m_originGizmo;

    QColor m_clearColor;
    bool m_looking = false;
    QPoint m_lastMousePos;

    GLuint m_pbos[2] = {0, 0};
    GLuint m_texture = 0;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_ebo = 0;
    GLuint m_program = 0;
    bool m_glInitialized = false;
    bool m_textureAllocated = false;
    int m_displaySlot = 0;
    bool m_renderPaused = false;
    bool m_showDisplayTexture = false;

    std::atomic<bool> m_hasNewFrame{false};
    std::atomic<bool> m_frameCallbackQueued{false};
};
