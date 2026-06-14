#pragma once

#include "CameraGpu.h"

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
    static constexpr float kDefaultFovYRad = 1.04719755f;
    static constexpr float kDefaultNearPlane = 0.1f;
    static constexpr float kDefaultFarPlane = 1000.0f;

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

    void setAspect(int imageW, int imageH);
    CameraGpu toGpu() const;
    void copyGeometryFrom(const PhysicalCamera& other);
    void applyGpuGeometry(const CameraGpu& camera);

    static glm::mat4 viewMatrixFromGpu(const CameraGpu& camera);
    static glm::mat4 projMatrixFromGpu(const CameraGpu& camera, int imageW, int imageH);
    static CameraGpu defaultGpu();

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

    glm::vec3 m_position;
    glm::quat m_orientation;
    float m_fovY;
    float m_near;
    float m_far;
    float m_aspect;
    float m_yawRad;
    float m_pitchRad;
};
