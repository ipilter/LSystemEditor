#pragma once

#include "SdfShape.h"

class SphereSdf : public SdfShape
{
public:
    SphereSdf(SdfFloat3 center, float radius);

    SdfFloat3 center() const override;
    void setCenter(SdfFloat3 center) override;
    float evalLocal(SdfFloat3 localP) const override;
    SdfAccelField buildAccelField() const override;
    std::unique_ptr<SdfShape> clone() const override;

    float radius() const { return m_radius; }
    void setRadius(float radius) { m_radius = radius; }

private:
    SdfFloat3 m_center{};
    float m_radius = 0.0f;
};
