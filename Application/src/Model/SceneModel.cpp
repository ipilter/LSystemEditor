#include "SceneModel.h"

#include "AppSettings.h"
#include "Brdf/BrdfDebug.h"
#include "PhysicalCamera.h"

#include <algorithm>

namespace {

constexpr int kMinRenderDimension = 1;
constexpr int kMaxRenderDimension = 10000;
constexpr int kMinMaxSamplesPerPixel = 0;
constexpr int kMaxMaxSamplesPerPixel = 1'000'000;
constexpr int kMinPreviewStepsPerLevel = 0;
constexpr int kMaxPreviewStepsPerLevel = 8;
constexpr int kMinRussianRouletteMinDepth = 0;
constexpr int kMaxRussianRouletteMinDepth = 64;
constexpr float kMinCreaseAngleDeg = 0.0f;
constexpr float kMaxCreaseAngleDeg = 180.0f;
constexpr float kMinEnvironmentIntensity = 0.0f;
constexpr float kMaxEnvironmentIntensity = 100.0f;
constexpr int kMinMinSamples = 1;
constexpr int kMaxMinSamples = 10'000;
constexpr float kMinRelativeErrorThreshold = 0.001f;
constexpr float kMaxRelativeErrorThreshold = 1.0f;
constexpr float kMinFocusDistanceMm = PhysicalCamera::kMinFocalLengthMm;
constexpr float kMaxFocusDistanceMm = 500'000.0f;
} // namespace

