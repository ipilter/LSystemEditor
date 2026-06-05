#include "Spline.h"

#include "Geometry/MathCore.h"

#include <cmath>

namespace {

Vec3 reflectVector(Vec3 v, Vec3 axis)
{
    const float dot = vecDot3(v, axis);
    return vecSub3(v, vecScale3(axis, 2.0f * dot));
}

Vec3 parallelTransportNormal(Vec3 prevTangent, Vec3 nextTangent, Vec3 prevNormal)
{
    const Vec3 t0 = vecNormalize3(prevTangent);
    const Vec3 t1 = vecNormalize3(nextTangent);
    const Vec3 axis = vecCross3(t0, t1);
    const float axisLen = vecLength3(axis);
    if (axisLen <= 1e-6f) {
        return prevNormal;
    }

    const Vec3 rotAxis = vecScale3(axis, 1.0f / axisLen);
    const float angle = std::asin(vecClamp(axisLen, -1.0f, 1.0f));
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    const Vec3 n = rotAxis;
    const float oneMinusC = 1.0f - c;

    return vecNormalize3(vecMake3(
        (c + n.x * n.x * oneMinusC) * prevNormal.x + (n.x * n.y * oneMinusC - n.z * s) * prevNormal.y
            + (n.x * n.z * oneMinusC + n.y * s) * prevNormal.z,
        (n.y * n.x * oneMinusC + n.z * s) * prevNormal.x + (c + n.y * n.y * oneMinusC) * prevNormal.y
            + (n.y * n.z * oneMinusC - n.x * s) * prevNormal.z,
        (n.z * n.x * oneMinusC - n.y * s) * prevNormal.x + (n.z * n.y * oneMinusC + n.x * s) * prevNormal.y
            + (c + n.z * n.z * oneMinusC) * prevNormal.z));
}

Vec3 initialNormal(Vec3 tangent)
{
    const Vec3 t = vecNormalize3(tangent);
    Vec3 up = vecMake3(0.0f, 1.0f, 0.0f);
    if (std::fabs(vecDot3(t, up)) > 0.95f) {
        up = vecMake3(1.0f, 0.0f, 0.0f);
    }
    return vecNormalize3(vecCross3(up, t));
}

struct LoftSamplePoint
{
    Vec3 position{};
    Vec3 tangent{};
    float radius = 0.1f;
};

void appendLoftSample(std::vector<LoftSamplePoint>& points, const LoftSamplePoint& sample)
{
    if (!points.empty()) {
        const LoftSamplePoint& last = points.back();
        if (vecLength3(vecSub3(sample.position, last.position)) <= 1e-6f) {
            return;
        }
    }
    points.push_back(sample);
}

std::vector<PathFrame> parallelTransportFramesFromSamples(const std::vector<LoftSamplePoint>& samples)
{
    std::vector<PathFrame> frames;
    frames.reserve(samples.size());
    if (samples.empty()) {
        return frames;
    }

    Vec3 normal = initialNormal(samples.front().tangent);
    for (size_t i = 0; i < samples.size(); ++i) {
        const LoftSamplePoint& sample = samples[i];
        if (i > 0) {
            normal = parallelTransportNormal(samples[i - 1].tangent, sample.tangent, normal);
        }

        const Vec3 tangent = vecNormalize3(sample.tangent);
        const Vec3 binormal = vecNormalize3(vecCross3(tangent, normal));
        normal = vecNormalize3(vecCross3(binormal, tangent));

        PathFrame frame{};
        frame.position = sample.position;
        frame.tangent = tangent;
        frame.normal = normal;
        frame.binormal = binormal;
        frame.radius = sample.radius;
        frames.push_back(frame);
    }

    return frames;
}

} // namespace

bool SplinePath::buildFromSegment(const TurtleSegment& segment, float hermiteTension)
{
    m_spans.clear();
    if (segment.states.size() < 2) {
        return false;
    }

    const float tension = hermiteTension > 0.0f ? hermiteTension : 1.0f;
    for (size_t i = 0; i + 1 < segment.states.size(); ++i) {
        const TurtleState& s0 = segment.states[i];
        const TurtleState& s1 = segment.states[i + 1];
        const Vec3 chord = vecSub3(s1.position, s0.position);
        const float chordLength = vecLength3(chord);

        HermiteSpan span{};
        span.p0 = s0.position;
        span.p1 = s1.position;
        span.m0 = vecScale3(vecNormalize3(s0.tangent), chordLength * tension);
        span.m1 = vecScale3(vecNormalize3(s1.tangent), chordLength * tension);
        span.r0 = s0.radius;
        span.r1 = s1.radius;
        span.chordLength = chordLength;
        m_spans.push_back(span);
    }

    return !m_spans.empty();
}

