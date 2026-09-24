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
// and asserts that it fits the arena. Sprint-frozen: F0 declares the members; F3 (S2) added the FF bodies and the FB
// seam, F9 (S3) the FB policies behind it (QuadKnee::solveFb, SmoothBranching::solveFb/commitFb, FeedbackZdf,
// FeedbackDelayed and its guard) and the optional side-chain hooks. Per chunk, control() branches on p_.topo: the FF
// or FB step of Stage.h, per sample, with the coefficients designed on control ticks.
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
//
// The bodies (F3, S2; F9, S3) follow the class; their conventions are documented there ("Bodies").

#include "fcdsp/core/ControlTicker.h"
#include "fcdsp/core/Rt.h"
#include "fcdsp/core/ScopedFtz.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/core/Smoother.h"
#include "fcdsp/engine/IEngine.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/params/EngineParams.h"
#include <bit>
#include <concepts>
#include <cstdint>
#include <new>
#include <utility>

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
    // S10 interface revision (X10): the settled stage 2 after the computer, for CurveOpts::stage2 ("Bodies").
    static void staticS2(const EngineParams&, const float* xDetDb, const float* r1Db, float* grDb, int n) noexcept;
};
// The size/alignment asserts live in FCDSP_DEFINE_MODE, one per Mode TU (01 §8.2).

