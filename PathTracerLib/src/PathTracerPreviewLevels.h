#pragma once

#include <utility>
#include <vector>

struct PreviewLevelDimensions
{
    int width = 0;
    int height = 0;
    int downscale = 1;
};

constexpr int kPreviewMaxPathDepth = 2;

/// Downscale factor for preview level `levelIndex` (0 = coarsest) when `previewLevelCount` levels exist.
/// Level 0 is 1/2^N, level N-1 is 1/2 of full resolution.
inline int previewLevelDownscale(int previewLevelCount, int levelIndex)
{
    if (previewLevelCount <= 0 || levelIndex < 0 || levelIndex >= previewLevelCount) {
        return 1;
    }

    const int exponent = previewLevelCount - levelIndex;
    return 1 << exponent;
}

inline int previewDimensionAxis(int fullAxis, int downscale)
{
    if (fullAxis <= 0 || downscale <= 0) {
        return 0;
    }
    const int dim = fullAxis / downscale;
    return dim >= 1 ? dim : 1;
}

inline PreviewLevelDimensions previewLevelDimensionsAt(
    int fullWidth,
    int fullHeight,
    int previewLevelCount,
    int levelIndex)
{
    const int downscale = previewLevelDownscale(previewLevelCount, levelIndex);
    return {
        previewDimensionAxis(fullWidth, downscale),
        previewDimensionAxis(fullHeight, downscale),
        downscale};
}

inline std::vector<PreviewLevelDimensions> buildPreviewLevelDimensions(
    int fullWidth,
    int fullHeight,
    int previewLevelCount)
{
    std::vector<PreviewLevelDimensions> levels;
    if (previewLevelCount <= 0 || fullWidth <= 0 || fullHeight <= 0) {
        return levels;
    }

    levels.reserve(static_cast<std::size_t>(previewLevelCount));
    for (int i = 0; i < previewLevelCount; ++i) {
        levels.push_back(previewLevelDimensionsAt(fullWidth, fullHeight, previewLevelCount, i));
    }
    return levels;
}
