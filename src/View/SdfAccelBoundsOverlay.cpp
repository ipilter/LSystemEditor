#include "SdfAccelBoundsOverlay.h"

#include "AppLog.h"

#include <QByteArray>
#include <QString>

#include <vector>

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

    m_aabbVertexCount = 0;
    m_octreeFullFirstVertex = 0;
    m_octreeFullVertexCount = 0;
    m_octreeExteriorFirstVertex = 0;
    m_octreeExteriorVertexCount = 0;
    m_octreeLeavesFirstVertex = 0;
    m_octreeLeavesVertexCount = 0;
    m_initialized = false;
    m_gl = nullptr;
}

void SdfAccelBoundsOverlay::rebuild(QOpenGLFunctions_4_5_Core* gl, const SdfAccelBoundsMesh& mesh)
{
    if (gl == nullptr || !m_initialized || m_vao == 0) {
        return;
    }

    std::vector<SdfAccelBoundsLineVertex> combined;
    combined.reserve(
        mesh.aabbLines.size() + mesh.octreeFullLines.size() + mesh.octreeExteriorLines.size()
        + mesh.octreeLeavesLines.size());
    combined.insert(combined.end(), mesh.aabbLines.begin(), mesh.aabbLines.end());

    m_aabbVertexCount = static_cast<int>(mesh.aabbLines.size());
    m_octreeFullFirstVertex = m_aabbVertexCount;
    combined.insert(combined.end(), mesh.octreeFullLines.begin(), mesh.octreeFullLines.end());
    m_octreeFullVertexCount = static_cast<int>(mesh.octreeFullLines.size());

    m_octreeExteriorFirstVertex = m_octreeFullFirstVertex + m_octreeFullVertexCount;
    combined.insert(combined.end(), mesh.octreeExteriorLines.begin(), mesh.octreeExteriorLines.end());
    m_octreeExteriorVertexCount = static_cast<int>(mesh.octreeExteriorLines.size());

    m_octreeLeavesFirstVertex = m_octreeExteriorFirstVertex + m_octreeExteriorVertexCount;
    combined.insert(combined.end(), mesh.octreeLeavesLines.begin(), mesh.octreeLeavesLines.end());
    m_octreeLeavesVertexCount = static_cast<int>(mesh.octreeLeavesLines.size());

    gl->glBindVertexArray(m_vao);
    gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    if (combined.empty()) {
        gl->glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_STATIC_DRAW);
    } else {
        gl->glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(combined.size() * sizeof(SdfAccelBoundsLineVertex)),
            combined.data(),
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
    const QColor& aabbColor,
    const QColor& octreeColor)
{
    Q_UNUSED(aabbColor);
    Q_UNUSED(octreeColor);

    if (gl == nullptr || !m_initialized || m_program == 0 || mode == SdfAccelBoundsOverlayMode::Off) {
        return;
    }

    const bool drawAabb =
        mode == SdfAccelBoundsOverlayMode::Aabb || mode == SdfAccelBoundsOverlayMode::Both;

    int octreeFirstVertex = 0;
    int octreeVertexCount = 0;
    switch (mode) {
    case SdfAccelBoundsOverlayMode::Octree:
    case SdfAccelBoundsOverlayMode::Both:
        octreeFirstVertex = m_octreeFullFirstVertex;
        octreeVertexCount = m_octreeFullVertexCount;
        break;
    case SdfAccelBoundsOverlayMode::OctreeExterior:
        octreeFirstVertex = m_octreeExteriorFirstVertex;
        octreeVertexCount = m_octreeExteriorVertexCount;
        break;
    case SdfAccelBoundsOverlayMode::OctreeLeaves:
        octreeFirstVertex = m_octreeLeavesFirstVertex;
        octreeVertexCount = m_octreeLeavesVertexCount;
        break;
    default:
        break;
    }

    const bool drawOctree = octreeVertexCount > 0;

    if ((drawAabb && m_aabbVertexCount <= 0) && !drawOctree) {
        return;
    }

    gl->glUseProgram(m_program);
    gl->glUniformMatrix4fv(gl->glGetUniformLocation(m_program, "uMvp"), 1, GL_FALSE, &viewProj[0][0]);
    gl->glBindVertexArray(m_vao);
    gl->glLineWidth(1.0f);

    if (drawAabb && m_aabbVertexCount > 0) {
        gl->glDrawArrays(GL_LINES, 0, m_aabbVertexCount);
    }

    if (drawOctree) {
        gl->glDrawArrays(GL_LINES, octreeFirstVertex, octreeVertexCount);
    }

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