// ==== Bodies (F3, S2; F9, S3) ========================================================================================
//
// Protocol (01 §5.5): construct -> prepare -> setParams -> snapParams [-> seed(carry)] -> control/colour per chunk.
//   prepare     rates, the 20 ms smoothers and ramps; then the current parameters' targets and reset().
//               Allocation-free. The feedback stability guard (01 §5.3, K2 #5c) runs inside it: reset() ->
//               snapParams() designs every Coeffs at the actual fs, and FeedbackDelayed<G>::design computes its bound
//               k <= alpha / (1 - alpha) there (and again on every control tick), falling back to FeedbackZdf<G>
//               per sample while the curve's loop gain exceeds it (FeedbackDelayed.h).
//   reset       silence: every state value-initialised, the detector seeded at the -240 dB floor, the ballistics and
//               stage 2 at 0 dB GR, the colour states reset, the ticker restarted; then snapParams().
//   setParams   per block: p_ and the smoother/ramp targets. Coefficients follow at the next control tick.
//   snapParams  the level smoothers and both ramps jump to their targets, and every Coeffs is re-designed from a
//               value-initialised one. CONVENTION for every policy: design() into a value-initialised Coeffs lands on
//               the targets (a policy that smooths inside its Coeffs, as SmoothBranching smooths its times, primes
//               there). So a snapped engine renders exactly the settled static behaviour at once.
//   control     per sample i of the chunk: tick -> design (every kTickSamples at the ABSOLUTE index, so block-size
//               invariant) -> LevelCtl from lvl_/lvl2_ (header comment) -> ScShape -> [B::sense(shaped SC), the
//               optional hook below] -> Detector -> FF or FB step -> stage 2 (faded in by s2On_) -> min(range) ->
//               x offAmt_. Both 20 ms LinearRamps are applied through rampShape (smootherstep): still exactly 0 and 1
//               at the ends and 20 ms long, but with no gain-slope
//               discontinuity, so GR OFF and stage 2 on/off do not click (K2 #4 iii). The kernel is chosen per
//               CHUNK: FB when the Traits compile it (kTopologies), p_.topo is kTopoFB and the key is internal (an FB
//               kernel evaluates FF on an external key, E §2.6); FF otherwise. The FB step (K2 #1, #5b; F9):
//                   r = B::solveFb(bc_, bal_, [x, l](FbAffine a) { return G::solveFb(gc_, x, l, a); });
//                   r = L::apply(r, link);  B::commitFb(bc_, bal_, r);
//               i.e. the per-lane solve (the ballistics pick their branches' affine maps, several branches take the
//               max of roots), then the link, then the commit of the linked value.
//               Outputs: grDb = applied GR (lanes 0-1 are the channels the host applies; lanes 2-3 are not
//               consumed), detDb = x (the curve-axis level + preGain), tgtDb = the linked static target (FF) or the
//               static FB curve at x (FB), s2GrDb = {stage-2 GR of c0, c1 (combine's aux lanes, x s2On), 0, 0}, bits =
//               B::status | b3 when the range clamp holds lanes 0-1 | b4 when stage 2 reduces gain.
//   colour      C::process on both channels (channel = the col_ index), in place at the OS rate.
//   carry/seed  Carry{ B::grDb(bal_), D::levelDb(det_), S2::grDb(s2_), releaseNowMs lanes 0-1, lane domain, valid 1 }.
//               Each seed receives its own policy's state, so a switch is continuous in every stage (grDb(state) is
//               required by the concepts since the S2 lead revision; the detection below stays harmless). seed()
//               applies the domain rule (K2 #3d): when carry.msDomain differs from this engine's lane domain (stmode !=
//               STEREO is M/S), every lane takes max(lane0, lane1) first. A carry with valid == 0 is a cold start
//               (no-op).
//   autoMakeup  E §2.2: r^(0 dBFS) = G::target at x = preGainDb with the TARGET level controls, lane 0; 0 unless
//               kEngAutoMakeup, and 0 while kEngGrOff.
//   finite      the carry lanes, the level smoothers and the ramps (01 §5.8).
//   Traits hooks (optional): M::internals(engine, out) (out is zeroed first), M::scDelaySamples(const EngineParams&).
//   Ballistics hooks (optional, F9; detected, not in the frozen concept, so they change nothing for a policy without
//               them): B::sense(bc_, bal_, v) is called once per sample with the shaped linear SC v (after ScShape,
//               before the detector, in both kernels), so a program-dependent ballistics policy can run its own side-
//               chain detectors (CrestAuto: the crest factor, E §2.5a); B::crestDb(bal_) (dB per lane) fills
//               EngineTelemetry::crestDb, else 0. The frozen BallisticsPolicy passes only GR values, which carry no
//               crest information; the handoff of F9 proposes both hooks for the concept at the next freeze.
//   Analysis    staticGr = G::target over 4 abscissae per call with the unsmoothed LevelCtl (FB: G::solveFb with
//               FbAffine{0, 1}); scShapeDb = SH::magDb; colourCurve = C::transfer; each designs a value-initialised
//               Coeffs and opens ScopedFtz. staticGr is the computer alone: range, stage 2 and GR OFF are the
//               caller's (analysis::staticGain, the probes).
//   S10 interface revision (X10; additive):
//   - staticS2(e, x, r1, gr, n): what control() applies after the computer, stage 2 only, settled: per abscissa
//               lerp(r1, S2::combineStatic(s2c, r1, x, l), rampShape(s2On settled)) over 4 abscissae per call with the
//               unsmoothed LevelCtl, i.e. exactly r1 while stage 2 is off (s2ThrDb >= kS2Off) and combineStatic while
//               it is on; the identity for a Stage2 without combineStatic (NoStage2). gr may alias r1.
//               ModeEntry::staticS2 points here when the Stage2 has combineStatic (makeModeEntry), else nullptr.
//   - FB commit with r^: when the ballistics have commitFb(c, s, r, rhat) (Stage.h HasCommitFbRhat) and the computer
//               has rhatFb (HasRhatFb), the FB step commits with rhat = G::rhatFb(gc_, x - r, l) at the linked r;
//               otherwise commitFb(c, s, r) as before.
//   - FbAffine::base reaches the computer unchanged through the solve lambda: G::solveFb honours it (Stage.h).

