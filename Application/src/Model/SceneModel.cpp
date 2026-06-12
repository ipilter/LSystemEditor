#include "SceneModel.h"

#include "AppSettings.h"

namespace {

constexpr int kMinRenderDimension = 1;
constexpr int kMaxRenderDimension = 10000;
constexpr int kMinMaxSamplesPerPixel = 0;
constexpr int kMaxMaxSamplesPerPixel = 1'000'000;
constexpr int kMinPreviewStepsPerLevel = 0;
constexpr int kMaxPreviewStepsPerLevel = 128;
constexpr float kMinCreaseAngleDeg = 0.0f;
constexpr float kMaxCreaseAngleDeg = 180.0f;
} // namespace

SceneModel::SceneModel(QObject* parent)
    : QObject(parent)
    , m_clearColor(AppSettings::instance().clearColor())
    , m_renderSize(AppSettings::instance().renderSize())
    , m_maxSamplesPerPixel(AppSettings::instance().maxSamplesPerPixel())
    , m_previewStepsPerLevel(AppSettings::instance().previewStepsPerLevel())
    , m_accelBvhColor(AppSettings::instance().accelBvhColor())
    , m_creaseAngleDeg(AppSettings::instance().creaseAngleDeg())
{
}

QColor SceneModel::clearColor() const
{
    return m_clearColor;
}

void SceneModel::setClearColor(const QColor& color)
{
    if (!color.isValid() || m_clearColor == color) {
        return;
    }

    m_clearColor = color;
    AppSettings::instance().setClearColor(color);
    emit clearColorChanged(m_clearColor);
}

QSize SceneModel::renderSize() const
{
    return m_renderSize;
}

void SceneModel::setRenderSize(int width, int height)
{
    const int w = clampDimension(width);
    const int h = clampDimension(height);
    if (m_renderSize.width() == w && m_renderSize.height() == h) {
        return;
    }

    m_renderSize = QSize(w, h);
    AppSettings::instance().setRenderSize(w, h);
    emit renderSizeChanged(m_renderSize);
}

int SceneModel::bufferByteSize() const
{
    return m_renderSize.width() * m_renderSize.height() * 4;
}

int SceneModel::maxSamplesPerPixel() const
{
    return m_maxSamplesPerPixel;
}

void SceneModel::setMaxSamplesPerPixel(int value)
{
    const int clamped = clampMaxSamples(value);
    if (m_maxSamplesPerPixel == clamped) {
        return;
    }

    m_maxSamplesPerPixel = clamped;
    AppSettings::instance().setMaxSamplesPerPixel(clamped);
    emit maxSamplesPerPixelChanged(m_maxSamplesPerPixel);
}

int SceneModel::previewStepsPerLevel() const
{
    return m_previewStepsPerLevel;
}

void SceneModel::setPreviewStepsPerLevel(int value)
{
    const int clamped = clampPreviewSteps(value);
    if (m_previewStepsPerLevel == clamped) {
        return;
    }

    m_previewStepsPerLevel = clamped;
    AppSettings::instance().setPreviewStepsPerLevel(clamped);
    emit previewStepsPerLevelChanged(m_previewStepsPerLevel);
}

MeshAccelBoundsOverlayMode SceneModel::boundsOverlayMode() const
{
    return m_boundsOverlayMode;
}

void SceneModel::setBoundsOverlayMode(MeshAccelBoundsOverlayMode mode)
{
    const MeshAccelBoundsOverlayMode clamped = clampBoundsOverlayMode(mode);
    if (m_boundsOverlayMode == clamped) {
        return;
    }

    m_boundsOverlayMode = clamped;
    emit boundsOverlayModeChanged(m_boundsOverlayMode);
}

QColor SceneModel::accelBvhColor() const
{
    return m_accelBvhColor;
}

void SceneModel::setAccelBvhColor(const QColor& color)
{
    if (!color.isValid() || m_accelBvhColor == color) {
        return;
    }

    m_accelBvhColor = color;
    AppSettings::instance().setAccelBvhColor(color);
    emit accelBvhColorChanged(m_accelBvhColor);
}

float SceneModel::creaseAngleDeg() const
{
    return m_creaseAngleDeg;
}

void SceneModel::setCreaseAngleDeg(float value)
{
    const float clamped = clampCreaseAngleDeg(value);
    if (m_creaseAngleDeg == clamped) {
        return;
    }

    m_creaseAngleDeg = clamped;
    AppSettings::instance().setCreaseAngleDeg(clamped);
    emit sceneChanged();
}

const std::vector<ProceduralInstance>& SceneModel::proceduralInstances() const
{
    return m_proceduralInstances;
}

void SceneModel::addProceduralInstance(ProceduralInstance instance)
{
    if (instance.commandString.empty()) {
        return;
    }
    m_proceduralInstances.push_back(std::move(instance));
    emit sceneChanged();
}

GLuint SceneModel::pboId(int index) const
{
    if (index < 0 || index >= bufferCount) {
        return 0;
    }
    return m_pboIds[index];
}

void SceneModel::setPboIds(GLuint pbo0, GLuint pbo1)
{
    m_pboIds[0] = pbo0;
    m_pboIds[1] = pbo1;
}

int SceneModel::clampDimension(int value)
{
    if (value < kMinRenderDimension) {
        return kMinRenderDimension;
    }
    if (value > kMaxRenderDimension) {
        return kMaxRenderDimension;
    }
    return value;
}

int SceneModel::clampMaxSamples(int value)
{
    if (value < kMinMaxSamplesPerPixel) {
        return kMinMaxSamplesPerPixel;
    }
    if (value > kMaxMaxSamplesPerPixel) {
        return kMaxMaxSamplesPerPixel;
    }
    return value;
}

int SceneModel::clampPreviewSteps(int value)
{
    if (value < kMinPreviewStepsPerLevel) {
        return kMinPreviewStepsPerLevel;
    }
    if (value > kMaxPreviewStepsPerLevel) {
        return kMaxPreviewStepsPerLevel;
    }
    return value;
}

MeshAccelBoundsOverlayMode SceneModel::clampBoundsOverlayMode(MeshAccelBoundsOverlayMode mode)
{
    switch (mode) {
    case MeshAccelBoundsOverlayMode::Off:
    case MeshAccelBoundsOverlayMode::Bvh:
        return mode;
    default: {
        const int raw = static_cast<int>(mode);
        if (raw == 1 || raw == 2 || raw == 3) {
            return MeshAccelBoundsOverlayMode::Bvh;
        }
        return MeshAccelBoundsOverlayMode::Off;
    }
    }
}

float SceneModel::clampCreaseAngleDeg(float value)
{
    if (value < kMinCreaseAngleDeg) {
        return kMinCreaseAngleDeg;
    }
    if (value > kMaxCreaseAngleDeg) {
        return kMaxCreaseAngleDeg;
    }
    return value;
}
