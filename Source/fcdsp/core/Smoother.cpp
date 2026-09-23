// Smoother.cpp: the prepare() bodies of Smoother4 and LinearRamp (01 §5.1; E §4.6; K2 #20). They run when an engine
// is prepared, never per sample; the per-sample members are inline in Smoother.h.
#include "fcdsp/core/Smoother.h"

#include "fcdsp/core/Units.h"

namespace fcdsp {

void Smoother4::prepare(float fs, float tauMs, simd::f32x4 epsilon) noexcept FCDSP_NONBLOCKING
{
    a = simd::set1(alphaFromTau(tauMs, fs));
    eps = epsilon;
}

void LinearRamp::prepare(float fs, float ms) noexcept FCDSP_NONBLOCKING
{
    const float msFs = ms * fs;                             // ramp length in samples, times 1000
    step = msFs > 1000.0f ? 1000.0f / msFs : 1.0f;          // shorter than one sample, <= 0 or NaN: instant
}

} // namespace fcdsp