namespace detail::modeengine {

inline constexpr float kRampMs = 20.0f;         // offAmt_ and s2On_: 20 ms linear ramps (K2 #4 iii, #20)
inline constexpr float kSilenceDb = -240.0f;    // dbFromLin(kLinFloor): the detector's reset level

template <int I>
inline simd::f32x4 bcast(simd::f32x4 v) noexcept FCDSP_NONBLOCKING { return simd::set1(simd::lane<I>(v)); }

// set1(max(lane0, lane1)): the M/S <-> L/R hand-over rule (K2 #3d).
inline simd::f32x4 maxLanes01(simd::f32x4 v) noexcept FCDSP_NONBLOCKING
{
    const float a = simd::lane<0>(v), b = simd::lane<1>(v);
    return simd::set1(a > b ? a : b);
}

// The applied amount of a 0...1 LinearRamp: smootherstep, w^3 (10 - 15w + 6w^2) (S7 lead revision; was smoothstep).
// Exactly 0 and 1 at the ends, monotone, zero slope AND zero curvature at both, so a GR OFF or stage-2 fade has no
// gain-slope discontinuity: on a pure tone a linear 20 ms fade of 8 dB of GR reads +40 dB on the click metric (C §5.0,
// energy above 8 kHz); smoothstep was ~+1 dB at 6 dB of GR but +3.3 dB at 10.5 dB (F4); the host ramps use this same
// shape (host/Ramps.h). Evaluated from the nearer end, as host/Ramps.h's blend() does: above 1/2 as 1 - S(1 - w)
// (1 - w is exact there, and S(1 - t) = 1 - S(t)), so near 1 the amount carries the rounding of the small S(1 - w)
// instead of the cancellation of 10 - 9 (+-5e-7: +-3.5e-6 dB on 7 dB of GR, +5.5 dB on the click metric when the
// steady output around it is as clean as Opto 2A's; S9 M3).
inline float rampShape(float w) noexcept FCDSP_NONBLOCKING
{
    const bool upper = w > 0.5f;
    const float v = upper ? 1.0f - w : w;
    const float s = v * v * v * (v * (v * 6.0f - 15.0f) + 10.0f);
    return upper ? 1.0f - s : s;
}

// (1 - w)*a + w*b with one rounding for the sum: exactly a at w = 0 and exactly b at w = 1.
inline simd::f32x4 lerp(simd::f32x4 a, simd::f32x4 b, float w) noexcept FCDSP_NONBLOCKING
{
    return simd::fma(simd::mul(simd::set1(1.0f - w), a), simd::set1(w), b);
}

inline float clampS2ThrDb(float s2ThrDb) noexcept FCDSP_NONBLOCKING
{
    return s2ThrDb < -40.0f ? -40.0f : (s2ThrDb < kS2Off ? s2ThrDb : kS2Off);
}

// The smoother targets (header comment): {thrDb, slope, min(rangeDb, 60), 0} and
// {clamp(s2ThrDb, -40, 24), kneeDb, 0, 0}.
inline simd::f32x4 lvlTarget(const EngineParams& p) noexcept FCDSP_NONBLOCKING
{
    alignas(16) const float v[4] = { p.thrDb, p.slope, p.rangeDb < kRangeOff ? p.rangeDb : kRangeOff, 0.0f };
    return simd::load(v);
}
inline simd::f32x4 lvl2Target(const EngineParams& p) noexcept FCDSP_NONBLOCKING
{
    alignas(16) const float v[4] = { clampS2ThrDb(p.s2ThrDb), p.kneeDb, 0.0f, 0.0f };
    return simd::load(v);
}

// The unsmoothed LevelCtl of staticGr and autoMakeupDb: the same floats the settled smoothers land on.
inline LevelCtl levelCtl(const EngineParams& p) noexcept FCDSP_NONBLOCKING
{
    return LevelCtl{ simd::set1(p.thrDb), simd::set1(p.slope), simd::set1(p.kneeDb),
                     simd::set1(clampS2ThrDb(p.s2ThrDb)) };
}

// Lanes 0-1 of the engine are L/R for STEREO, else M/S (every other universal stereo code encodes, 01 §3.1).
inline uint8_t laneDomain(const EngineParams& p) noexcept FCDSP_NONBLOCKING
{
    return static_cast<uint8_t>(p.stmode == 0 ? LaneDomain::lr : LaneDomain::ms);
}

inline bool finite(float v) noexcept FCDSP_NONBLOCKING
{
    return (std::bit_cast<uint32_t>(v) & 0x7f800000u) != 0x7f800000u;
}
inline bool finite(simd::f32x4 v) noexcept FCDSP_NONBLOCKING
{
    return finite(simd::lane<0>(v)) && finite(simd::lane<1>(v)) && finite(simd::lane<2>(v)) && finite(simd::lane<3>(v));
}

// A nominal context for the rate-independent analysis designs (the gain computer, the colour transfer).
inline StageCtx analysisCtx(float fs = 48000.0f) noexcept { return StageCtx{ fs, fs, 1, {} }; }

// Optional hooks, detected rather than required (not in the frozen concepts; see "Bodies").
template <class P>
concept HasGrDb = requires (const typename P::State& s) { { P::grDb(s) } noexcept -> std::same_as<simd::f32x4>; };
template <class M, class E>
concept HasInternals = requires (const E& e, float* out) { { M::internals(e, out) } noexcept; };
template <class M>
concept HasScDelay = requires (const EngineParams& p) {
    { M::scDelaySamples(p) } noexcept -> std::convertible_to<int>;
};

template <class B>
concept HasSense = requires (const typename B::Coeffs& c, typename B::State& s, simd::f32x4 v) {
    { B::sense(c, s, v) } noexcept;
};
template <class B>
concept HasCrestDb = requires (const typename B::State& s) {
    { B::crestDb(s) } noexcept -> std::same_as<simd::f32x4>;
};

template <class M>
inline constexpr bool kCompilesFb = (M::kTopologies & (1u << kTopoFB)) != 0;

} // namespace detail::modeengine

