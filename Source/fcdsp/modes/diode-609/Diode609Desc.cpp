// Diode 609 (slot 5, `diode-609`): the Mode's descriptor, physical() and specs, from 01 §10.7's sketch and D §2.5 / §5
// (Neve 33609, with the 2254's steps where the 33609's are unverified [U]). Diode609.h holds the traits and Diode609.cpp
// FCDSP_DEFINE_MODE(Diode609); docs/modes/diode-609.md lists every [H] constant with its source.
//
// - External linkage (SPRINTS §7 D24): the traits header declares `extern const ModeDescriptor kDiode609;`.
// - provisional = false (M5, S10): the real traits are installed, so the fidelity rows are blocking spec rows and the
//   goldens can be blessed. revision 1: the Mode has never shipped (tests/fixtures/modes-ever.tsv), so fitting the [H]
//   constants below moves no released print hash (K2 #10).
// - THRESHOLD and LIMIT THRESHOLD are dBu switches: plain dBFS = dBu - 22 (0 dBFS = +22 dBu, 01 §3.1), shown in DBU
//   through a DisplayMap; stage 2 is OFF above +15 dBu, at the host range end 24 (kS2Off, K2 #20). The OFF step carries
//   no kTagOff (ADR-62: kTagOff means only "main gain reduction disabled"; crossmode.no_off).
// - KNEE (M5; was locked 5): derived from RATIO. The 33609's compressor is "soft and progressive, so the true ratio is
//   only attained >5dB above the threshold", the 2254's knee "fairly soft ... over a 10dB range" (D §2.5). QuadKnee's
//   FB knee W is defined at the output (QuadKnee.h), so in input dB it spans T - W/2 ... T + (1 + k) W/2, a span of
//   W (1 + R) / 2 with R = 1 + k: the knee whose INPUT span is 10 dB is W = 20 / (1 + R) (1.5:1 8 dB, 2:1 6.67, 3:1 5,
//   4:1 4, 6:1 2.86), and the true ratio is reached 6 ... 8.6 dB over the threshold.
// - A1 / A2 (the self-adjusting recoveries, "a1 = 100 ms/2000 ms, a2 = 50 ms/5000 ms", D §2.5 [V S10]) are RELEASE
//   steps tagged kTagAuto / kTagAuto2 (01 §10.2), plus kTagProgram. Both release switches carry them and
//   EngineParams::tags is one OR over every parameter, so physical() routes each switch's AUTO position through its own
//   m[] slots (ADR-64; Diode609.h), where DualRelease reads its three times and each AutoSwitch its selection.
// - ATTACK SLOW also adds the 33609's reduced sensitivity below 100 Hz: m[0] = 100 (Hz) for the Mode-internal SC
//   high-pass (SlowHp); the host SC HPF stays the standard extension.
// - Time constants (ADR-63): the TimeSpecs declare the published closed-loop times; physical() converts each attack to
//   its loop's open-loop tau, tau = t_published (1 + k), k the loop gain above the knee (the compressor's
//   QuadKnee::loopGain(slope) = R - 1; the limiter's k = 99, SharedElementMax::kSlope). Releases are not converted: the
//   level drops below the threshold and the loop opens (FET 76's reasoning, docs/modes/fet-76.md).
// - Defaults (DW, kept): THRESHOLD +4 dBu (-18 dBFS, 0 VU), RATIO 2:1, ATTACK FAST, RELEASE 400 ms, GAIN 0 dB, LINK
//   STEREO, LIMIT OFF, LIMIT ATTACK FAST, LIMIT RELEASE 100 ms.
// - Rigor character (a diode bridge with transformer colour and program-dependent recovery), family custom (the
//   progressive knee). Link law max (the side chains are combined: "compressed by the same amount", D §2.5).
// - Mode-local helpers (the dial maps, the knee law, the release spec) live here in an unnamed namespace (ModeKit.h is
//   frozen); their tables are namespace-scope constants (no function-local statics in fcdsp, C D12).

