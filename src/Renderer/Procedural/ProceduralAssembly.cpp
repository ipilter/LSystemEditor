#include "ProceduralAssembly.h"

#include "Loft.h"

#include <algorithm>
#include <unordered_set>
#include <vector>

namespace {

bool isValidManifold(const manifold::Manifold& mesh)
{
    return mesh.Status() == manifold::Manifold::Error::NoError && mesh.NumTri() > 0;
}

manifold::Manifold binaryUnionAll(std::vector<manifold::Manifold> parts)
{
    parts.erase(
        std::remove_if(parts.begin(), parts.end(), [](const manifold::Manifold& m) {
            return !isValidManifold(m);
        }),
        parts.end());

    if (parts.empty()) {
        return manifold::Manifold();
    }

    while (parts.size() > 1) {
        std::vector<manifold::Manifold> next;
        next.reserve((parts.size() + 1) / 2);
        for (size_t i = 0; i < parts.size(); i += 2) {
            if (i + 1 < parts.size()) {
                manifold::Manifold merged = parts[i] + parts[i + 1];
                if (isValidManifold(merged)) {
                    next.push_back(std::move(merged));
                } else if (isValidManifold(parts[i])) {
                    next.push_back(std::move(parts[i]));
                } else if (isValidManifold(parts[i + 1])) {
                    next.push_back(std::move(parts[i + 1]));
                }
            } else {
                next.push_back(std::move(parts[i]));
            }
        }
        parts = std::move(next);
    }

    return parts.front();
}

std::vector<manifold::Manifold> loftAllSegments(const TurtleOutput& output, const ProceduralBuildParams& params)
{
    std::vector<manifold::Manifold> lofts;
    lofts.reserve(output.segments.size());

    for (const TurtleSegment& segment : output.segments) {
        manifold::Manifold loft = loftOrSphereFromSegment(segment, params);
        if (isValidManifold(loft)) {
            lofts.push_back(std::move(loft));
        }
    }

    return lofts;
}

} // namespace

manifold::Manifold assembleLofts(const TurtleOutput& output, const ProceduralBuildParams& params)
{
    if (output.segments.empty()) {
        return manifold::Manifold();
    }

    std::vector<manifold::Manifold> segmentLofts = loftAllSegments(output, params);
    if (segmentLofts.empty()) {
        return manifold::Manifold();
    }

    std::unordered_set<size_t> groupedIndices;
    std::vector<manifold::Manifold> assemblyParts;

    for (const BranchGroup& group : output.branchGroups) {
        if (group.segmentIndices.size() < 2) {
            continue;
        }

        std::vector<manifold::Manifold> groupParts;
        groupParts.reserve(group.segmentIndices.size());
        for (size_t segmentIndex : group.segmentIndices) {
            if (segmentIndex < segmentLofts.size() && isValidManifold(segmentLofts[segmentIndex])) {
                groupedIndices.insert(segmentIndex);
                groupParts.push_back(segmentLofts[segmentIndex]);
            }
        }

        manifold::Manifold grouped = binaryUnionAll(std::move(groupParts));
        if (isValidManifold(grouped)) {
            assemblyParts.push_back(std::move(grouped));
        }
    }

    std::vector<manifold::Manifold> ungroupedParts;
    for (size_t i = 0; i < segmentLofts.size(); ++i) {
        if (groupedIndices.find(i) == groupedIndices.end() && isValidManifold(segmentLofts[i])) {
            ungroupedParts.push_back(segmentLofts[i]);
        }
    }

    manifold::Manifold ungroupedUnion = binaryUnionAll(std::move(ungroupedParts));
    if (isValidManifold(ungroupedUnion)) {
        assemblyParts.push_back(std::move(ungroupedUnion));
    }

    manifold::Manifold result = binaryUnionAll(std::move(assemblyParts));
    if (!isValidManifold(result)) {
        return manifold::Manifold();
    }

    manifold::Manifold refined = result.RefineToTolerance(static_cast<double>(params.globalRefineTolerance));
    if (isValidManifold(refined)) {
        result = std::move(refined);
    }

    return result;
}