template <class M>
IEngine* ModeEngine<M>::construct(void* arena) noexcept FCDSP_NONBLOCKING
{
    return ::new (arena) ModeEngine<M>();
}

template <class M>
void ModeEngine<M>::prepare(const PrepareInfo& info) noexcept FCDSP_NONBLOCKING
{
    namespace me = detail::modeengine;
    ctx_ = StageCtx{ info.fs, info.fs * static_cast<float>(info.osFactor), info.osFactor, info.scratch };
    lvl_.prepare(info.fs);
    lvl2_.prepare(info.fs);
    offAmt_.prepare(info.fs, me::kRampMs);
    s2On_.prepare(info.fs, me::kRampMs);
    ModeEngine::setParams(p_);
    ModeEngine::reset();
}

template <class M>
void ModeEngine<M>::reset() noexcept FCDSP_NONBLOCKING
{
    det_ = {};
    bal_ = {};
    s2_ = {};
    sh_ = {};
    M::Detector::seed(det_, simd::set1(detail::modeengine::kSilenceDb));
    M::Ballistics::seed(bal_, simd::set1(0.0f));
    M::Stage2::seed(s2_, simd::set1(0.0f));
    M::Colour::reset(col_[0]);
    M::Colour::reset(col_[1]);
    tick_ = ControlTicker{};
    ModeEngine::snapParams();
}

template <class M>
void ModeEngine<M>::setParams(const EngineParams& p) noexcept FCDSP_NONBLOCKING
{
    namespace me = detail::modeengine;
    p_ = p;
    lvl_.setTarget(me::lvlTarget(p));
    lvl2_.setTarget(me::lvl2Target(p));
    offAmt_.setTarget((p.flags & kEngGrOff) != 0 ? 0.0f : 1.0f);
    s2On_.setTarget(p.s2ThrDb < kS2Off ? 1.0f : 0.0f);
}

template <class M>
void ModeEngine<M>::snapParams() noexcept FCDSP_NONBLOCKING
{
    lvl_.snap();
    lvl2_.snap();
    offAmt_.cur = offAmt_.tgt;
    s2On_.cur = s2On_.tgt;
    dc_ = {};
    gc_ = {};
    bc_ = {};
    s2c_ = {};
    cc_ = {};
    shc_ = {};
    M::ScShape::design(shc_, p_, ctx_);
    M::Detector::design(dc_, p_, ctx_);
    M::Computer::design(gc_, p_, ctx_);
    M::Ballistics::design(bc_, p_, ctx_);
    M::Stage2::design(s2c_, p_, ctx_);
    M::Colour::design(cc_, p_, ctx_);
}

