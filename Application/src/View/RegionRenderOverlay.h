#pragma once

#include <QColor>
#include <QOpenGLFunctions_4_5_Core>
#include <QRect>
#include <QtGui/qopengl.h>

#include <glm/glm.hpp>

class RegionRenderOverlay
{
public:
    RegionRenderOverlay() = default;
    ~RegionRenderOverlay();

    RegionRenderOverlay(const RegionRenderOverlay&) = delete;
    RegionRenderOverlay& operator=(const RegionRenderOverlay&) = delete;

    void initialize(QOpenGLFunctions_4_5_Core* gl);
    void release(QOpenGLFunctions_4_5_Core* gl);
    void draw(
        QOpenGLFunctions_4_5_Core* gl,
        const glm::mat4& viewProj,
        const QRect& regionPx,
        int renderWidth,
        int renderHeight,
        const QColor& color);

private:
    GLuint compileShader(QOpenGLFunctions_4_5_Core* gl, GLenum type, const char* source);
    GLuint linkProgram(QOpenGLFunctions_4_5_Core* gl, GLuint vertexShader, GLuint fragmentShader);

    QOpenGLFunctions_4_5_Core* m_gl = nullptr;
    GLuint m_program = 0;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    bool m_initialized = false;
};
