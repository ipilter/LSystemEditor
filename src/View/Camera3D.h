#pragma once

#include "CameraGpu.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

struct CameraFrustum
{
    float left = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    float top = 0.0f;
    float nearPlane = 0.0f;
    float farPlane = 0.0f;
};

class Camera3D
{
public:
    Camera3D();

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

    static glm::mat4 viewMatrixFromGpu(const CameraGpu& camera);
    static glm::mat4 projMatrixFromGpu(const CameraGpu& camera, int imageW, int imageH);

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
