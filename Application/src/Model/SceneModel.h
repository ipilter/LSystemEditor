#pragma once

#include "MeshAccel/MeshAccelTypes.h"
#include "Procedural/ProceduralTypes.h"

#include <QColor>
#include <QObject>
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

    int previewStepsPerLevel() const;
    void setPreviewStepsPerLevel(int value);

    MeshAccelBoundsOverlayMode boundsOverlayMode() const;
    void setBoundsOverlayMode(MeshAccelBoundsOverlayMode mode);

    QColor accelBvhColor() const;
    void setAccelBvhColor(const QColor& color);

    float creaseAngleDeg() const;
    void setCreaseAngleDeg(float value);

    QString environmentHdrPath() const;
    void setEnvironmentHdrPath(const QString& path);

    const std::vector<ProceduralInstance>& proceduralInstances() const;
    void addProceduralInstance(ProceduralInstance instance);

    GLuint pboId(int index) const;
    void setPboIds(GLuint pbo0, GLuint pbo1);

signals:
    void clearColorChanged(const QColor& color);
    void renderSizeChanged(const QSize& size);
    void maxSamplesPerPixelChanged(int value);
    void previewStepsPerLevelChanged(int value);
    void boundsOverlayModeChanged(MeshAccelBoundsOverlayMode mode);
    void accelBvhColorChanged(const QColor& color);
    void environmentHdrPathChanged(const QString& path);
    void sceneChanged();

private:
    static MeshAccelBoundsOverlayMode clampBoundsOverlayMode(MeshAccelBoundsOverlayMode mode);
    static int clampDimension(int value);
    static int clampMaxSamples(int value);
    static int clampPreviewSteps(int value);
    static float clampCreaseAngleDeg(float value);

    QColor m_clearColor;
    QSize m_renderSize;
    int m_maxSamplesPerPixel = 8;
    int m_previewStepsPerLevel = 2;
    MeshAccelBoundsOverlayMode m_boundsOverlayMode = MeshAccelBoundsOverlayMode::Off;
    QColor m_accelBvhColor = QColor(230, 200, 0);
    float m_creaseAngleDeg = 50.0f;
    QString m_environmentHdrPath;
    std::vector<ProceduralInstance> m_proceduralInstances;
    GLuint m_pboIds[bufferCount] = {0, 0};
};
