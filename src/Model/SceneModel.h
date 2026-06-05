#pragma once

#include "Geometry/GeometryTypes.h"
#include "MeshAccel/MeshAccelTypes.h"
#include "Procedural/ProceduralTypes.h"
#include "SceneDefaults.h"
#include "ScenePrimitive.h"

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

    MeshAccelBoundsOverlayMode boundsOverlayMode() const;
    void setBoundsOverlayMode(MeshAccelBoundsOverlayMode mode);

    QColor accelBvhColor() const;
    void setAccelBvhColor(const QColor& color);

    const std::vector<std::unique_ptr<ScenePrimitive>>& primitives() const;
    void addPrimitive(std::unique_ptr<ScenePrimitive> primitive);

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
    void boundsOverlayModeChanged(MeshAccelBoundsOverlayMode mode);
    void accelBvhColorChanged(const QColor& color);
    void sceneChanged();

private:
    static RenderDebugVisualMode clampVisualMode(RenderDebugVisualMode mode);
    static MeshAccelBoundsOverlayMode clampBoundsOverlayMode(MeshAccelBoundsOverlayMode mode);
    static int clampDimension(int value);
    static int clampMaxSamples(int value);
    static int clampPreviewSteps(int value);

    QColor m_clearColor;
    QSize m_renderSize;
    int m_maxSamplesPerPixel = 8;
    int m_previewStepsPerLevel = 2;
    RenderDebugVisualMode m_debugVisualMode = RenderDebugVisualMode::Off;
    MeshAccelBoundsOverlayMode m_boundsOverlayMode = MeshAccelBoundsOverlayMode::Off;
    QColor m_accelBvhColor = QColor(230, 200, 0);
    std::vector<std::unique_ptr<ScenePrimitive>> m_primitives;
    std::vector<ProceduralInstance> m_proceduralInstances;
    GLuint m_pboIds[bufferCount] = {0, 0};
};