template <class M>
Carry ModeEngine<M>::carry() const noexcept FCDSP_NONBLOCKING
{
    namespace me = detail::modeengine;
    using B = typename M::Ballistics;
    using S2 = typename M::Stage2;
    Carry c;
    if constexpr (me::HasGrDb<B>)
        c.grDb = B::grDb(bal_);
    c.detDb = M::Detector::levelDb(det_);
    if constexpr (me::HasGrDb<S2>)
        c.s2GrDb = S2::grDb(s2_);
    const simd::f32x4 rel = B::releaseNowMs(bc_, bal_);
    c.relNowMs[0] = simd::lane<0>(rel);
    c.relNowMs[1] = simd::lane<1>(rel);
    c.msDomain = me::laneDomain(p_);
    c.valid = 1;
    return c;
}

template <class M>
void ModeEngine<M>::seed(const Carry& c) noexcept FCDSP_NONBLOCKING
{
    namespace me = detail::modeengine;
    if (c.valid == 0)
        return;
    simd::f32x4 gr = c.grDb, det = c.detDb, s2 = c.s2GrDb;
    if (c.msDomain != me::laneDomain(p_))
    {
        gr = me::maxLanes01(gr);
        det = me::maxLanes01(det);
        s2 = me::maxLanes01(s2);
    }
    M::Detector::seed(det_, det);
    M::Ballistics::seed(bal_, gr);
    M::Stage2::seed(s2_, s2);
}

template <class M>
void ModeEngine<M>::control(const ControlIo& io) noexcept FCDSP_NONBLOCKING
{
    namespace me = detail::modeengine;
    using SH = typename M::ScShape;
    using D = typename M::Detector;
    using G = typename M::Computer;
    using L = typename M::Link;
    using B = typename M::Ballistics;
    using S2 = typename M::Stage2;
    using C = typename M::Colour;

    const float link = p_.link;
    const auto run = [&]<bool kFb>() noexcept FCDSP_NONBLOCKING {
        for (int i = 0; i < io.n; ++i)
        {
            if (tick_.advance(io.sampleIndex + static_cast<uint64_t>(i)))
            {
                SH::design(shc_, p_, ctx_);
                D::design(dc_, p_, ctx_);
                G::design(gc_, p_, ctx_);
                B::design(bc_, p_, ctx_);
                S2::design(s2c_, p_, ctx_);
                C::design(cc_, p_, ctx_);
            }
            const simd::f32x4 a = lvl_.tick(), b = lvl2_.tick();
            const LevelCtl l{ me::bcast<0>(a), me::bcast<1>(a), me::bcast<1>(b), me::bcast<0>(b) };
            const simd::f32x4 range = me::bcast<2>(a);

            const simd::f32x4 v = SH::tick(shc_, sh_, io.sc[i]);
            if constexpr (me::HasSense<B>)
                B::sense(bc_, bal_, v);
            const simd::f32x4 x = D::tick(dc_, det_, v);
            simd::f32x4 tgt, r1;
            if constexpr (kFb)
            {
                const auto solve = [&](FbAffine fa) noexcept { return G::solveFb(gc_, x, l, fa); };
                r1 = B::solveFb(bc_, std::as_const(bal_), solve);
                r1 = L::apply(r1, link);
                if constexpr (HasCommitFbRhat<B> && HasRhatFb<G>)
                    B::commitFb(bc_, bal_, r1, G::rhatFb(gc_, simd::sub(x, r1), l));   // S10: r^_fb at the linked r
                else
                    B::commitFb(bc_, bal_, r1);
                tgt = io.tgtDb != nullptr ? G::solveFb(gc_, x, l, FbAffine{ simd::set1(0.0f), simd::set1(1.0f) })
                                          : r1;
            }
            else
            {
                tgt = L::apply(G::target(gc_, x, l), link);
                r1 = B::tick(bc_, bal_, tgt);
            }

            const simd::f32x4 r2 = S2::combine(s2c_, s2_, r1, x, l);
            const float w2 = me::rampShape(s2On_.tick());
            const simd::f32x4 rs = me::lerp(r1, r2, w2);
            const simd::f32x4 r = simd::mul(simd::min(rs, range), simd::set1(me::rampShape(offAmt_.tick())));
            io.grDb[i] = r;
            if (io.detDb != nullptr)
                io.detDb[i] = x;
            if (io.tgtDb != nullptr)
                io.tgtDb[i] = tgt;
            const float s2a = w2 * simd::lane<2>(r2), s2b = w2 * simd::lane<3>(r2);
            if (io.s2GrDb != nullptr)
            {
                alignas(16) const float s2v[4] = { s2a, s2b, 0.0f, 0.0f };
                io.s2GrDb[i] = simd::load(s2v);
            }
            if (io.bits != nullptr)
            {
                const bool ranged =
                    simd::lane<0>(rs) > simd::lane<0>(range) || simd::lane<1>(rs) > simd::lane<1>(range);
                const bool stage2 = s2a > 0.0f || s2b > 0.0f;
                io.bits[i] = static_cast<uint8_t>(B::status(bal_) | (ranged ? 1u << 3 : 0u) | (stage2 ? 1u << 4 : 0u));
            }
        }
    };

    if constexpr (me::kCompilesFb<M>)
    {
        if (p_.topo == kTopoFB && !io.keyExternal)
        {
            run.template operator()<true>();
            return;
        }
    }
    run.template operator()<false>();
}

