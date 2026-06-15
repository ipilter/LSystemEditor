#pragma once

#include <QByteArray>
#include <QColor>
#include <QHash>
#include <QObject>
#include <QSize>
#include <QString>

namespace DebounceElementIds {

inline constexpr const char* kRenderSize = "renderSize";
inline constexpr const char* kMaxSamples = "maxSamples";
inline constexpr const char* kPreviewSteps = "previewSteps";
inline constexpr const char* kPhysicalCamera = "physicalCamera";

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

    float creaseAngleDeg() const;
    void setCreaseAngleDeg(float value);

    QString environmentHdrPath() const;
    void setEnvironmentHdrPath(const QString& path);

    float fStop() const;
    void setFStop(float value);

    float shutterSpeedSeconds() const;
    void setShutterSpeedSeconds(float value);

    float iso() const;
    void setIso(float value);

    QByteArray windowGeometry() const;
    void setWindowGeometry(const QByteArray& geometry);

    QByteArray horizontalSplitterState() const;
    void setHorizontalSplitterState(const QByteArray& state);

    QByteArray verticalSplitterState() const;
    void setVerticalSplitterState(const QByteArray& state);

    void load();
    void save();

signals:
    void debounceMsChanged(const QString& elementId, int ms);

private:
    static int clampDebounceMs(int value);
    static int clampRenderDimension(int value);
    static int clampMaxSamplesPerPixel(int value);
    static int clampPreviewStepsPerLevel(int value);
    static float clampCreaseAngleDeg(float value);
    static float clampFStop(float value);
    static float clampShutterSpeedSeconds(float value);
    static float clampIso(float value);
    void seedDefaultDebounceValues();

    static AppSettings* s_instance;

    QHash<QString, int> m_debounceMs;
    QSize m_renderSize = QSize(16, 16);
    QColor m_clearColor = QColor(10, 10, 10);
    int m_maxSamplesPerPixel = 1024;
    int m_previewStepsPerLevel = 0;
    QColor m_accelBvhColor = QColor(230, 200, 0);
    float m_creaseAngleDeg = 50.0f;
    QString m_environmentHdrPath;
    float m_fStop = 0.0f;
    float m_shutterSpeedSeconds = 0.0f;
    float m_iso = 0.0f;
    QByteArray m_windowGeometry;
    QByteArray m_horizontalSplitterState;
    QByteArray m_verticalSplitterState;
};
