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
constexpr int kDefaultOctreeMaxDepth = 5;
constexpr int kMinOctreeMaxDepth = 1;
constexpr int kMaxOctreeMaxDepth = 10;

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
constexpr const char* kAccelAabbColorRedKey = "accelAabbColorRed";
constexpr const char* kAccelAabbColorGreenKey = "accelAabbColorGreen";
constexpr const char* kAccelAabbColorBlueKey = "accelAabbColorBlue";
constexpr const char* kAccelOctreeColorRedKey = "accelOctreeColorRed";
constexpr const char* kAccelOctreeColorGreenKey = "accelOctreeColorGreen";
constexpr const char* kAccelOctreeColorBlueKey = "accelOctreeColorBlue";
constexpr const char* kOctreeMaxDepthKey = "octreeMaxDepth";

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
    , m_octreeMaxDepth(kDefaultOctreeMaxDepth)
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

QColor AppSettings::accelAabbColor() const
{
    return m_accelAabbColor;
}

void AppSettings::setAccelAabbColor(const QColor& color)
{
    if (!color.isValid() || m_accelAabbColor == color) {
        return;
    }

    m_accelAabbColor = color;
    save();
}

QColor AppSettings::accelOctreeColor() const
{
    return m_accelOctreeColor;
}

void AppSettings::setAccelOctreeColor(const QColor& color)
{
    if (!color.isValid() || m_accelOctreeColor == color) {
        return;
    }

    m_accelOctreeColor = color;
    save();
}

int AppSettings::octreeMaxDepth() const
{
    return m_octreeMaxDepth;
}

void AppSettings::setOctreeMaxDepth(int value)
{
    const int clamped = clampOctreeMaxDepth(value);
    if (m_octreeMaxDepth == clamped) {
        return;
    }

    m_octreeMaxDepth = clamped;
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

int AppSettings::clampOctreeMaxDepth(int value)
{
    if (value < kMinOctreeMaxDepth) {
        return kMinOctreeMaxDepth;
    }
    if (value > kMaxOctreeMaxDepth) {
        return kMaxOctreeMaxDepth;
    }
    return value;
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

    m_accelAabbColor = loadColor(
        kAccelAabbColorRedKey,
        kAccelAabbColorGreenKey,
        kAccelAabbColorBlueKey,
        m_accelAabbColor);
    m_accelOctreeColor = loadColor(
        kAccelOctreeColorRedKey,
        kAccelOctreeColorGreenKey,
        kAccelOctreeColorBlueKey,
        m_accelOctreeColor);

    const QVariant octreeMaxDepthValue = settings.value(kOctreeMaxDepthKey);
    if (octreeMaxDepthValue.isValid()) {
        bool ok = false;
        const int octreeMaxDepth = octreeMaxDepthValue.toInt(&ok);
        if (ok) {
            m_octreeMaxDepth = clampOctreeMaxDepth(octreeMaxDepth);
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
    settings.setValue(kAccelAabbColorRedKey, m_accelAabbColor.red());
    settings.setValue(kAccelAabbColorGreenKey, m_accelAabbColor.green());
    settings.setValue(kAccelAabbColorBlueKey, m_accelAabbColor.blue());
    settings.setValue(kAccelOctreeColorRedKey, m_accelOctreeColor.red());
    settings.setValue(kAccelOctreeColorGreenKey, m_accelOctreeColor.green());
    settings.setValue(kAccelOctreeColorBlueKey, m_accelOctreeColor.blue());
    settings.setValue(kOctreeMaxDepthKey, m_octreeMaxDepth);
    settings.sync();
}
