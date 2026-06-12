#include "OriginGizmoOverlay.h"

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

struct GizmoVertex
{
    float px;
    float py;
    float pz;
    float cr;
    float cg;
    float cb;
};

constexpr float kAxisLength = 1.0f;

constexpr GizmoVertex kGizmoVertices[] = {
    {0.0f, 0.0f, 0.0f, 0.86f, 0.31f, 0.31f},
    {kAxisLength, 0.0f, 0.0f, 0.86f, 0.31f, 0.31f},
    {0.0f, 0.0f, 0.0f, 0.31f, 0.78f, 0.31f},
    {0.0f, kAxisLength, 0.0f, 0.31f, 0.78f, 0.31f},
    {0.0f, 0.0f, 0.0f, 0.31f, 0.47f, 0.86f},
    {0.0f, 0.0f, kAxisLength, 0.31f, 0.47f, 0.86f},
};

} // namespace

OriginGizmoOverlay::~OriginGizmoOverlay()
{
    if (m_gl != nullptr) {
        release(m_gl);
    }
}

void OriginGizmoOverlay::initialize(QOpenGLFunctions_4_5_Core* gl)
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
        AppLog::instance().error(QStringLiteral("OriginGizmoOverlay: shader program failed to link"));
        return;
    }

    gl->glGenVertexArrays(1, &m_vao);
    gl->glGenBuffers(1, &m_vbo);

    m_vertexCount = static_cast<int>(sizeof(kGizmoVertices) / sizeof(kGizmoVertices[0]));

    gl->glBindVertexArray(m_vao);
    gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    gl->glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(sizeof(kGizmoVertices)),
        kGizmoVertices,
        GL_STATIC_DRAW);

    gl->glEnableVertexAttribArray(0);
    gl->glVertexAttribPointer(
        0,
        3,
        GL_FLOAT,
        GL_FALSE,
        static_cast<GLsizei>(sizeof(GizmoVertex)),
        nullptr);
    gl->glEnableVertexAttribArray(1);
    gl->glVertexAttribPointer(
        1,
        3,
        GL_FLOAT,
        GL_FALSE,
        static_cast<GLsizei>(sizeof(GizmoVertex)),
        reinterpret_cast<const void*>(3 * sizeof(float)));

    gl->glBindVertexArray(0);
    m_initialized = true;
}

void OriginGizmoOverlay::release(QOpenGLFunctions_4_5_Core* gl)
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

void OriginGizmoOverlay::draw(QOpenGLFunctions_4_5_Core* gl, const glm::mat4& viewProj)
{
    if (gl == nullptr || !m_initialized || m_program == 0 || m_vertexCount <= 0) {
        return;
    }

    gl->glUseProgram(m_program);
    gl->glUniformMatrix4fv(gl->glGetUniformLocation(m_program, "uMvp"), 1, GL_FALSE, &viewProj[0][0]);
    gl->glBindVertexArray(m_vao);
    gl->glLineWidth(2.0f);
    gl->glDrawArrays(GL_LINES, 0, m_vertexCount);
    gl->glBindVertexArray(0);
}

GLuint OriginGizmoOverlay::compileShader(QOpenGLFunctions_4_5_Core* gl, GLenum type, const char* source)
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
            QStringLiteral("OriginGizmoOverlay shader compile failed (%1): %2")
                .arg(typeName, QString::fromUtf8(log)));
        gl->glDeleteShader(shader);
        return 0;
    }

    return shader;
}

GLuint OriginGizmoOverlay::linkProgram(QOpenGLFunctions_4_5_Core* gl, GLuint vertexShader, GLuint fragmentShader)
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
            QStringLiteral("OriginGizmoOverlay shader link failed: %1").arg(QString::fromUtf8(log)));
        gl->glDeleteProgram(program);
        return 0;
    }

    return program;
}