template <class M>
void ModeEngine<M>::colour(const AudioIo& io) noexcept FCDSP_NONBLOCKING
{
    M::Colour::process(cc_, col_[0], io.wet[0], io.grDbOs[0], io.nOs, 0);
    M::Colour::process(cc_, col_[1], io.wet[1], io.grDbOs[1], io.nOs, 1);
}

template <class M>
float ModeEngine<M>::autoMakeupDb() const noexcept FCDSP_NONBLOCKING
{
    if ((p_.flags & kEngAutoMakeup) == 0 || (p_.flags & kEngGrOff) != 0)
        return 0.0f;
    const LevelCtl l = detail::modeengine::levelCtl(p_);
    return simd::lane<0>(M::Computer::target(gc_, simd::set1(p_.preGainDb), l));
}

template <class M>
int ModeEngine<M>::scDelaySamples() const noexcept FCDSP_NONBLOCKING
{
    if constexpr (detail::modeengine::HasScDelay<M>)
        return static_cast<int>(M::scDelaySamples(p_));
    else
        return 0;
}

template <class M>
void ModeEngine<M>::internals(float out[kInternals]) const noexcept FCDSP_NONBLOCKING
{
    for (int i = 0; i < kInternals; ++i)
        out[i] = 0.0f;
    if constexpr (detail::modeengine::HasInternals<M, ModeEngine<M>>)
        M::internals(*this, out);
}

template <class M>
void ModeEngine<M>::telemetry(EngineTelemetry& t) const noexcept FCDSP_NONBLOCKING
{
    const simd::f32x4 atk = M::Ballistics::attackNowMs(bc_, bal_);
    const simd::f32x4 rel = M::Ballistics::releaseNowMs(bc_, bal_);
    t.attackNowMs[0] = simd::lane<0>(atk);
    t.attackNowMs[1] = simd::lane<1>(atk);
    t.releaseNowMs[0] = simd::lane<0>(rel);
    t.releaseNowMs[1] = simd::lane<1>(rel);
    if constexpr (detail::modeengine::HasCrestDb<typename M::Ballistics>)
    {
        const simd::f32x4 crest = M::Ballistics::crestDb(bal_);
        t.crestDb[0] = simd::lane<0>(crest);
        t.crestDb[1] = simd::lane<1>(crest);
    }
    else
    {
        t.crestDb[0] = 0.0f;                      // no crest detector in this Mode's ballistics
        t.crestDb[1] = 0.0f;
    }
}

