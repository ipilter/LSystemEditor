#pragma once

#include "SdfAccelTypes.h"
#include "SdfAccelBoundsCore.h"

#include <functional>

struct SdfAccelField
{
    SdfFloat3 worldCenter{};
    SdfAccelPayloadGpu payload{};
    SdfFloat3 localBoundsMin{};
    SdfFloat3 localBoundsMax{};
    std::function<float(SdfFloat3)> evalLocal;
};

inline SdfAccelAabb sdfAccelFieldLocalAabb(const SdfAccelField& field, float padding)
{
    return sdfAccelExpandAabb(sdfAccelMakeAabb(field.localBoundsMin, field.localBoundsMax), padding);
}
