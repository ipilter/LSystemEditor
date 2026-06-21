#pragma once

#include "SceneUnits.h"

#include <QByteArray>
#include <QColor>
#include <QHash>
#include <QObject>
#include <QPoint>
#include <QSize>
#include <QString>

namespace DebounceElementIds {

inline constexpr const char* kRenderSize = "renderSize";
inline constexpr const char* kMaxSamples = "maxSamples";
inline constexpr const char* kPreviewSteps = "previewSteps";
inline constexpr const char* kPhysicalCamera = "physicalCamera";

} // namespace DebounceElementIds

struct CameraDynamicsSettings
{
    float linearSpeedMmPerSec = SceneUnits::kDefaultLinearSpeedMmPerSec;
    float angularSpeedRadPerSec = SceneUnits::kDefaultAngularSpeedRadPerSec;
    float mouseSensitivity = 0.001f;
    int tickIntervalMs = 16;
    int motionResetThrottleMs = 250;
    int motionStopDebounceMs = 200;
    float defaultPositionXmm = 0.0f;
    float defaultPositionYmm = 0.0f;
    float defaultPositionZmm = 0.0f;
    float defaultYawDeg = 0.0f;
    float defaultPitchDeg = 0.0f;
    float defaultRollDeg = 0.0f;

    bool operator==(const CameraDynamicsSettings& other) const;
    bool operator!=(const CameraDynamicsSettings& other) const { return !(*this == other); }
};

Q_DECLARE_METATYPE(CameraDynamicsSettings)

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

    int minSamples() const;
    void setMinSamples(int value);

    float relativeErrorThreshold() const;
    void setRelativeErrorThreshold(float value);

    int previewStepsPerLevel() const;
    void setPreviewStepsPerLevel(int value);

    int russianRouletteMinDepth() const;
    void setRussianRouletteMinDepth(int value);

    QColor accelBvhColor() const;
    void setAccelBvhColor(const QColor& color);

    float creaseAngleDeg() const;
    void setCreaseAngleDeg(float value);

    QString environmentHdrPath() const;
    void setEnvironmentHdrPath(const QString& path);

    QString lsystemFilePath() const;
    void setLsystemFilePath(const QString& path);

    int lsystemEditorFontSize() const;
    void setLsystemEditorFontSize(int value);

    int logFontSize() const;
    void setLogFontSize(int value);

    float environmentIntensity() const;
    void setEnvironmentIntensity(float value);

    float fStop() const;
    void setFStop(float value);

    float focalLengthMm() const;
    void setFocalLengthMm(float value);

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

    CameraDynamicsSettings cameraDynamicsSettings() const;
    void setCameraDynamicsSettings(const CameraDynamicsSettings& settings);

    bool regionRenderEnabled() const;
    void setRegionRenderEnabled(bool enabled);

    QPoint regionBottomLeft() const;
    void setRegionBottomLeft(int x, int y);

    QPoint regionTopRight() const;
    void setRegionTopRight(int x, int y);

    QColor regionRenderColor() const;
    void setRegionRenderColor(const QColor& color);

    void load();
    void save();

signals:
    void debounceMsChanged(const QString& elementId, int ms);
    void cameraDynamicsSettingsChanged(const CameraDynamicsSettings& settings);

private:
    static int clampDebounceMs(int value);
    static CameraDynamicsSettings clampCameraDynamicsSettings(const CameraDynamicsSettings& settings);
    static int clampRenderDimension(int value);
    static int clampMaxSamplesPerPixel(int value);
    static int clampMinSamples(int value);
    static float clampRelativeErrorThreshold(float value);
    static int clampPreviewStepsPerLevel(int value);
    static int clampRussianRouletteMinDepth(int value);
    static float clampCreaseAngleDeg(float value);
    static float clampFStop(float value);
    static float clampFocalLengthMm(float value);
    static float clampShutterSpeedSeconds(float value);
    static float clampIso(float value);
    static float clampEnvironmentIntensity(float value);
    static int clampRegionCoordinate(int value, int maxInclusive);
    static int clampEditorFontSize(int value);
    void seedDefaultDebounceValues();

    static AppSettings* s_instance;

    QHash<QString, int> m_debounceMs;
    QSize m_renderSize = QSize(16, 16);
    QColor m_clearColor = QColor(10, 10, 10);
    int m_maxSamplesPerPixel = 1024;
    int m_minSamples = 16;
    float m_relativeErrorThreshold = 0.02f;
    int m_previewStepsPerLevel = 0;
    int m_russianRouletteMinDepth = 3;
    QColor m_accelBvhColor = QColor(230, 200, 0);
    float m_creaseAngleDeg = 50.0f;
    QString m_environmentHdrPath;
    QString m_lsystemFilePath;
    int m_lsystemEditorFontSize = 9;
    int m_logFontSize = 9;
    float m_environmentIntensity = 1.0f;
    float m_fStop = 0.0f;
    float m_focalLengthMm = 0.0f;
    float m_shutterSpeedSeconds = 0.0f;
    float m_iso = 0.0f;
    QByteArray m_windowGeometry;
    QByteArray m_horizontalSplitterState;
    QByteArray m_verticalSplitterState;
    CameraDynamicsSettings m_cameraDynamicsSettings;
    bool m_regionRenderEnabled = false;
    int m_regionBottomLeftX = 0;
    int m_regionBottomLeftY = 0;
    int m_regionTopRightX = 0;
    int m_regionTopRightY = 0;
    QColor m_regionRenderColor = QColor(255, 255, 128);
};
