#pragma once

#include "AppSettings.h"
#include "PhysicalCamera.h"
#include "SceneUnits.h"

#include <glm/glm.hpp>

#include <cmath>

// Fixed-rate camera movement: displacement = input * speed * dt (mm and radians).
class CameraDynamicsController
{
public:
    static constexpr float kPositionEpsilon = 0.5f;
    static constexpr float kAngleEpsilon = 1.0e-3f;

    void applySettings(const CameraDynamicsSettings& settings)
    {
        m_linearSpeedMmPerSec = settings.linearSpeedMmPerSec;
        m_angularSpeedRadPerSec = settings.angularSpeedRadPerSec;
        m_defaultPositionMm = glm::vec3(
            settings.defaultPositionXmm,
            settings.defaultPositionYmm,
            settings.defaultPositionZmm);
        m_defaultOrientationDeg = glm::vec3(
            settings.defaultYawDeg,
            settings.defaultPitchDeg,
            settings.defaultRollDeg);
    }

    glm::vec3 defaultPositionMm() const { return m_defaultPositionMm; }
    glm::vec3 defaultOrientationDeg() const { return m_defaultOrientationDeg; }

    void setLinearInput(const glm::vec3& unitInput) { m_linearInput = unitInput; }
    void setAngularInput(const glm::vec3& unitInput) { m_angularInput = unitInput; }
    void setSpeedScale(float scale) { m_speedScale = scale; }

    void reset()
    {
        m_linearInput = glm::vec3(0.0f);
        m_angularInput = glm::vec3(0.0f);
        m_speedScale = 1.0f;
    }

    bool step(PhysicalCamera& camera, float fixedDt)
    {
        if (fixedDt <= 0.0f) {
            return false;
        }

        const glm::vec3 prevPosition = camera.position();

        const float linearSpeed = m_linearSpeedMmPerSec * m_speedScale;
        const float angularSpeed = m_angularSpeedRadPerSec * m_speedScale;

        camera.translateLocal(
            m_linearInput.x * linearSpeed * fixedDt,
            m_linearInput.y * linearSpeed * fixedDt,
            m_linearInput.z * linearSpeed * fixedDt);
        camera.addEulerDelta(
            m_angularInput.y * angularSpeed * fixedDt,
            m_angularInput.x * angularSpeed * fixedDt,
            m_angularInput.z * angularSpeed * fixedDt);

        const bool inputActive =
            glm::length(m_linearInput) > 0.0f || glm::length(m_angularInput) > 0.0f;
        const bool displaced = glm::length(camera.position() - prevPosition) > kPositionEpsilon;
        const bool rotated =
            std::abs(m_angularInput.x) * angularSpeed * fixedDt > kAngleEpsilon
            || std::abs(m_angularInput.y) * angularSpeed * fixedDt > kAngleEpsilon
            || std::abs(m_angularInput.z) * angularSpeed * fixedDt > kAngleEpsilon;

        return inputActive || displaced || rotated;
    }

private:
    float m_linearSpeedMmPerSec = SceneUnits::kDefaultLinearSpeedMmPerSec;
    float m_angularSpeedRadPerSec = SceneUnits::kDefaultAngularSpeedRadPerSec;
    glm::vec3 m_defaultPositionMm{0.0f};
    glm::vec3 m_defaultOrientationDeg{0.0f};
    glm::vec3 m_linearInput{0.0f};
    glm::vec3 m_angularInput{0.0f};
    float m_speedScale = 1.0f;
};
