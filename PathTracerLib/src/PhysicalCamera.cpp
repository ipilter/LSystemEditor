#include "PhysicalCamera.h"

#include "SceneUnits.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>

#include <vector_types.h>

namespace {

constexpr float kMaxPitchRad = glm::radians(89.0f);

constexpr ShutterSpeedPreset kShutterSpeedPresets[] = {
    {"1/8000", 1.0f / 8000.0f},
    {"1/6400", 1.0f / 6400.0f},
    {"1/5000", 1.0f / 5000.0f},
    {"1/4000", 1.0f / 4000.0f},
    {"1/3200", 1.0f / 3200.0f},
    {"1/2500", 1.0f / 2500.0f},
    {"1/2000", 1.0f / 2000.0f},
    {"1/1600", 1.0f / 1600.0f},
    {"1/1250", 1.0f / 1250.0f},
    {"1/1000", 1.0f / 1000.0f},
    {"1/800", 1.0f / 800.0f},
    {"1/640", 1.0f / 640.0f},
    {"1/500", 1.0f / 500.0f},
    {"1/400", 1.0f / 400.0f},
    {"1/350", 1.0f / 350.0f},
    {"1/250", 1.0f / 250.0f},
    {"1/200", 1.0f / 200.0f},
    {"1/160", 1.0f / 160.0f},
    {"1/125", 1.0f / 125.0f},
    {"1/100", 1.0f / 100.0f},
    {"1/80", 1.0f / 80.0f},
    {"1/60", 1.0f / 60.0f},
    {"1/50", 1.0f / 50.0f},
    {"1/40", 1.0f / 40.0f},
    {"1/30", 1.0f / 30.0f},
    {"1/25", 1.0f / 25.0f},
    {"1/20", 1.0f / 20.0f},
    {"1/15", 1.0f / 15.0f},
    {"1/13", 1.0f / 13.0f},
    {"1/10", 1.0f / 10.0f},
    {"1/8", 1.0f / 8.0f},
    {"1/6", 1.0f / 6.0f},
    {"1/5", 1.0f / 5.0f},
    {"1/4", 1.0f / 4.0f},
    {"1/3", 1.0f / 3.0f},
    {"1/2.5", 1.0f / 2.5f},
    {"1/2", 1.0f / 2.0f},
    {"1/1.6", 1.0f / 1.6f},
    {"1/1.3", 1.0f / 1.3f},
    {"1\"", 1.0f},
    {"1.3\"", 1.3f},
    {"1.6\"", 1.6f},
    {"2\"", 2.0f},
    {"2.5\"", 2.5f},
    {"3\"", 3.0f},
    {"4\"", 4.0f},
    {"5\"", 5.0f},
    {"6\"", 6.0f},
    {"8\"", 8.0f},
    {"10\"", 10.0f},
    {"13\"", 13.0f},
    {"15\"", 15.0f},
    {"20\"", 20.0f},
    {"25\"", 25.0f},
    {"30\"", 30.0f},
    {"60\"", 60.0f},
    {"90\"", 90.0f},
    {"120\"", 120.0f},
};

constexpr IsoPreset kIsoPresets[] = {
    {"100", 100.0f},
    {"200", 200.0f},
    {"400", 400.0f},
    {"800", 800.0f},
    {"1600", 1600.0f},
    {"3200", 3200.0f},
    {"6400", 6400.0f},
    {"12800", 12800.0f},
    {"25600", 25600.0f},
};

} // namespace

PhysicalCamera::PhysicalCamera()
    : m_position(0.0f, 0.0f, 0.0f)
    , m_orientation(1.0f, 0.0f, 0.0f, 0.0f)
    , m_fovY(0.0f)
    , m_near(0.0f)
    , m_far(kDefaultFarPlane)
    , m_aspect(1.0f)
    , m_yawRad(0.0f)
    , m_pitchRad(0.0f)
    , m_rollRad(0.0f)
{
    rebuildOrientation();
    updateOpticsFromFocalLengthMm();
    setDefaultFocusPoint();
}

glm::vec3 PhysicalCamera::forward() const
{
    return glm::mat3_cast(m_orientation) * glm::vec3(0.0f, 0.0f, -1.0f);
}

glm::vec3 PhysicalCamera::right() const
{
    return glm::mat3_cast(m_orientation) * glm::vec3(1.0f, 0.0f, 0.0f);
}

glm::vec3 PhysicalCamera::up() const
{
    return glm::mat3_cast(m_orientation) * glm::vec3(0.0f, 1.0f, 0.0f);
}

