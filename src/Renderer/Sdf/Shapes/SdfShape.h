#pragma once

#include "SdfAccel/SdfAccelField.h"
#include "Sdf/SdfTypes.h"

#include <memory>

class SdfShape
{
public:
    virtual ~SdfShape() = default;

    virtual SdfFloat3 center() const = 0;
    virtual void setCenter(SdfFloat3 center) = 0;
    virtual float evalLocal(SdfFloat3 localP) const = 0;
    virtual float evalWorld(SdfFloat3 worldP) const;
    virtual SdfAccelField buildAccelField() const = 0;
    virtual std::unique_ptr<SdfShape> clone() const = 0;
};
