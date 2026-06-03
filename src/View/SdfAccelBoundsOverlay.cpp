#include "SdfAccelBoundsOverlay.h"

#include "AppLog.h"

#include <QByteArray>
#include <QString>

namespace {

constexpr const char* kVertexShader = R"(#version 460 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;

uniform mat4 uMvp;

out vec3 vColor;

void main()
{
    gl_Position = uMvp * vec4(aPos, 1.0);
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

} // namespace

SdfAccelBoundsOverlay::~SdfAccelBoundsOverlay()
{
    if (m_gl != nullptr) {
        release(m_gl);
    }
}

void SdfAccelBoundsOverlay::initialize(QOpenGLFunctions_4_5_Core* gl)
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
        AppLog::instance().error(QStringLiteral("SdfAccelBoundsOverlay: shader program failed to link"));
        return;
    }

    gl->glGenVertexArrays(1, &m_vao);
    gl->glGenBuffers(1, &m_vbo);
    m_initialized = true;
}

void SdfAccelBoundsOverlay::release(QOpenGLFunctions_4_5_Core* gl)
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

void SdfAccelBoundsOverlay::rebuild(QOpenGLFunctions_4_5_Core* gl, const SdfAccelBoundsMesh& mesh)
{
    if (gl == nullptr || !m_initialized || m_vao == 0) {
        return;
    }

    m_vertexCount = static_cast<int>(mesh.bvhLines.size());

    gl->glBindVertexArray(m_vao);
    gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    if (mesh.bvhLines.empty()) {
        gl->glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_STATIC_DRAW);
    } else {
        gl->glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(mesh.bvhLines.size() * sizeof(SdfAccelBoundsLineVertex)),
            mesh.bvhLines.data(),
            GL_STATIC_DRAW);
    }

    gl->glEnableVertexAttribArray(0);
    gl->glVertexAttribPointer(
        0,
        3,
        GL_FLOAT,
        GL_FALSE,
        static_cast<GLsizei>(sizeof(SdfAccelBoundsLineVertex)),
        nullptr);
    gl->glEnableVertexAttribArray(1);
    gl->glVertexAttribPointer(
        1,
        3,
        GL_FLOAT,
        GL_FALSE,
        static_cast<GLsizei>(sizeof(SdfAccelBoundsLineVertex)),
        reinterpret_cast<const void*>(3 * sizeof(float)));

    gl->glBindVertexArray(0);
}

void SdfAccelBoundsOverlay::draw(
    QOpenGLFunctions_4_5_Core* gl,
    const glm::mat4& viewProj,
    SdfAccelBoundsOverlayMode mode,
    const QColor& boundsColor)
{
    Q_UNUSED(boundsColor);

    if (gl == nullptr || !m_initialized || m_program == 0 || mode != SdfAccelBoundsOverlayMode::Bvh) {
        return;
    }

    if (m_vertexCount <= 0) {
        return;
    }

    gl->glUseProgram(m_program);
    gl->glUniformMatrix4fv(gl->glGetUniformLocation(m_program, "uMvp"), 1, GL_FALSE, &viewProj[0][0]);
    gl->glBindVertexArray(m_vao);
    gl->glLineWidth(1.0f);
    gl->glDrawArrays(GL_LINES, 0, m_vertexCount);
    gl->glBindVertexArray(0);
}

GLuint SdfAccelBoundsOverlay::compileShader(QOpenGLFunctions_4_5_Core* gl, GLenum type, const char* source)
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
            QStringLiteral("SdfAccelBoundsOverlay shader compile failed (%1): %2")
                .arg(typeName, QString::fromUtf8(log)));
        gl->glDeleteShader(shader);
        return 0;
    }

    return shader;
}

GLuint SdfAccelBoundsOverlay::linkProgram(QOpenGLFunctions_4_5_Core* gl, GLuint vertexShader, GLuint fragmentShader)
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
            QStringLiteral("SdfAccelBoundsOverlay shader link failed: %1").arg(QString::fromUtf8(log)));
        gl->glDeleteProgram(program);
        return 0;
    }

    return program;
}