CameraFrustum PhysicalCamera::frustum(int imageW, int imageH) const
{
    const float aspect = imageW > 0 && imageH > 0 ? static_cast<float>(imageW) / static_cast<float>(imageH)
                                                    : m_aspect;
    const float tanHalf = glm::tan(m_fovY * 0.5f);
    const float top = m_near * tanHalf;
    const float rightExtent = top * aspect;

    return CameraFrustum{
        -rightExtent,
        rightExtent,
        -top,
        top,
        m_near,
        m_far,
    };
}

glm::mat4 PhysicalCamera::viewMatrix() const
{
    return glm::lookAt(m_position, m_position + forward(), up());
}

glm::mat4 PhysicalCamera::projMatrix(int imageW, int imageH) const
{
    const float aspect = imageW > 0 && imageH > 0 ? static_cast<float>(imageW) / static_cast<float>(imageH)
                                                    : m_aspect;
    return glm::perspective(m_fovY, aspect, m_near, m_far);
}

void PhysicalCamera::moveForward(float distance)
{
    m_position += forward() * distance;
}

void PhysicalCamera::moveBackward(float distance)
{
    m_position -= forward() * distance;
}

void PhysicalCamera::moveLeft(float distance)
{
    m_position -= right() * distance;
}

void PhysicalCamera::moveRight(float distance)
{
    m_position += right() * distance;
}

void PhysicalCamera::moveUp(float distance)
{
    m_position += up() * distance;
}

void PhysicalCamera::moveDown(float distance)
{
    m_position -= up() * distance;
}

void PhysicalCamera::yawPitch(float deltaYaw, float deltaPitch)
{
    addEulerDelta(deltaYaw, deltaPitch, 0.0f);
}

void PhysicalCamera::addEulerDelta(float deltaYaw, float deltaPitch, float deltaRoll)
{
    m_yawRad += deltaYaw;
    m_pitchRad = glm::clamp(m_pitchRad + deltaPitch, -kMaxPitchRad, kMaxPitchRad);
    m_rollRad += deltaRoll;
    rebuildOrientation();
}

void PhysicalCamera::orbitAroundPoint(const glm::vec3& pivot, float deltaYaw, float deltaPitch)
{
    glm::vec3 offset = m_position - pivot;
    const float radius = glm::length(offset);
    if (radius < SceneUnits::kMinRayEpsilonMm) {
        return;
    }

    const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);

    if (std::abs(deltaYaw) > 0.0f) {
        offset = glm::vec3(glm::rotate(glm::mat4(1.0f), deltaYaw, worldUp) * glm::vec4(offset, 1.0f));
    }

    if (std::abs(deltaPitch) > 0.0f) {
        glm::vec3 pitchAxis = glm::cross(worldUp, offset);
        const float pitchAxisLen = glm::length(pitchAxis);
        if (pitchAxisLen > 1.0e-6f) {
            pitchAxis /= pitchAxisLen;
            offset = glm::vec3(glm::rotate(glm::mat4(1.0f), deltaPitch, pitchAxis) * glm::vec4(offset, 1.0f));
        }
    }

    const glm::vec3 dirFromPivot = glm::normalize(offset);
    const float maxElevation = std::sin(kMaxPitchRad);
    if (std::abs(dirFromPivot.y) > maxElevation) {
        const glm::vec3 horizontal(dirFromPivot.x, 0.0f, dirFromPivot.z);
        const float horizontalLen = glm::length(horizontal);
        if (horizontalLen > 1.0e-6f) {
            const float clampedY = glm::clamp(dirFromPivot.y, -maxElevation, maxElevation);
            const float horizontalScale = std::sqrt(std::max(0.0f, 1.0f - clampedY * clampedY));
            const glm::vec3 clampedDir = glm::normalize(glm::vec3(
                horizontal.x / horizontalLen * horizontalScale,
                clampedY,
                horizontal.z / horizontalLen * horizontalScale));
            offset = clampedDir * radius;
        }
    }

    m_position = pivot + offset;

    const glm::vec3 lookDirection = glm::normalize(pivot - m_position);
    syncEulerFromDirection(lookDirection);

    m_focusPoint = pivot;
    refreshFocusDistanceFromPoint();
}

void PhysicalCamera::translateLocal(float rightAmount, float upAmount, float forwardAmount)
{
    m_position += right() * rightAmount + up() * upAmount + forward() * forwardAmount;
}

void PhysicalCamera::setPosition(const glm::vec3& position)
{
    m_position = position;
}

void PhysicalCamera::setPosition(float x, float y, float z)
{
    m_position = glm::vec3(x, y, z);
}

