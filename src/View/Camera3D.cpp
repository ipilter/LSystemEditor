#include "Camera3D.h"

#include <glm/gtc/constants.hpp>

#include <vector_types.h>

namespace {

constexpr float kDefaultNear = 0.1f;
constexpr float kDefaultFar = 1000.0f;
constexpr float kMaxPitchRad = glm::radians(89.0f);

} // namespace

Camera3D::Camera3D()
    : m_position(0.0f, 0.5f, 4.0f)
    , m_orientation(1.0f, 0.0f, 0.0f, 0.0f)
    , m_fovY(glm::radians(60.0f))
    , m_near(kDefaultNear)
    , m_far(kDefaultFar)
    , m_aspect(1.0f)
    , m_yawRad(0.0f)
    , m_pitchRad(0.0f)
{
    rebuildOrientation();
}

glm::vec3 Camera3D::forward() const
{
    return glm::mat3_cast(m_orientation) * glm::vec3(0.0f, 0.0f, -1.0f);
}

glm::vec3 Camera3D::right() const
{
    return glm::mat3_cast(m_orientation) * glm::vec3(1.0f, 0.0f, 0.0f);
}

glm::vec3 Camera3D::up() const
{
    return glm::mat3_cast(m_orientation) * glm::vec3(0.0f, 1.0f, 0.0f);
}

CameraFrustum Camera3D::frustum(int imageW, int imageH) const
{
    const float aspect = imageW > 0 && imageH > 0 ? static_cast<float>(imageW) / static_cast<float>(imageH)
                                                    : m_aspect;
    const float tanHalf = glm::tan(m_fovY * 0.5f);
    const float top = m_near * tanHalf;
    const float right = top * aspect;

    return CameraFrustum{
        -right,
        right,
        -top,
        top,
        m_near,
        m_far,
    };
}

glm::mat4 Camera3D::viewMatrix() const
{
    return glm::lookAt(m_position, m_position + forward(), up());
}

glm::mat4 Camera3D::projMatrix(int imageW, int imageH) const
{
    const float aspect = imageW > 0 && imageH > 0 ? static_cast<float>(imageW) / static_cast<float>(imageH)
                                                    : m_aspect;
    return glm::perspective(m_fovY, aspect, m_near, m_far);
}

void Camera3D::moveForward(float distance)
{
    m_position += forward() * distance;
}

void Camera3D::moveBackward(float distance)
{
    m_position -= forward() * distance;
}

void Camera3D::moveLeft(float distance)
{
    m_position -= right() * distance;
}

void Camera3D::moveRight(float distance)
{
    m_position += right() * distance;
}

void Camera3D::moveUp(float distance)
{
    m_position += up() * distance;
}

void Camera3D::moveDown(float distance)
{
    m_position -= up() * distance;
}

void Camera3D::yawPitch(float deltaYaw, float deltaPitch)
{
    m_yawRad += deltaYaw;
    m_pitchRad = glm::clamp(m_pitchRad + deltaPitch, -kMaxPitchRad, kMaxPitchRad);
    rebuildOrientation();
}

void Camera3D::setAspect(int imageW, int imageH)
{
    if (imageW > 0 && imageH > 0) {
        m_aspect = static_cast<float>(imageW) / static_cast<float>(imageH);
    }
}

CameraGpu Camera3D::toGpu() const
{
    CameraGpu gpu{};
    gpu.position = float3{m_position.x, m_position.y, m_position.z};
    gpu.orientation = float4{m_orientation.w, m_orientation.x, m_orientation.y, m_orientation.z};
    gpu.fovY = m_fovY;
    gpu.aspect = m_aspect;
    gpu.nearPlane = m_near;
    gpu.farPlane = m_far;
    return gpu;
}

void Camera3D::rebuildOrientation()
{
    const glm::quat yawQuat = glm::angleAxis(m_yawRad, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::quat pitchQuat = glm::angleAxis(m_pitchRad, glm::vec3(1.0f, 0.0f, 0.0f));
    m_orientation = glm::normalize(yawQuat * pitchQuat);
}
