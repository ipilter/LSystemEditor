#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

class QuadViewCamera2D
{
public:
    glm::mat4 viewProjMatrix(float sx, float sy) const
    {
        const float halfW = sx / m_zoom;
        const float halfH = sy / m_zoom;
        return glm::ortho(
            m_center.x - halfW,
            m_center.x + halfW,
            m_center.y - halfH,
            m_center.y + halfH,
            -1.0f,
            1.0f);
    }

    void pan(float ndcDeltaX, float ndcDeltaY)
    {
        m_center.x += ndcDeltaX;
        m_center.y += ndcDeltaY;
    }

    void zoomAt(float factor, float cursorNdcX, float cursorNdcY, float sx, float sy)
    {
        const float oldHalfW = sx / m_zoom;
        const float oldHalfH = sy / m_zoom;

        const float worldX = m_center.x + cursorNdcX * oldHalfW;
        const float worldY = m_center.y + cursorNdcY * oldHalfH;

        m_zoom = std::clamp(m_zoom * factor, kMinZoom, kMaxZoom);

        const float newHalfW = sx / m_zoom;
        const float newHalfH = sy / m_zoom;

        m_center.x = worldX - cursorNdcX * newHalfW;
        m_center.y = worldY - cursorNdcY * newHalfH;
    }

    bool isDefault() const
    {
        return m_center.x == 0.0f && m_center.y == 0.0f && m_zoom == 1.0f;
    }

    void reset()
    {
        m_center = glm::vec2(0.0f);
        m_zoom = 1.0f;
    }

    static std::pair<float, float> widgetToNdc(float x, float y, int widgetW, int widgetH)
    {
        if (widgetW <= 0 || widgetH <= 0) {
            return {0.0f, 0.0f};
        }
        const float ndcX = 2.0f * x / static_cast<float>(widgetW) - 1.0f;
        const float ndcY = 1.0f - 2.0f * y / static_cast<float>(widgetH);
        return {ndcX, ndcY};
    }

private:
    static constexpr float kMinZoom = 0.1f;
    static constexpr float kMaxZoom = 32.0f;

    glm::vec2 m_center{0.0f};
    float m_zoom = 1.0f;
};
