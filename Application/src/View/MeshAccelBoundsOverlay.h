#pragma once

#include "MeshAccel/MeshAccelBoundsMesh.h"
#include "MeshAccel/MeshAccelTypes.h"

#include <QColor>
#include <QOpenGLFunctions_4_5_Core>
#include <QtGui/qopengl.h>

#include <glm/glm.hpp>

class MeshAccelBoundsOverlay
{
public:
    MeshAccelBoundsOverlay() = default;
    ~MeshAccelBoundsOverlay();

    MeshAccelBoundsOverlay(const MeshAccelBoundsOverlay&) = delete;
    MeshAccelBoundsOverlay& operator=(const MeshAccelBoundsOverlay&) = delete;

    void initialize(QOpenGLFunctions_4_5_Core* gl);
    void release(QOpenGLFunctions_4_5_Core* gl);
    void rebuild(QOpenGLFunctions_4_5_Core* gl, const MeshAccelBoundsMesh& mesh);
    void draw(
        QOpenGLFunctions_4_5_Core* gl,
        const glm::mat4& viewProj,
        RenderViewOverlayMode mode,
        const QColor& boundsColor);

private:
    GLuint compileShader(QOpenGLFunctions_4_5_Core* gl, GLenum type, const char* source);
    GLuint linkProgram(QOpenGLFunctions_4_5_Core* gl, GLuint vertexShader, GLuint fragmentShader);

    QOpenGLFunctions_4_5_Core* m_gl = nullptr;
    GLuint m_program = 0;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    int m_vertexCount = 0;
    bool m_initialized = false;
};