SceneModel::SceneModel(QObject* parent)
    : QObject(parent)
    , m_clearColor(AppSettings::instance().clearColor())
    , m_renderSize(AppSettings::instance().renderSize())
    , m_maxSamplesPerPixel(AppSettings::instance().maxSamplesPerPixel())
    , m_minSamples(AppSettings::instance().minSamples())
    , m_relativeErrorThreshold(AppSettings::instance().relativeErrorThreshold())
    , m_previewStepsPerLevel(AppSettings::instance().previewStepsPerLevel())
    , m_russianRouletteMinDepth(AppSettings::instance().russianRouletteMinDepth())
    , m_accelBvhColor(AppSettings::instance().accelBvhColor())
    , m_creaseAngleDeg(AppSettings::instance().creaseAngleDeg())
    , m_environmentHdrPath(AppSettings::instance().environmentHdrPath())
    , m_environmentIntensity(AppSettings::instance().environmentIntensity())
    , m_fStop(AppSettings::instance().fStop())
    , m_focalLengthMm(AppSettings::instance().focalLengthMm())
    , m_shutterSpeedSeconds(AppSettings::instance().shutterSpeedSeconds())
    , m_iso(AppSettings::instance().iso())
    , m_regionRenderEnabled(AppSettings::instance().regionRenderEnabled())
    , m_regionRenderColor(AppSettings::instance().regionRenderColor())
{
    const QPoint bottomLeft = AppSettings::instance().regionBottomLeft();
    const QPoint topRight = AppSettings::instance().regionTopRight();
    m_regionRect = normalizeRegionRect(
        bottomLeft.x(),
        topRight.y(),
        topRight.x(),
        bottomLeft.y(),
        m_renderSize.width(),
        m_renderSize.height());
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
    clampRegionToRenderSize();
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

int SceneModel::minSamples() const
{
    return m_minSamples;
}

void SceneModel::setMinSamples(int value)
{
    const int clamped = clampMinSamples(value);
    if (m_minSamples == clamped) {
        return;
    }

    m_minSamples = clamped;
    AppSettings::instance().setMinSamples(clamped);
    emit minSamplesChanged(m_minSamples);
}

float SceneModel::relativeErrorThreshold() const
{
    return m_relativeErrorThreshold;
}

void SceneModel::setRelativeErrorThreshold(float value)
{
    const float clamped = clampRelativeErrorThreshold(value);
    if (m_relativeErrorThreshold == clamped) {
        return;
    }

    m_relativeErrorThreshold = clamped;
    AppSettings::instance().setRelativeErrorThreshold(clamped);
    emit relativeErrorThresholdChanged(m_relativeErrorThreshold);
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

int SceneModel::russianRouletteMinDepth() const
{
    return m_russianRouletteMinDepth;
}

void SceneModel::setRussianRouletteMinDepth(int value)
{
    const int clamped = clampRussianRouletteMinDepth(value);
    if (m_russianRouletteMinDepth == clamped) {
        return;
    }

    m_russianRouletteMinDepth = clamped;
    AppSettings::instance().setRussianRouletteMinDepth(clamped);
    emit russianRouletteMinDepthChanged(m_russianRouletteMinDepth);
}

RenderViewOverlayMode SceneModel::boundsOverlayMode() const
{
    return m_renderViewOverlayMode;
}

void SceneModel::setBoundsOverlayMode(RenderViewOverlayMode mode)
{
    const RenderViewOverlayMode clamped = clampBoundsOverlayMode(mode);
    if (m_renderViewOverlayMode == clamped) {
        return;
    }

    m_renderViewOverlayMode = clamped;
    emit boundsOverlayModeChanged(m_renderViewOverlayMode);
}

int SceneModel::brdfDebugFlags() const
{
    return m_brdfDebugFlags;
}

void SceneModel::setBrdfDebugFlags(int flags)
{
    const int clamped = clampBrdfDebugFlags(flags);
    if (m_brdfDebugFlags == clamped) {
        return;
    }

    m_brdfDebugFlags = clamped;
    emit brdfDebugFlagsChanged(m_brdfDebugFlags);
}

bool SceneModel::sceneOverlayVisible() const
{
    return m_sceneOverlayVisible;
}

void SceneModel::setSceneOverlayVisible(bool visible)
{
    if (m_sceneOverlayVisible == visible) {
        return;
    }

    m_sceneOverlayVisible = visible;
    emit sceneOverlayVisibleChanged(m_sceneOverlayVisible);
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

QString SceneModel::environmentHdrPath() const
{
    return m_environmentHdrPath;
}

void SceneModel::setEnvironmentHdrPath(const QString& path)
{
    const QString normalized = path.trimmed();
    if (m_environmentHdrPath == normalized) {
        return;
    }

    m_environmentHdrPath = normalized;
    AppSettings::instance().setEnvironmentHdrPath(normalized);
    emit environmentHdrPathChanged(m_environmentHdrPath);
}

float SceneModel::environmentIntensity() const
{
    return m_environmentIntensity;
}

void SceneModel::setEnvironmentIntensity(float value)
{
    const float clamped = clampEnvironmentIntensity(value);
    if (m_environmentIntensity == clamped) {
        return;
    }

    m_environmentIntensity = clamped;
    AppSettings::instance().setEnvironmentIntensity(clamped);
    emit environmentIntensityChanged(m_environmentIntensity);
}

float SceneModel::fStop() const
{
    return m_fStop;
}

void SceneModel::setFStop(float value)
{
    const float clamped = clampFStop(value);
    if (m_fStop == clamped) {
        return;
    }

    m_fStop = clamped;
    AppSettings::instance().setFStop(clamped);
    emit fStopChanged(m_fStop);
}

float SceneModel::focalLengthMm() const
{
    return m_focalLengthMm;
}

void SceneModel::setFocalLengthMm(float value)
{
    const float clamped = clampFocalLengthMm(value);
    if (m_focalLengthMm == clamped) {
        return;
    }

    m_focalLengthMm = clamped;
    AppSettings::instance().setFocalLengthMm(clamped);
    emit focalLengthMmChanged(m_focalLengthMm);
}

float SceneModel::shutterSpeedSeconds() const
{
    return m_shutterSpeedSeconds;
}

void SceneModel::setShutterSpeedSeconds(float value)
{
    const float clamped = clampShutterSpeedSeconds(value);
    if (m_shutterSpeedSeconds == clamped) {
        return;
    }

    m_shutterSpeedSeconds = clamped;
    AppSettings::instance().setShutterSpeedSeconds(clamped);
    emit shutterSpeedSecondsChanged(m_shutterSpeedSeconds);
}

float SceneModel::iso() const
{
    return m_iso;
}

void SceneModel::setIso(float value)
{
    const float clamped = clampIso(value);
    if (m_iso == clamped) {
        return;
    }

    m_iso = clamped;
    AppSettings::instance().setIso(clamped);
    emit isoChanged(m_iso);
}

glm::vec3 SceneModel::focusPoint() const
{
    return m_focusPoint;
}

bool SceneModel::focusValid() const
{
    return m_focusValid;
}

bool SceneModel::focusPointPinned() const
{
    return m_focusPointPinned;
}

float SceneModel::focusDistanceMm() const
{
    return m_focusDistanceMm;
}

void SceneModel::pinFocusPoint(const glm::vec3& point)
{
    const bool pinnedChanged = !m_focusPointPinned;

    m_focusPoint = point;
    m_focusValid = true;
    m_focusPointPinned = true;

    if (pinnedChanged) {
        emit focusPointPinnedChanged(true);
    }
}

void SceneModel::setFocusDistanceMm(float distanceMm)
{
    const float clamped = clampFocusDistanceMm(distanceMm);
    const bool distanceChanged = m_focusDistanceMm != clamped;
    const bool pinnedChanged = m_focusPointPinned;

    m_focusDistanceMm = clamped;
    m_focusPointPinned = false;

    if (distanceChanged) {
        emit focusDistanceMmChanged(m_focusDistanceMm);
    }
    if (pinnedChanged) {
        emit focusPointPinnedChanged(false);
    }
}

void SceneModel::syncFocusDistanceMm(float distanceMm)
{
    const float clamped = clampFocusDistanceMm(distanceMm);
    if (m_focusDistanceMm == clamped) {
        return;
    }

    m_focusDistanceMm = clamped;
    emit focusDistanceMmChanged(m_focusDistanceMm);
}

void SceneModel::clearFocusPoint()
{
    if (!m_focusValid && !m_focusPointPinned) {
        return;
    }

    m_focusValid = false;
    if (m_focusPointPinned) {
        m_focusPointPinned = false;
        emit focusPointPinnedChanged(false);
    }
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

void SceneModel::resetScene()
{
    if (m_proceduralInstances.empty()) {
        return;
    }

    m_proceduralInstances.clear();
    emit sceneChanged();
}

bool SceneModel::regionRenderEnabled() const
{
    return m_regionRenderEnabled;
}

void SceneModel::setRegionRenderEnabled(bool enabled)
{
    if (m_regionRenderEnabled == enabled) {
        return;
    }

    m_regionRenderEnabled = enabled;
    AppSettings::instance().setRegionRenderEnabled(enabled);
    emit regionRenderEnabledChanged(m_regionRenderEnabled);
}

QRect SceneModel::regionRect() const
{
    return m_regionRect;
}

void SceneModel::setRegionRect(int minX, int minY, int maxX, int maxY)
{
    const QRect normalized = normalizeRegionRect(
        minX,
        minY,
        maxX,
        maxY,
        m_renderSize.width(),
        m_renderSize.height());
    if (m_regionRect == normalized) {
        return;
    }

    m_regionRect = normalized;
    AppSettings::instance().setRegionBottomLeft(normalized.left(), normalized.bottom());
    AppSettings::instance().setRegionTopRight(normalized.right(), normalized.top());
    emit regionRectChanged(m_regionRect);
}

QColor SceneModel::regionRenderColor() const
{
    return m_regionRenderColor;
}

void SceneModel::setRegionRenderColor(const QColor& color)
{
    if (!color.isValid() || m_regionRenderColor == color) {
        return;
    }

    m_regionRenderColor = color;
    AppSettings::instance().setRegionRenderColor(color);
    emit regionRenderColorChanged(m_regionRenderColor);
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

int SceneModel::clampMinSamples(int value)
{
    if (value < kMinMinSamples) {
        return kMinMinSamples;
    }
    if (value > kMaxMinSamples) {
        return kMaxMinSamples;
    }
    return value;
}

float SceneModel::clampRelativeErrorThreshold(float value)
{
    if (value < kMinRelativeErrorThreshold) {
        return kMinRelativeErrorThreshold;
    }
    if (value > kMaxRelativeErrorThreshold) {
        return kMaxRelativeErrorThreshold;
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

int SceneModel::clampRussianRouletteMinDepth(int value)
{
    if (value < kMinRussianRouletteMinDepth) {
        return kMinRussianRouletteMinDepth;
    }
    if (value > kMaxRussianRouletteMinDepth) {
        return kMaxRussianRouletteMinDepth;
    }
    return value;
}

RenderViewOverlayMode SceneModel::clampBoundsOverlayMode(RenderViewOverlayMode mode)
{
    switch (mode) {
    case RenderViewOverlayMode::Render:
    case RenderViewOverlayMode::Bvh:
    case RenderViewOverlayMode::AdaptiveSampling:
    case RenderViewOverlayMode::Uv:
        return mode;
    default:
        return RenderViewOverlayMode::Render;
    }
}

int SceneModel::clampBrdfDebugFlags(int flags)
{
    constexpr int kAllowedMask = BrdfDebugFlags::kForceTransmitLobeOnly
        | BrdfDebugFlags::kDisableRefract
        | BrdfDebugFlags::kDisableReflect
        | BrdfDebugFlags::kDisableTirFallback
        | BrdfDebugFlags::kTintGlassPaths;
    return flags & kAllowedMask;
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

float SceneModel::clampFStop(float value)
{
    return PhysicalCamera::clampFStop(value);
}

float SceneModel::clampFocalLengthMm(float value)
{
    return PhysicalCamera::clampFocalLengthMm(value);
}

float SceneModel::clampShutterSpeedSeconds(float value)
{
    return PhysicalCamera::clampShutterSpeedSeconds(value);
}

float SceneModel::clampIso(float value)
{
    return PhysicalCamera::snapIsoToNearestPreset(value);
}

float SceneModel::clampFocusDistanceMm(float value)
{
    if (value < kMinFocusDistanceMm) {
        return kMinFocusDistanceMm;
    }
    if (value > kMaxFocusDistanceMm) {
        return kMaxFocusDistanceMm;
    }
    return value;
}

float SceneModel::clampEnvironmentIntensity(float value)
{
    if (value < kMinEnvironmentIntensity) {
        return kMinEnvironmentIntensity;
    }
    if (value > kMaxEnvironmentIntensity) {
        return kMaxEnvironmentIntensity;
    }
    return value;
}

QRect SceneModel::normalizeRegionRect(int minX, int minY, int maxX, int maxY, int renderW, int renderH)
{
    const int maxCoordX = std::max(0, renderW - 1);
    const int maxCoordY = std::max(0, renderH - 1);
    const int left = std::max(0, std::min(minX, maxX));
    const int right = std::min(maxCoordX, std::max(minX, maxX));
    const int top = std::max(0, std::min(minY, maxY));
    const int bottom = std::min(maxCoordY, std::max(minY, maxY));
    return QRect(QPoint(left, top), QPoint(right, bottom));
}

void SceneModel::clampRegionToRenderSize()
{
    const QRect normalized = normalizeRegionRect(
        m_regionRect.left(),
        m_regionRect.top(),
        m_regionRect.right(),
        m_regionRect.bottom(),
        m_renderSize.width(),
        m_renderSize.height());
    if (m_regionRect == normalized) {
        return;
    }

    m_regionRect = normalized;
    AppSettings::instance().setRegionBottomLeft(normalized.left(), normalized.bottom());
    AppSettings::instance().setRegionTopRight(normalized.right(), normalized.top());
    emit regionRectChanged(m_regionRect);
}
