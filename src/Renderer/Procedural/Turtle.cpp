#include "Turtle.h"

#include "Geometry/MathCore.h"

#include <cmath>
#include <cctype>
#include <string>

namespace {

struct TurtlePose
{
    Vec3 position{0.0f, 0.0f, 0.0f};
    Vec3 tangent{0.0f, 0.0f, -1.0f};
    Vec3 up{0.0f, 1.0f, 0.0f};
};

struct BranchFrame
{
    TurtlePose pose;
    size_t segmentCountAtPush = 0;
};

bool symbolNameIs(const Symbol& sym, const char* name)
{
    if (sym.name.size() != std::char_traits<char>::length(name)) {
        return false;
    }
    for (size_t i = 0; i < sym.name.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(sym.name[i]))
            != std::tolower(static_cast<unsigned char>(name[i]))) {
            return false;
        }
    }
    return true;
}

bool symbolNameExactly(const Symbol& sym, const char* name)
{
    return sym.name == name;
}

bool trySymbolFloat(const Symbol& sym, size_t argIndex, float& outValue)
{
    if (argIndex >= sym.args.size() || !sym.args[argIndex]) {
        return false;
    }
    if (sym.args[argIndex]->kind != Expr::Kind::Number) {
        return false;
    }
    outValue = static_cast<float>(sym.args[argIndex]->number_value);
    return true;
}

struct ForwardStep
{
    float step = 0.0f;
    float radius = 1.0f;
};

ForwardStep parseForwardStep(const Symbol& sym, const TurtleParams& params)
{
    float arg0 = 0.0f;
    float arg1 = 0.0f;
    const bool hasArg0 = trySymbolFloat(sym, 0, arg0);
    const bool hasArg1 = trySymbolFloat(sym, 1, arg1);

    ForwardStep result{};
    result.step = params.defaultStepLength;
    result.radius = params.defaultRadius;
    if (hasArg0 && hasArg1) {
        result.step = arg0;
        result.radius = arg1;
    } else if (hasArg0) {
        result.step = arg0;
        result.radius = arg0;
    }
    return result;
}

Vec3 rotateAroundAxis(Vec3 v, Vec3 axis, float angleRad)
{
    const Vec3 n = vecNormalize3(axis);
    const float c = std::cos(angleRad);
    const float s = std::sin(angleRad);
    const float oneMinusC = 1.0f - c;

    return vecMake3(
        (c + n.x * n.x * oneMinusC) * v.x + (n.x * n.y * oneMinusC - n.z * s) * v.y
            + (n.x * n.z * oneMinusC + n.y * s) * v.z,
        (n.y * n.x * oneMinusC + n.z * s) * v.x + (c + n.y * n.y * oneMinusC) * v.y
            + (n.y * n.z * oneMinusC - n.x * s) * v.z,
        (n.z * n.x * oneMinusC - n.y * s) * v.x + (n.z * n.y * oneMinusC + n.x * s) * v.y
            + (c + n.z * n.z * oneMinusC) * v.z);
}

void rotatePose(TurtlePose& pose, Vec3 axis, float degrees)
{
    const float radians = degrees * 3.14159265f / 180.0f;
    pose.tangent = vecNormalize3(rotateAroundAxis(pose.tangent, axis, radians));
    pose.up = vecNormalize3(rotateAroundAxis(pose.up, axis, radians));
}

Vec3 poseRight(const TurtlePose& pose)
{
    return vecNormalize3(vecCross3(pose.up, pose.tangent));
}

void applyYaw(TurtlePose& pose, float degrees)
{
    rotatePose(pose, pose.up, degrees);
}

void applyPitch(TurtlePose& pose, float degrees)
{
    rotatePose(pose, poseRight(pose), degrees);
}

void applyRoll(TurtlePose& pose, float degrees)
{
    rotatePose(pose, pose.tangent, degrees);
}