#include "fcdsp/modes/diode-609/Diode609.h"

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

// THRESHOLD -20 ... +10 dBu in 2 dB steps (2254 [V S11]; the 33609's steps are [U]) as dBFS = dBu - 22.
constexpr Step kThr[] = { { -42, "−20" }, { -40, "−18" }, { -38, "−16" }, { -36, "−14" }, { -34, "−12" },
                          { -32, "−10" }, { -30, "−8" }, { -28, "−6" }, { -26, "−4" }, { -24, "−2" }, { -22, "0" },
                          { -20, "2" }, { -18, "4" }, { -16, "6" }, { -14, "8" }, { -12, "10" } };
constexpr Step kRatio[] = { { 1.0f - 1.0f / 1.5f, "1.5", "1.5:1" }, { 0.5f, "2", "2:1" },
                            { 1.0f - 1.0f / 3.0f, "3", "3:1" }, { 0.75f, "4", "4:1" },
                            { 1.0f - 1.0f / 6.0f, "6", "6:1" } };                              // D §2.5 [V~]
constexpr Step kAtk[]   = { { 3, "FAST", "FAST ~3 MS" }, { 6, "SLOW", "SLOW ~6 MS" } };         // SLOW: + SC high-pass
constexpr Step kRel[]   = { { 100, ".1", "0.1 S" }, { 400, ".4", "0.4 S" }, { 800, ".8", "0.8 S" },
                            { 1500, "1.5", "1.5 S" },
                            { 2000, "A1", "AUTO 1 100 MS/2 S", kTagAuto | kTagProgram },
                            { 5000, "A2", "AUTO 2 50 MS/5 S", kTagAuto2 | kTagProgram } };      // D §2.5 [V S10]
constexpr Step kGain[]  = { { 0, "0" }, { 2, "2" }, { 4, "4" }, { 6, "6" }, { 8, "8" }, { 10, "10" }, { 12, "12" },
                            { 14, "14" }, { 16, "16" }, { 18, "18" }, { 20, "20" } };           // 2254 [V S11]
constexpr Step kLink[]  = { { 0, "DUAL", "DUAL MONO" }, { 1, "STEREO", "STEREO (SIDE CHAINS LINKED)" } };
// LIMIT THRESHOLD +4 ... +15 dBu in 1 dB steps [U steps] as dBFS = dBu - 22, then OFF at the host range end.
constexpr Step kS2Thr[] = { { -18, "4" }, { -17, "5" }, { -16, "6" }, { -15, "7" }, { -14, "8" }, { -13, "9" },
                            { -12, "10" }, { -11, "11" }, { -10, "12" }, { -9, "13" }, { -8, "14" }, { -7, "15" },
                            { 24, "OFF", "LIMITER OUT" } };
constexpr Step kS2Atk[] = { { 2, "FAST", "FAST ~2 MS" }, { 4, "SLOW", "SLOW ~4 MS" } };         // D §2.5 [V S10]
constexpr Step kS2Rel[] = { { 50, ".05", "50 MS" }, { 100, ".1", "0.1 S" }, { 200, ".2", "0.2 S" },
                            { 800, ".8", "0.8 S" },
                            { 2000, "A1", "AUTO 1 100 MS/2 S", kTagAuto | kTagProgram },
                            { 5000, "A2", "AUTO 2 50 MS/5 S", kTagAuto2 | kTagProgram } };      // [U list] D §8.6
constexpr Step kPeak[]  = { { 0, "PEAK", "PEAK · FEEDBACK" } };
constexpr Step kDiode[] = { { 0, "DIODE", "DIODE BRIDGE + TRANSFORMERS" } };