Vec3 SplinePath::hermitePoint(const HermiteSpan& span, float t)
{
    const float t2 = t * t;
    const float t3 = t2 * t;
    const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
    const float h10 = t3 - 2.0f * t2 + t;
    const float h01 = -2.0f * t3 + 3.0f * t2;
    const float h11 = t3 - t2;

    return vecAdd3(
        vecAdd3(vecScale3(span.p0, h00), vecScale3(span.m0, h10)),
        vecAdd3(vecScale3(span.p1, h01), vecScale3(span.m1, h11)));
}

Vec3 SplinePath::hermiteTangent(const HermiteSpan& span, float t)
{
    const float t2 = t * t;
    const float dh00 = 6.0f * t2 - 6.0f * t;
    const float dh10 = 3.0f * t2 - 4.0f * t + 1.0f;
    const float dh01 = -6.0f * t2 + 6.0f * t;
    const float dh11 = 3.0f * t2 - 2.0f * t;

    return vecNormalize3(vecAdd3(
        vecAdd3(vecScale3(span.p0, dh00), vecScale3(span.m0, dh10)),
        vecAdd3(vecScale3(span.p1, dh01), vecScale3(span.m1, dh11))));
}

std::vector<SplineSample> SplinePath::sampleUniform(int samplesPerSpan) const
{
    std::vector<SplineSample> samples;
    if (m_spans.empty()) {
        return samples;
    }

    const int perSpan = samplesPerSpan < 2 ? 2 : samplesPerSpan;
    for (size_t spanIndex = 0; spanIndex < m_spans.size(); ++spanIndex) {
        const HermiteSpan& span = m_spans[spanIndex];
        const int startStep = spanIndex == 0 ? 0 : 1;
        for (int step = startStep; step < perSpan; ++step) {
            const float t = static_cast<float>(step) / static_cast<float>(perSpan - 1);
            SplineSample sample{};
            sample.position = hermitePoint(span, t);
            sample.tangent = hermiteTangent(span, t);
            sample.radius = vecLerp(span.r0, span.r1, t);
            samples.push_back(sample);
        }
    }

    return samples;
}

std::vector<PathFrame> SplinePath::computeParallelTransportFrames(int samplesPerSpan) const
{
    const std::vector<SplineSample> samples = sampleUniform(samplesPerSpan);
    std::vector<LoftSamplePoint> points;
    points.reserve(samples.size());
    for (const SplineSample& sample : samples) {
        points.push_back(LoftSamplePoint{sample.position, sample.tangent, sample.radius});
    }
    return parallelTransportFramesFromSamples(points);
}

bool SplinePath::spanNeedsRefinement(const HermiteSpan& span, int samplesPerSpan)
{
    if (samplesPerSpan <= 2) {
        return false;
    }

    if (span.chordLength <= 1e-6f) {
        return false;
    }

    const Vec3 chordDir = vecScale3(vecSub3(span.p1, span.p0), 1.0f / span.chordLength);
    const Vec3 tangent0 = vecNormalize3(span.m0);
    const Vec3 tangent1 = vecNormalize3(span.m1);
    if (vecDot3(tangent0, chordDir) < 0.999f || vecDot3(tangent1, chordDir) < 0.999f) {
        return true;
    }
    if (vecDot3(tangent0, tangent1) < 0.999f) {
        return true;
    }

    const Vec3 midPoint = hermitePoint(span, 0.5f);
    const Vec3 linearMid = vecScale3(vecAdd3(span.p0, span.p1), 0.5f);
    return vecLength3(vecSub3(midPoint, linearMid)) > 1e-4f * span.chordLength;
}

std::vector<PathFrame> SplinePath::computeLoftFrames(int samplesPerSpan) const
{
    if (m_spans.empty()) {
        return {};
    }

    const int perSpan = samplesPerSpan < 2 ? 2 : samplesPerSpan;
    std::vector<LoftSamplePoint> points;
    points.reserve(m_spans.size() + 1);

    for (size_t spanIndex = 0; spanIndex < m_spans.size(); ++spanIndex) {
        const HermiteSpan& span = m_spans[spanIndex];

        if (spanIndex == 0) {
            appendLoftSample(
                points,
                LoftSamplePoint{span.p0, vecNormalize3(span.m0), span.r0});
        }

        if (spanNeedsRefinement(span, perSpan)) {
            for (int step = 1; step < perSpan - 1; ++step) {
                const float t = static_cast<float>(step) / static_cast<float>(perSpan - 1);
                appendLoftSample(
                    points,
                    LoftSamplePoint{
                        hermitePoint(span, t),
                        hermiteTangent(span, t),
                        vecLerp(span.r0, span.r1, t)});
            }
        }

        appendLoftSample(
            points,
            LoftSamplePoint{span.p1, vecNormalize3(span.m1), span.r1});
    }

    return parallelTransportFramesFromSamples(points);
}

float SplinePath::totalArcLength() const
{
    float length = 0.0f;
    for (const HermiteSpan& span : m_spans) {
        length += span.chordLength;
    }
    return length;
}
