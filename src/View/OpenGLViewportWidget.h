#pragma once

#include "Camera2D.h"

#include <QColor>
#include <QOpenGLFunctions_4_5_Core>
#include <QOpenGLWidget>
#include <QPoint>
#include <QtGui/qopengl.h>

class CudaGlInterop;
class SceneModel;

class OpenGLViewportWidget : public QOpenGLWidget, protected QOpenGLFunctions_4_5_Core
{
    Q_OBJECT

public:
    explicit OpenGLViewportWidget(QWidget* parent = nullptr);
    ~OpenGLViewportWidget() override;

    void setClearColor(const QColor& color);
    void setSceneModel(SceneModel* model);

protected:
    void initializeGL() override;
    void paintGL() override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void recreateGpuBuffers();
    void uploadDisplayTexture();
    void releaseGlResources();
    GLuint compileShader(GLenum type, const char* source);
    GLuint linkProgram(GLuint vertexShader, GLuint fragmentShader);

    SceneModel* m_model = nullptr;
    CudaGlInterop* m_cudaInterop = nullptr;
    Camera2D m_camera;

    QColor m_clearColor;
    bool m_panning = false;
    QPoint m_lastMousePos;

    GLuint m_pbos[2] = {0, 0};
    GLuint m_texture = 0;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_ebo = 0;
    GLuint m_program = 0;
    bool m_glInitialized = false;
};
