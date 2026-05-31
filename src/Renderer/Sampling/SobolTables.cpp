#include "SobolTables.h"

#include <cstdint>
#include <cstring>

#include "SobolDirectionData.h"

namespace {

constexpr int kNloptMaxDim = MAXDIM;

void fillDimension0(uint32_t* out, int maxDimensions)
{
    for (int bit = 0; bit < kSobolBits; ++bit) {
        out[static_cast<std::size_t>(bit) * static_cast<std::size_t>(maxDimensions)] =
            1u << (31 - bit);
    }
}

void fillDimension(uint32_t* out, int maxDimensions, int dim)
{
    if (dim <= 0 || dim >= maxDimensions || dim >= kNloptMaxDim) {
        return;
    }

    const int dimIndex = dim - 1;
    uint32_t polynomial = sobol_a[dimIndex];
    unsigned degree = 0;
    uint32_t poly = polynomial;
    while (poly != 0) {
        ++degree;
        poly >>= 1;
    }
    --degree;

    for (int bit = 0; bit < static_cast<int>(degree); ++bit) {
        out[static_cast<std::size_t>(bit) * static_cast<std::size_t>(maxDimensions) +
            static_cast<std::size_t>(dim)] = sobol_minit[bit][dimIndex];
    }

    for (int bit = static_cast<int>(degree); bit < kSobolBits; ++bit) {
        uint32_t coeff = sobol_a[dimIndex];
        uint32_t value = out[static_cast<std::size_t>(bit - static_cast<int>(degree)) *
                                  static_cast<std::size_t>(maxDimensions) +
                              static_cast<std::size_t>(dim)];

        for (unsigned k = 0; k < degree; ++k) {
            if (coeff & 1u) {
                value ^= out[(static_cast<std::size_t>(bit - static_cast<int>(degree) + k) *
                              static_cast<std::size_t>(maxDimensions)) +
                             static_cast<std::size_t>(dim)] << (degree - k);
            }
            coeff >>= 1;
        }

        out[static_cast<std::size_t>(bit) * static_cast<std::size_t>(maxDimensions) +
            static_cast<std::size_t>(dim)] = value;
    }
}

} // namespace

bool buildSobolMatricesHost(uint32_t* out, int maxDimensions)
{
    if (out == nullptr || maxDimensions <= 0 || maxDimensions > kMaxSobolDimensions) {
        return false;
    }

    const std::size_t count =
        static_cast<std::size_t>(maxDimensions) * static_cast<std::size_t>(kSobolBits);
    std::memset(out, 0, count * sizeof(uint32_t));

    fillDimension0(out, maxDimensions);

    const int limit = maxDimensions < kNloptMaxDim ? maxDimensions : kNloptMaxDim;
    for (int dim = 1; dim < limit; ++dim) {
        fillDimension(out, maxDimensions, dim);
    }

    return true;
}
