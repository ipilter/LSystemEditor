#pragma once

#include "ProceduralTypes.h"

#include <vector>

class SplinePath
{
public:
    bool buildFromSegment(const TurtleSegment& segment, float hermiteTension = 1.0f);

    std::vector<SplineSample> sampleUniform(int samplesPerSpan) const;
    std::vector<PathFrame> computeParallelTransportFrames(int samplesPerSpan) const;

    /** Ring positions for loft: turtle knots plus optional interior samples on curved spans. */
    std::vector<PathFrame> computeLoftFrames(int samplesPerSpan) const;

    float totalArcLength() const;
    bool empty() const { return m_spans.empty(); }

private:
    struct HermiteSpan
    {
        Vec3 p0{};
        Vec3 p1{};
        Vec3 m0{};
        Vec3 m1{};
        float r0 = 0.1f;
        float r1 = 0.1f;
        float chordLength = 0.0f;
    };

    static Vec3 hermitePoint(const HermiteSpan& span, float t);
    static Vec3 hermiteTangent(const HermiteSpan& span, float t);
    static bool spanNeedsRefinement(const HermiteSpan& span, int samplesPerSpan);
    static bool spanNeedsRadiusRefinement(const HermiteSpan& span);
    static float spanRadiusSlope(const HermiteSpan& span);
    static int spanRingIntervalCount(const HermiteSpan& span, int samplesPerSpan, bool adjacentRadiusSpan);

    std::vector<HermiteSpan> m_spans;
};
