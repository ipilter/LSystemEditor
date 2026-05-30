#pragma once

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

    GLuint pboId(int index) const;
    void setPboIds(GLuint pbo0, GLuint pbo1);

signals:
    void clearColorChanged(const QColor& color);
    void renderSizeChanged(const QSize& size);

private:
    static int clampDimension(int value);

    QColor m_clearColor;
    QSize m_renderSize;
    GLuint m_pboIds[bufferCount] = {0, 0};
};
