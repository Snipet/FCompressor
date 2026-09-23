// Sanitize.cpp: input sanitisation (01 §5.1, §5.8; K2 #13). Integer operations on the bit patterns only, so the result
// is the same on both architectures, in any FP mode and at any optimisation or LTO setting: a NaN or an infinity
// (exponent field all ones) becomes +0, a finite |x| > 1e6 becomes +-1e6 (sign kept), and everything else, denormals
// included, is copied bit for bit (FTZ flushes denormals downstream). in may equal out (in-place); n <= 0 does
// nothing.
#include "fcdsp/core/Sanitize.h"

#include <bit>
#include <cstdint>

namespace fcdsp {

int sanitize(const float* in, float* out, int n) noexcept FCDSP_NONBLOCKING
{
    constexpr uint32_t kExpMask = 0x7f800000u, kMagMask = 0x7fffffffu, kSignMask = 0x80000000u;
    constexpr uint32_t kLimit = std::bit_cast<uint32_t>(1.0e6f);      // 0x49742400; +120 dBFS
    static_assert(kLimit == 0x49742400u);

    int replaced = 0;
    for (int i = 0; i < n; ++i)
    {
        uint32_t b = std::bit_cast<uint32_t>(in[i]);
        if ((b & kExpMask) == kExpMask)                     // NaN or +-inf: the bit test of 01 §5.1
        {
            b = 0;
            ++replaced;
        }
        else if ((b & kMagMask) > kLimit)                   // finite magnitudes order like their bit patterns
        {
            b = (b & kSignMask) | kLimit;
            ++replaced;
        }
        out[i] = std::bit_cast<float>(b);
    }
    return replaced;
}

} // namespace fcdsp
