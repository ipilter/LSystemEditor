#pragma once

#include "MeshAccel/MeshAccelTypes.h"
#include "Procedural/ProceduralTypes.h"

#include <QColor>
#include <QHash>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QSize>

#include <QtGui/qopengl.h>

#include <vector>

class SceneModel : public QObject
{
    Q_OBJECT

public:
    static constexpr int bufferCount = 2;

    explicit SceneModel(QObject* parent = nullptr);

    QColor clearColor() const;
    void setClearColor(const QColor& color);

    QSize renderSize() const;
    void setRenderSize(int width, int height);
    int bufferByteSize() const;

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

    RenderViewOverlayMode boundsOverlayMode() const;
    void setBoundsOverlayMode(RenderViewOverlayMode mode);

    QColor accelBvhColor() const;
    void setAccelBvhColor(const QColor& color);

    float creaseAngleDeg() const;
    void setCreaseAngleDeg(float value);

    QString environmentHdrPath() const;
    void setEnvironmentHdrPath(const QString& path);

    float environmentIntensity() const;
    void setEnvironmentIntensity(float value);

    float fStop() const;
    void setFStop(float value);

    float shutterSpeedSeconds() const;
    void setShutterSpeedSeconds(float value);

    float iso() const;
    void setIso(float value);

    const std::vector<ProceduralInstance>& proceduralInstances() const;
    void addProceduralInstance(ProceduralInstance instance);
    void resetScene();

    bool regionRenderEnabled() const;
    void setRegionRenderEnabled(bool enabled);

    QRect regionRect() const;
    void setRegionRect(int minX, int minY, int maxX, int maxY);

    QColor regionRenderColor() const;
    void setRegionRenderColor(const QColor& color);

    GLuint pboId(int index) const;
    void setPboIds(GLuint pbo0, GLuint pbo1);

signals:
    void clearColorChanged(const QColor& color);
    void renderSizeChanged(const QSize& size);
    void maxSamplesPerPixelChanged(int value);
    void minSamplesChanged(int value);
    void relativeErrorThresholdChanged(float value);
    void previewStepsPerLevelChanged(int value);
    void russianRouletteMinDepthChanged(int value);
    void boundsOverlayModeChanged(RenderViewOverlayMode mode);
    void accelBvhColorChanged(const QColor& color);
    void environmentHdrPathChanged(const QString& path);
    void environmentIntensityChanged(float value);
    void fStopChanged(float value);
    void shutterSpeedSecondsChanged(float value);
    void isoChanged(float value);
    void sceneChanged();
    void regionRenderEnabledChanged(bool enabled);
    void regionRectChanged(const QRect& rect);
    void regionRenderColorChanged(const QColor& color);

private:
    static RenderViewOverlayMode clampBoundsOverlayMode(RenderViewOverlayMode mode);
    static int clampDimension(int value);
    static int clampMaxSamples(int value);
    static int clampMinSamples(int value);
    static float clampRelativeErrorThreshold(float value);
    static int clampPreviewSteps(int value);
    static int clampRussianRouletteMinDepth(int value);
    static float clampCreaseAngleDeg(float value);
    static float clampFStop(float value);
    static float clampShutterSpeedSeconds(float value);
    static float clampIso(float value);
    static float clampEnvironmentIntensity(float value);
    static QRect normalizeRegionRect(int minX, int minY, int maxX, int maxY, int renderW, int renderH);
    void clampRegionToRenderSize();

    QColor m_clearColor;
    QSize m_renderSize;
    int m_maxSamplesPerPixel = 8;
    int m_minSamples = 16;
    float m_relativeErrorThreshold = 0.02f;
    int m_previewStepsPerLevel = 2;
    int m_russianRouletteMinDepth = 3;
    RenderViewOverlayMode m_renderViewOverlayMode = RenderViewOverlayMode::Render;
    QColor m_accelBvhColor = QColor(230, 200, 0);
    float m_creaseAngleDeg = 50.0f;
    QString m_environmentHdrPath;
    float m_environmentIntensity = 1.0f;
    float m_fStop = 0.0f;
    float m_shutterSpeedSeconds = 0.0f;
    float m_iso = 0.0f;
    std::vector<ProceduralInstance> m_proceduralInstances;
    GLuint m_pboIds[bufferCount] = {0, 0};
    bool m_regionRenderEnabled = false;
    QRect m_regionRect;
    QColor m_regionRenderColor = QColor(255, 255, 128);
};
