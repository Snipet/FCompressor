// FastMath.cpp: tanPi, sinPi and cosPi (01 §5.1; K2 #14). They run per control tick (SVF g = tanPi(fc/fs), the tilt
// prewarp), so they are out of line; log2/exp2/tanh/logCosh are inline in FastMath.h. Written with fcdsp::simd ops only
// (fused fma, IEEE div, exact abs/sel and reflections), evaluated on a vector and returned from lane 0, so both
// backends produce the same bits and a later vector form would equal these lane for lane.
//
// Both reduce to y in [0, 1/4] with exact reflections (1 - a for a in [1/2, 1] and 1/2 - b for b in [1/4, 1/2] are
// Sterbenz-exact), then use
//   sin(pi y) = y * (s0 + s1 y^2 + s2 y^4 + s3 y^6)      relative error 3.2e-9 (minimax fit)
//   cos(pi y) = 1 + y^2 * (c1 + c2 y^2 + c3 y^4 + c4 y^6)  relative error 6.4e-11
// so the measured errors (dsp.simd) are float rounding: sinPi/cosPi 8.4e-8 absolute and 1.3e-7 relative, tanPi
// 2.0e-7 relative over [0, 0.499]; all three monotone where the function is. Exact values: sinPi(0) = sinPi(+-1) = 0,
// sinPi(+-1/2) = +-1, cosPi(0) = 1, cosPi(+-1/2) = 0, cosPi(+-1) = -1, tanPi(0) = 0; sinPi is odd and cosPi even,
// exactly.
#include "fcdsp/core/FastMath.h"

namespace fcdsp {

namespace {

using simd::f32x4;

f32x4 sinPiPoly(f32x4 y) noexcept FCDSP_NONBLOCKING       // sin(pi y), |y| <= 1/4
{
    const f32x4 u = simd::mul(y, y);
    f32x4 s = simd::set1(-0.589012206f);
    s = simd::fma(simd::set1(2.54976106f), s, u);
    s = simd::fma(simd::set1(-5.16770792f), s, u);
    s = simd::fma(simd::set1(3.14159274f), s, u);
    return simd::mul(y, s);
}

f32x4 cosPiPoly(f32x4 y) noexcept FCDSP_NONBLOCKING       // cos(pi y), |y| <= 1/4
{
    const f32x4 u = simd::mul(y, y);
    f32x4 c = simd::set1(0.231364235f);
    c = simd::fma(simd::set1(-1.33505058f), c, u);
    c = simd::fma(simd::set1(4.05870771f), c, u);
    c = simd::fma(simd::set1(-4.93480206f), c, u);
    return simd::fma(simd::set1(1.0f), u, c);
}

} // namespace

float tanPi(float x) noexcept FCDSP_NONBLOCKING
{
    // x in [0, 1/4]: sin/cos of pi x. x in (1/4, 1/2]: tan(pi x) = cos(pi y)/sin(pi y) with y = 1/2 - x (exact).
    const f32x4 v = simd::set1(x);
    const simd::m32x4 low = simd::ge(simd::set1(0.25f), v);
    const f32x4 y = simd::sel(low, v, simd::sub(simd::set1(0.5f), v));
    const f32x4 s = sinPiPoly(y), c = cosPiPoly(y);
    return simd::lane<0>(simd::div(simd::sel(low, s, c), simd::sel(low, c, s)));
}

float sinPi(float x) noexcept FCDSP_NONBLOCKING
{
    // a = |x|; b = a or 1 - a, in [0, 1/2] (sin(pi a) = sin(pi (1 - a))); b > 1/4: cos(pi (1/2 - b)).
    const f32x4 v = simd::set1(x), a = simd::abs(v);
    const f32x4 b = simd::sel(simd::gt(a, simd::set1(0.5f)), simd::sub(simd::set1(1.0f), a), a);
    const simd::m32x4 high = simd::gt(b, simd::set1(0.25f));
    const f32x4 y = simd::sel(high, simd::sub(simd::set1(0.5f), b), b);
    const f32x4 r = simd::sel(high, cosPiPoly(y), sinPiPoly(y));
    return simd::lane<0>(simd::sel(simd::gt(simd::set1(0.0f), v), simd::neg(r), r));
}

float cosPi(float x) noexcept FCDSP_NONBLOCKING
{
    // a = |x|; cos(pi a) = -cos(pi (1 - a)) for a > 1/2; b in [0, 1/2]; b > 1/4: sin(pi (1/2 - b)).
    const f32x4 a = simd::abs(simd::set1(x));
    const simd::m32x4 flip = simd::gt(a, simd::set1(0.5f));
    const f32x4 b = simd::sel(flip, simd::sub(simd::set1(1.0f), a), a);
    const simd::m32x4 high = simd::gt(b, simd::set1(0.25f));
    const f32x4 y = simd::sel(high, simd::sub(simd::set1(0.5f), b), b);
    const f32x4 r = simd::sel(high, sinPiPoly(y), cosPiPoly(y));
    return simd::lane<0>(simd::sel(flip, simd::neg(r), r));
}

} // namespace fcdsp
