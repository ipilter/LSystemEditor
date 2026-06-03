#include "SphereSdf.h"

#include "Sdf/SdfPrimitiveType.h"
#include "SdfAccel/SdfAccelBoundsCore.h"
#include "SdfMathCore.h"
#include "SdfPrimitivesCore.h"

SphereSdf::SphereSdf(SdfFloat3 center, float radius)
    : m_center(center)
    , m_radius(radius)
{
}

SdfFloat3 SphereSdf::center() const
{
    return m_center;
}

void SphereSdf::setCenter(SdfFloat3 center)
{
    m_center = center;
}

float SphereSdf::evalLocal(SdfFloat3 localP) const
{
    return sdSphere(localP, m_radius);
}

SdfAccelField SphereSdf::buildAccelField() const
{
    SdfAccelField field{};
    field.worldCenter = m_center;
    field.payload.type = static_cast<uint32_t>(SdfPrimitiveType::Sphere);
    field.payload.param0 = m_radius;
    field.localBoundsMin = sdfSub3(m_center, sdfMakeFloat3(m_radius, m_radius, m_radius));
    field.localBoundsMax = sdfAdd3(m_center, sdfMakeFloat3(m_radius, m_radius, m_radius));
    field.evalLocal = [center = m_center, radius = m_radius](SdfFloat3 worldP) {
        return sdSphere(sdfSub3(worldP, center), radius);
    };
    return field;
}

std::unique_ptr<SdfShape> SphereSdf::clone() const
{
    return std::make_unique<SphereSdf>(m_center, m_radius);
}
