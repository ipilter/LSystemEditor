#pragma once

#include "Sdf/SdfTypes.h"

#include <QColor>
#include <QHash>
#include <QObject>
#include <QSize>
#include <QString>

namespace DebounceElementIds {

inline constexpr const char* kRenderSize = "renderSize";
inline constexpr const char* kMaxSamples = "maxSamples";
inline constexpr const char* kPreviewSteps = "previewSteps";

} // namespace DebounceElementIds

class AppSettings : public QObject
{
    Q_OBJECT

public:
    static AppSettings& instance();
    static void setInstance(AppSettings* settings);

    explicit AppSettings(QObject* parent = nullptr);

    static int defaultDebounceMsFor(const QString& elementId);

    int debounceMsFor(const QString& elementId) const;
    void setDebounceMs(const QString& elementId, int ms);

    QSize renderSize() const;
    void setRenderSize(int width, int height);

    QColor clearColor() const;
    void setClearColor(const QColor& color);

    int maxSamplesPerPixel() const;
    void setMaxSamplesPerPixel(int value);

    int previewStepsPerLevel() const;
    void setPreviewStepsPerLevel(int value);

    QColor accelBvhColor() const;
    void setAccelBvhColor(const QColor& color);

    SdfTraversalMode sdfTraversalMode() const;
    void setSdfTraversalMode(SdfTraversalMode mode);

    void load();
    void save();

signals:
    void debounceMsChanged(const QString& elementId, int ms);

private:
    static int clampDebounceMs(int value);
    static int clampRenderDimension(int value);
    static int clampMaxSamplesPerPixel(int value);
    static int clampPreviewStepsPerLevel(int value);
    static SdfTraversalMode clampSdfTraversalMode(SdfTraversalMode mode);
    void seedDefaultDebounceValues();

    static AppSettings* s_instance;

    QHash<QString, int> m_debounceMs;
    QSize m_renderSize = QSize(16, 16);
    QColor m_clearColor = QColor(10, 10, 10);
    int m_maxSamplesPerPixel = 1024;
    int m_previewStepsPerLevel = 0;
    QColor m_accelBvhColor = QColor(230, 200, 0);
    SdfTraversalMode m_sdfTraversalMode = SdfTraversalMode::BvhAccel;
};
