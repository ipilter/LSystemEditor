#pragma once

#include "AppSettings.h"
#include "PhysicalCamera.h"

#include <glm/glm.hpp>

#include <cmath>

class CameraDynamicsController
{
public:
    static constexpr float kMass = 1.0f;
    static constexpr float kInertia = 1.0f;
    static constexpr float kVelocityEpsilon = 1.0e-5f;
    static constexpr float kPositionEpsilon = 1.0e-6f;
    static constexpr float kAngleEpsilon = 1.0e-7f;

    void applySettings(const CameraDynamicsSettings& settings)
    {
        m_thrustLinear = settings.thrustLinear;
        m_dragLinear = settings.dragLinear;
        m_thrustAngular = settings.thrustAngular;
        m_dragAngular = settings.dragAngular;
    }

    void setLinearInput(const glm::vec3& unitInput) { m_linearInput = unitInput; }
    void setAngularInput(const glm::vec3& unitInput) { m_angularInput = unitInput; }
    void setThrustScale(float scale) { m_thrustScale = scale; }

    void reset()
    {
        m_linearVelocity = glm::vec3(0.0f);
        m_angularVelocity = glm::vec3(0.0f);
        m_linearInput = glm::vec3(0.0f);
        m_angularInput = glm::vec3(0.0f);
        m_thrustScale = 1.0f;
    }

    bool step(PhysicalCamera& camera, float dt)
    {
        if (dt <= 0.0f) {
            return false;
        }

        const glm::vec3 prevPosition = camera.position();

        const float thrustLinear = m_thrustLinear * m_thrustScale;
        const float thrustAngular = m_thrustAngular * m_thrustScale;

        const glm::vec3 linearAccel =
            (m_linearInput * thrustLinear - m_linearVelocity * m_dragLinear) / kMass;
        const glm::vec3 angularAccel =
            (m_angularInput * thrustAngular - m_angularVelocity * m_dragAngular) / kInertia;

        m_linearVelocity += linearAccel * dt;
        m_angularVelocity += angularAccel * dt;

        camera.translateLocal(
            m_linearVelocity.x * dt,
            m_linearVelocity.y * dt,
            m_linearVelocity.z * dt);
        camera.addEulerDelta(
            m_angularVelocity.y * dt,
            m_angularVelocity.x * dt,
            m_angularVelocity.z * dt);

        const bool moving =
            glm::length(m_linearVelocity) > kVelocityEpsilon
            || glm::length(m_angularVelocity) > kVelocityEpsilon;
        const bool inputActive =
            glm::length(m_linearInput) > 0.0f || glm::length(m_angularInput) > 0.0f;
        const bool displaced = glm::length(camera.position() - prevPosition) > kPositionEpsilon;
        const bool rotated =
            std::abs(m_angularVelocity.x) * dt > kAngleEpsilon
            || std::abs(m_angularVelocity.y) * dt > kAngleEpsilon
            || std::abs(m_angularVelocity.z) * dt > kAngleEpsilon;

        return moving || inputActive || displaced || rotated;
    }

private:
    float m_thrustLinear = 2.0f;
    float m_dragLinear = 4.0f;
    float m_thrustAngular = 2.0f;
    float m_dragAngular = 5.0f;
    glm::vec3 m_linearVelocity{0.0f};
    glm::vec3 m_angularVelocity{0.0f};
    glm::vec3 m_linearInput{0.0f};
    glm::vec3 m_angularInput{0.0f};
    float m_thrustScale = 1.0f;
};
