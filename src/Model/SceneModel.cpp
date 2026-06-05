#include "SceneModel.h"

#include "AppSettings.h"

namespace {

constexpr int kMinRenderDimension = 1;
constexpr int kMaxRenderDimension = 10000;
constexpr int kMinMaxSamplesPerPixel = 0;
constexpr int kMaxMaxSamplesPerPixel = 1'000'000;
constexpr int kMinPreviewStepsPerLevel = 0;
constexpr int kMaxPreviewStepsPerLevel = 128;
constexpr float kMinSunAzimuth = 0.0f;
constexpr float kMaxSunAzimuth = 360.0f;
constexpr float kMinSunElevation = -90.0f;
constexpr float kMaxSunElevation = 90.0f;
constexpr float kMinSunDiskSize = 0.1f;
constexpr float kMaxSunDiskSize = 30.0f;
constexpr int kMinSecondaryBounceCount = 0;
constexpr int kMaxSecondaryBounceCount = 8;
} // namespace

SceneModel::SceneModel(QObject* parent)
    : QObject(parent)
    , m_clearColor(AppSettings::instance().clearColor())
    , m_renderSize(AppSettings::instance().renderSize())
    , m_maxSamplesPerPixel(AppSettings::instance().maxSamplesPerPixel())
    , m_previewStepsPerLevel(AppSettings::instance().previewStepsPerLevel())
    , m_accelBvhColor(AppSettings::instance().accelBvhColor())
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

RenderDebugVisualMode SceneModel::debugVisualMode() const
{
    return m_debugVisualMode;
}

void SceneModel::setDebugVisualMode(RenderDebugVisualMode mode)
{
    const RenderDebugVisualMode clamped = clampVisualMode(mode);
    if (m_debugVisualMode == clamped) {
        return;
    }

    m_debugVisualMode = clamped;
    emit debugVisualModeChanged(m_debugVisualMode);
}

float SceneModel::sunAzimuthDeg() const
{
    return m_sunAzimuthDeg;
}

float SceneModel::sunElevationDeg() const
{
    return m_sunElevationDeg;
}

QColor SceneModel::sunColor() const
{
    return m_sunColor;
}

float SceneModel::sunDiskSizeDeg() const
{
    return m_sunDiskSizeDeg;
}

void SceneModel::setSunAzimuthDeg(float value)
{
    const float clamped = clampSunAzimuth(value);
    if (m_sunAzimuthDeg == clamped) {
        return;
    }

    m_sunAzimuthDeg = clamped;
    emit sunSettingsChanged();
}

void SceneModel::setSunElevationDeg(float value)
{
    const float clamped = clampSunElevation(value);
    if (m_sunElevationDeg == clamped) {
        return;
    }

    m_sunElevationDeg = clamped;
    emit sunSettingsChanged();
}

void SceneModel::setSunColor(const QColor& color)
{
    if (!color.isValid() || m_sunColor == color) {
        return;
    }

    m_sunColor = color;
    emit sunSettingsChanged();
}

void SceneModel::setSunDiskSizeDeg(float value)
{
    const float clamped = clampSunDiskSize(value);
    if (m_sunDiskSizeDeg == clamped) {
        return;
    }

    m_sunDiskSizeDeg = clamped;
    emit sunSettingsChanged();
}

int SceneModel::secondaryBounceCount() const
{
    return m_secondaryBounceCount;
}

void SceneModel::setSecondaryBounceCount(int value)
{
    const int clamped = clampSecondaryBounceCount(value);
    if (m_secondaryBounceCount == clamped) {
        return;
    }

    m_secondaryBounceCount = clamped;
    emit secondaryBounceCountChanged(m_secondaryBounceCount);
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

RenderDebugVisualMode SceneModel::clampVisualMode(RenderDebugVisualMode mode)
{
    switch (mode) {
    case RenderDebugVisualMode::Normals:
    case RenderDebugVisualMode::Off:
        return mode;
    default:
        return RenderDebugVisualMode::Normals;
    }
}

float SceneModel::clampSunAzimuth(float value)
{
    if (value < kMinSunAzimuth) {
        return kMinSunAzimuth;
    }
    if (value > kMaxSunAzimuth) {
        return kMaxSunAzimuth;
    }
    return value;
}

float SceneModel::clampSunElevation(float value)
{
    if (value < kMinSunElevation) {
        return kMinSunElevation;
    }
    if (value > kMaxSunElevation) {
        return kMaxSunElevation;
    }
    return value;
}

float SceneModel::clampSunDiskSize(float value)
{
    if (value < kMinSunDiskSize) {
        return kMinSunDiskSize;
    }
    if (value > kMaxSunDiskSize) {
        return kMaxSunDiskSize;
    }
    return value;
}

int SceneModel::clampSecondaryBounceCount(int value)
{
    if (value < kMinSecondaryBounceCount) {
        return kMinSecondaryBounceCount;
    }
    if (value > kMaxSecondaryBounceCount) {
        return kMaxSecondaryBounceCount;
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
