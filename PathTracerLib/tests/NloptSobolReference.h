#pragma once

#include <cstdint>

#include <vector>

#include "SobolDirectionData.h"

namespace NloptSobolReference {

struct SobolData
{
    unsigned sdim = 0;
    std::vector<uint32_t> mdata;
    uint32_t* m[32]{};
    std::vector<uint32_t> x;
    std::vector<unsigned> b;
    uint32_t n = 0;
};

inline unsigned rightzero32(uint32_t value)
{
#if defined(__GNUC__) && ((__GNUC__ == 3 && __GNUC_MINOR__ >= 4) || __GNUC__ > 3)
    return static_cast<unsigned>(__builtin_ctz(~value));
#else
    const uint32_t a = 0x05f66a47u;
    static const unsigned decode[32] = {
        0, 1, 2, 26, 23, 3, 15, 27, 24, 21, 19, 4, 12, 16, 28, 6,
        31, 25, 22, 14, 20, 18, 11, 5, 30, 13, 17, 10, 29, 9, 8, 7};
    value = ~value;
    value = a * (value & (~value + 1u));
    return decode[value >> 27];
#endif
}

inline bool init(SobolData& sd, unsigned sdim)
{
    if (sdim == 0 || sdim > static_cast<unsigned>(MAXDIM)) {
        return false;
    }

    sd.sdim = sdim;
    sd.mdata.assign(static_cast<std::size_t>(sdim) * 32u, 0u);
    for (int j = 0; j < 32; ++j) {
        sd.m[j] = sd.mdata.data() + static_cast<std::size_t>(j) * sdim;
        sd.m[j][0] = 1u;
    }

    for (unsigned i = 1; i < sdim; ++i) {
        uint32_t a = sobol_a[i - 1];
        unsigned d = 0;
        while (a != 0) {
            ++d;
            a >>= 1;
        }
        --d;

        for (unsigned j = 0; j < d; ++j) {
            sd.m[j][i] = sobol_minit[j][i - 1];
        }

        for (unsigned j = d; j < 32u; ++j) {
            a = sobol_a[i - 1];
            sd.m[j][i] = sd.m[j - d][i];
            for (unsigned k = 0; k < d; ++k) {
                sd.m[j][i] ^= ((a & 1u) * sd.m[j - d + k][i]) << (d - k);
                a >>= 1;
            }
        }
    }

    sd.x.assign(sdim, 0u);
    sd.b.assign(sdim, 0u);
    sd.n = 0;
    return true;
}

inline bool next01(SobolData& sd, std::vector<double>& out)
{
    if (sd.n == 4294967295u) {
        return false;
    }

    const unsigned c = rightzero32(sd.n++);
    out.resize(sd.sdim);
    for (unsigned i = 0; i < sd.sdim; ++i) {
        unsigned bit = sd.b[i];
        if (bit >= c) {
            sd.x[i] ^= sd.m[c][i] << (bit - c);
            out[i] = static_cast<double>(sd.x[i]) / static_cast<double>(1u << (bit + 1));
        } else {
            sd.x[i] = (sd.x[i] << (c - bit)) ^ sd.m[c][i];
            sd.b[i] = c;
            out[i] = static_cast<double>(sd.x[i]) / static_cast<double>(1u << (c + 1));
        }
    }
    return true;
}

inline double sampleAt(unsigned sdim, unsigned dimension, uint32_t index)
{
    SobolData sd;
    if (!init(sd, sdim)) {
        return 0.0;
    }

    std::vector<double> point;
    for (uint32_t i = 0; i <= index; ++i) {
        if (!next01(sd, point)) {
            return 0.0;
        }
    }
    return point[dimension];
}

} // namespace NloptSobolReference
