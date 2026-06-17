#include "AppSettings.h"

#include "PhysicalCamera.h"

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
constexpr int kMinRussianRouletteMinDepth = 0;
constexpr int kMaxRussianRouletteMinDepth = 64;
constexpr int kDefaultRussianRouletteMinDepth = 3;
constexpr const char* kSettingsOrg = "PathTracer";
constexpr const char* kSettingsApp = "pathtracer";
constexpr const char* kDebounceGroup = "debounce";
constexpr const char* kRenderWidthKey = "renderWidth";
constexpr const char* kRenderHeightKey = "renderHeight";
constexpr const char* kClearColorRedKey = "clearColorRed";
constexpr const char* kClearColorGreenKey = "clearColorGreen";
constexpr const char* kClearColorBlueKey = "clearColorBlue";
constexpr const char* kMaxSamplesPerPixelKey = "maxSamplesPerPixel";
constexpr const char* kPreviewStepsPerLevelKey = "previewStepsPerLevel";
constexpr const char* kRussianRouletteMinDepthKey = "russianRouletteMinDepth";
constexpr float kDefaultCreaseAngleDeg = 50.0f;
constexpr float kMinCreaseAngleDeg = 0.0f;
constexpr float kMaxCreaseAngleDeg = 180.0f;
constexpr float kMinEnvironmentIntensity = 0.0f;
constexpr float kMaxEnvironmentIntensity = 100.0f;
constexpr const char* kCreaseAngleDegKey = "creaseAngleDeg";
constexpr const char* kEnvironmentHdrPathKey = "environmentHdrPath";
constexpr const char* kLsystemFilePathKey = "lsystemFilePath";
constexpr const char* kEnvironmentIntensityKey = "environmentIntensity";
constexpr const char* kFStopKey = "fStop";
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
constexpr const char* kCameraThrustLinearKey = "thrustLinear";
constexpr const char* kCameraDragLinearKey = "dragLinear";
constexpr const char* kCameraThrustAngularKey = "thrustAngular";
constexpr const char* kCameraDragAngularKey = "dragAngular";
constexpr const char* kCameraMouseSensitivityKey = "mouseSensitivity";
constexpr const char* kCameraTickIntervalMsKey = "tickIntervalMs";
constexpr const char* kCameraMotionResetThrottleMsKey = "motionResetThrottleMs";
constexpr const char* kCameraMotionStopDebounceMsKey = "motionStopDebounceMs";
constexpr float kMinCameraThrust = 0.1f;
constexpr float kMaxCameraThrust = 20.0f;
constexpr float kMinCameraDrag = 0.1f;
constexpr float kMaxCameraDrag = 30.0f;
constexpr float kMinCameraMouseSensitivity = 0.01f;
constexpr float kMaxCameraMouseSensitivity = 2.0f;
constexpr int kMinCameraTickIntervalMs = 8;
constexpr int kMaxCameraTickIntervalMs = 100;
constexpr int kMinCameraMotionTimingMs = 0;
constexpr int kMaxCameraMotionTimingMs = 2000;

bool isKnownDebounceElementId(const QString& elementId)
{
    return elementId == DebounceElementIds::kRenderSize
        || elementId == DebounceElementIds::kMaxSamples
        || elementId == DebounceElementIds::kPreviewSteps
        || elementId == DebounceElementIds::kPhysicalCamera;
}

} // namespace