void PhysicalCamera::setEulerAngles(float yawRad, float pitchRad, float rollRad)
{
    m_yawRad = yawRad;
    m_pitchRad = glm::clamp(pitchRad, -kMaxPitchRad, kMaxPitchRad);
    m_rollRad = rollRad;
    rebuildOrientation();
}

void PhysicalCamera::setAspect(int imageW, int imageH)
{
    if (imageW > 0 && imageH > 0) {
        m_aspect = static_cast<float>(imageW) / static_cast<float>(imageH);
    }
}

void PhysicalCamera::setFocusPoint(const glm::vec3& point)
{
    m_focusPoint = point;
    const glm::vec3 delta = point - m_position;
    const float distance = glm::dot(delta, forward());
    m_focusDistance = distance > 0.0f ? distance : 0.0f;
    m_focusValid = m_focusDistance > 0.0f;
}

void PhysicalCamera::setFocusDistance(float distance)
{
    m_focusDistance = distance > 0.0f ? distance : 0.0f;
    m_focusValid = m_focusDistance > 0.0f;
    if (m_focusValid) {
        m_focusPoint = m_position + forward() * m_focusDistance;
    }
}

void PhysicalCamera::clearFocusPoint()
{
    m_focusValid = false;
}

void PhysicalCamera::setDefaultFocusPoint()
{
    m_focusDistance = kDefaultFocusDistance;
    m_focusPoint = m_position + forward() * m_focusDistance;
    m_focusValid = true;
}

void PhysicalCamera::refreshFocusDistanceFromPoint()
{
    if (!m_focusValid) {
        return;
    }

    const glm::vec3 delta = m_focusPoint - m_position;
    const float distance = glm::dot(delta, forward());
    m_focusDistance = distance > 0.0f ? distance : 0.0f;
    m_focusValid = m_focusDistance > 0.0f;
}

float PhysicalCamera::computeFocusDistance() const
{
    if (!m_focusValid) {
        return 0.0f;
    }

    return m_focusDistance;
}

float PhysicalCamera::clampFocalLengthMm(float value)
{
    return std::max(kMinFocalLengthMm, std::min(value, kMaxFocalLengthMm));
}

float PhysicalCamera::focalLengthMmToFovY(float focalLengthMm)
{
    const float clamped = clampFocalLengthMm(focalLengthMm);
    return 2.0f * std::atan(kSensorHeightMm / (2.0f * clamped));
}

float PhysicalCamera::fovYToFocalLengthMm(float fovY)
{
    const float halfFov = fovY * 0.5f;
    const float tanHalf = std::tan(halfFov);
    if (tanHalf <= 0.0f) {
        return kMaxFocalLengthMm;
    }
    return clampFocalLengthMm(kSensorHeightMm / (2.0f * tanHalf));
}

void PhysicalCamera::updateOpticsFromFocalLengthMm()
{
    m_fovY = focalLengthMmToFovY(m_focalLengthMm);
    m_near = SceneUnits::millimetersToUnits(m_focalLengthMm);
}

void PhysicalCamera::setFocalLengthMm(float focalLengthMm)
{
    m_focalLengthMm = clampFocalLengthMm(focalLengthMm);
    updateOpticsFromFocalLengthMm();
}

float PhysicalCamera::apertureRadius() const
{
    if (!m_focusValid || fStop <= 0.0f) {
        return 0.0f;
    }
    const float pupilRadiusMm = m_focalLengthMm / (2.0f * fStop);
    return SceneUnits::millimetersToUnits(pupilRadiusMm);
}

void PhysicalCamera::primaryRay(float u, float v, glm::vec3& ro, glm::vec3& rd) const
{
    const float ndcX = 2.0f * u - 1.0f;
    const float ndcY = 1.0f - 2.0f * v;

    const float tanHalf = glm::tan(m_fovY * 0.5f);
    const float top = m_near * tanHalf;
    const float rightExtent = top * m_aspect;

    const glm::vec3 viewDir = glm::mat3_cast(m_orientation) * glm::vec3(ndcX * rightExtent, ndcY * top, -m_near);
    ro = m_position;
    rd = glm::normalize(viewDir);
}

