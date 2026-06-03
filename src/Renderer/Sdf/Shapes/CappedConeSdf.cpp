#include "CappedConeSdf.h"

#include "Sdf/SdfPrimitiveType.h"
#include "SdfAccel/SdfAccelBoundsCore.h"
#include "SdfMathCore.h"
#include "SdfPrimitivesCore.h"

CappedConeSdf::CappedConeSdf(SdfFloat3 center, float halfHeight, float radiusBottom, float radiusTop)
    : m_center(center)
    , m_halfHeight(halfHeight)
    , m_radiusBottom(radiusBottom)
    , m_radiusTop(radiusTop)
{
}

SdfFloat3 CappedConeSdf::center() const
{
    return m_center;
}

void CappedConeSdf::setCenter(SdfFloat3 center)
{
    m_center = center;
}

float CappedConeSdf::evalLocal(SdfFloat3 localP) const
{
    return sdCappedCone(localP, m_halfHeight, m_radiusBottom, m_radiusTop);
}

SdfAccelField CappedConeSdf::buildAccelField() const
{
    SdfAccelField field{};
    field.worldCenter = m_center;
    field.payload.type = static_cast<uint32_t>(SdfPrimitiveType::CappedCone);
    field.payload.param0 = m_halfHeight;
    field.payload.param1 = m_radiusBottom;
    field.payload.param2 = m_radiusTop;
    const SdfAccelAabb bounds =
        sdfAccelCappedConeBounds(m_center, m_halfHeight, m_radiusBottom, m_radiusTop);
    field.localBoundsMin = bounds.min;
    field.localBoundsMax = bounds.max;
    field.evalLocal = [center = m_center, halfHeight = m_halfHeight, radiusBottom = m_radiusBottom, radiusTop = m_radiusTop](
                          SdfFloat3 worldP) {
        return sdCappedCone(sdfSub3(worldP, center), halfHeight, radiusBottom, radiusTop);
    };
    return field;
}

std::unique_ptr<SdfShape> CappedConeSdf::clone() const
{
    return std::make_unique<CappedConeSdf>(m_center, m_halfHeight, m_radiusBottom, m_radiusTop);
}
