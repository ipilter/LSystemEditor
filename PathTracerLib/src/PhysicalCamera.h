#pragma once

#include "CameraGpu.h"
#include "SceneUnits.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>

struct CameraFrustum
{
    float left = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    float top = 0.0f;
    float nearPlane = 0.0f;
    float farPlane = 0.0f;
};

struct ShutterSpeedPreset
{
    const char* label;
    float seconds;
};

struct IsoPreset
{
    const char* label;
    float iso;
};

class PhysicalCamera
{
public:
    static constexpr float kDefaultFStop = 2.8f;
    static constexpr float kDefaultShutterSpeedSeconds = 1.0f / 125.0f;
    static constexpr float kDefaultIso = 100.0f;
    static constexpr float kMinIso = 100.0f;
    static constexpr float kMaxIso = 25600.0f;
    static constexpr float kMinFStop = 1.2f;
    static constexpr float kMaxFStop = 22.0f;
    static constexpr float kMinShutterSpeedSeconds = 1.0f / 8000.0f;
    static constexpr float kMaxShutterSpeedSeconds = 120.0f;
    static constexpr float kDisplayMiddleGray = 0.18f;
    static constexpr float kDefaultFarPlane = SceneUnits::kDefaultRayTMaxMm;
    static constexpr float kDefaultFocusDistance = 1000.0f;
    static constexpr float kSensorHeightMm = 23.9f;
    static constexpr float kMinFocalLengthMm = 14.0f;
    static constexpr float kMaxFocalLengthMm = 1000.0f;
    static constexpr float kDefaultFocalLengthMm = 24.0f;

    PhysicalCamera();

    glm::vec3 forward() const;
    glm::vec3 right() const;
    glm::vec3 up() const;

    CameraFrustum frustum(int imageW, int imageH) const;
    glm::mat4 viewMatrix() const;
    glm::mat4 projMatrix(int imageW, int imageH) const;

    void moveForward(float distance);
    void moveBackward(float distance);
    void moveLeft(float distance);
    void moveRight(float distance);
    void moveUp(float distance);
    void moveDown(float distance);
    void yawPitch(float deltaYaw, float deltaPitch);
    void addEulerDelta(float deltaYaw, float deltaPitch, float deltaRoll);
    void translateLocal(float rightAmount, float upAmount, float forwardAmount);

    glm::vec3 position() const { return m_position; }

    glm::vec3 focusPoint() const { return m_focusPoint; }
    bool focusValid() const { return m_focusValid; }
    float focusDistance() const { return m_focusDistance; }
    float focalLengthMm() const { return m_focalLengthMm; }

    void setFocusPoint(const glm::vec3& point);
    void setFocusDistance(float distance);
    void clearFocusPoint();
    void setDefaultFocusPoint();
    void refreshFocusDistanceFromPoint();

    float computeFocusDistance() const;
    float apertureRadius() const;

    void primaryRay(float u, float v, glm::vec3& ro, glm::vec3& rd) const;

    void setAspect(int imageW, int imageH);
    void setFocalLengthMm(float focalLengthMm);
    CameraGpu toGpu() const;
    void copyGeometryFrom(const PhysicalCamera& other);
    void applyGpuGeometry(const CameraGpu& camera);

    static glm::mat4 viewMatrixFromGpu(const CameraGpu& camera);
    static glm::mat4 projMatrixFromGpu(const CameraGpu& camera, int imageW, int imageH);
    static CameraGpu defaultGpu();

    static float clampFocalLengthMm(float value);
    static float focalLengthMmToFovY(float focalLengthMm);
    static float fovYToFocalLengthMm(float fovY);

    float exposureMultiplier() const;
    static float exposureMultiplier(float fStop, float shutterSec, float iso);
    static float exposureValue(float fStop, float shutterSec, float iso);
    static float clampFStop(float value);
    static float clampShutterSpeedSeconds(float value);
    static float clampIso(float value);
    static float snapIsoToNearestPreset(float iso);
    static PhysicalCamera forAverageLuminance(float averageLuminance);
    static float reinhardDisplay(float linearRadiance, float exposureMultiplier);
    static std::size_t shutterSpeedPresetCount();
    static const ShutterSpeedPreset* shutterSpeedPresets();
    static std::size_t isoPresetCount();
    static const IsoPreset* isoPresets();

    float fStop = kDefaultFStop;
    float shutterSpeedSeconds = kDefaultShutterSpeedSeconds;
    float iso = kDefaultIso;

private:
    void rebuildOrientation();
    void updateOpticsFromFocalLengthMm();

    glm::vec3 m_position;
    glm::quat m_orientation;
    float m_fovY;
    float m_near;
    float m_far;
    float m_aspect;
    float m_yawRad;
    float m_pitchRad;
    float m_rollRad;
    float m_focalLengthMm = kDefaultFocalLengthMm;
    glm::vec3 m_focusPoint{};
    float m_focusDistance = kDefaultFocusDistance;
    bool m_focusValid = false;
};
