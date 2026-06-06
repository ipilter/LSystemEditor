#pragma once

#include "Geometry/GeometryTypes.h"
#include "MeshAccel/MeshAccelTypes.h"
#include "Procedural/ProceduralTypes.h"

#include <QColor>
#include <QObject>
#include <QSize>

#include <QtGui/qopengl.h>

#include <memory>
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

    int previewStepsPerLevel() const;
    void setPreviewStepsPerLevel(int value);

    RenderDebugVisualMode debugVisualMode() const;
    void setDebugVisualMode(RenderDebugVisualMode mode);

    float sunAzimuthDeg() const;
    float sunElevationDeg() const;
    QColor sunColor() const;
    float sunDiskSizeDeg() const;
    void setSunAzimuthDeg(float value);
    void setSunElevationDeg(float value);
    void setSunColor(const QColor& color);
    void setSunDiskSizeDeg(float value);

    int secondaryBounceCount() const;
    void setSecondaryBounceCount(int value);

    MeshAccelBoundsOverlayMode boundsOverlayMode() const;
    void setBoundsOverlayMode(MeshAccelBoundsOverlayMode mode);

    QColor accelBvhColor() const;
    void setAccelBvhColor(const QColor& color);

    float creaseAngleDeg() const;
    void setCreaseAngleDeg(float value);

    const std::vector<ProceduralInstance>& proceduralInstances() const;
    void addProceduralInstance(ProceduralInstance instance);

    GLuint pboId(int index) const;
    void setPboIds(GLuint pbo0, GLuint pbo1);

signals:
    void clearColorChanged(const QColor& color);
    void renderSizeChanged(const QSize& size);
    void maxSamplesPerPixelChanged(int value);
    void previewStepsPerLevelChanged(int value);
    void debugVisualModeChanged(RenderDebugVisualMode mode);
    void sunSettingsChanged();
    void secondaryBounceCountChanged(int value);
    void boundsOverlayModeChanged(MeshAccelBoundsOverlayMode mode);
    void accelBvhColorChanged(const QColor& color);
    void sceneChanged();

private:
    static RenderDebugVisualMode clampVisualMode(RenderDebugVisualMode mode);
    static MeshAccelBoundsOverlayMode clampBoundsOverlayMode(MeshAccelBoundsOverlayMode mode);
    static int clampDimension(int value);
    static int clampMaxSamples(int value);
    static int clampPreviewSteps(int value);
    static float clampSunAzimuth(float value);
    static float clampSunElevation(float value);
    static float clampSunDiskSize(float value);
    static int clampSecondaryBounceCount(int value);
    static float clampCreaseAngleDeg(float value);

    QColor m_clearColor;
    QSize m_renderSize;
    int m_maxSamplesPerPixel = 8;
    int m_previewStepsPerLevel = 2;
    RenderDebugVisualMode m_debugVisualMode = RenderDebugVisualMode::Off;
    float m_sunAzimuthDeg = 15.0f;
    float m_sunElevationDeg = 45.0f;
    QColor m_sunColor = QColor(255, 245, 230);
    float m_sunDiskSizeDeg = 0.53f;
    int m_secondaryBounceCount = 8;
    MeshAccelBoundsOverlayMode m_boundsOverlayMode = MeshAccelBoundsOverlayMode::Off;
    QColor m_accelBvhColor = QColor(230, 200, 0);
    float m_creaseAngleDeg = 50.0f;
    std::vector<ProceduralInstance> m_proceduralInstances;
    GLuint m_pboIds[bufferCount] = {0, 0};
};
