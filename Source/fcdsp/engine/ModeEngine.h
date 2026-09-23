#pragma once

// ModeEngine<Traits>: one Mode's engine, compiled from its traits' policies (01 §5.3; E §3.5-3.6). A Mode is one
// directory with a Traits header naming one policy per slot, for example (modes/fet-76/Fet76.h):
//
//     struct Fet76 {
//         static constexpr const ModeDescriptor& desc = kFet76;     // extern, defined in Fet76Desc.cpp (D24)
//         using Detector   = stage::PeakLog;
//         using Computer   = stage::QuadKnee;
//         using Link       = stage::LinkMax;
//         using Ballistics = stage::SmoothBranching;
//         using Stage2     = stage::NoStage2;
//         using Colour     = stage::ColourSelect<stage::FetColour>; // voice picks the revision's constants
//         using ScShape    = stage::Flat;                           // Mode-internal SC shaping (not the host filter)
//         static constexpr uint8_t kTopologies = 1u << kTopoFB;     // kernels compiled in (bitmask)
//         static void internals(const auto& engine, float out[kInternals]) noexcept FCDSP_NONBLOCKING;
//     };
//
// FCDSP_DEFINE_MODE(Traits) (DefineMode.h) instantiates the engine and its analysis entry points in the Mode's own TU
// and asserts that it fits the arena. Sprint-frozen: F0 declares the members; F3 (S2) adds the FF bodies and F9 (S3)
// the FB bodies. Per chunk, control() branches on p_.topo: the FF or FB step of Stage.h, per sample, with the
// coefficients designed on control ticks.
//
// Real time (FZ0 errata, R-F0 #1): construct and every IEngine override are FCDSP_NONBLOCKING; an out-of-line
// definition repeats the macro. The analysis statics are not on the audio path and are not annotated.
//
// Level smoothing and the per-sample LevelCtl (FZ0 errata, R-F0 #2; 01 §5.1):
//   setParams(p):  lvl_.setTarget ({thrDb, slope, min(rangeDb, kRangeOff), 0})
//                  lvl2_.setTarget({clamp(s2ThrDb, -40, kS2Off), kneeDb, 0, 0})
//   per sample:    const simd::f32x4 a = lvl_.tick(), b = lvl2_.tick();
//                  const LevelCtl l{ bcast<0>(a), bcast<1>(a), bcast<1>(b), bcast<0>(b) };   // thr, slope, knee, s2thr
//                  const simd::f32x4 range = bcast<2>(a);                                  // r = min(r, range)
//                  where bcast<I>(v) = simd::set1(simd::lane<I>(v)) (one DUP on NEON).
//   staticGr (analysis) builds the same LevelCtl from the targets, unsmoothed:
//                  { set1(thrDb), set1(slope), set1(kneeDb), set1(clamp(s2ThrDb, -40, kS2Off)) }
//                  so the settled engine (the smoothers land exactly) and staticGr agree bit for bit.
//
// Traits hooks (FZ0 errata, R-F0 #6): ModeEngine befriends its Traits M, so M::internals(engine, out) reads the
// members below by name (det_, bal_, s2_, col_, sh_, the Coeffs, lvl_, lvl2_, p_ ...) through a const reference. The
// member names are part of this frozen declaration. The hook is header-inline and nonblocking (ModeEngine::internals
// calls it on the audio thread's telemetry path).

#include "fcdsp/core/ControlTicker.h"
#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/core/Smoother.h"
#include "fcdsp/engine/IEngine.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/params/EngineParams.h"

namespace fcdsp {

template <class M>
class ModeEngine final : public IEngine {
    static_assert(DetectorPolicy<typename M::Detector> && GainComputerPolicy<typename M::Computer> &&
                  LinkPolicy<typename M::Link> && BallisticsPolicy<typename M::Ballistics> &&
                  Stage2Policy<typename M::Stage2> && ColourPolicy<typename M::Colour> &&
                  ScShapePolicy<typename M::ScShape>);
    friend M;                         // the Traits' internals hook reads the state (FZ0 errata, R-F0 #6)

    alignas(16) typename M::Detector::State   det_{};
    alignas(16) typename M::Ballistics::State bal_{};
    alignas(16) typename M::Stage2::State     s2_{};
    alignas(16) typename M::Colour::State     col_[2]{};
    typename M::Detector::Coeffs dc_{}; typename M::Computer::Coeffs gc_{};
    typename M::Ballistics::Coeffs bc_{}; typename M::Stage2::Coeffs s2c_{}; typename M::Colour::Coeffs cc_{};
    typename M::ScShape::State sh_{}; typename M::ScShape::Coeffs shc_{};
    Smoother4 lvl_{}, lvl2_{};        // {thrDb, slope, min(rangeDb,60), -}, {s2ThrDb, kneeDb, -, -}: -> LevelCtl above
    LinearRamp offAmt_{}, s2On_{};    // GR OFF and stage 2 on/off: 20 ms ramps, never a step (K2 #4, #20)
    EngineParams p_{}; ControlTicker tick_{}; StageCtx ctx_{};

public:
    static IEngine* construct(void* arena) noexcept FCDSP_NONBLOCKING;   // placement-new; RT-safe

    void  prepare(const PrepareInfo&) noexcept FCDSP_NONBLOCKING override;
    void  reset() noexcept FCDSP_NONBLOCKING override;
    void  setParams(const EngineParams&) noexcept FCDSP_NONBLOCKING override;
    void  snapParams() noexcept FCDSP_NONBLOCKING override;
    Carry carry() const noexcept FCDSP_NONBLOCKING override;
    void  seed(const Carry&) noexcept FCDSP_NONBLOCKING override;
    void  control(const ControlIo& io) noexcept FCDSP_NONBLOCKING override;   // loop: tick -> design; FF or FB step
                                                                              // (Stage.h); branch per CHUNK on p_.topo
    void  colour(const AudioIo&) noexcept FCDSP_NONBLOCKING override;
    float autoMakeupDb() const noexcept FCDSP_NONBLOCKING override;
    int   scDelaySamples() const noexcept FCDSP_NONBLOCKING override;
    void  internals(float out[kInternals]) const noexcept FCDSP_NONBLOCKING override;   // M::internals(*this, out)
    void  telemetry(EngineTelemetry&) const noexcept FCDSP_NONBLOCKING override;
    bool  finite() const noexcept FCDSP_NONBLOCKING override;

    // Analysis entry points, instantiated from the SAME policies the audio thread runs (01 §7; E §6.5):
    static void staticGr(const EngineParams&, const float* xDetDb, float* grDb, int n) noexcept;
    static void scShapeDb(const EngineParams&, float fs, const float* hz, float* magDb, int n) noexcept;
    static void colourCurve(const EngineParams&, float grDb, const float* x, float* y, int n) noexcept;
};
// The size/alignment asserts live in FCDSP_DEFINE_MODE, one per Mode TU (01 §8.2).

} // namespace fcdsp
