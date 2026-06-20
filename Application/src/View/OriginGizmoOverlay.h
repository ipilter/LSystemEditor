#pragma once

#include <QOpenGLFunctions_4_5_Core>
#include <QtGui/qopengl.h>

#include "OverlayLineClip.h"

#include <glm/glm.hpp>

#include <vector>

class OriginGizmoOverlay
{
public:
    OriginGizmoOverlay() = default;
    ~OriginGizmoOverlay();

    OriginGizmoOverlay(const OriginGizmoOverlay&) = delete;
    OriginGizmoOverlay& operator=(const OriginGizmoOverlay&) = delete;

    void initialize(QOpenGLFunctions_4_5_Core* gl);
    void release(QOpenGLFunctions_4_5_Core* gl);
    void draw(
        QOpenGLFunctions_4_5_Core* gl,
        const glm::mat4& sceneMvp,
        const glm::mat4& quadViewProj);

private:
    GLuint compileShader(QOpenGLFunctions_4_5_Core* gl, GLenum type, const char* source);
    GLuint linkProgram(QOpenGLFunctions_4_5_Core* gl, GLuint vertexShader, GLuint fragmentShader);

    QOpenGLFunctions_4_5_Core* m_gl = nullptr;
    GLuint m_program = 0;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    int m_vertexCount = 0;
    bool m_initialized = false;
    std::vector<OverlayDrawVertex> m_clippedVertices;
};