// [V S10] The self-adjusting recoveries: the fast and the slow release of each (ms), D §2.5 "a1 = 100 ms/2000 ms and
// a2 = 50 ms/5000 ms", both switches (the limiter's a1/a2 are the same circuit's).
constexpr float kA1FastMs = 100.f, kA1SlowMs = 2000.f;
constexpr float kA2FastMs = 50.f, kA2SlowMs = 5000.f;
// [H] The slow path's charge time: how long program must hold the GR before the recovery turns slow ("self-adjusting",
// the 2254's "attack time adjusted automatically along with the release" is not modelled). Unpublished; 0.5 s puts the
// turn inside a sustained phrase and leaves a drum hit (< 0.1 s) on the fast recovery (docs/modes/diode-609.md).
constexpr float kAutoChargeMs = 500.f;
// [V S10] ATTACK SLOW: "reduced sensitivity to <100Hz signals", a first-order high-pass at 100 Hz (D §2.5).
constexpr float kSlowHpHz = 100.f;
// The knee's input span (dB), D §2.5 ("over a 10dB range"); the knee law below.
constexpr float kKneeInputSpanDb = 10.f;

float dbu     (float p) noexcept { return p + 22.f; }       // 0 dBFS = +22 dBu (01 §3.1)
float dbuPlain(float d) noexcept { return d - 22.f; }

// The output-domain knee W whose input span W (2 + k) / 2 = W (1 + R) / 2 is kKneeInputSpanDb (file comment). The
// ratio's plain value is the slope S = 1 - 1/R; S outside [0, 1) (never on a detent) takes the 2:1 knee.
float kneeFromRatio(const ParamView& v) noexcept {
    const float s = v[Pid::ratio].plain;
    const float r = s >= 0.f && s < 1.f ? 1.f / (1.f - s) : 2.f;
    return 2.f * kKneeInputSpanDb / (1.f + r);
}

constexpr ParamTable kDiode609Params = [] {
    ParamTable t = allNa("NOT ON THIS CIRCUIT");
    t[Pid::thr]    = { named(stepped(kThr, -18), nullptr, { &dbu, &dbuPlain, "DBU", 0 }) };
    t[Pid::ratio]  = { stepped(kRatio, 0.5f) };
    t[Pid::knee]   = { brief(derived(&kneeFromRatio, Pid::ratio, "= RATIO",
                                     "PROGRESSIVE: THE TRUE RATIO ONLY >5 DB OVER, A 10 DB KNEE AT EVERY RATIO"),
                             "PROGRESSIVE KNEE") };
    t[Pid::atk]    = { stepped(kAtk, 3) };
    t[Pid::rel]    = { stepped(kRel, 400) };
    t[Pid::tmode]  = { na(0, "A1 AND A2 ARE RELEASE POSITIONS") };
    t[Pid::det]    = { locked(kPeak, "PEAK SENSING AFTER THE GAIN ELEMENT (FEEDBACK)") };
    t[Pid::schpf]  = { extSchpf() };
    t[Pid::link]   = { stepped(kLink, 1) };
    t[Pid::stmode] = { extStereo() };
    t[Pid::voice]  = { locked(kDiode, "DIODE-BRIDGE GAIN ELEMENT AND TRANSFORMERS") };
    t[Pid::drive]  = { extDrive() };
    t[Pid::makeup] = { stepped(kGain, 0) };
    t[Pid::mix]    = { extMix() };
    t[Pid::s2thr]  = { named(stepped(kS2Thr, 24), nullptr, { &dbu, &dbuPlain, "DBU", 0 }) };
    t[Pid::s2atk]  = { stepped(kS2Atk, 2) };
    t[Pid::s2rel]  = { stepped(kS2Rel, 100) };
    return t;
}();

// One release switch's AUTO position into its DualRelease slots {tau_Rf, tau_C, tau_Rs}; 0 on the manual positions
// (the AutoSwitch then runs SmoothBranching on the switch's own time).
void autoRelease(uint16_t tag, float* slots) noexcept {
    const bool a2 = (tag & kTagAuto2) != 0, a1 = (tag & kTagAuto) != 0;
    slots[0] = a2 ? kA2FastMs : (a1 ? kA1FastMs : 0.f);
    slots[1] = a1 || a2 ? kAutoChargeMs : 0.f;
    slots[2] = a2 ? kA2SlowMs : (a1 ? kA1SlowMs : 0.f);
}

