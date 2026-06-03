#pragma once

#include "Sdf/SdfTypes.h"
#include "Sdf/Shapes/SdfShape.h"
#include "SdfAccel/SdfSceneContent.h"

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

    SdfDebugVisualMode sdfVisualMode() const;
    void setSdfVisualMode(SdfDebugVisualMode mode);

    SdfTraversalMode sdfTraversalMode() const;
    void setSdfTraversalMode(SdfTraversalMode mode);

    SdfAccelBoundsOverlayMode boundsOverlayMode() const;
    void setBoundsOverlayMode(SdfAccelBoundsOverlayMode mode);

    QColor accelBvhColor() const;
    void setAccelBvhColor(const QColor& color);

    const std::vector<std::unique_ptr<SdfShape>>& sdfShapes() const;
    void addSdfShape(std::unique_ptr<SdfShape> shape);

    GLuint pboId(int index) const;
    void setPboIds(GLuint pbo0, GLuint pbo1);

signals:
    void clearColorChanged(const QColor& color);
    void renderSizeChanged(const QSize& size);
    void maxSamplesPerPixelChanged(int value);
    void previewStepsPerLevelChanged(int value);
    void sdfVisualModeChanged(SdfDebugVisualMode mode);
    void sdfTraversalModeChanged(SdfTraversalMode mode);
    void boundsOverlayModeChanged(SdfAccelBoundsOverlayMode mode);
    void accelBvhColorChanged(const QColor& color);
    void sdfSceneChanged();

private:
    static SdfDebugVisualMode clampVisualMode(SdfDebugVisualMode mode);
    static SdfTraversalMode clampTraversalMode(SdfTraversalMode mode);
    static SdfAccelBoundsOverlayMode clampBoundsOverlayMode(SdfAccelBoundsOverlayMode mode);
    static int clampDimension(int value);
    static int clampMaxSamples(int value);
    static int clampPreviewSteps(int value);

    QColor m_clearColor;
    QSize m_renderSize;
    int m_maxSamplesPerPixel = 8;
    int m_previewStepsPerLevel = 2;
    SdfDebugVisualMode m_sdfVisualMode = SdfDebugVisualMode::Off;
    SdfTraversalMode m_sdfTraversalMode = SdfTraversalMode::BvhAccel;
    SdfAccelBoundsOverlayMode m_boundsOverlayMode = SdfAccelBoundsOverlayMode::Off;
    QColor m_accelBvhColor = QColor(230, 200, 0);
    std::vector<std::unique_ptr<SdfShape>> m_sdfShapes;
    GLuint m_pboIds[bufferCount] = {0, 0};
};
