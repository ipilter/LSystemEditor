#include "AppSettings.h"

#include <QSettings>

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
constexpr const char* kAccelBvhColorRedKey = "accelBvhColorRed";
constexpr const char* kAccelBvhColorGreenKey = "accelBvhColorGreen";
constexpr const char* kAccelBvhColorBlueKey = "accelBvhColorBlue";
constexpr const char* kLegacyAccelAabbColorRedKey = "accelAabbColorRed";
constexpr const char* kLegacyAccelAabbColorGreenKey = "accelAabbColorGreen";
constexpr const char* kLegacyAccelAabbColorBlueKey = "accelAabbColorBlue";
constexpr const char* kLegacyAccelOctreeColorRedKey = "accelOctreeColorRed";
constexpr const char* kLegacyAccelOctreeColorGreenKey = "accelOctreeColorGreen";
constexpr const char* kLegacyAccelOctreeColorBlueKey = "accelOctreeColorBlue";
constexpr const char* kSdfTraversalModeKey = "sdfTraversalMode";

bool isKnownDebounceElementId(const QString& elementId)
{
    return elementId == DebounceElementIds::kRenderSize
        || elementId == DebounceElementIds::kMaxSamples
        || elementId == DebounceElementIds::kPreviewSteps;
}

} // namespace

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
    return kDefaultUiDebounceMs;
}

void AppSettings::seedDefaultDebounceValues()
{
    m_debounceMs.insert(DebounceElementIds::kRenderSize, kDefaultUiDebounceMs);
    m_debounceMs.insert(DebounceElementIds::kMaxSamples, kDefaultMaxSamplesDebounceMs);
    m_debounceMs.insert(DebounceElementIds::kPreviewSteps, kDefaultPreviewStepsDebounceMs);
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

SdfTraversalMode AppSettings::sdfTraversalMode() const
{
    return m_sdfTraversalMode;
}

void AppSettings::setSdfTraversalMode(SdfTraversalMode mode)
{
    const SdfTraversalMode clamped = clampSdfTraversalMode(mode);
    if (m_sdfTraversalMode == clamped) {
        return;
    }

    m_sdfTraversalMode = clamped;
    save();
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

SdfTraversalMode AppSettings::clampSdfTraversalMode(SdfTraversalMode mode)
{
    switch (mode) {
    case SdfTraversalMode::BruteForce:
    case SdfTraversalMode::BvhAccel:
        return mode;
    default:
        return SdfTraversalMode::BvhAccel;
    }
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

    const QVariant sdfTraversalModeValue = settings.value(kSdfTraversalModeKey);
    if (sdfTraversalModeValue.isValid()) {
        bool ok = false;
        const int mode = sdfTraversalModeValue.toInt(&ok);
        if (ok) {
            m_sdfTraversalMode = clampSdfTraversalMode(static_cast<SdfTraversalMode>(mode));
        }
    }
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
    settings.setValue(kAccelBvhColorRedKey, m_accelBvhColor.red());
    settings.setValue(kAccelBvhColorGreenKey, m_accelBvhColor.green());
    settings.setValue(kAccelBvhColorBlueKey, m_accelBvhColor.blue());
    settings.setValue(kSdfTraversalModeKey, static_cast<int>(m_sdfTraversalMode));
    settings.sync();
}