void diodePhysical(const ParamView& v, EngineParams& e) noexcept {
    e.topo = kTopoFB;                                       // both stages are feedback (D §2.5 [V S11])
    e.m[Diode609::kSlowHpSlot] = v[Pid::atk].step == 1 ? kSlowHpHz : 0.f;
    autoRelease(v[Pid::rel].tag, &e.m[Diode609::kCompFastSlot]);
    autoRelease(v[Pid::s2rel].tag, &e.m[Diode609::kLimitFastSlot]);
    // ADR-63: published (closed-loop) attacks -> the loops' open-loop tau (file comment)
    e.atkTauMs *= 1.f + stage::QuadKnee::loopGain(e.slope);
    e.s2AtkTauMs *= 1.f + stage::QuadKnee::loopGain(Diode609::Stage2::kSlope);
}

DetectorLaw diodeLaw(const EngineParams&) noexcept { return DetectorLaw::peak; }

// RELEASE: A1 / A2 are program-dependent between their published fast and slow recoveries (the D2 range, C §5.3);
// every other position is kit::releaseFromView's.
TimeSpec diodeRelease(const ParamView& v, const EngineParams& e) noexcept {
    TimeSpec t = releaseFromView(v, e);
    const uint16_t tag = v[Pid::rel].tag;
    if ((tag & (kTagAuto | kTagAuto2)) != 0) {
        const bool a2 = (tag & kTagAuto2) != 0;
        t.lo = (a2 ? kA2FastMs : kA1FastMs) / 1000.f;
        t.hi = (a2 ? kA2SlowMs : kA1SlowMs) / 1000.f;
        t.program = true;
    }
    return t;
}

constexpr InternalSpec kDiodeInt[] = { { "COMP GR", "DB", 0, 30, 1, false }, { "LIMIT GR", "DB", 0, 30, 1, true },
                                       { "LIMIT WINS", "", 0, 1, 0, false } };

} // namespace

extern constexpr ModeDescriptor kDiode609 {
    .key = "diode-609", .name = "DIODE 609", .group = Group::diode, .introducedInStateVersion = 1, .revision = 1,
    .provisional = false,
    .topologyLine = "DIODE BRIDGE · FEEDBACK · COMPRESSOR + LIMITER",
    .specLine = "DIODE 609   DIODE BRIDGE · 1.5–6 · FAST/SLOW · .1–1.5 S + A1/A2 · LIMITER +4…+15 DBU",
    .params = kDiode609Params, .physical = &diodePhysical,
    .stage2 = Stage2Kind::sharedElementMax, .linkLaw = LinkLaw::max, .detectorLaw = &diodeLaw,
    .hasColour = true, .colourStatic = false, .wantsLookahead = false,
    .rigor = Rigor::character, .family = CurveFamily::custom,
    .attackSpec = &attackFromView, .releaseSpec = &diodeRelease, .tailSeconds = &tailFromRelease,
    .ctBudgetNsPerSample = 70, .internals = kDiodeInt };

} // namespace fcdsp::modes

// Traits (Diode609.h, 01 §10.7): PeakLog; QuadKnee in FB (the progressive knee, derived from RATIO); LinkMax;
// AutoSwitch<SmoothBranching, DualRelease> for A1/A2 inside the FB solve (max of the fast and slow roots, 01 §5.3);
// Stage2 SharedElementMax<PeakLog, AutoSwitch<SmoothBranching, DualRelease>> on the aux lanes (E §3.3), switched on and
// off through the 20 ms s2On_ ramp (01 §5.1), never a step; ScShape SlowHp (m[0]); Colour DiodeBridge; kTopologies = FB.