CameraGpu PhysicalCamera::toGpu() const
{
    CameraGpu gpu{};
    gpu.position = float3{m_position.x, m_position.y, m_position.z};
    gpu.orientation = float4{m_orientation.w, m_orientation.x, m_orientation.y, m_orientation.z};
    gpu.fovY = m_fovY;
    gpu.aspect = m_aspect;
    gpu.nearPlane = m_near;
    gpu.farPlane = m_far;
    gpu.apertureRadius = apertureRadius();
    gpu.focusDistance = computeFocusDistance();
    const glm::vec3 derivedFocusPoint = m_position + forward() * m_focusDistance;
    gpu.focusPoint = float3{derivedFocusPoint.x, derivedFocusPoint.y, derivedFocusPoint.z};
    gpu.focusValid = m_focusValid ? 1 : 0;
    return gpu;
}

void PhysicalCamera::copyGeometryFrom(const PhysicalCamera& other)
{
    m_position = other.m_position;
    m_orientation = other.m_orientation;
    m_fovY = other.m_fovY;
    m_near = other.m_near;
    m_far = other.m_far;
    m_aspect = other.m_aspect;
    m_yawRad = other.m_yawRad;
    m_pitchRad = other.m_pitchRad;
    m_rollRad = other.m_rollRad;
    m_focalLengthMm = other.m_focalLengthMm;
    m_focusPoint = other.m_focusPoint;
    m_focusDistance = other.m_focusDistance;
    m_focusValid = other.m_focusValid;
}

void PhysicalCamera::applyGpuGeometry(const CameraGpu& camera)
{
    m_position = glm::vec3(camera.position.x, camera.position.y, camera.position.z);
    m_orientation = glm::quat(
        camera.orientation.x,
        camera.orientation.y,
        camera.orientation.z,
        camera.orientation.w);
    m_fovY = camera.fovY;
    m_aspect = camera.aspect;
    m_near = camera.nearPlane;
    m_far = camera.farPlane;
    m_focalLengthMm = fovYToFocalLengthMm(camera.fovY);
    m_focusPoint = glm::vec3(camera.focusPoint.x, camera.focusPoint.y, camera.focusPoint.z);
    m_focusDistance = camera.focusDistance;
    m_focusValid = camera.focusValid != 0;
}

glm::mat4 PhysicalCamera::viewMatrixFromGpu(const CameraGpu& camera)
{
    const glm::quat orientation(
        camera.orientation.x,
        camera.orientation.y,
        camera.orientation.z,
        camera.orientation.w);
    const glm::vec3 position(camera.position.x, camera.position.y, camera.position.z);
    const glm::vec3 forwardDir = orientation * glm::vec3(0.0f, 0.0f, -1.0f);
    const glm::vec3 upDir = orientation * glm::vec3(0.0f, 1.0f, 0.0f);
    return glm::lookAt(position, position + forwardDir, upDir);
}

glm::mat4 PhysicalCamera::projMatrixFromGpu(const CameraGpu& camera, int imageW, int imageH)
{
    const float aspect = imageW > 0 && imageH > 0
        ? static_cast<float>(imageW) / static_cast<float>(imageH)
        : camera.aspect;
    return glm::perspective(camera.fovY, aspect, camera.nearPlane, camera.farPlane);
}

CameraGpu PhysicalCamera::defaultGpu()
{
    return PhysicalCamera{}.toGpu();
}

float PhysicalCamera::exposureMultiplier() const
{
    return exposureMultiplier(fStop, shutterSpeedSeconds, iso);
}

float PhysicalCamera::exposureMultiplier(float fStopValue, float shutterSec, float isoValue)
{
    const float clampedFStop = fStopValue < 0.1f ? 0.1f : fStopValue;
    const float clampedIso = isoValue < 1.0f ? 1.0f : isoValue;
    return shutterSec * (clampedIso / 100.0f) / (clampedFStop * clampedFStop);
}

float PhysicalCamera::exposureValue(float fStopValue, float shutterSec, float isoValue)
{
    const float clampedFStop = fStopValue < 0.1f ? 0.1f : fStopValue;
    const float clampedShutter = shutterSec < 1.0e-8f ? 1.0e-8f : shutterSec;
    const float clampedIso = isoValue < 1.0f ? 1.0f : isoValue;
    return std::log2((clampedFStop * clampedFStop) / clampedShutter) - std::log2(clampedIso / 100.0f);
}

float PhysicalCamera::clampFStop(float value)
{
    return std::max(kMinFStop, std::min(value, kMaxFStop));
}

float PhysicalCamera::clampShutterSpeedSeconds(float value)
{
    return std::max(kMinShutterSpeedSeconds, std::min(value, kMaxShutterSpeedSeconds));
}

float PhysicalCamera::clampIso(float value)
{
    return std::max(kMinIso, std::min(value, kMaxIso));
}

