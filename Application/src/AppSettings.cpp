#include "AppSettings.h"

#include "PhysicalCamera.h"
#include "SceneUnits.h"

#include <QSettings>

#include <algorithm>

namespace {

constexpr int kDefaultUiDebounceMs = 300;
constexpr int kDefaultMaxSamplesDebounceMs = 0;
constexpr int kDefaultPreviewStepsDebounceMs = 0;
constexpr int kMinDebounceMs = 0;
constexpr int kMaxDebounceMs = 2000;

constexpr int kDefaultRenderDimension = 16;
constexpr int kMinRenderDimension = 1;
constexpr int kMaxRenderDimension = 8192;
constexpr int kDefaultClearColorComponent = 10;
constexpr int kDefaultMaxSamplesPerPixel = 1024;
constexpr int kMinMaxSamplesPerPixel = 0;
constexpr int kMaxMaxSamplesPerPixel = 1'000'000;
constexpr int kMinPreviewStepsPerLevel = 0;
constexpr int kMaxPreviewStepsPerLevel = 128;
constexpr int kDefaultUiUpdateEveryNSamples = 1;
constexpr int kMinUiUpdateEveryNSamples = 1;
constexpr int kMaxUiUpdateEveryNSamples = 10'000;
constexpr int kMinRussianRouletteMinDepth = 0;
constexpr int kMaxRussianRouletteMinDepth = 64;
constexpr int kDefaultRussianRouletteMinDepth = 3;
constexpr int kMinMaxSubsurfaceScatters = 1;
constexpr int kMaxMaxSubsurfaceScatters = 128;
constexpr int kDefaultMaxSubsurfaceScatters = 8;
constexpr const char* kSettingsOrg = "PathTracer";
constexpr const char* kSettingsApp = "pathtracer";
constexpr const char* kDebounceGroup = "debounce";
constexpr const char* kRenderWidthKey = "renderWidth";
constexpr const char* kRenderHeightKey = "renderHeight";
constexpr const char* kClearColorRedKey = "clearColorRed";
constexpr const char* kClearColorGreenKey = "clearColorGreen";
constexpr const char* kClearColorBlueKey = "clearColorBlue";
constexpr int kDefaultMinSamples = 16;
constexpr int kMinMinSamples = 1;
constexpr int kMaxMinSamples = 10'000;
constexpr float kDefaultRelativeErrorThreshold = 0.02f;
constexpr float kMinRelativeErrorThreshold = 0.001f;
constexpr float kMaxRelativeErrorThreshold = 1.0f;
constexpr const char* kMaxSamplesPerPixelKey = "maxSamplesPerPixel";
constexpr const char* kMinSamplesKey = "minSamples";
constexpr const char* kRelativeErrorThresholdKey = "relativeErrorThreshold";
constexpr const char* kPreviewStepsPerLevelKey = "previewStepsPerLevel";
constexpr const char* kUiUpdateEveryNSamplesKey = "uiUpdateEveryNSamples";
constexpr const char* kRussianRouletteMinDepthKey = "russianRouletteMinDepth";
constexpr const char* kMaxSubsurfaceScattersKey = "maxSubsurfaceScatters";
constexpr float kDefaultCreaseAngleDeg = 50.0f;
constexpr float kMinCreaseAngleDeg = 0.0f;
constexpr float kMaxCreaseAngleDeg = 180.0f;
constexpr float kMinEnvironmentIntensity = 0.0f;
constexpr float kMaxEnvironmentIntensity = 100.0f;
constexpr int kMinEnvironmentRotationY = 0;
constexpr int kMaxEnvironmentRotationY = 359;
constexpr const char* kCreaseAngleDegKey = "creaseAngleDeg";
constexpr const char* kEnvironmentHdrPathKey = "environmentHdrPath";
constexpr const char* kLsystemFilePathKey = "lsystemFilePath";
constexpr const char* kLsystemEditorFontSizeKey = "lsystemEditorFontSize";
constexpr const char* kLogFontSizeKey = "logFontSize";
constexpr const char* kEnvironmentIntensityKey = "environmentIntensity";
constexpr const char* kEnvironmentRotationYKey = "environmentRotationY";
constexpr const char* kFStopKey = "fStop";
constexpr const char* kFocalLengthMmKey = "focalLengthMm";
constexpr const char* kShutterSpeedSecondsKey = "shutterSpeedSeconds";
constexpr const char* kIsoKey = "iso";
constexpr const char* kWindowGeometryKey = "windowGeometry";
constexpr const char* kHorizontalSplitterStateKey = "horizontalSplitterState";
constexpr const char* kVerticalSplitterStateKey = "verticalSplitterState";
constexpr const char* kAccelBvhColorRedKey = "accelBvhColorRed";
constexpr const char* kAccelBvhColorGreenKey = "accelBvhColorGreen";
constexpr const char* kAccelBvhColorBlueKey = "accelBvhColorBlue";
constexpr const char* kLegacyAccelAabbColorRedKey = "accelAabbColorRed";
constexpr const char* kLegacyAccelAabbColorGreenKey = "accelAabbColorGreen";
constexpr const char* kLegacyAccelAabbColorBlueKey = "accelAabbColorBlue";
constexpr const char* kLegacyAccelOctreeColorRedKey = "accelOctreeColorRed";
constexpr const char* kLegacyAccelOctreeColorGreenKey = "accelOctreeColorGreen";
constexpr const char* kLegacyAccelOctreeColorBlueKey = "accelOctreeColorBlue";
constexpr const char* kCameraSettingsGroup = "camera";
constexpr const char* kCameraLinearSpeedKey = "linearSpeedMmPerSec";
constexpr const char* kCameraAngularSpeedKey = "angularSpeedRadPerSec";
constexpr const char* kCameraThrustLinearKey = "thrustLinear";
constexpr const char* kCameraDragLinearKey = "dragLinear";
constexpr const char* kCameraThrustAngularKey = "thrustAngular";
constexpr const char* kCameraDragAngularKey = "dragAngular";
constexpr const char* kCameraMouseSensitivityKey = "mouseSensitivity";
constexpr const char* kCameraMouseSensitivityRadPerPixelKey = "mouseSensitivityRadPerPixel";
constexpr const char* kCameraTickIntervalMsKey = "tickIntervalMs";
constexpr const char* kCameraMotionResetThrottleMsKey = "motionResetThrottleMs";
constexpr const char* kCameraMotionStopDebounceMsKey = "motionStopDebounceMs";
constexpr const char* kCameraDefaultPositionXmmKey = "defaultPositionXmm";
constexpr const char* kCameraDefaultPositionYmmKey = "defaultPositionYmm";
constexpr const char* kCameraDefaultPositionZmmKey = "defaultPositionZmm";
constexpr const char* kCameraDefaultYawDegKey = "defaultYawDeg";
constexpr const char* kCameraDefaultPitchDegKey = "defaultPitchDeg";
constexpr const char* kCameraDefaultRollDegKey = "defaultRollDeg";
constexpr const char* kRegionRenderGroup = "regionRender";
constexpr const char* kRegionRenderEnabledKey = "enabled";
constexpr const char* kRegionBottomLeftXKey = "bottomLeftX";
constexpr const char* kRegionBottomLeftYKey = "bottomLeftY";
constexpr const char* kRegionTopRightXKey = "topRightX";
constexpr const char* kRegionTopRightYKey = "topRightY";
constexpr const char* kRegionRenderColorRedKey = "colorRed";
constexpr const char* kRegionRenderColorGreenKey = "colorGreen";
constexpr const char* kRegionRenderColorBlueKey = "colorBlue";
constexpr int kDefaultRegionRenderColorRed = 255;
constexpr int kDefaultRegionRenderColorGreen = 255;
constexpr int kDefaultRegionRenderColorBlue = 128;
constexpr float kMinCameraLinearSpeed = 1.0f;
constexpr float kMaxCameraLinearSpeed = 10000.0f;
constexpr float kMinCameraAngularSpeed = 0.05f;
constexpr float kMaxCameraAngularSpeed = 4.0f;
constexpr float kMinCameraMouseSensitivity = 0.0001f;
constexpr float kMaxCameraMouseSensitivity = 0.05f;
constexpr float kLegacyMouseSensitivityScaleThreshold = 0.01f;
constexpr float kLegacyMouseSensitivityToRadPerPixel = 150.0f;
constexpr int kMinCameraTickIntervalMs = 8;
constexpr int kMaxCameraTickIntervalMs = 100;
constexpr int kMinCameraMotionTimingMs = 0;
constexpr int kMaxCameraMotionTimingMs = 2000;
constexpr float kMinCameraDefaultPositionMm = -SceneUnits::kDefaultRayTMaxMm;
constexpr float kMaxCameraDefaultPositionMm = SceneUnits::kDefaultRayTMaxMm;
constexpr float kMinCameraDefaultAngleDeg = -360.0f;
constexpr float kMaxCameraDefaultAngleDeg = 360.0f;
constexpr float kMinCameraDefaultPitchDeg = -89.0f;
constexpr float kMaxCameraDefaultPitchDeg = 89.0f;
constexpr int kDefaultEditorFontSize = 9;
constexpr int kMinEditorFontSize = 6;
constexpr int kMaxEditorFontSize = 48;

bool isKnownDebounceElementId(const QString& elementId)
{
    return elementId == DebounceElementIds::kRenderSize
        || elementId == DebounceElementIds::kMaxSamples
        || elementId == DebounceElementIds::kPreviewSteps
        || elementId == DebounceElementIds::kPhysicalCamera;
}

void clampRegionRectToRenderSize(
    int renderW,
    int renderH,
    int& bottomLeftX,
    int& bottomLeftY,
    int& topRightX,
    int& topRightY)
{
    const int maxX = std::max(0, renderW - 1);
    const int maxY = std::max(0, renderH - 1);
    auto clampCoord = [](int value, int maxInclusive) {
        if (value < 0) {
            return 0;
        }
        if (value > maxInclusive) {
            return maxInclusive;
        }
        return value;
    };
    bottomLeftX = clampCoord(bottomLeftX, maxX);
    bottomLeftY = clampCoord(bottomLeftY, maxY);
    topRightX = clampCoord(topRightX, maxX);
    topRightY = clampCoord(topRightY, maxY);
}

void defaultRegionRectForRenderSize(int renderW, int renderH, int& bottomLeftX, int& bottomLeftY, int& topRightX, int& topRightY)
{
    const int maxX = std::max(0, renderW - 1);
    const int maxY = std::max(0, renderH - 1);
    bottomLeftX = 0;
    bottomLeftY = maxY;
    topRightX = maxX;
    topRightY = 0;
}

} // namespace

