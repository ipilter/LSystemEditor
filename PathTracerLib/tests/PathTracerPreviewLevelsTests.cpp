#include "PathTracerPreviewLevels.h"

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

void expectEq(int actual, int expected, const char* message)
{
    if (actual != expected) {
        std::cerr << "FAIL: " << message << " (got " << actual << ", expected " << expected << ")\n";
        ++gFailures;
    }
}

void testNoPreviewLevels()
{
    const auto levels = buildPreviewLevelDimensions(1920, 1080, 0);
    expectTrue(levels.empty(), "zero preview levels should produce no buffers");

    expectEq(previewLevelDownscale(0, 0), 1, "zero preview count downscale fallback");
}

void testSinglePreviewLevel()
{
    const auto levels = buildPreviewLevelDimensions(1920, 1080, 1);
    expectTrue(levels.size() == 1, "one preview level");
    expectEq(levels[0].width, 960, "single level width is half");
    expectEq(levels[0].height, 540, "single level height is half");
    expectEq(levels[0].downscale, 2, "single level downscale is 2");
}

void testTwoPreviewLevels()
{
    const auto levels = buildPreviewLevelDimensions(1920, 1080, 2);
    expectTrue(levels.size() == 2, "two preview levels");

    expectEq(levels[0].width, 480, "coarse level width is quarter");
    expectEq(levels[0].height, 270, "coarse level height is quarter");
    expectEq(levels[0].downscale, 4, "coarse level downscale is 4");

    expectEq(levels[1].width, 960, "fine preview width is half");
    expectEq(levels[1].height, 540, "fine preview height is half");
    expectEq(levels[1].downscale, 2, "fine preview downscale is 2");
}

void testMinimumDimensionClamp()
{
    const PreviewLevelDimensions dim = previewLevelDimensionsAt(3, 3, 2, 0);
    expectEq(dim.width, 1, "minimum preview width clamped to 1");
    expectEq(dim.height, 1, "minimum preview height clamped to 1");
}

} // namespace

int main()
{
    testNoPreviewLevels();
    testSinglePreviewLevel();
    testTwoPreviewLevels();
    testMinimumDimensionClamp();

    if (gFailures == 0) {
        std::cout << "All PathTracer preview level tests passed.\n";
        return 0;
    }

    std::cerr << gFailures << " test(s) failed.\n";
    return 1;
}