float PhysicalCamera::snapIsoToNearestPreset(float iso)
{
    const float clamped = clampIso(iso);
    float bestIso = kIsoPresets[0].iso;
    float bestDelta = std::abs(clamped - bestIso);
    for (const IsoPreset& preset : kIsoPresets) {
        const float delta = std::abs(clamped - preset.iso);
        if (delta < bestDelta) {
            bestDelta = delta;
            bestIso = preset.iso;
        }
    }
    return bestIso;
}

float PhysicalCamera::reinhardDisplay(float linearRadiance, float exposureMultiplier)
{
    const float exposed = linearRadiance * exposureMultiplier;
    return exposed / (1.0f + exposed);
}

PhysicalCamera PhysicalCamera::forAverageLuminance(float averageLuminance)
{
    PhysicalCamera camera{};
    camera.fStop = kDefaultFStop;
    camera.iso = kDefaultIso;
    camera.shutterSpeedSeconds = kDefaultShutterSpeedSeconds;

    const float safeY = averageLuminance > 1.0e-6f ? averageLuminance : 1.0e-6f;
    const float targetLinear = kDisplayMiddleGray / (1.0f - kDisplayMiddleGray);
    const float targetExposureMult = targetLinear / safeY;

    float shutter = targetExposureMult * camera.fStop * camera.fStop / (camera.iso / 100.0f);

    if (shutter > kMaxShutterSpeedSeconds) {
        camera.shutterSpeedSeconds = kMaxShutterSpeedSeconds;
        float actualMult = exposureMultiplier(camera.fStop, camera.shutterSpeedSeconds, camera.iso);
        if (actualMult < targetExposureMult) {
            camera.fStop = std::sqrt(
                camera.shutterSpeedSeconds * (camera.iso / 100.0f) / targetExposureMult);
            camera.fStop = clampFStop(camera.fStop);
            actualMult = exposureMultiplier(camera.fStop, camera.shutterSpeedSeconds, camera.iso);
            if (actualMult < targetExposureMult) {
                camera.iso = targetExposureMult * camera.fStop * camera.fStop * 100.0f /
                    camera.shutterSpeedSeconds;
                camera.iso = clampIso(camera.iso);
            }
        }
    } else if (shutter < kMinShutterSpeedSeconds) {
        camera.shutterSpeedSeconds = kMinShutterSpeedSeconds;
        float actualMult = exposureMultiplier(camera.fStop, camera.shutterSpeedSeconds, camera.iso);
        if (actualMult > targetExposureMult) {
            camera.fStop = std::sqrt(
                camera.shutterSpeedSeconds * (camera.iso / 100.0f) / targetExposureMult);
            camera.fStop = clampFStop(camera.fStop);
            actualMult = exposureMultiplier(camera.fStop, camera.shutterSpeedSeconds, camera.iso);
            if (actualMult > targetExposureMult) {
                camera.iso = targetExposureMult * camera.fStop * camera.fStop * 100.0f /
                    camera.shutterSpeedSeconds;
                camera.iso = clampIso(camera.iso);
            }
        }
    } else {
        camera.shutterSpeedSeconds = shutter;
    }

    return camera;
}

std::size_t PhysicalCamera::shutterSpeedPresetCount()
{
    return sizeof(kShutterSpeedPresets) / sizeof(kShutterSpeedPresets[0]);
}

const ShutterSpeedPreset* PhysicalCamera::shutterSpeedPresets()
{
    return kShutterSpeedPresets;
}

std::size_t PhysicalCamera::isoPresetCount()
{
    return sizeof(kIsoPresets) / sizeof(kIsoPresets[0]);
}

const IsoPreset* PhysicalCamera::isoPresets()
{
    return kIsoPresets;
}

void PhysicalCamera::rebuildOrientation()
{
    const glm::quat yawQuat = glm::angleAxis(m_yawRad, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::quat pitchQuat = glm::angleAxis(m_pitchRad, glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::quat rollQuat = glm::angleAxis(m_rollRad, glm::vec3(0.0f, 0.0f, 1.0f));
    m_orientation = glm::normalize(yawQuat * pitchQuat * rollQuat);
}

void PhysicalCamera::syncEulerFromDirection(const glm::vec3& direction)
{
    const glm::vec3 fwd = glm::normalize(direction);
    m_rollRad = 0.0f;
    m_pitchRad = glm::clamp(std::asin(glm::clamp(fwd.y, -1.0f, 1.0f)), -kMaxPitchRad, kMaxPitchRad);
    m_yawRad = std::atan2(-fwd.x, -fwd.z);
    rebuildOrientation();
}
