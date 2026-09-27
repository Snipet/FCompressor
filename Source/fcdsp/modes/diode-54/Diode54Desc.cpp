// Diode 54 (slot 11, `diode-54`): the Mode's descriptor, physical() and specs (D §2.5 the Neve 2254, M17; D §5.2–5.6
// row M17). Diode54.h holds the traits and Diode54.cpp FCDSP_DEFINE_MODE(Diode54); docs/modes/diode-54.md lists every
// [H] constant with its source. The engine, the dBu law and the knee law are Diode 609's (Diode609Desc.cpp).
//
// - External linkage (SPRINTS §7 D24): the traits header declares `extern const ModeDescriptor kDiode54;`.
// - provisional = false: the Mode's own traits are installed, so the fidelity rows are blocking spec rows and the
//   goldens can be blessed. revision 1: the Mode has never shipped (tests/fixtures/modes-ever.tsv).
// - THRESHOLD -20 ... +10 dBu and GAIN 0 ... 20 dB in 2 dB steps (D §2.5 [V S11]), dBFS = dBu - 22 (01 §3.1).
// - RATIO 1.5 / 2 / 3 / 4 / 6 with the knee "fairly soft ... over a 10dB range" (D §2.5 [V S11]): KNEE is derived from
//   RATIO by Diode 609's law, the output-domain W whose input span is 10 dB, W = 20 / (1 + R).
// - ATTACK: "fixed 5 ms. An optional fast attack is adjustable from 100 µs to 2 ms" (D §2.5 [V S11], the /R). It is a
//   hybrid: continuous 0.1 ... 2 ms (the FAST pot) plus the 5 MS step outside that range; default 5 MS. LIMIT ATTACK
//   is the same switch on the limiter's side chain. ADR-63: both are closed-loop times, converted to the loops'
//   open-loop tau in physical() (t (1 + k)), as in Diode 609.
// - RECOVERY .1 / .2 / .8 S + AUTO for both stages (D §2.5 [V S11]; a secondary source's 400 / 800 / 1500 is [C]).
//   AUTO is DualRelease: a fast recovery of 100 ms, and after sustained program a slow one, the slow path charging in
//   0.5 s [H]. Its tau_Rs is 1.35 s so the cascade's 1/e time lands inside the published 1.5 s (the fast path's lag adds
//   about tau_Rf, Bus G's lesson: 1.45 s measured); the release spec keeps the published 0.1–1.5 s. Diode 609's A1 is
//   100 ms / 2 s. The 2254's "attack time is adjusted automatically along with the
//   release" in AUTO is not modelled: AUTO keeps the ATTACK switch's time.
// - LIMIT THRESHOLD +4 ... +20 dBu in 2 dB steps, then OFF at the host range end (kS2Off, K2 #20; D §2.5 [V S11]; the
//   33609's is +4 ... +15). Its ratio is SharedElementMax's (> 100:1, [C]).
// - No ATTACK SLOW high-pass: that is the 33609's (ScShape Flat); the host SC HPF is the standard extension.
// - Defaults: THRESHOLD +4 dBu (-18 dBFS, 0 VU), RATIO 3:1, ATTACK 5 MS, RECOVERY 0.2 S, GAIN 0 dB, LINK STEREO, LIMIT
//   OFF, LIMIT ATTACK 5 MS, LIMIT RECOVERY 0.1 S.
// - Rigor character, family custom (the progressive knee), link law max (the side chains' outputs are combined).
// - Mode-local helpers live here in an unnamed namespace (ModeKit.h is frozen); the tables are namespace-scope constants
//   (no function-local statics, C D12).

#include "fcdsp/modes/diode-54/Diode54.h"

#include "fcdsp/engine/stages/gain/QuadKnee.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/ModeKit.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include <cstdint>

