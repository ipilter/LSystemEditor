#pragma once

#include <glm/glm.hpp>

#include <vector>

struct OverlayDrawVertex
{
    glm::vec4 clipPos{0.0f};
    glm::vec3 color{1.0f};
};

namespace OverlayLineClip {

inline constexpr float kMinHomogeneousW = 1.0e-4f;

inline bool clipSegmentToPositiveW(glm::vec4& a, glm::vec4& b, float minW = kMinHomogeneousW)
{
    if (a.w <= minW && b.w <= minW) {
        return false;
    }

    if (a.w <= minW) {
        const float t = (minW - a.w) / (b.w - a.w);
        a = a + t * (b - a);
    } else if (b.w <= minW) {
        const float t = (minW - a.w) / (b.w - a.w);
        b = a + t * (b - a);
    }

    return a.w > minW && b.w > minW;
}

inline glm::vec4 projectSceneClipToWidgetClip(const glm::vec4& sceneClip, const glm::mat4& quadViewProj)
{
    const glm::vec3 ndc = glm::vec3(sceneClip) / sceneClip.w;
    return quadViewProj * glm::vec4(ndc, 1.0f);
}

inline void appendClippedWorldLine(
    const glm::vec3& aWorld,
    const glm::vec3& bWorld,
    const glm::vec3& color,
    const glm::mat4& sceneMvp,
    const glm::mat4& quadViewProj,
    std::vector<OverlayDrawVertex>& out,
    float minW = kMinHomogeneousW)
{
    glm::vec4 clipA = sceneMvp * glm::vec4(aWorld, 1.0f);
    glm::vec4 clipB = sceneMvp * glm::vec4(bWorld, 1.0f);
    if (!clipSegmentToPositiveW(clipA, clipB, minW)) {
        return;
    }

    out.push_back({projectSceneClipToWidgetClip(clipA, quadViewProj), color});
    out.push_back({projectSceneClipToWidgetClip(clipB, quadViewProj), color});
}

} // namespace OverlayLineClip
