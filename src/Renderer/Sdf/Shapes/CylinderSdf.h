#pragma once

#include "SdfShape.h"

class CylinderSdf : public SdfShape
{
public:
    CylinderSdf(SdfFloat3 center, SdfFloat2 halfExtents);

    SdfFloat3 center() const override;
    void setCenter(SdfFloat3 center) override;
    float evalLocal(SdfFloat3 localP) const override;
    SdfAccelField buildAccelField() const override;
    std::unique_ptr<SdfShape> clone() const override;

    SdfFloat2 halfExtents() const { return m_halfExtents; }
    void setHalfExtents(SdfFloat2 halfExtents) { m_halfExtents = halfExtents; }

private:
    SdfFloat3 m_center{};
    SdfFloat2 m_halfExtents{};
};