namespace fcdsp::modes {

namespace {

using namespace kit;

// THRESHOLD -20 ... +10 dBu in 2 dB steps as dBFS = dBu - 22 (D §2.5 [V S11]).
constexpr Step kThr[] = { { -42, "−20" }, { -40, "−18" }, { -38, "−16" }, { -36, "−14" }, { -34, "−12" },
                          { -32, "−10" }, { -30, "−8" }, { -28, "−6" }, { -26, "−4" }, { -24, "−2" }, { -22, "0" },
                          { -20, "2" }, { -18, "4" }, { -16, "6" }, { -14, "8" }, { -12, "10" } };
constexpr Step kRatio[] = { { 1.0f - 1.0f / 1.5f, "1.5", "1.5:1" }, { 0.5f, "2", "2:1" },
                            { 1.0f - 1.0f / 3.0f, "3", "3:1" }, { 0.75f, "4", "4:1" },
                            { 1.0f - 1.0f / 6.0f, "6", "6:1" } };
// ATTACK: the FAST pot's 0.1 ... 2 ms is the hybrid's range; the fixed 5 ms is its one step outside it.
constexpr float kFastLoMs = 0.1f, kFastHiMs = 2.0f, kFixedMs = 5.0f;
constexpr Step kAtkFixed[] = { { kFixedMs, "5", "5 MS" } };
constexpr float kAutoPlainMs = 1500.f;          // AUTO's nominal (its published slow end), above the manual positions
constexpr Step kRel[]   = { { 100, ".1", "0.1 S" }, { 200, ".2", "0.2 S" }, { 800, ".8", "0.8 S" },
                            { kAutoPlainMs, "AUTO", "AUTO 100 MS/1.5 S", kTagAuto | kTagProgram } };
constexpr Step kGain[]  = { { 0, "0" }, { 2, "2" }, { 4, "4" }, { 6, "6" }, { 8, "8" }, { 10, "10" }, { 12, "12" },
                            { 14, "14" }, { 16, "16" }, { 18, "18" }, { 20, "20" } };
constexpr Step kLink[]  = { { 0, "DUAL", "DUAL MONO" }, { 1, "STEREO", "STEREO (SIDE CHAINS COMBINED)" } };
// LIMIT THRESHOLD +4 ... +20 dBu in 2 dB steps as dBFS = dBu - 22, then OFF at the host range end.
constexpr Step kS2Thr[] = { { -18, "4" }, { -16, "6" }, { -14, "8" }, { -12, "10" }, { -10, "12" }, { -8, "14" },
                            { -6, "16" }, { -4, "18" }, { -2, "20" }, { 24, "OFF", "LIMITER OUT" } };
constexpr Step kPeak[]  = { { 0, "PEAK", "PEAK · FEEDBACK" } };
constexpr Step kDiode[] = { { 0, "DIODE", "DIODE BRIDGE + TRANSFORMERS" } };

// [H] AUTO recovery: fast and slow (ms), and the slow path's charge (docs/modes/diode-54.md); the published range is
// kAutoFastMs ... kAutoPublishedSlowMs, and tau_Rs sits below its end by the fast path's lag (file comment).
constexpr float kAutoFastMs = 100.f, kAutoSlowMs = 1350.f, kAutoChargeMs = 500.f;
constexpr float kAutoPublishedSlowMs = 1500.f;
constexpr float kKneeInputSpanDb = 10.f;        // "fairly soft ... over a 10dB range"

// A hybrid's reason (required for every kind but continuous and stepped: the footer and the a11y help).
constexpr ParamSpec hybridAttack() noexcept {
    ParamSpec s = hybrid(kFastLoMs, kFastHiMs, kAtkFixed, kFixedMs);
    s.reason = "5 MS FIXED, OR THE FAST POT: 0.1–2 MS";
    return s;
}

float dbu     (float p) noexcept { return p + 22.f; }       // 0 dBFS = +22 dBu (01 §3.1)
float dbuPlain(float d) noexcept { return d - 22.f; }

// Diode 609's knee law: the output-domain W whose input span W (1 + R) / 2 is kKneeInputSpanDb.
float kneeFromRatio(const ParamView& v) noexcept {
    const float s = v[Pid::ratio].plain;
    const float r = s >= 0.f && s < 1.f ? 1.f / (1.f - s) : 2.f;
    return 2.f * kKneeInputSpanDb / (1.f + r);
}

constexpr ParamTable kDiode54Params = [] {
    ParamTable t = allNa("NOT ON THIS CIRCUIT");
    t[Pid::thr]    = { named(stepped(kThr, -18), nullptr, { &dbu, &dbuPlain, "DBU", 0 }) };
    t[Pid::ratio]  = { stepped(kRatio, 1.0f - 1.0f / 3.0f) };
    t[Pid::knee]   = { brief(derived(&kneeFromRatio, Pid::ratio, "= RATIO",
                                     "FAIRLY SOFT: A 10 DB KNEE AT EVERY RATIO"),
                             "SOFT KNEE") };
    t[Pid::atk]    = { hybridAttack() };
    t[Pid::rel]    = { named(stepped(kRel, 200), "RECOVERY") };
    t[Pid::tmode]  = { na(0, "AUTO IS A RECOVERY POSITION") };
    t[Pid::det]    = { locked(kPeak, "PEAK SENSING AFTER THE GAIN ELEMENT (FEEDBACK)") };
    t[Pid::schpf]  = { extSchpf() };
    t[Pid::link]   = { stepped(kLink, 1) };
    t[Pid::stmode] = { extStereo() };
    t[Pid::voice]  = { locked(kDiode, "DIODE-BRIDGE GAIN ELEMENT AND TRANSFORMERS") };
    t[Pid::drive]  = { extDrive() };
    t[Pid::makeup] = { stepped(kGain, 0) };
    t[Pid::mix]    = { extMix() };
    t[Pid::s2thr]  = { named(stepped(kS2Thr, 24), nullptr, { &dbu, &dbuPlain, "DBU", 0 }) };
    t[Pid::s2atk]  = { hybridAttack() };
    t[Pid::s2rel]  = { named(stepped(kRel, 100), "LIM RECOV") };
    return t;
}();

// One recovery switch's AUTO position into its DualRelease slots {tau_Rf, tau_C, tau_Rs}; 0 on the manual positions.
void autoRecovery(uint16_t tag, float* slots) noexcept {
    const bool a = (tag & kTagAuto) != 0;
    slots[0] = a ? kAutoFastMs : 0.f;
    slots[1] = a ? kAutoChargeMs : 0.f;
    slots[2] = a ? kAutoSlowMs : 0.f;
}

void diode54Physical(const ParamView& v, EngineParams& e) noexcept {
    e.topo = kTopoFB;                                       // both stages are feedback (D §2.5 [V S11])
    autoRecovery(v[Pid::rel].tag, &e.m[Diode54::kCompFastSlot]);
    autoRecovery(v[Pid::s2rel].tag, &e.m[Diode54::kLimitFastSlot]);
    // ADR-63: published (closed-loop) attacks -> the loops' open-loop tau
    e.atkTauMs *= 1.f + stage::QuadKnee::loopGain(e.slope);
    e.s2AtkTauMs *= 1.f + stage::QuadKnee::loopGain(Diode54::Stage2::kSlope);
}

DetectorLaw diode54Law(const EngineParams&) noexcept { return DetectorLaw::peak; }

// RECOVERY AUTO: program-dependent between its fast and slow recoveries; every other position is kit's.
TimeSpec diode54Release(const ParamView& v, const EngineParams& e) noexcept {
    TimeSpec t = releaseFromView(v, e);
    if ((v[Pid::rel].tag & kTagAuto) != 0) {
        t.lo = kAutoFastMs / 1000.f;
        t.hi = kAutoPublishedSlowMs / 1000.f;
        t.program = true;
    }
    return t;
}

constexpr InternalSpec kDiode54Int[] = { { "COMP GR", "DB", 0, 30, 1, false }, { "LIMIT GR", "DB", 0, 30, 1, true },
                                         { "LIMIT WINS", "", 0, 1, 0, false } };

} // namespace

extern constexpr ModeDescriptor kDiode54 {
    .key = "diode-54", .name = "DIODE 54", .group = Group::diode, .introducedInStateVersion = 1, .revision = 1,
    .provisional = false,
    .topologyLine = "DIODE BRIDGE · FEEDBACK · COMPRESSOR + LIMITER",
    .specLine = "DIODE 54   DIODE BRIDGE · 1.5–6 · 5 MS / .1–2 MS · .1/.2/.8 S + AUTO · LIMITER +4…+20 DBU",
    .params = kDiode54Params, .physical = &diode54Physical,
    .stage2 = Stage2Kind::sharedElementMax, .linkLaw = LinkLaw::max, .detectorLaw = &diode54Law,
    .hasColour = true, .colourStatic = false, .wantsLookahead = false,
    .rigor = Rigor::character, .family = CurveFamily::custom,
    .attackSpec = &attackFromView, .releaseSpec = &diode54Release, .tailSeconds = &tailFromRelease,
    .ctBudgetNsPerSample = 70, .internals = kDiode54Int };

} // namespace fcdsp::modes