bool CameraDynamicsSettings::operator==(const CameraDynamicsSettings& other) const
{
    return linearSpeedMmPerSec == other.linearSpeedMmPerSec
        && angularSpeedRadPerSec == other.angularSpeedRadPerSec
        && mouseSensitivity == other.mouseSensitivity
        && tickIntervalMs == other.tickIntervalMs
        && motionResetThrottleMs == other.motionResetThrottleMs
        && motionStopDebounceMs == other.motionStopDebounceMs
        && defaultPositionXmm == other.defaultPositionXmm
        && defaultPositionYmm == other.defaultPositionYmm
        && defaultPositionZmm == other.defaultPositionZmm
        && defaultYawDeg == other.defaultYawDeg
        && defaultPitchDeg == other.defaultPitchDeg
        && defaultRollDeg == other.defaultRollDeg;
}

AppSettings* AppSettings::s_instance = nullptr;

AppSettings& AppSettings::instance()
{
    return *s_instance;
}

void AppSettings::setInstance(AppSettings* settings)
{
    s_instance = settings;
}

AppSettings::AppSettings(QObject* parent)
    : QObject(parent)
    , m_renderSize(kDefaultRenderDimension, kDefaultRenderDimension)
    , m_clearColor(kDefaultClearColorComponent, kDefaultClearColorComponent, kDefaultClearColorComponent)
    , m_maxSamplesPerPixel(kDefaultMaxSamplesPerPixel)
    , m_fStop(PhysicalCamera::kDefaultFStop)
    , m_focalLengthMm(PhysicalCamera::kDefaultFocalLengthMm)
    , m_shutterSpeedSeconds(PhysicalCamera::kDefaultShutterSpeedSeconds)
    , m_iso(PhysicalCamera::kDefaultIso)
{
    seedDefaultDebounceValues();
    load();
}

int AppSettings::defaultDebounceMsFor(const QString& elementId)
{
    if (elementId == DebounceElementIds::kMaxSamples) {
        return kDefaultMaxSamplesDebounceMs;
    }
    if (elementId == DebounceElementIds::kPreviewSteps) {
        return kDefaultPreviewStepsDebounceMs;
    }
    if (elementId == DebounceElementIds::kPhysicalCamera) {
        return kDefaultUiDebounceMs;
    }
    return kDefaultUiDebounceMs;
}

