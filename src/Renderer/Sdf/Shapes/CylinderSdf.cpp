#include "CylinderSdf.h"

#include "Sdf/SdfPrimitiveType.h"
#include "SdfAccel/SdfAccelBoundsCore.h"
#include "SdfMathCore.h"
#include "SdfPrimitivesCore.h"

CylinderSdf::CylinderSdf(SdfFloat3 center, SdfFloat2 halfExtents)
    : m_center(center)
    , m_halfExtents(halfExtents)
{
}

SdfFloat3 CylinderSdf::center() const
{
    return m_center;
}

void CylinderSdf::setCenter(SdfFloat3 center)
{
    m_center = center;
}

float CylinderSdf::evalLocal(SdfFloat3 localP) const
{
    return sdCylinder(localP, m_halfExtents);
}

SdfAccelField CylinderSdf::buildAccelField() const
{
    SdfAccelField field{};
    field.worldCenter = m_center;
    field.payload.type = static_cast<uint32_t>(SdfPrimitiveType::Cylinder);
    field.payload.halfExtents = m_halfExtents;
    field.payload.param0 = m_halfExtents.x;
    field.payload.param1 = m_halfExtents.y;
    const SdfAccelAabb bounds = sdfAccelCylinderBounds(m_center, m_halfExtents);
    field.localBoundsMin = bounds.min;
    field.localBoundsMax = bounds.max;
    field.evalLocal = [center = m_center, halfExtents = m_halfExtents](SdfFloat3 worldP) {
        return sdCylinder(sdfSub3(worldP, center), halfExtents);
    };
    return field;
}

std::unique_ptr<SdfShape> CylinderSdf::clone() const
{
    return std::make_unique<CylinderSdf>(m_center, m_halfExtents);
}