template <class M>
bool ModeEngine<M>::finite() const noexcept FCDSP_NONBLOCKING
{
    namespace me = detail::modeengine;
    const Carry c = ModeEngine::carry();
    return me::finite(c.grDb) && me::finite(c.detDb) && me::finite(c.s2GrDb) && me::finite(c.relNowMs[0])
        && me::finite(c.relNowMs[1]) && me::finite(lvl_.cur) && me::finite(lvl2_.cur) && me::finite(offAmt_.cur)
        && me::finite(s2On_.cur);
}

template <class M>
void ModeEngine<M>::staticGr(const EngineParams& e, const float* xDetDb, float* grDb, int n) noexcept
{
    namespace me = detail::modeengine;
    using G = typename M::Computer;
    const ScopedFtz ftz;
    typename G::Coeffs gc{};
    G::design(gc, e, me::analysisCtx());
    const LevelCtl l = me::levelCtl(e);
    for (int i = 0; i < n; i += 4)
    {
        const int m = n - i < 4 ? n - i : 4;
        alignas(16) float buf[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        for (int k = 0; k < m; ++k)
            buf[k] = xDetDb[i + k];
        simd::f32x4 r = G::target(gc, simd::load(buf), l);
        if constexpr (me::kCompilesFb<M>)
            if (e.topo == kTopoFB)
                r = G::solveFb(gc, simd::load(buf), l, FbAffine{ simd::set1(0.0f), simd::set1(1.0f) });
        simd::store(buf, r);
        for (int k = 0; k < m; ++k)
            grDb[i + k] = buf[k];
    }
}

template <class M>
void ModeEngine<M>::staticS2(const EngineParams& e, const float* xDetDb, const float* r1Db, float* grDb, int n) noexcept
{
    namespace me = detail::modeengine;
    using S2 = typename M::Stage2;
    const ScopedFtz ftz;
    if constexpr (!HasCombineStatic<S2>)
    {
        for (int i = 0; i < n; ++i)                   // no static stage 2 (NoStage2): the identity
            grDb[i] = r1Db[i];
    }
    else
    {
        typename S2::Coeffs c{};
        S2::design(c, e, me::analysisCtx());
        const LevelCtl l = me::levelCtl(e);
        const float w2 = me::rampShape(e.s2ThrDb < kS2Off ? 1.0f : 0.0f);   // the settled s2On_ (setParams' target)
        for (int i = 0; i < n; i += 4)
        {
            const int m = n - i < 4 ? n - i : 4;
            alignas(16) float xb[4] = { 0.0f, 0.0f, 0.0f, 0.0f }, rb[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            for (int k = 0; k < m; ++k)
            {
                xb[k] = xDetDb[i + k];
                rb[k] = r1Db[i + k];
            }
            const simd::f32x4 r1 = simd::load(rb);
            simd::store(rb, me::lerp(r1, S2::combineStatic(c, r1, simd::load(xb), l), w2));
            for (int k = 0; k < m; ++k)
                grDb[i + k] = rb[k];
        }
    }
}

template <class M>
void ModeEngine<M>::scShapeDb(const EngineParams& e, float fs, const float* hz, float* magDb, int n) noexcept
{
    using SH = typename M::ScShape;
    const ScopedFtz ftz;
    typename SH::Coeffs c{};
    SH::design(c, e, detail::modeengine::analysisCtx(fs));
    for (int i = 0; i < n; ++i)
        magDb[i] = SH::magDb(c, hz[i], fs);
}

template <class M>
void ModeEngine<M>::colourCurve(const EngineParams& e, float grDb, const float* x, float* y, int n) noexcept
{
    using C = typename M::Colour;
    const ScopedFtz ftz;
    typename C::Coeffs c{};
    C::design(c, e, detail::modeengine::analysisCtx());
    for (int i = 0; i < n; ++i)
        y[i] = C::transfer(c, x[i], grDb);
}

} // namespace fcdsp