void AppSettings::seedDefaultDebounceValues()
{
    m_debounceMs.insert(DebounceElementIds::kRenderSize, kDefaultUiDebounceMs);
    m_debounceMs.insert(DebounceElementIds::kMaxSamples, kDefaultMaxSamplesDebounceMs);
    m_debounceMs.insert(DebounceElementIds::kPreviewSteps, kDefaultPreviewStepsDebounceMs);
    m_debounceMs.insert(DebounceElementIds::kPhysicalCamera, kDefaultUiDebounceMs);
}

int AppSettings::debounceMsFor(const QString& elementId) const
{
    const auto it = m_debounceMs.constFind(elementId);
    if (it != m_debounceMs.constEnd()) {
        return it.value();
    }
    return defaultDebounceMsFor(elementId);
}

void AppSettings::setDebounceMs(const QString& elementId, int ms)
{
    const int clamped = clampDebounceMs(ms);
    const auto it = m_debounceMs.constFind(elementId);
    if (it != m_debounceMs.constEnd() && it.value() == clamped) {
        return;
    }

    m_debounceMs.insert(elementId, clamped);
    emit debounceMsChanged(elementId, clamped);
    save();
}

QSize AppSettings::renderSize() const
{
    return m_renderSize;
}

void AppSettings::setRenderSize(int width, int height)
{
    const int w = clampRenderDimension(width);
    const int h = clampRenderDimension(height);
    if (m_renderSize.width() == w && m_renderSize.height() == h) {
        return;
    }

    m_renderSize = QSize(w, h);
    clampRegionRectToRenderSize(
        m_renderSize.width(),
        m_renderSize.height(),
        m_regionBottomLeftX,
        m_regionBottomLeftY,
        m_regionTopRightX,
        m_regionTopRightY);
    save();
}

QColor AppSettings::clearColor() const
{
    return m_clearColor;
}

void AppSettings::setClearColor(const QColor& color)
{
    if (!color.isValid() || m_clearColor == color) {
        return;
    }

    m_clearColor = color;
    save();
}

int AppSettings::maxSamplesPerPixel() const
{
    return m_maxSamplesPerPixel;
}

void AppSettings::setMaxSamplesPerPixel(int value)
{
    const int clamped = clampMaxSamplesPerPixel(value);
    if (m_maxSamplesPerPixel == clamped) {
        return;
    }

    m_maxSamplesPerPixel = clamped;
    save();
}

int AppSettings::minSamples() const
{
    return m_minSamples;
}

void AppSettings::setMinSamples(int value)
{
    const int clamped = clampMinSamples(value);
    if (m_minSamples == clamped) {
        return;
    }

    m_minSamples = clamped;
    save();
}

float AppSettings::relativeErrorThreshold() const
{
    return m_relativeErrorThreshold;
}

void AppSettings::setRelativeErrorThreshold(float value)
{
    const float clamped = clampRelativeErrorThreshold(value);
    if (m_relativeErrorThreshold == clamped) {
        return;
    }

    m_relativeErrorThreshold = clamped;
    save();
}

int AppSettings::previewStepsPerLevel() const
{
    return m_previewStepsPerLevel;
}

void AppSettings::setPreviewStepsPerLevel(int value)
{
    const int clamped = clampPreviewStepsPerLevel(value);
    if (m_previewStepsPerLevel == clamped) {
        return;
    }

    m_previewStepsPerLevel = clamped;
    save();
}

int AppSettings::uiUpdateEveryNSamples() const
{
    return m_uiUpdateEveryNSamples;
}

void AppSettings::setUiUpdateEveryNSamples(int value)
{
    const int clamped = clampUiUpdateEveryNSamples(value);
    if (m_uiUpdateEveryNSamples == clamped) {
        return;
    }

    m_uiUpdateEveryNSamples = clamped;
    emit uiUpdateEveryNSamplesChanged(m_uiUpdateEveryNSamples);
    save();
}

int AppSettings::russianRouletteMinDepth() const
{
    return m_russianRouletteMinDepth;
}

void AppSettings::setRussianRouletteMinDepth(int value)
{
    const int clamped = clampRussianRouletteMinDepth(value);
    if (m_russianRouletteMinDepth == clamped) {
        return;
    }

    m_russianRouletteMinDepth = clamped;
    save();
}

int AppSettings::maxSubsurfaceScatters() const
{
    return m_maxSubsurfaceScatters;
}

void AppSettings::setMaxSubsurfaceScatters(int value)
{
    const int clamped = clampMaxSubsurfaceScatters(value);
    if (m_maxSubsurfaceScatters == clamped) {
        return;
    }

    m_maxSubsurfaceScatters = clamped;
    save();
}

QColor AppSettings::accelBvhColor() const
{
    return m_accelBvhColor;
}

void AppSettings::setAccelBvhColor(const QColor& color)
{
    if (!color.isValid() || m_accelBvhColor == color) {
        return;
    }

    m_accelBvhColor = color;
    save();
}

float AppSettings::creaseAngleDeg() const
{
    return m_creaseAngleDeg;
}

void AppSettings::setCreaseAngleDeg(float value)
{
    const float clamped = clampCreaseAngleDeg(value);
    if (m_creaseAngleDeg == clamped) {
        return;
    }

    m_creaseAngleDeg = clamped;
    save();
}

QString AppSettings::environmentHdrPath() const
{
    return m_environmentHdrPath;
}

void AppSettings::setEnvironmentHdrPath(const QString& path)
{
    const QString normalized = path.trimmed();
    if (m_environmentHdrPath == normalized) {
        return;
    }

    m_environmentHdrPath = normalized;
    save();
}

QString AppSettings::lsystemFilePath() const
{
    return m_lsystemFilePath;
}

void AppSettings::setLsystemFilePath(const QString& path)
{
    const QString normalized = path.trimmed();
    if (m_lsystemFilePath == normalized) {
        return;
    }

    m_lsystemFilePath = normalized;
    save();
}

int AppSettings::lsystemEditorFontSize() const
{
    return m_lsystemEditorFontSize;
}

void AppSettings::setLsystemEditorFontSize(int value)
{
    const int clamped = clampEditorFontSize(value);
    if (m_lsystemEditorFontSize == clamped) {
        return;
    }

    m_lsystemEditorFontSize = clamped;
    save();
}

int AppSettings::logFontSize() const
{
    return m_logFontSize;
}

void AppSettings::setLogFontSize(int value)
{
    const int clamped = clampEditorFontSize(value);
    if (m_logFontSize == clamped) {
        return;
    }

    m_logFontSize = clamped;
    save();
}

float AppSettings::environmentIntensity() const
{
    return m_environmentIntensity;
}

void AppSettings::setEnvironmentIntensity(float value)
{
    const float clamped = clampEnvironmentIntensity(value);
    if (m_environmentIntensity == clamped) {
        return;
    }

    m_environmentIntensity = clamped;
    save();
}

