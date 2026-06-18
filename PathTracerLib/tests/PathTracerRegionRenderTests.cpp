#include "PathTracerRegionRender.h"

#include <iostream>

namespace {

int gFailures = 0;

void expectTrue(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++gFailures;
    }
}

void testNormalizeRegionRectOrdersCorners()
{
    const QRect rect = PathTracerRegionRender::normalizeRegionRect(10, 20, 2, 5, 32, 32);
    expectTrue(rect.left() == 2, "left corner normalized");
    expectTrue(rect.top() == 5, "top corner normalized");
    expectTrue(rect.right() == 10, "right corner normalized");
    expectTrue(rect.bottom() == 20, "bottom corner normalized");
}

void testNormalizeRegionRectClampsToRenderSize()
{
    const QRect rect = PathTracerRegionRender::normalizeRegionRect(-5, -3, 100, 100, 16, 16);
    expectTrue(rect.left() == 0, "clamped left");
    expectTrue(rect.top() == 0, "clamped top");
    expectTrue(rect.right() == 15, "clamped right");
    expectTrue(rect.bottom() == 15, "clamped bottom");
}

void testBuildRegionActiveIndicesMatchesRowMajorLayout()
{
    const QRect region(1, 2, 3, 3);
    const auto indices = PathTracerRegionRender::buildRegionActiveIndices(region, 8);
    expectTrue(indices.size() == 6U, "region index count");
    expectTrue(indices[0] == 2 * 8 + 1, "first index");
    expectTrue(indices[1] == 2 * 8 + 2, "second index");
    expectTrue(indices[2] == 2 * 8 + 3, "third index");
    expectTrue(indices[5] == 3 * 8 + 3, "last index");
}

void testRegionPixelCountUsesInclusiveBounds()
{
    const QRect region(0, 0, 1, 1);
    expectTrue(PathTracerRegionRender::regionPixelCount(region) == 4, "2x2 inclusive pixel count");
}

} // namespace

int main()
{
    testNormalizeRegionRectOrdersCorners();
    testNormalizeRegionRectClampsToRenderSize();
    testBuildRegionActiveIndicesMatchesRowMajorLayout();
    testRegionPixelCountUsesInclusiveBounds();

    if (gFailures != 0) {
        std::cerr << gFailures << " test failure(s)\n";
        return 1;
    }

    std::cout << "All PathTracerRegionRender tests passed\n";
    return 0;
}
