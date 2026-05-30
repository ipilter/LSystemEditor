#include "OpenGLViewportWidget.h"

#include "CudaGlInterop.h"
#include "SceneModel.h"

#include <QKeyEvent>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QSurfaceFormat>
#include <QWheelEvent>

namespace {

constexpr const char* kVertexShader = R"(#version 460 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;

uniform mat4 uViewProj;
uniform vec2 uQuadSize;

out vec2 vTexCoord;

void main()
{
    vec2 worldPos = aPos * uQuadSize;
    gl_Position = uViewProj * vec4(worldPos, 0.0, 1.0);
    vTexCoord = aTexCoord;
}
)";

constexpr const char* kFragmentShader = R"(#version 460 core
uniform sampler2D uTexture;

in vec2 vTexCoord;

out vec4 fragColor;

void main()
{
    fragColor = texture(uTexture, vTexCoord);
}
)";

constexpr float kQuadVertices[] = {
    -0.5f, -0.5f, 0.0f, 0.0f,
     0.5f, -0.5f, 1.0f, 0.0f,
    -0.5f,  0.5f, 0.0f, 1.0f,
     0.5f,  0.5f, 1.0f, 1.0f,
};

constexpr unsigned int kQuadIndices[] = {
    0, 1, 2,
    2, 1, 3,
};

constexpr float kZoomFactor = 1.1f;

} // namespace

OpenGLViewportWidget::OpenGLViewportWidget(QWidget* parent)
    : QOpenGLWidget(parent)
    , m_cudaInterop(new CudaGlInterop())
    , m_clearColor(QColor(10, 10, 10))
{
    QSurfaceFormat format;
    format.setVersion(4, 6);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    setFormat(format);

    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

OpenGLViewportWidget::~OpenGLViewportWidget()
{
    makeCurrent();
    releaseGlResources();
    doneCurrent();
    delete m_cudaInterop;
    m_cudaInterop = nullptr;
}

void OpenGLViewportWidget::setClearColor(const QColor& color)
{
    if (!color.isValid() || m_clearColor == color) {
        return;
    }

    m_clearColor = color;
    if (m_glInitialized) {
        update();
    }
}

void OpenGLViewportWidget::setSceneModel(SceneModel* model)
{
    if (m_model == model) {
        return;
    }

    if (m_model != nullptr) {
        disconnect(m_model, nullptr, this, nullptr);
    }

    m_model = model;

    if (m_model == nullptr) {
        return;
    }

    connect(m_model, &SceneModel::renderSizeChanged, this, [this]() {
        if (!m_glInitialized) {
            return;
        }
        makeCurrent();
        recreateGpuBuffers();
        doneCurrent();
        update();
    });

    if (m_glInitialized) {
        makeCurrent();
        recreateGpuBuffers();
        doneCurrent();
        update();
    }
}

void OpenGLViewportWidget::initializeGL()
{
    initializeOpenGLFunctions();

    const GLuint vertexShader = compileShader(GL_VERTEX_SHADER, kVertexShader);
    const GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, kFragmentShader);
    m_program = linkProgram(vertexShader, fragmentShader);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadVertices), kQuadVertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kQuadIndices), kQuadIndices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<const void*>(2 * sizeof(float)));

    glBindVertexArray(0);

    m_glInitialized = true;

    if (m_model != nullptr) {
        recreateGpuBuffers();
    }
}

void OpenGLViewportWidget::paintGL()
{
    const float r = static_cast<float>(m_clearColor.redF());
    const float g = static_cast<float>(m_clearColor.greenF());
    const float b = static_cast<float>(m_clearColor.blueF());
    const float a = static_cast<float>(m_clearColor.alphaF());

    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);

    if (m_model == nullptr || m_texture == 0 || m_program == 0) {
        return;
    }

    const int renderW = m_model->renderSize().width();
    const int renderH = m_model->renderSize().height();
    if (renderW <= 0 || renderH <= 0) {
        return;
    }

    const QMatrix4x4 viewProj = m_camera.viewProjection(width(), height());

    glUseProgram(m_program);

    const int viewProjLoc = glGetUniformLocation(m_program, "uViewProj");
    glUniformMatrix4fv(viewProjLoc, 1, GL_FALSE, viewProj.constData());

    const int quadSizeLoc = glGetUniformLocation(m_program, "uQuadSize");
    glUniform2f(quadSizeLoc, static_cast<float>(renderW), static_cast<float>(renderH));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glUniform1i(glGetUniformLocation(m_program, "uTexture"), 0);

    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void OpenGLViewportWidget::wheelEvent(QWheelEvent* event)
{
    const float factor = event->angleDelta().y() > 0 ? kZoomFactor : (1.0f / kZoomFactor);
    m_camera.zoomAt(factor,
                    static_cast<float>(event->position().x()),
                    static_cast<float>(event->position().y()),
                    width(),
                    height());
    update();
    event->accept();
}

void OpenGLViewportWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        setFocus();
        m_panning = true;
        m_lastMousePos = event->pos();
        event->accept();
    }
}

void OpenGLViewportWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_panning) {
        return;
    }

    const QPoint delta = event->pos() - m_lastMousePos;
    m_lastMousePos = event->pos();
    m_camera.pan(static_cast<float>(delta.x()), static_cast<float>(delta.y()));
    update();
    event->accept();
}

void OpenGLViewportWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_panning = false;
        event->accept();
    }
}

void OpenGLViewportWidget::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_F && m_model != nullptr) {
        const int renderW = m_model->renderSize().width();
        const int renderH = m_model->renderSize().height();
        if (renderW > 0 && renderH > 0) {
            m_camera.focusOnImage(renderW, renderH, width(), height());
            update();
            event->accept();
            return;
        }
    }

    QOpenGLWidget::keyPressEvent(event);
}

void OpenGLViewportWidget::recreateGpuBuffers()
{
    if (m_model == nullptr) {
        return;
    }

    m_cudaInterop->unregisterAll();

    if (m_texture != 0) {
        glDeleteTextures(1, &m_texture);
        m_texture = 0;
    }
    if (m_pbos[0] != 0 || m_pbos[1] != 0) {
        glDeleteBuffers(2, m_pbos);
        m_pbos[0] = 0;
        m_pbos[1] = 0;
    }
    m_model->setPboIds(0, 0);

    const int w = m_model->renderSize().width();
    const int h = m_model->renderSize().height();
    const std::size_t byteSize = static_cast<std::size_t>(m_model->bufferByteSize());
    if (w <= 0 || h <= 0 || byteSize == 0) {
        return;
    }

    glGenBuffers(2, m_pbos);
    for (int i = 0; i < 2; ++i) {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pbos[i]);
        glBufferData(GL_PIXEL_UNPACK_BUFFER, static_cast<GLsizeiptr>(byteSize), nullptr, GL_DYNAMIC_DRAW);
    }
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    if (!m_cudaInterop->registerPbo(static_cast<unsigned int>(m_pbos[0]), byteSize, 0)
        || !m_cudaInterop->registerPbo(static_cast<unsigned int>(m_pbos[1]), byteSize, 1)) {
        return;
    }

    if (!m_cudaInterop->initPboRandomGray(0, w, h, 1u)
        || !m_cudaInterop->initPboRandomGray(1, w, h, 2u)) {
        return;
    }

    m_model->setPboIds(m_pbos[0], m_pbos[1]);

    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    uploadDisplayTexture();
}

void OpenGLViewportWidget::uploadDisplayTexture()
{
    if (m_model == nullptr || m_texture == 0 || m_pbos[0] == 0) {
        return;
    }

    const int w = m_model->renderSize().width();
    const int h = m_model->renderSize().height();

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pbos[0]);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA8,
                 w,
                 h,
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 nullptr);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void OpenGLViewportWidget::releaseGlResources()
{
    if (m_cudaInterop != nullptr) {
        m_cudaInterop->unregisterAll();
    }

    if (m_texture != 0) {
        glDeleteTextures(1, &m_texture);
        m_texture = 0;
    }
    if (m_pbos[0] != 0 || m_pbos[1] != 0) {
        glDeleteBuffers(2, m_pbos);
        m_pbos[0] = 0;
        m_pbos[1] = 0;
    }
    if (m_model != nullptr) {
        m_model->setPboIds(0, 0);
    }
    if (m_vao != 0) {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_vbo != 0) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_ebo != 0) {
        glDeleteBuffers(1, &m_ebo);
        m_ebo = 0;
    }
    if (m_program != 0) {
        glDeleteProgram(m_program);
        m_program = 0;
    }
    m_glInitialized = false;
}

GLuint OpenGLViewportWidget::compileShader(GLenum type, const char* source)
{
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    return shader;
}

GLuint OpenGLViewportWidget::linkProgram(GLuint vertexShader, GLuint fragmentShader)
{
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    return program;
}