int AppSettings::environmentRotationY() const
{
    return m_environmentRotationY;
}

void AppSettings::setEnvironmentRotationY(int degrees)
{
    const int clamped = clampEnvironmentRotationY(degrees);
    if (m_environmentRotationY == clamped) {
        return;
    }

    m_environmentRotationY = clamped;
    save();
}

float AppSettings::fStop() const
{
    return m_fStop;
}

void AppSettings::setFStop(float value)
{
    const float clamped = clampFStop(value);
    if (m_fStop == clamped) {
        return;
    }

    m_fStop = clamped;
    save();
}

float AppSettings::focalLengthMm() const
{
    return m_focalLengthMm;
}

void AppSettings::setFocalLengthMm(float value)
{
    const float clamped = clampFocalLengthMm(value);
    if (m_focalLengthMm == clamped) {
        return;
    }

    m_focalLengthMm = clamped;
    save();
}

float AppSettings::shutterSpeedSeconds() const
{
    return m_shutterSpeedSeconds;
}

void AppSettings::setShutterSpeedSeconds(float value)
{
    const float clamped = clampShutterSpeedSeconds(value);
    if (m_shutterSpeedSeconds == clamped) {
        return;
    }

    m_shutterSpeedSeconds = clamped;
    save();
}

float AppSettings::iso() const
{
    return m_iso;
}

void AppSettings::setIso(float value)
{
    const float clamped = clampIso(value);
    if (m_iso == clamped) {
        return;
    }

    m_iso = clamped;
    save();
}

QByteArray AppSettings::windowGeometry() const
{
    return m_windowGeometry;
}

void AppSettings::setWindowGeometry(const QByteArray& geometry)
{
    if (m_windowGeometry == geometry) {
        return;
    }

    m_windowGeometry = geometry;
    save();
}

QByteArray AppSettings::horizontalSplitterState() const
{
    return m_horizontalSplitterState;
}

void AppSettings::setHorizontalSplitterState(const QByteArray& state)
{
    if (m_horizontalSplitterState == state) {
        return;
    }

    m_horizontalSplitterState = state;
    save();
}

QByteArray AppSettings::verticalSplitterState() const
{
    return m_verticalSplitterState;
}

void AppSettings::setVerticalSplitterState(const QByteArray& state)
{
    if (m_verticalSplitterState == state) {
        return;
    }

    m_verticalSplitterState = state;
    save();
}

CameraDynamicsSettings AppSettings::cameraDynamicsSettings() const
{
    return m_cameraDynamicsSettings;
}

void AppSettings::setCameraDynamicsSettings(const CameraDynamicsSettings& settings)
{
    const CameraDynamicsSettings clamped = clampCameraDynamicsSettings(settings);
    if (m_cameraDynamicsSettings == clamped) {
        return;
    }

    m_cameraDynamicsSettings = clamped;
    emit cameraDynamicsSettingsChanged(m_cameraDynamicsSettings);
    save();
}

bool AppSettings::regionRenderEnabled() const
{
    return m_regionRenderEnabled;
}

void AppSettings::setRegionRenderEnabled(bool enabled)
{
    if (m_regionRenderEnabled == enabled) {
        return;
    }

    m_regionRenderEnabled = enabled;
    save();
}

QPoint AppSettings::regionBottomLeft() const
{
    return QPoint(m_regionBottomLeftX, m_regionBottomLeftY);
}

void AppSettings::setRegionBottomLeft(int x, int y)
{
    const int maxX = std::max(0, m_renderSize.width() - 1);
    const int maxY = std::max(0, m_renderSize.height() - 1);
    const int clampedX = clampRegionCoordinate(x, maxX);
    const int clampedY = clampRegionCoordinate(y, maxY);
    if (m_regionBottomLeftX == clampedX && m_regionBottomLeftY == clampedY) {
        return;
    }

    m_regionBottomLeftX = clampedX;
    m_regionBottomLeftY = clampedY;
    save();
}

QPoint AppSettings::regionTopRight() const
{
    return QPoint(m_regionTopRightX, m_regionTopRightY);
}

void AppSettings::setRegionTopRight(int x, int y)
{
    const int maxX = std::max(0, m_renderSize.width() - 1);
    const int maxY = std::max(0, m_renderSize.height() - 1);
    const int clampedX = clampRegionCoordinate(x, maxX);
    const int clampedY = clampRegionCoordinate(y, maxY);
    if (m_regionTopRightX == clampedX && m_regionTopRightY == clampedY) {
        return;
    }

    m_regionTopRightX = clampedX;
    m_regionTopRightY = clampedY;
    save();
}

QColor AppSettings::regionRenderColor() const
{
    return m_regionRenderColor;
}

void AppSettings::setRegionRenderColor(const QColor& color)
{
    if (!color.isValid() || m_regionRenderColor == color) {
        return;
    }

    m_regionRenderColor = color;
    save();
}

int AppSettings::clampRegionCoordinate(int value, int maxInclusive)
{
    if (value < 0) {
        return 0;
    }
    if (value > maxInclusive) {
        return maxInclusive;
    }
    return value;
}

int AppSettings::clampEditorFontSize(int value)
{
    if (value < kMinEditorFontSize) {
        return kMinEditorFontSize;
    }
    if (value > kMaxEditorFontSize) {
        return kMaxEditorFontSize;
    }
    return value;
}

float AppSettings::clampFStop(float value)
{
    return PhysicalCamera::clampFStop(value);
}

float AppSettings::clampFocalLengthMm(float value)
{
    return PhysicalCamera::clampFocalLengthMm(value);
}

float AppSettings::clampShutterSpeedSeconds(float value)
{
    return PhysicalCamera::clampShutterSpeedSeconds(value);
}

float AppSettings::clampIso(float value)
{
    return PhysicalCamera::snapIsoToNearestPreset(value);
}

float AppSettings::clampEnvironmentIntensity(float value)
{
    if (value < kMinEnvironmentIntensity) {
        return kMinEnvironmentIntensity;
    }
    if (value > kMaxEnvironmentIntensity) {
        return kMaxEnvironmentIntensity;
    }
    return value;
}

int AppSettings::clampEnvironmentRotationY(int degrees)
{
    if (degrees < kMinEnvironmentRotationY) {
        return kMinEnvironmentRotationY;
    }
    if (degrees > kMaxEnvironmentRotationY) {
        return kMaxEnvironmentRotationY;
    }
    return degrees;
}

float AppSettings::clampCreaseAngleDeg(float value)
{
    if (value < kMinCreaseAngleDeg) {
        return kMinCreaseAngleDeg;
    }
    if (value > kMaxCreaseAngleDeg) {
        return kMaxCreaseAngleDeg;
    }
    return value;
}

