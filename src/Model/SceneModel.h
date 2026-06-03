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

    SdfAccelBoundsOverlayMode boundsOverlayMode() const;
    void setBoundsOverlayMode(SdfAccelBoundsOverlayMode mode);

    QColor accelAabbColor() const;
    void setAccelAabbColor(const QColor& color);

    QColor accelOctreeColor() const;
    void setAccelOctreeColor(const QColor& color);

    int octreeMaxDepth() const;
    void setOctreeMaxDepth(int value);

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
    void boundsOverlayModeChanged(SdfAccelBoundsOverlayMode mode);
    void accelAabbColorChanged(const QColor& color);
    void accelOctreeColorChanged(const QColor& color);
    void octreeMaxDepthChanged(int value);
    void sdfSceneChanged();

private:
    static SdfDebugVisualMode clampVisualMode(SdfDebugVisualMode mode);
    static SdfAccelBoundsOverlayMode clampBoundsOverlayMode(SdfAccelBoundsOverlayMode mode);
    static int clampDimension(int value);
    static int clampMaxSamples(int value);
    static int clampPreviewSteps(int value);
    static int clampOctreeMaxDepth(int value);

    QColor m_clearColor;
    QSize m_renderSize;
    int m_maxSamplesPerPixel = 8;
    int m_previewStepsPerLevel = 2;
    SdfDebugVisualMode m_sdfVisualMode = SdfDebugVisualMode::Off;
    SdfAccelBoundsOverlayMode m_boundsOverlayMode = SdfAccelBoundsOverlayMode::Off;
    QColor m_accelAabbColor = QColor(0, 200, 80);
    QColor m_accelOctreeColor = QColor(230, 200, 0);
    int m_octreeMaxDepth = 5;
    std::vector<std::unique_ptr<SdfShape>> m_sdfShapes;
    GLuint m_pboIds[bufferCount] = {0, 0};
};
