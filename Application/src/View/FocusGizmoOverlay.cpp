#include "FocusGizmoOverlay.h"

#include "AppLog.h"

#include <QByteArray>
#include <QString>

#include <cstddef>

namespace {

constexpr const char* kVertexShader = R"(#version 460 core
layout(location = 0) in vec4 aClipPos;
layout(location = 1) in vec3 aColor;

out vec3 vColor;

void main()
{
    gl_Position = aClipPos;
    vColor = aColor;
}
)";

constexpr const char* kFragmentShader = R"(#version 460 core
in vec3 vColor;

out vec4 fragColor;

void main()
{
    fragColor = vec4(vColor, 1.0);
}
)";

constexpr glm::vec3 kWhite{1.0f, 1.0f, 1.0f};

} // namespace

FocusGizmoOverlay::~FocusGizmoOverlay()
{
    if (m_gl != nullptr) {
        release(m_gl);
    }
}

void FocusGizmoOverlay::initialize(QOpenGLFunctions_4_5_Core* gl)
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
        AppLog::instance().error(QStringLiteral("FocusGizmoOverlay: shader program failed to link"));
        return;
    }

    gl->glGenVertexArrays(1, &m_vao);
    gl->glGenBuffers(1, &m_vbo);

    gl->glBindVertexArray(m_vao);
    gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    gl->glEnableVertexAttribArray(0);
    gl->glVertexAttribPointer(
        0,
        4,
        GL_FLOAT,
        GL_FALSE,
        static_cast<GLsizei>(sizeof(OverlayDrawVertex)),
        nullptr);
    gl->glEnableVertexAttribArray(1);
    gl->glVertexAttribPointer(
        1,
        3,
        GL_FLOAT,
        GL_FALSE,
        static_cast<GLsizei>(sizeof(OverlayDrawVertex)),
        reinterpret_cast<const void*>(offsetof(OverlayDrawVertex, color)));

    gl->glBindVertexArray(0);
    m_initialized = true;
}

void FocusGizmoOverlay::release(QOpenGLFunctions_4_5_Core* gl)
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

    m_vertexCount = 0;
    m_initialized = false;
    m_gl = nullptr;
}

void FocusGizmoOverlay::draw(
    QOpenGLFunctions_4_5_Core* gl,
    const glm::mat4& sceneMvp,
    const glm::mat4& quadViewProj,
    const glm::vec3& centerWorld,
    const glm::vec3& cameraRight,
    const glm::vec3& cameraUp,
    float sizeMm)
{
    if (gl == nullptr || !m_initialized || m_program == 0 || sizeMm <= 0.0f) {
        return;
    }

    const glm::vec3 right = glm::normalize(cameraRight);
    const glm::vec3 up = glm::normalize(cameraUp);
    const float halfSize = sizeMm * 0.5f;

    const glm::vec3 corners[4] = {
        centerWorld - right * halfSize - up * halfSize,
        centerWorld + right * halfSize - up * halfSize,
        centerWorld + right * halfSize + up * halfSize,
        centerWorld - right * halfSize + up * halfSize,
    };

    m_clippedVertices.clear();
    m_clippedVertices.reserve(8);

    for (int i = 0; i < 4; ++i) {
        const int next = (i + 1) % 4;
        OverlayLineClip::appendClippedWorldLine(
            corners[i],
            corners[next],
            kWhite,
            sceneMvp,
            quadViewProj,
            m_clippedVertices);
    }

    m_vertexCount = static_cast<int>(m_clippedVertices.size());
    if (m_vertexCount <= 0) {
        return;
    }

    gl->glUseProgram(m_program);
    gl->glBindVertexArray(m_vao);
    gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    gl->glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(m_clippedVertices.size() * sizeof(OverlayDrawVertex)),
        m_clippedVertices.data(),
        GL_STREAM_DRAW);
    gl->glLineWidth(2.0f);
    gl->glDrawArrays(GL_LINES, 0, m_vertexCount);
    gl->glBindVertexArray(0);
}

GLuint FocusGizmoOverlay::compileShader(QOpenGLFunctions_4_5_Core* gl, GLenum type, const char* source)
{
    const GLuint shader = gl->glCreateShader(type);
    gl->glShaderSource(shader, 1, &source, nullptr);
    gl->glCompileShader(shader);

    GLint compiled = GL_FALSE;
    gl->glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) {
        GLint logLength = 0;
        gl->glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
        QByteArray log;
        log.resize(logLength);
        gl->glGetShaderInfoLog(shader, logLength, nullptr, log.data());

        const QString typeName =
            type == GL_VERTEX_SHADER ? QStringLiteral("vertex") : QStringLiteral("fragment");
        AppLog::instance().error(
            QStringLiteral("FocusGizmoOverlay shader compile failed (%1): %2")
                .arg(typeName, QString::fromUtf8(log)));
        gl->glDeleteShader(shader);
        return 0;
    }

    return shader;
}

GLuint FocusGizmoOverlay::linkProgram(QOpenGLFunctions_4_5_Core* gl, GLuint vertexShader, GLuint fragmentShader)
{
    if (vertexShader == 0 || fragmentShader == 0) {
        return 0;
    }

    const GLuint program = gl->glCreateProgram();
    gl->glAttachShader(program, vertexShader);
    gl->glAttachShader(program, fragmentShader);
    gl->glLinkProgram(program);

    GLint linked = GL_FALSE;
    gl->glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        GLint logLength = 0;
        gl->glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
        QByteArray log;
        log.resize(logLength);
        gl->glGetProgramInfoLog(program, logLength, nullptr, log.data());

        AppLog::instance().error(
            QStringLiteral("FocusGizmoOverlay shader link failed: %1").arg(QString::fromUtf8(log)));
        gl->glDeleteProgram(program);
        return 0;
    }

    return program;
}