int AppSettings::clampDebounceMs(int value)
{
    if (value < kMinDebounceMs) {
        return kMinDebounceMs;
    }
    if (value > kMaxDebounceMs) {
        return kMaxDebounceMs;
    }
    return value;
}

int AppSettings::clampRenderDimension(int value)
{
    if (value < kMinRenderDimension) {
        return kMinRenderDimension;
    }
    if (value > kMaxRenderDimension) {
        return kMaxRenderDimension;
    }
    return value;
}

int AppSettings::clampMaxSamplesPerPixel(int value)
{
    if (value < kMinMaxSamplesPerPixel) {
        return kMinMaxSamplesPerPixel;
    }
    if (value > kMaxMaxSamplesPerPixel) {
        return kMaxMaxSamplesPerPixel;
    }
    return value;
}

int AppSettings::clampMinSamples(int value)
{
    if (value < kMinMinSamples) {
        return kMinMinSamples;
    }
    if (value > kMaxMinSamples) {
        return kMaxMinSamples;
    }
    return value;
}

float AppSettings::clampRelativeErrorThreshold(float value)
{
    if (value < kMinRelativeErrorThreshold) {
        return kMinRelativeErrorThreshold;
    }
    if (value > kMaxRelativeErrorThreshold) {
        return kMaxRelativeErrorThreshold;
    }
    return value;
}

int AppSettings::clampPreviewStepsPerLevel(int value)
{
    if (value < kMinPreviewStepsPerLevel) {
        return kMinPreviewStepsPerLevel;
    }
    if (value > kMaxPreviewStepsPerLevel) {
        return kMaxPreviewStepsPerLevel;
    }
    return value;
}

int AppSettings::clampUiUpdateEveryNSamples(int value)
{
    if (value < kMinUiUpdateEveryNSamples) {
        return kMinUiUpdateEveryNSamples;
    }
    if (value > kMaxUiUpdateEveryNSamples) {
        return kMaxUiUpdateEveryNSamples;
    }
    return value;
}

int AppSettings::clampRussianRouletteMinDepth(int value)
{
    if (value < kMinRussianRouletteMinDepth) {
        return kMinRussianRouletteMinDepth;
    }
    if (value > kMaxRussianRouletteMinDepth) {
        return kMaxRussianRouletteMinDepth;
    }
    return value;
}

int AppSettings::clampMaxSubsurfaceScatters(int value)
{
    if (value < kMinMaxSubsurfaceScatters) {
        return kMinMaxSubsurfaceScatters;
    }
    if (value > kMaxMaxSubsurfaceScatters) {
        return kMaxMaxSubsurfaceScatters;
    }
    return value;
}

CameraDynamicsSettings AppSettings::clampCameraDynamicsSettings(const CameraDynamicsSettings& settings)
{
    CameraDynamicsSettings clamped = settings;
    clamped.linearSpeedMmPerSec = std::max(
        kMinCameraLinearSpeed,
        std::min(settings.linearSpeedMmPerSec, kMaxCameraLinearSpeed));
    clamped.angularSpeedRadPerSec = std::max(
        kMinCameraAngularSpeed,
        std::min(settings.angularSpeedRadPerSec, kMaxCameraAngularSpeed));
    clamped.mouseSensitivity =
        std::max(kMinCameraMouseSensitivity, std::min(settings.mouseSensitivity, kMaxCameraMouseSensitivity));
    clamped.tickIntervalMs =
        std::max(kMinCameraTickIntervalMs, std::min(settings.tickIntervalMs, kMaxCameraTickIntervalMs));
    clamped.motionResetThrottleMs = std::max(
        kMinCameraMotionTimingMs,
        std::min(settings.motionResetThrottleMs, kMaxCameraMotionTimingMs));
    clamped.motionStopDebounceMs = std::max(
        kMinCameraMotionTimingMs,
        std::min(settings.motionStopDebounceMs, kMaxCameraMotionTimingMs));
    clamped.defaultPositionXmm = std::max(
        kMinCameraDefaultPositionMm,
        std::min(settings.defaultPositionXmm, kMaxCameraDefaultPositionMm));
    clamped.defaultPositionYmm = std::max(
        kMinCameraDefaultPositionMm,
        std::min(settings.defaultPositionYmm, kMaxCameraDefaultPositionMm));
    clamped.defaultPositionZmm = std::max(
        kMinCameraDefaultPositionMm,
        std::min(settings.defaultPositionZmm, kMaxCameraDefaultPositionMm));
    clamped.defaultYawDeg = std::max(
        kMinCameraDefaultAngleDeg,
        std::min(settings.defaultYawDeg, kMaxCameraDefaultAngleDeg));
    clamped.defaultPitchDeg = std::max(
        kMinCameraDefaultPitchDeg,
        std::min(settings.defaultPitchDeg, kMaxCameraDefaultPitchDeg));
    clamped.defaultRollDeg = std::max(
        kMinCameraDefaultAngleDeg,
        std::min(settings.defaultRollDeg, kMaxCameraDefaultAngleDeg));
    return clamped;
}

