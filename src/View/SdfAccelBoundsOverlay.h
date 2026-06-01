#pragma once

#include "Sdf/SdfTypes.h"
#include "SdfAccel/SdfAccelBoundsMesh.h"

#include <QColor>
#include <QOpenGLFunctions_4_5_Core>
#include <QtGui/qopengl.h>

#include <glm/glm.hpp>

class SdfAccelBoundsOverlay
{
public:
    SdfAccelBoundsOverlay() = default;
    ~SdfAccelBoundsOverlay();

    SdfAccelBoundsOverlay(const SdfAccelBoundsOverlay&) = delete;
    SdfAccelBoundsOverlay& operator=(const SdfAccelBoundsOverlay&) = delete;

    void initialize(QOpenGLFunctions_4_5_Core* gl);
    void release(QOpenGLFunctions_4_5_Core* gl);
    void rebuild(QOpenGLFunctions_4_5_Core* gl, const SdfAccelBoundsMesh& mesh);
    void draw(
        QOpenGLFunctions_4_5_Core* gl,
        const glm::mat4& viewProj,
        SdfAccelBoundsOverlayMode mode,
        const QColor& aabbColor,
        const QColor& octreeColor);

private:
    GLuint compileShader(QOpenGLFunctions_4_5_Core* gl, GLenum type, const char* source);
    GLuint linkProgram(QOpenGLFunctions_4_5_Core* gl, GLuint vertexShader, GLuint fragmentShader);

    QOpenGLFunctions_4_5_Core* m_gl = nullptr;
    GLuint m_program = 0;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    int m_aabbVertexCount = 0;
    int m_octreeVertexCount = 0;
    std::size_t m_aabbByteOffset = 0;
    bool m_initialized = false;
};
