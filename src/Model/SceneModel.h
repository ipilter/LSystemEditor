#pragma once

#include "Sdf/SdfTypes.h"

#include <QColor>
#include <QObject>
#include <QSize>

#include <QtGui/qopengl.h>

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

    SdfVisualMode sdfVisualMode() const;
    void setSdfVisualMode(SdfVisualMode mode);

    GLuint pboId(int index) const;
    void setPboIds(GLuint pbo0, GLuint pbo1);

signals:
    void clearColorChanged(const QColor& color);
    void renderSizeChanged(const QSize& size);
    void maxSamplesPerPixelChanged(int value);
    void previewStepsPerLevelChanged(int value);
    void sdfVisualModeChanged(SdfVisualMode mode);

private:
    static SdfVisualMode clampVisualMode(SdfVisualMode mode);
    static int clampDimension(int value);
    static int clampMaxSamples(int value);
    static int clampPreviewSteps(int value);

    QColor m_clearColor;
    QSize m_renderSize;
    int m_maxSamplesPerPixel = 1024;
    int m_previewStepsPerLevel = 0;
    SdfVisualMode m_sdfVisualMode = SdfVisualMode::StepCount;
    GLuint m_pboIds[bufferCount] = {0, 0};
};
