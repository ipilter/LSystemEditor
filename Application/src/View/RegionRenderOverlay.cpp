#include "RegionRenderOverlay.h"

#include "AppLog.h"

#include <QByteArray>
#include <QString>

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>

namespace {

constexpr const char* kVertexShader = R"(#version 460 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aColor;

uniform mat4 uViewProj;

out vec4 vColor;

void main()
{
    gl_Position = uViewProj * vec4(aPos, 0.0, 1.0);
    vColor = aColor;
}
)";

constexpr const char* kFragmentShader = R"(#version 460 core
in vec4 vColor;

out vec4 fragColor;

void main()
{
    fragColor = vColor;
}
)";

struct OverlayVertex
{
    float x;
    float y;
    float r;
    float g;
    float b;
    float a;
};

float imagePixelToNdcX(int pixel, int renderWidth)
{
    if (renderWidth <= 0) {
        return 0.0f;
    }
    return 2.0f * static_cast<float>(pixel) / static_cast<float>(renderWidth) - 1.0f;
}

float imagePixelToNdcY(int pixel, int renderHeight)
{
    if (renderHeight <= 0) {
        return 0.0f;
    }
    return 1.0f - 2.0f * static_cast<float>(pixel) / static_cast<float>(renderHeight);
}

} // namespace

RegionRenderOverlay::~RegionRenderOverlay()
{
    if (m_gl != nullptr) {
        release(m_gl);
    }
}

void RegionRenderOverlay::initialize(QOpenGLFunctions_4_5_Core* gl)
{
    if (gl == nullptr || m_initialized) {
        return;
    }

    m_gl = gl;

    const GLuint vertexShader = compileShader(gl, GL_VERTEX_SHADER, kVertexShader);
    const GLuint fragmentShader = compileShader(gl, GL_FRAGMENT_SHADER, kFragmentShader);
    m_program = linkProgram(gl, vertexShader, fragmentShader);
    gl->glDeleteShader(vertexShader);
    gl->glDeleteShader(fragmentShader);

    if (m_program == 0) {
        AppLog::instance().error(QStringLiteral("RegionRenderOverlay: shader program failed to link"));
        return;
    }

    gl->glGenVertexArrays(1, &m_vao);
    gl->glGenBuffers(1, &m_vbo);
    m_initialized = true;
}

void RegionRenderOverlay::release(QOpenGLFunctions_4_5_Core* gl)
{
    if (gl == nullptr) {
        return;
    }

    if (m_vbo != 0) {
        gl->glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_vao != 0) {
        gl->glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_program != 0) {
        gl->glDeleteProgram(m_program);
        m_program = 0;
    }

    m_initialized = false;
    m_gl = nullptr;
}

void RegionRenderOverlay::draw(
    QOpenGLFunctions_4_5_Core* gl,
    const glm::mat4& viewProj,
    const QRect& regionPx,
    int renderWidth,
    int renderHeight,
    const QColor& color)
{
    if (gl == nullptr || !m_initialized || m_program == 0 || renderWidth <= 0 || renderHeight <= 0 || regionPx.isEmpty()) {
        return;
    }

    const float leftNdc = imagePixelToNdcX(regionPx.left(), renderWidth);
    const float rightNdc = imagePixelToNdcX(regionPx.right() + 1, renderWidth);
    const float topNdc = imagePixelToNdcY(regionPx.top(), renderHeight);
    const float bottomNdc = imagePixelToNdcY(regionPx.bottom() + 1, renderHeight);

    const float r = static_cast<float>(color.red()) / 255.0f;
    const float g = static_cast<float>(color.green()) / 255.0f;
    const float b = static_cast<float>(color.blue()) / 255.0f;
    const float a = static_cast<float>(color.alpha()) / 255.0f;

    const std::array<OverlayVertex, 5> vertices = {{
        {leftNdc, topNdc, r, g, b, a},
        {rightNdc, topNdc, r, g, b, a},
        {rightNdc, bottomNdc, r, g, b, a},
        {leftNdc, bottomNdc, r, g, b, a},
        {leftNdc, topNdc, r, g, b, a},
    }};

    gl->glBindVertexArray(m_vao);
    gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    gl->glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(vertices.size() * sizeof(OverlayVertex)),
        vertices.data(),
        GL_DYNAMIC_DRAW);

    gl->glEnableVertexAttribArray(0);
    gl->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(OverlayVertex), reinterpret_cast<void*>(0));
    gl->glEnableVertexAttribArray(1);
    gl->glVertexAttribPointer(
        1,
        4,
        GL_FLOAT,
        GL_FALSE,
        sizeof(OverlayVertex),
        reinterpret_cast<void*>(offsetof(OverlayVertex, r)));

    gl->glUseProgram(m_program);
    gl->glUniformMatrix4fv(gl->glGetUniformLocation(m_program, "uViewProj"), 1, GL_FALSE, glm::value_ptr(viewProj));

    gl->glDisable(GL_DEPTH_TEST);
    gl->glLineWidth(2.0f);
    gl->glDrawArrays(GL_LINE_STRIP, 0, static_cast<GLsizei>(vertices.size()));

    gl->glBindVertexArray(0);
}

GLuint RegionRenderOverlay::compileShader(QOpenGLFunctions_4_5_Core* gl, GLenum type, const char* source)
{
    const GLuint shader = gl->glCreateShader(type);
    gl->glShaderSource(shader, 1, &source, nullptr);
    gl->glCompileShader(shader);

    GLint compiled = GL_FALSE;
    gl->glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) {
        GLint logLength = 0;
        gl->glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
        QByteArray log(static_cast<int>(std::max(logLength, 1)), '\0');
        gl->glGetShaderInfoLog(shader, logLength, nullptr, log.data());
        AppLog::instance().error(
            QStringLiteral("RegionRenderOverlay shader compile failed (%1): %2")
                .arg(type == GL_VERTEX_SHADER ? QStringLiteral("vertex") : QStringLiteral("fragment"))
                .arg(QString::fromUtf8(log)));
        gl->glDeleteShader(shader);
        return 0;
    }

    return shader;
}

GLuint RegionRenderOverlay::linkProgram(QOpenGLFunctions_4_5_Core* gl, GLuint vertexShader, GLuint fragmentShader)
{
    const GLuint program = gl->glCreateProgram();
    gl->glAttachShader(program, vertexShader);
    gl->glAttachShader(program, fragmentShader);
    gl->glLinkProgram(program);

    GLint linked = GL_FALSE;
    gl->glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        GLint logLength = 0;
        gl->glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
        QByteArray log(static_cast<int>(std::max(logLength, 1)), '\0');
        gl->glGetProgramInfoLog(program, logLength, nullptr, log.data());
        AppLog::instance().error(
            QStringLiteral("RegionRenderOverlay shader link failed: %1").arg(QString::fromUtf8(log)));
        gl->glDeleteProgram(program);
        return 0;
    }

    return program;
}