void AppSettings::load()
{
    QSettings settings(kSettingsOrg, kSettingsApp);

    settings.beginGroup(kDebounceGroup);
    const QStringList debounceKeys = settings.childKeys();
    for (const QString& key : debounceKeys) {
        if (!isKnownDebounceElementId(key)) {
            continue;
        }
        bool ok = false;
        const int loaded = settings.value(key).toInt(&ok);
        if (ok && loaded >= kMinDebounceMs && loaded <= kMaxDebounceMs) {
            m_debounceMs.insert(key, loaded);
        }
    }
    settings.endGroup();   

    const QVariant renderWidthValue = settings.value(kRenderWidthKey);
    const QVariant renderHeightValue = settings.value(kRenderHeightKey);
    if (renderWidthValue.isValid() && renderHeightValue.isValid()) {
        bool widthOk = false;
        bool heightOk = false;
        const int width = renderWidthValue.toInt(&widthOk);
        const int height = renderHeightValue.toInt(&heightOk);
        if (widthOk && heightOk) {
            m_renderSize = QSize(clampRenderDimension(width), clampRenderDimension(height));
        }
    }

    const QVariant redValue = settings.value(kClearColorRedKey);
    const QVariant greenValue = settings.value(kClearColorGreenKey);
    const QVariant blueValue = settings.value(kClearColorBlueKey);
    if (redValue.isValid() && greenValue.isValid() && blueValue.isValid()) {
        bool redOk = false;
        bool greenOk = false;
        bool blueOk = false;
        const int red = redValue.toInt(&redOk);
        const int green = greenValue.toInt(&greenOk);
        const int blue = blueValue.toInt(&blueOk);
        if (redOk && greenOk && blueOk
            && red >= 0 && red <= 255
            && green >= 0 && green <= 255
            && blue >= 0 && blue <= 255) {
            m_clearColor = QColor(red, green, blue);
        }
    }

    const QVariant maxSamplesValue = settings.value(kMaxSamplesPerPixelKey);
    if (maxSamplesValue.isValid()) {
        bool ok = false;
        const int maxSamples = maxSamplesValue.toInt(&ok);
        if (ok) {
            m_maxSamplesPerPixel = clampMaxSamplesPerPixel(maxSamples);
        }
    }

    const QVariant minSamplesValue = settings.value(kMinSamplesKey);
    if (minSamplesValue.isValid()) {
        bool ok = false;
        const int minSamples = minSamplesValue.toInt(&ok);
        if (ok) {
            m_minSamples = clampMinSamples(minSamples);
        }
    }

    const QVariant relativeErrorValue = settings.value(kRelativeErrorThresholdKey);
    if (relativeErrorValue.isValid()) {
        bool ok = false;
        const double relativeError = relativeErrorValue.toDouble(&ok);
        if (ok) {
            m_relativeErrorThreshold = clampRelativeErrorThreshold(static_cast<float>(relativeError));
        }
    }

    const QVariant previewStepsValue = settings.value(kPreviewStepsPerLevelKey);
    if (previewStepsValue.isValid()) {
        bool ok = false;
        const int previewSteps = previewStepsValue.toInt(&ok);
        if (ok) {
            m_previewStepsPerLevel = clampPreviewStepsPerLevel(previewSteps);
        }
    }

    const QVariant uiUpdateEveryNSamplesValue = settings.value(kUiUpdateEveryNSamplesKey);
    if (uiUpdateEveryNSamplesValue.isValid()) {
        bool ok = false;
        const int uiUpdateEveryNSamples = uiUpdateEveryNSamplesValue.toInt(&ok);
        if (ok) {
            m_uiUpdateEveryNSamples = clampUiUpdateEveryNSamples(uiUpdateEveryNSamples);
        }
    }

    const QVariant rrDepthValue = settings.value(kRussianRouletteMinDepthKey);
    if (rrDepthValue.isValid()) {
        bool ok = false;
        const int rrDepth = rrDepthValue.toInt(&ok);
        if (ok) {
            m_russianRouletteMinDepth = clampRussianRouletteMinDepth(rrDepth);
        }
    }

    const QVariant maxSssScattersValue = settings.value(kMaxSubsurfaceScattersKey);
    if (maxSssScattersValue.isValid()) {
        bool ok = false;
        const int maxSssScatters = maxSssScattersValue.toInt(&ok);
        if (ok) {
            m_maxSubsurfaceScatters = clampMaxSubsurfaceScatters(maxSssScatters);
        }
    }

    auto loadColor = [&settings](const char* redKey, const char* greenKey, const char* blueKey, const QColor& fallback) {
        const QVariant redValue = settings.value(redKey);
        const QVariant greenValue = settings.value(greenKey);
        const QVariant blueValue = settings.value(blueKey);
        if (!redValue.isValid() || !greenValue.isValid() || !blueValue.isValid()) {
            return fallback;
        }

        bool redOk = false;
        bool greenOk = false;
        bool blueOk = false;
        const int red = redValue.toInt(&redOk);
        const int green = greenValue.toInt(&greenOk);
        const int blue = blueValue.toInt(&blueOk);
        if (redOk && greenOk && blueOk
            && red >= 0 && red <= 255
            && green >= 0 && green <= 255
            && blue >= 0 && blue <= 255) {
            return QColor(red, green, blue);
        }
        return fallback;
    };

    m_accelBvhColor = loadColor(
        kAccelBvhColorRedKey,
        kAccelBvhColorGreenKey,
        kAccelBvhColorBlueKey,
        m_accelBvhColor);
    if (!settings.contains(kAccelBvhColorRedKey)) {
        m_accelBvhColor = loadColor(
            kLegacyAccelOctreeColorRedKey,
            kLegacyAccelOctreeColorGreenKey,
            kLegacyAccelOctreeColorBlueKey,
            m_accelBvhColor);
    }
    if (!settings.contains(kAccelBvhColorRedKey)) {
        m_accelBvhColor = loadColor(
            kLegacyAccelAabbColorRedKey,
            kLegacyAccelAabbColorGreenKey,
            kLegacyAccelAabbColorBlueKey,
            m_accelBvhColor);
    }

    const QVariant creaseAngleValue = settings.value(kCreaseAngleDegKey);
    if (creaseAngleValue.isValid()) {
        bool ok = false;
        const float creaseAngle = static_cast<float>(creaseAngleValue.toDouble(&ok));
        if (ok) {
            m_creaseAngleDeg = clampCreaseAngleDeg(creaseAngle);
        }
    }

    const QVariant environmentHdrPathValue = settings.value(kEnvironmentHdrPathKey);
    if (environmentHdrPathValue.isValid()) {
        m_environmentHdrPath = environmentHdrPathValue.toString().trimmed();
    }

    const QVariant lsystemFilePathValue = settings.value(kLsystemFilePathKey);
    if (lsystemFilePathValue.isValid()) {
        m_lsystemFilePath = lsystemFilePathValue.toString().trimmed();
    }

    const QVariant lsystemEditorFontSizeValue = settings.value(kLsystemEditorFontSizeKey);
    if (lsystemEditorFontSizeValue.isValid()) {
        bool ok = false;
        const int fontSize = lsystemEditorFontSizeValue.toInt(&ok);
        if (ok) {
            m_lsystemEditorFontSize = clampEditorFontSize(fontSize);
        }
    }

    const QVariant logFontSizeValue = settings.value(kLogFontSizeKey);
    if (logFontSizeValue.isValid()) {
        bool ok = false;
        const int fontSize = logFontSizeValue.toInt(&ok);
        if (ok) {
            m_logFontSize = clampEditorFontSize(fontSize);
        }
    }

    const QVariant environmentIntensityValue = settings.value(kEnvironmentIntensityKey);
    if (environmentIntensityValue.isValid()) {
        bool ok = false;
        const float intensity = static_cast<float>(environmentIntensityValue.toDouble(&ok));
        if (ok) {
            m_environmentIntensity = clampEnvironmentIntensity(intensity);
        }
    }

    const QVariant environmentRotationYValue = settings.value(kEnvironmentRotationYKey);
    if (environmentRotationYValue.isValid()) {
        bool ok = false;
        const int rotationY = environmentRotationYValue.toInt(&ok);
        if (ok) {
            m_environmentRotationY = clampEnvironmentRotationY(rotationY);
        }
    }

    const QVariant fStopValue = settings.value(kFStopKey);
    if (fStopValue.isValid()) {
        bool ok = false;
        const float fStop = static_cast<float>(fStopValue.toDouble(&ok));
        if (ok) {
            m_fStop = clampFStop(fStop);
        }
    }

    const QVariant focalLengthValue = settings.value(kFocalLengthMmKey);
    if (focalLengthValue.isValid()) {
        bool ok = false;
        const float focalLengthMm = static_cast<float>(focalLengthValue.toDouble(&ok));
        if (ok) {
            m_focalLengthMm = clampFocalLengthMm(focalLengthMm);
        }
    }

    const QVariant shutterSpeedValue = settings.value(kShutterSpeedSecondsKey);
    if (shutterSpeedValue.isValid()) {
        bool ok = false;
        const float shutterSpeed = static_cast<float>(shutterSpeedValue.toDouble(&ok));
        if (ok) {
            m_shutterSpeedSeconds = clampShutterSpeedSeconds(shutterSpeed);
        }
    }

    const QVariant isoValue = settings.value(kIsoKey);
    if (isoValue.isValid()) {
        bool ok = false;
        const float iso = static_cast<float>(isoValue.toDouble(&ok));
        if (ok) {
            m_iso = clampIso(iso);
        }
    }

    m_windowGeometry = settings.value(kWindowGeometryKey).toByteArray();
    m_horizontalSplitterState = settings.value(kHorizontalSplitterStateKey).toByteArray();
    m_verticalSplitterState = settings.value(kVerticalSplitterStateKey).toByteArray();

    settings.beginGroup(kCameraSettingsGroup);
    CameraDynamicsSettings loadedCameraSettings = m_cameraDynamicsSettings;
    const auto loadFloat = [&settings](const char* key, float fallback) {
        const QVariant value = settings.value(key);
        if (!value.isValid()) {
            return fallback;
        }
        bool ok = false;
        const float loaded = static_cast<float>(value.toDouble(&ok));
        return ok ? loaded : fallback;
    };
    const auto loadInt = [&settings](const char* key, int fallback) {
        const QVariant value = settings.value(key);
        if (!value.isValid()) {
            return fallback;
        }
        bool ok = false;
        const int loaded = value.toInt(&ok);
        return ok ? loaded : fallback;
    };
    loadedCameraSettings.linearSpeedMmPerSec =
        loadFloat(kCameraLinearSpeedKey, loadedCameraSettings.linearSpeedMmPerSec);
    loadedCameraSettings.angularSpeedRadPerSec =
        loadFloat(kCameraAngularSpeedKey, loadedCameraSettings.angularSpeedRadPerSec);
    if (!settings.contains(kCameraLinearSpeedKey)) {
        float thrustLinear = loadFloat(kCameraThrustLinearKey, SceneUnits::kDefaultLinearThrustMmPerSec2);
        float dragLinear = loadFloat(kCameraDragLinearKey, SceneUnits::kDefaultLinearDragPerSec);
        if (thrustLinear < SceneUnits::kLegacyLinearThrustScaleThreshold) {
            thrustLinear *= 1000.0f;
        }
        if (dragLinear >= SceneUnits::kLegacyMisscaledLinearDragThreshold) {
            dragLinear /= 1000.0f;
        }
        if (dragLinear > 0.0f) {
            loadedCameraSettings.linearSpeedMmPerSec = thrustLinear / dragLinear;
        }
    }
    if (!settings.contains(kCameraAngularSpeedKey)) {
        const float thrustAngular = loadFloat(kCameraThrustAngularKey, 2.0f);
        const float dragAngular = loadFloat(kCameraDragAngularKey, 5.0f);
        if (dragAngular > 0.0f) {
            loadedCameraSettings.angularSpeedRadPerSec = thrustAngular / dragAngular;
        }
    }
    loadedCameraSettings.mouseSensitivity =
        loadFloat(kCameraMouseSensitivityKey, loadedCameraSettings.mouseSensitivity);
    const bool mouseSensitivityUsesRadPerPixel =
        settings.value(kCameraMouseSensitivityRadPerPixelKey, false).toBool();
    if (!mouseSensitivityUsesRadPerPixel
        && loadedCameraSettings.mouseSensitivity >= kLegacyMouseSensitivityScaleThreshold) {
        loadedCameraSettings.mouseSensitivity /=
            kLegacyMouseSensitivityToRadPerPixel;
    }
    loadedCameraSettings.tickIntervalMs = loadInt(kCameraTickIntervalMsKey, loadedCameraSettings.tickIntervalMs);
    loadedCameraSettings.motionResetThrottleMs =
        loadInt(kCameraMotionResetThrottleMsKey, loadedCameraSettings.motionResetThrottleMs);
    loadedCameraSettings.motionStopDebounceMs =
        loadInt(kCameraMotionStopDebounceMsKey, loadedCameraSettings.motionStopDebounceMs);
    loadedCameraSettings.defaultPositionXmm =
        loadFloat(kCameraDefaultPositionXmmKey, loadedCameraSettings.defaultPositionXmm);
    loadedCameraSettings.defaultPositionYmm =
        loadFloat(kCameraDefaultPositionYmmKey, loadedCameraSettings.defaultPositionYmm);
    loadedCameraSettings.defaultPositionZmm =
        loadFloat(kCameraDefaultPositionZmmKey, loadedCameraSettings.defaultPositionZmm);
    loadedCameraSettings.defaultYawDeg =
        loadFloat(kCameraDefaultYawDegKey, loadedCameraSettings.defaultYawDeg);
    loadedCameraSettings.defaultPitchDeg =
        loadFloat(kCameraDefaultPitchDegKey, loadedCameraSettings.defaultPitchDeg);
    loadedCameraSettings.defaultRollDeg =
        loadFloat(kCameraDefaultRollDegKey, loadedCameraSettings.defaultRollDeg);
    settings.endGroup();
    m_cameraDynamicsSettings = clampCameraDynamicsSettings(loadedCameraSettings);

    settings.beginGroup(kRegionRenderGroup);
    m_regionRenderEnabled = settings.value(kRegionRenderEnabledKey, false).toBool();
    const bool hasRegionCoords = settings.contains(kRegionBottomLeftXKey)
        && settings.contains(kRegionBottomLeftYKey)
        && settings.contains(kRegionTopRightXKey)
        && settings.contains(kRegionTopRightYKey);
    if (hasRegionCoords) {
        m_regionBottomLeftX = settings.value(kRegionBottomLeftXKey).toInt();
        m_regionBottomLeftY = settings.value(kRegionBottomLeftYKey).toInt();
        m_regionTopRightX = settings.value(kRegionTopRightXKey).toInt();
        m_regionTopRightY = settings.value(kRegionTopRightYKey).toInt();
    } else {
        defaultRegionRectForRenderSize(
            m_renderSize.width(),
            m_renderSize.height(),
            m_regionBottomLeftX,
            m_regionBottomLeftY,
            m_regionTopRightX,
            m_regionTopRightY);
    }
    clampRegionRectToRenderSize(
        m_renderSize.width(),
        m_renderSize.height(),
        m_regionBottomLeftX,
        m_regionBottomLeftY,
        m_regionTopRightX,
        m_regionTopRightY);
    m_regionRenderColor = loadColor(
        kRegionRenderColorRedKey,
        kRegionRenderColorGreenKey,
        kRegionRenderColorBlueKey,
        QColor(kDefaultRegionRenderColorRed, kDefaultRegionRenderColorGreen, kDefaultRegionRenderColorBlue));
    settings.endGroup();
}

