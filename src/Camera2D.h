#pragma once

#include <QMatrix4x4>
#include <QVector2D>

struct Camera2DFrustum
{
    float left = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    float top = 0.0f;
};

class Camera2D
{
public:
    Camera2D();

    QMatrix4x4 viewProjection(int viewportW, int viewportH) const;
    Camera2DFrustum frustum(int viewportW, int viewportH) const;

    void pan(float dx, float dy);
    void zoomAt(float factor, float screenX, float screenY, int viewportW, int viewportH);
    void resetView();
    void focusOnRect(float centerX,
                     float centerY,
                     float rectW,
                     float rectH,
                     int viewportW,
                     int viewportH,
                     float fillRatio = 0.9f);
    void focusOnImage(int imageW, int imageH, int viewportW, int viewportH, float fillRatio = 0.9f);

private:
    QVector2D halfExtents(int viewportW, int viewportH) const;

    QVector2D m_center;
    float m_zoom = 1.0f;
};
