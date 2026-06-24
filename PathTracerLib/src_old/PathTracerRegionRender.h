#pragma once

#include <QRect>

#include <algorithm>
#include <vector>

namespace PathTracerRegionRender {

inline QRect normalizeRegionRect(int minX, int minY, int maxX, int maxY, int renderW, int renderH)
{
    const int maxCoordX = std::max(0, renderW - 1);
    const int maxCoordY = std::max(0, renderH - 1);
    const int left = std::max(0, std::min(minX, maxX));
    const int right = std::min(maxCoordX, std::max(minX, maxX));
    const int top = std::max(0, std::min(minY, maxY));
    const int bottom = std::min(maxCoordY, std::max(minY, maxY));
    return QRect(QPoint(left, top), QPoint(right, bottom));
}

inline int regionPixelCount(const QRect& region)
{
    if (region.isEmpty()) {
        return 0;
    }
    return (region.right() - region.left() + 1) * (region.bottom() - region.top() + 1);
}

inline std::vector<int> buildRegionActiveIndices(const QRect& region, int width)
{
    std::vector<int> indices;
    if (region.isEmpty() || width <= 0) {
        return indices;
    }

    const int count = regionPixelCount(region);
    indices.reserve(static_cast<std::size_t>(count));

    for (int y = region.top(); y <= region.bottom(); ++y) {
        for (int x = region.left(); x <= region.right(); ++x) {
            indices.push_back(y * width + x);
        }
    }

    return indices;
}

} // namespace PathTracerRegionRender
