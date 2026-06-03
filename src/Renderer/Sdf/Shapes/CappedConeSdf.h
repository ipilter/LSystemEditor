#pragma once

#include "SdfShape.h"

class CappedConeSdf : public SdfShape
{
public:
    CappedConeSdf(SdfFloat3 center, float halfHeight, float radiusBottom, float radiusTop);

    SdfFloat3 center() const override;
    void setCenter(SdfFloat3 center) override;
    float evalLocal(SdfFloat3 localP) const override;
    SdfAccelField buildAccelField() const override;
    std::unique_ptr<SdfShape> clone() const override;

    float halfHeight() const { return m_halfHeight; }
    float radiusBottom() const { return m_radiusBottom; }
    float radiusTop() const { return m_radiusTop; }

private:
    SdfFloat3 m_center{};
    float m_halfHeight = 0.0f;
    float m_radiusBottom = 0.0f;
    float m_radiusTop = 0.0f;
};
