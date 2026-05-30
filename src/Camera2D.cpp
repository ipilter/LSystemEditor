#include "Camera2D.h"

#include <algorithm>

Camera2D::Camera2D()
    : m_center(0.0f, 0.0f)
{
}

QVector2D Camera2D::halfExtents(int viewportW, int viewportH) const
{
    if (viewportW <= 0 || viewportH <= 0) {
        return QVector2D(0.0f, 0.0f);
    }

    return QVector2D((static_cast<float>(viewportW) * 0.5f) / m_zoom,
                     (static_cast<float>(viewportH) * 0.5f) / m_zoom);
}

QMatrix4x4 Camera2D::viewProjection(int viewportW, int viewportH) const
{
    const QVector2D half = halfExtents(viewportW, viewportH);
    if (half.x() <= 0.0f || half.y() <= 0.0f) {
        return QMatrix4x4();
    }

    QMatrix4x4 matrix;
    matrix.ortho(m_center.x() - half.x(), m_center.x() + half.x(),
                 m_center.y() - half.y(), m_center.y() + half.y(),
                 -1.0f, 1.0f);
    return matrix;
}

Camera2DFrustum Camera2D::frustum(int viewportW, int viewportH) const
{
    const QVector2D half = halfExtents(viewportW, viewportH);
    return Camera2DFrustum{
        m_center.x() - half.x(),
        m_center.x() + half.x(),
        m_center.y() - half.y(),
        m_center.y() + half.y(),
    };
}

void Camera2D::pan(float dx, float dy)
{
    m_center.setX(m_center.x() - dx / m_zoom);
    m_center.setY(m_center.y() + dy / m_zoom);
}

void Camera2D::zoomAt(float factor, float screenX, float screenY, int viewportW, int viewportH)
{
    if (viewportW <= 0 || viewportH <= 0 || factor <= 0.0f) {
        return;
    }

    const float offsetX = screenX - static_cast<float>(viewportW) * 0.5f;
    const float offsetY = screenY - static_cast<float>(viewportH) * 0.5f;

    const float worldX = m_center.x() + offsetX / m_zoom;
    const float worldY = m_center.y() - offsetY / m_zoom;

    m_zoom *= factor;

    m_center.setX(worldX - offsetX / m_zoom);
    m_center.setY(worldY + offsetY / m_zoom);
}

void Camera2D::resetView()
{
    m_center = QVector2D(0.0f, 0.0f);
    m_zoom = 1.0f;
}

void Camera2D::focusOnRect(float centerX,
                           float centerY,
                           float rectW,
                           float rectH,
                           int viewportW,
                           int viewportH,
                           float fillRatio)
{
    if (viewportW <= 0 || viewportH <= 0 || rectW <= 0.0f || rectH <= 0.0f || fillRatio <= 0.0f) {
        return;
    }

    m_center = QVector2D(centerX, centerY);
    m_zoom = std::min(fillRatio * static_cast<float>(viewportW) / rectW,
                      fillRatio * static_cast<float>(viewportH) / rectH);
}

void Camera2D::focusOnImage(int imageW, int imageH, int viewportW, int viewportH, float fillRatio)
{
    focusOnRect(0.0f,
                0.0f,
                static_cast<float>(imageW),
                static_cast<float>(imageH),
                viewportW,
                viewportH,
                fillRatio);
}