void applyForward(TurtlePose& pose, float step, float radius, std::vector<TurtleState>& currentStates)
{
    currentStates.push_back(TurtleState{pose.position, pose.tangent, radius});
    pose.position = vecAdd3(pose.position, vecScale3(pose.tangent, step));
}

void applyMoveOnly(TurtlePose& pose, float step)
{
    pose.position = vecAdd3(pose.position, vecScale3(pose.tangent, step));
}

class TurtleInterpreter
{
public:
    explicit TurtleInterpreter(const TurtleParams& params)
        : m_params(params)
    {
    }

    TurtleOutput execute(const std::vector<Symbol>& symbols)
    {
        m_output = TurtleOutput{};
        m_currentStates.clear();
        m_pose = TurtlePose{};
        m_branchStack.clear();

        for (const Symbol& sym : symbols) {
            if (sym.kind == SymbolKind::SubsystemRef) {
                continue;
            }

            if (symbolNameIs(sym, "[")) {
                flushSegment();
                BranchFrame frame{};
                frame.pose = m_pose;
                frame.segmentCountAtPush = m_output.segments.size();
                m_branchStack.push_back(frame);
                continue;
            }

            if (symbolNameIs(sym, "]")) {
                flushSegment();
                if (!m_branchStack.empty()) {
                    const BranchFrame frame = m_branchStack.back();
                    m_branchStack.pop_back();
                    m_pose = frame.pose;

                    const size_t branchSegmentCount = m_output.segments.size() - frame.segmentCountAtPush;
                    if (branchSegmentCount > 1) {
                        BranchGroup group{};
                        group.segmentIndices.reserve(branchSegmentCount);
                        for (size_t i = 0; i < branchSegmentCount; ++i) {
                            group.segmentIndices.push_back(frame.segmentCountAtPush + i);
                        }
                        m_output.branchGroups.push_back(std::move(group));
                    }
                }
                continue;
            }

            if (symbolNameExactly(sym, "f")) {
                flushSegment();
                const ForwardStep forward = parseForwardStep(sym, m_params);
                applyMoveOnly(m_pose, forward.step);
                continue;
            }

            if (symbolNameExactly(sym, "F")) {
                const ForwardStep forward = parseForwardStep(sym, m_params);
                applyForward(m_pose, forward.step, forward.radius, m_currentStates);
                continue;
            }

            float arg = 0.0f;
            const bool hasArg = trySymbolFloat(sym, 0, arg);

            if (symbolNameIs(sym, "Yaw") && hasArg) {
                applyYaw(m_pose, arg);
                continue;
            }

            if ((symbolNameIs(sym, "Pitch") || symbolNameIs(sym, "Pich")) && hasArg) {
                applyPitch(m_pose, arg);
                continue;
            }

            if (symbolNameIs(sym, "Roll") && hasArg) {
                applyRoll(m_pose, arg);
                continue;
            }

            if (symbolNameIs(sym, "Mat") && hasArg) {
                m_currentMaterialId = static_cast<uint32_t>(arg);
                continue;
            }
        }

        flushSegment();
        return m_output;
    }

private:
    void flushSegment()
    {
        if (m_currentStates.empty()) {
            return;
        }

        const TurtleState& last = m_currentStates.back();
        m_currentStates.push_back(TurtleState{m_pose.position, m_pose.tangent, last.radius});

        TurtleSegment segment{};
        segment.states = std::move(m_currentStates);
        segment.materialId = m_currentMaterialId;
        m_output.segments.push_back(std::move(segment));
        m_currentStates.clear();
    }

    TurtleParams m_params;
    TurtleOutput m_output;
    TurtlePose m_pose;
    std::vector<TurtleState> m_currentStates;
    std::vector<BranchFrame> m_branchStack;
    uint32_t m_currentMaterialId = 0;
};

} // namespace

TurtleOutput turtleExecute(const std::vector<Symbol>& symbols, const TurtleParams& params)
{
    TurtleInterpreter interpreter(params);
    return interpreter.execute(symbols);
}