bool CameraDynamicsSettings::operator==(const CameraDynamicsSettings& other) const
{
    return thrustLinear == other.thrustLinear
        && dragLinear == other.dragLinear
        && thrustAngular == other.thrustAngular
        && dragAngular == other.dragAngular
        && mouseSensitivity == other.mouseSensitivity
        && tickIntervalMs == other.tickIntervalMs
        && motionResetThrottleMs == other.motionResetThrottleMs
        && motionStopDebounceMs == other.motionStopDebounceMs;
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

float AppSettings::clampFStop(float value)
{
    return PhysicalCamera::clampFStop(value);
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

CameraDynamicsSettings AppSettings::clampCameraDynamicsSettings(const CameraDynamicsSettings& settings)
{
    CameraDynamicsSettings clamped = settings;
    clamped.thrustLinear = std::max(kMinCameraThrust, std::min(settings.thrustLinear, kMaxCameraThrust));
    clamped.dragLinear = std::max(kMinCameraDrag, std::min(settings.dragLinear, kMaxCameraDrag));
    clamped.thrustAngular = std::max(kMinCameraThrust, std::min(settings.thrustAngular, kMaxCameraThrust));
    clamped.dragAngular = std::max(kMinCameraDrag, std::min(settings.dragAngular, kMaxCameraDrag));
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

    const QVariant previewStepsValue = settings.value(kPreviewStepsPerLevelKey);
    if (previewStepsValue.isValid()) {
        bool ok = false;
        const int previewSteps = previewStepsValue.toInt(&ok);
        if (ok) {
            m_previewStepsPerLevel = clampPreviewStepsPerLevel(previewSteps);
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

    const QVariant environmentIntensityValue = settings.value(kEnvironmentIntensityKey);
    if (environmentIntensityValue.isValid()) {
        bool ok = false;
        const float intensity = static_cast<float>(environmentIntensityValue.toDouble(&ok));
        if (ok) {
            m_environmentIntensity = clampEnvironmentIntensity(intensity);
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
    loadedCameraSettings.thrustLinear = loadFloat(kCameraThrustLinearKey, loadedCameraSettings.thrustLinear);
    loadedCameraSettings.dragLinear = loadFloat(kCameraDragLinearKey, loadedCameraSettings.dragLinear);
    loadedCameraSettings.thrustAngular = loadFloat(kCameraThrustAngularKey, loadedCameraSettings.thrustAngular);
    loadedCameraSettings.dragAngular = loadFloat(kCameraDragAngularKey, loadedCameraSettings.dragAngular);
    loadedCameraSettings.mouseSensitivity =
        loadFloat(kCameraMouseSensitivityKey, loadedCameraSettings.mouseSensitivity);
    loadedCameraSettings.tickIntervalMs = loadInt(kCameraTickIntervalMsKey, loadedCameraSettings.tickIntervalMs);
    loadedCameraSettings.motionResetThrottleMs =
        loadInt(kCameraMotionResetThrottleMsKey, loadedCameraSettings.motionResetThrottleMs);
    loadedCameraSettings.motionStopDebounceMs =
        loadInt(kCameraMotionStopDebounceMsKey, loadedCameraSettings.motionStopDebounceMs);
    settings.endGroup();
    m_cameraDynamicsSettings = clampCameraDynamicsSettings(loadedCameraSettings);
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
    settings.setValue(kPreviewStepsPerLevelKey, m_previewStepsPerLevel);
    settings.setValue(kRussianRouletteMinDepthKey, m_russianRouletteMinDepth);
    settings.setValue(kAccelBvhColorRedKey, m_accelBvhColor.red());
    settings.setValue(kAccelBvhColorGreenKey, m_accelBvhColor.green());
    settings.setValue(kAccelBvhColorBlueKey, m_accelBvhColor.blue());
    settings.setValue(kCreaseAngleDegKey, static_cast<double>(m_creaseAngleDeg));
    settings.setValue(kEnvironmentHdrPathKey, m_environmentHdrPath);
    settings.setValue(kLsystemFilePathKey, m_lsystemFilePath);
    settings.setValue(kEnvironmentIntensityKey, static_cast<double>(m_environmentIntensity));
    settings.setValue(kFStopKey, static_cast<double>(m_fStop));
    settings.setValue(kShutterSpeedSecondsKey, static_cast<double>(m_shutterSpeedSeconds));
    settings.setValue(kIsoKey, static_cast<double>(m_iso));
    settings.setValue(kWindowGeometryKey, m_windowGeometry);
    settings.setValue(kHorizontalSplitterStateKey, m_horizontalSplitterState);
    settings.setValue(kVerticalSplitterStateKey, m_verticalSplitterState);

    settings.beginGroup(kCameraSettingsGroup);
    settings.setValue(kCameraThrustLinearKey, static_cast<double>(m_cameraDynamicsSettings.thrustLinear));
    settings.setValue(kCameraDragLinearKey, static_cast<double>(m_cameraDynamicsSettings.dragLinear));
    settings.setValue(kCameraThrustAngularKey, static_cast<double>(m_cameraDynamicsSettings.thrustAngular));
    settings.setValue(kCameraDragAngularKey, static_cast<double>(m_cameraDynamicsSettings.dragAngular));
    settings.setValue(kCameraMouseSensitivityKey, static_cast<double>(m_cameraDynamicsSettings.mouseSensitivity));
    settings.setValue(kCameraTickIntervalMsKey, m_cameraDynamicsSettings.tickIntervalMs);
    settings.setValue(kCameraMotionResetThrottleMsKey, m_cameraDynamicsSettings.motionResetThrottleMs);
    settings.setValue(kCameraMotionStopDebounceMsKey, m_cameraDynamicsSettings.motionStopDebounceMs);
    settings.endGroup();

    settings.sync();
}
