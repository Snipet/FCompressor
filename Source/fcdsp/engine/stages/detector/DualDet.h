#pragma once

// stage::DualDet: the PEAK + RMS detector (01 §5.2 catalogue, §10.3 Clean det PK+RMS; E §3.3 "DualDet (peak and RMS)").
// It runs PeakLog and RmsLog side by side on every lane and reports their mean in dB, the RMS lifted by a sine's
// crest factor so both read the same level on a sine:
//
//     level = ( peakDb + (rmsDb + kSineCrestDb) ) / 2,     kSineCrestDb = 20 log10(sqrt 2) = 3.0103 dB
//
// So a sine reads its peak (Clean's PK+RMS uses DetectorLaw::peak for the curve's x axis, cleanLaw), a square reads
// 1.5 dB above its peak, and a transient reads half-way between its peak and its RMS body: the peak detector catches
// the edge, the RMS window weighs the sustain ([H]: the PK+RMS blend of docs/modes/clean.md). Both halves are the
// catalogue policies' own arithmetic (PeakLog: instantaneous 20 log10|v|; RmsLog: the 20 ms mean square), and both
// levels stay readable for the Mode's internals (Clean's PEAK DET and RMS DET words).
//
// E §3.3 packs the peak and RMS recurrences into the aux lanes of one f32x4; the host does not promise a copy of the
// channels in lanes 2-3, so DualDet keeps two f32x4 (four lanes each, lanes 0-1 meaningful) instead: the cost is one
// extra fma and log2 per sample.

#include "fcdsp/core/FastMath.h"
#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/engine/stages/detector/PeakLog.h"
#include "fcdsp/engine/stages/detector/RmsLog.h"
#include "fcdsp/params/EngineParams.h"

namespace fcdsp::stage {

struct DualDet {
    static constexpr float kSineCrestDb = 3.01029996f;  // a sine's peak over its RMS, dB

    struct Coeffs { RmsLog::Coeffs rms{}; };
    struct State {
        PeakLog::State pk{};                    // instantaneous peak level, dB
        RmsLog::State rms{};                    // mean square
    };

    static void design(Coeffs& c, const EngineParams& p, const StageCtx& x) noexcept FCDSP_NONBLOCKING
    {
        RmsLog::design(c.rms, p, x);
    }

    static simd::f32x4 tick(const Coeffs& c, State& s, simd::f32x4 v) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 pk = PeakLog::tick(PeakLog::Coeffs{}, s.pk, v);
        const simd::f32x4 rms = RmsLog::tick(c.rms, s.rms, v);
        return blend(pk, rms);
    }

    // Carry::detDb is a peak-law level: the peak takes it, the RMS the RMS of a sine with that peak.
    static void seed(State& s, simd::f32x4 levelDb) noexcept FCDSP_NONBLOCKING
    {
        PeakLog::seed(s.pk, levelDb);
        RmsLog::seed(s.rms, simd::sub(levelDb, simd::set1(kSineCrestDb)));
    }

    static simd::f32x4 levelDb(const State& s) noexcept FCDSP_NONBLOCKING
    {
        return blend(PeakLog::levelDb(s.pk), RmsLog::levelDb(s.rms));
    }

    // The two halves, for internals (Clean: PEAK DET, RMS DET).
    static simd::f32x4 peakDb(const State& s) noexcept FCDSP_NONBLOCKING { return PeakLog::levelDb(s.pk); }
    static simd::f32x4 rmsDb(const State& s) noexcept FCDSP_NONBLOCKING { return RmsLog::levelDb(s.rms); }

private:
    static simd::f32x4 blend(simd::f32x4 peak, simd::f32x4 rms) noexcept FCDSP_NONBLOCKING
    {
        const simd::f32x4 half = simd::set1(0.5f);
        return simd::mul(half, simd::add(peak, simd::add(rms, simd::set1(kSineCrestDb))));
    }
};

static_assert(DetectorPolicy<DualDet>);

} // namespace fcdsp::stage