void AppSettings::save()
{
    QSettings settings(kSettingsOrg, kSettingsApp);

    settings.beginGroup(kDebounceGroup);
    settings.remove(QString());
    for (auto it = m_debounceMs.constBegin(); it != m_debounceMs.constEnd(); ++it) {
        settings.setValue(it.key(), it.value());
    }
    settings.endGroup();

    settings.setValue(kRenderWidthKey, m_renderSize.width());
    settings.setValue(kRenderHeightKey, m_renderSize.height());
    settings.setValue(kClearColorRedKey, m_clearColor.red());
    settings.setValue(kClearColorGreenKey, m_clearColor.green());
    settings.setValue(kClearColorBlueKey, m_clearColor.blue());
    settings.setValue(kMaxSamplesPerPixelKey, m_maxSamplesPerPixel);
    settings.setValue(kMinSamplesKey, m_minSamples);
    settings.setValue(kRelativeErrorThresholdKey, static_cast<double>(m_relativeErrorThreshold));
    settings.setValue(kPreviewStepsPerLevelKey, m_previewStepsPerLevel);
    settings.setValue(kUiUpdateEveryNSamplesKey, m_uiUpdateEveryNSamples);
    settings.setValue(kRussianRouletteMinDepthKey, m_russianRouletteMinDepth);
    settings.setValue(kMaxSubsurfaceScattersKey, m_maxSubsurfaceScatters);
    settings.setValue(kAccelBvhColorRedKey, m_accelBvhColor.red());
    settings.setValue(kAccelBvhColorGreenKey, m_accelBvhColor.green());
    settings.setValue(kAccelBvhColorBlueKey, m_accelBvhColor.blue());
    settings.setValue(kCreaseAngleDegKey, static_cast<double>(m_creaseAngleDeg));
    settings.setValue(kEnvironmentHdrPathKey, m_environmentHdrPath);
    settings.setValue(kLsystemFilePathKey, m_lsystemFilePath);
    settings.setValue(kLsystemEditorFontSizeKey, m_lsystemEditorFontSize);
    settings.setValue(kLogFontSizeKey, m_logFontSize);
    settings.setValue(kEnvironmentIntensityKey, static_cast<double>(m_environmentIntensity));
    settings.setValue(kEnvironmentRotationYKey, m_environmentRotationY);
    settings.setValue(kFStopKey, static_cast<double>(m_fStop));
    settings.setValue(kFocalLengthMmKey, static_cast<double>(m_focalLengthMm));
    settings.setValue(kShutterSpeedSecondsKey, static_cast<double>(m_shutterSpeedSeconds));
    settings.setValue(kIsoKey, static_cast<double>(m_iso));
    settings.setValue(kWindowGeometryKey, m_windowGeometry);
    settings.setValue(kHorizontalSplitterStateKey, m_horizontalSplitterState);
    settings.setValue(kVerticalSplitterStateKey, m_verticalSplitterState);

    settings.beginGroup(kCameraSettingsGroup);
    settings.setValue(
        kCameraLinearSpeedKey,
        static_cast<double>(m_cameraDynamicsSettings.linearSpeedMmPerSec));
    settings.setValue(
        kCameraAngularSpeedKey,
        static_cast<double>(m_cameraDynamicsSettings.angularSpeedRadPerSec));
    settings.setValue(kCameraMouseSensitivityKey, static_cast<double>(m_cameraDynamicsSettings.mouseSensitivity));
    settings.setValue(kCameraMouseSensitivityRadPerPixelKey, true);
    settings.setValue(kCameraTickIntervalMsKey, m_cameraDynamicsSettings.tickIntervalMs);
    settings.setValue(kCameraMotionResetThrottleMsKey, m_cameraDynamicsSettings.motionResetThrottleMs);
    settings.setValue(kCameraMotionStopDebounceMsKey, m_cameraDynamicsSettings.motionStopDebounceMs);
    settings.setValue(
        kCameraDefaultPositionXmmKey,
        static_cast<double>(m_cameraDynamicsSettings.defaultPositionXmm));
    settings.setValue(
        kCameraDefaultPositionYmmKey,
        static_cast<double>(m_cameraDynamicsSettings.defaultPositionYmm));
    settings.setValue(
        kCameraDefaultPositionZmmKey,
        static_cast<double>(m_cameraDynamicsSettings.defaultPositionZmm));
    settings.setValue(
        kCameraDefaultYawDegKey,
        static_cast<double>(m_cameraDynamicsSettings.defaultYawDeg));
    settings.setValue(
        kCameraDefaultPitchDegKey,
        static_cast<double>(m_cameraDynamicsSettings.defaultPitchDeg));
    settings.setValue(
        kCameraDefaultRollDegKey,
        static_cast<double>(m_cameraDynamicsSettings.defaultRollDeg));
    settings.endGroup();

    settings.beginGroup(kRegionRenderGroup);
    settings.setValue(kRegionRenderEnabledKey, m_regionRenderEnabled);
    settings.setValue(kRegionBottomLeftXKey, m_regionBottomLeftX);
    settings.setValue(kRegionBottomLeftYKey, m_regionBottomLeftY);
    settings.setValue(kRegionTopRightXKey, m_regionTopRightX);
    settings.setValue(kRegionTopRightYKey, m_regionTopRightY);
    settings.setValue(kRegionRenderColorRedKey, m_regionRenderColor.red());
    settings.setValue(kRegionRenderColorGreenKey, m_regionRenderColor.green());
    settings.setValue(kRegionRenderColorBlueKey, m_regionRenderColor.blue());
    settings.endGroup();

    settings.sync();
}
