// Mu 67 (slot 4, `mu-67`): the Mode's descriptor, physical() and specs, from 01 §10.7's sketch, K1 #26 / ADR-56 and
// D §2.3 / §5 (Fairchild 660/670). Mu67.h holds the traits and Mu67.cpp FCDSP_DEFINE_MODE(Mu67); docs/modes/mu-67.md
// lists every fitted [H] constant with its source and the measurements behind it.
//
// - External linkage (SPRINTS §7 D24): the traits header declares `extern const ModeDescriptor kMu67;`.
// - provisional = false (M4, S10): the real traits are installed, so the fidelity rows are blocking spec rows and the
//   goldens can be blessed. revision 1: the Mode has never shipped (tests/fixtures/modes-ever.tsv), so fitting the [H]
//   constants moves no released print hash (K2 #10).
// - TIME (the TC switch) is a `rel` list (01 §10.2 rule: the Fairchild TC goes on rel, whose plain values are strictly
//   increasing); ATTACK is derived from it. TC5 / TC6 are programme-dependent (kTagTc5 / kTagTc6 | kTagProgram): the
//   TcSelector runs MultiStage3 there, with the stage times physical() puts in m[2..6] (Mu67.h).
// - The curve, fitted (01 §10.7 had o_c = 2 + 0.35 knee, S_max = 0.98 - 0.002 knee [H], a feed-forward reading). In
//   the loop the progressive law acts on the OUTPUT overshoot (ProgressiveKnee.h), whose closed-loop local ratio rises
//   much faster with a large asymptote, so both constants were refitted against the hardware's description (D §2.3
//   [V S3/S4]: "a very low ratio, between 1:1 and 2:1 for smaller peaks ... gradually increases to up to 20:1"; DC
//   THRESH "fully anticlockwise gives hard-knee peak limiting, fully clockwise a gentle ratio and knee"):
//       o_c   = kOnsetAtZeroDb + kOnsetPerDb * knee       0.5 dB (DC 0) ... 40.5 dB (DC 10)
//       R_max = kRatioMaxAtZero + kRatioMaxPerDb * knee   20:1 (DC 0, SOS's "up to 20:1" [V S3]) ... 10:1 (DC 10)
//   At the default DC 5 the static curve reads 1.5:1 1 dB over, 1.9:1 at 2 dB, 3.6:1 at 10 dB, 4.9:1 at 20 dB and
//   6.5:1 at 40 dB; DC 0 is 7.7:1 1 dB over and 17:1 at 10 dB (a hard-knee limiter), DC 10 1.2:1 ... 3.9:1
//   (docs/modes/mu-67.md).
//   EngineParams::slope carries S_max (the asymptote; FB loop gain k = S / (1 - S)), not the RATIO slot's value.
// - RATIO is derived from DC THRESH (K1 #26, ADR-56): the local ratio of the Mode's own static FB curve at T_in + 10 dB
//   (muRatioNominal, as a slope S), with kFlagProgram so the slot prints the live EFF ratio (02 §8.1: analysis::
//   localRatio at the operating point).
// - AC THRESH default, fitted (01 §10.7 had 0 dBFS): -18 dBFS, dial 3.4: the operating level at unity INPUT, 0 VU =
//   +4 dBu = -18 dBFS (ADR-59), which is also the host default threshold (Clean's, FET 76's): a fresh Mu 67 compresses
//   programme at 0 VU as the others do. Dial = (thr + 40) / 6.4, not inverted (10 = no compression, D §2.3 [V S4]; the
//   S4 lead ruling: 01 §10.2's matrix cell is corrected).
// - Time constants (ADR-63): the descriptor's TimeSpec is the PUBLISHED closed-loop time (the TC table, D §2.3);
//   physical() converts the attack to the loop's open-loop tau. The progressive loop's gain varies with level (from
//   about k at the step's start to the static point's local gain), so no single 1 + k converts it: the factor is the
//   one that gives the continuous-time loop the published attack on the D2 step (T - 20 -> T + 20 dB, the 63 % point
//   of the GR's dB change), Mu67::attackOpenLoopFactor (a linear loop gives FET 76's 1 + k). The releases are not
//   converted: measured the hardware way (the level drops below the threshold) they run with the loop open.
// - DC THRESH dial 0...10 over knee 0...40 dB (dial = knee / 4); fully anticlockwise (0) is the hard-knee limiter.
// - INPUT (on drive) is the 20 dB input attenuator in 1 dB steps, -20 ... 0 dB, default 0 (D §2.3 [V S3]); character
//   only (level-compensated, like every drive): it drives TubePushPull.
// - SC HPF is the Herchild-precedent extension OFF / 50 / 100 / 200 / 350 Hz (D §5.5), default OFF.
// - LAT/VERT is stmode's universal M/S code (1): the Router encodes lanes 0-1 as lateral (M) and vertical (S), LINK
//   ties them (the Herchild's linked M/S); a change is a kernel-key crossfade (01 §5.5).
// - Link law max (the AGC link ties the two channels' control voltages), rigor character, family custom, detector law
//   custom (the axis is labelled with the active det step, TUBE: K1 #28).
// - Internals (E §7, vari-mu): BIAS (history), EFF RATIO, TC WEIGHT (Mu67.h).
// - Mode-local helpers (the curve laws, the TC table) live here in an unnamed namespace (ModeKit.h is frozen); the
//   tables are namespace-scope constants (no function-local statics, C D12).

#include "fcdsp/modes/mu-67/Mu67.h"

#include "fcdsp/core/FastMath.h"
#include "fcdsp/engine/stages/gain/QuadKnee.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/ModeKit.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include <cstddef>
#include <cstdint>

namespace fcdsp::modes {

extern const ModeDescriptor kMu67;               // also declared by modes/mu-67/Mu67.h

namespace {

using namespace kit;

constexpr Step kTc[] = { { 300, "1", "TC1 0.3 S" }, { 800, "2", "TC2 0.8 S" }, { 2000, "3", "TC3 2 S" },
                         { 5000, "4", "TC4 5 S" },
                         { 10000, "5", "TC5 2/10 S", kTagTc5 | kTagProgram },
                         { 25000, "6", "TC6 .3/10/25 S", kTagTc6 | kTagProgram } };            // D §2.3 [V S3]
// Attack per TC (ms): TC4 0.8 ms per S3 [C: 0.4 per S4], D §8.2.
constexpr float kTcAttackMs[] = { 0.2f, 0.2f, 0.4f, 0.8f, 0.4f, 0.2f };

// The programme networks (MultiStage3, ms) [H, fitted]: tau_R1 / tau_C2 / tau_R2 / tau_C3 / tau_R3. D §2.3's times
// ([V S3]) are the recoveries (t63 of the GR's dB fall, the TIME spec's law) the network gives: after a peak (a burst of
// up to 0.2 s at T + 20 dB), after multiple peaks (2 s of programme, TC6) and after consistently high programme (20 s
// and more). A serial network releases slower than its last stage alone (the stage behind it is still falling), so the
// stage times are fitted to those recoveries, not copied: TC5 2.0 / 10.0 s (6.5 s after 2 s), TC6 0.30 / 10.5 / 25.7 s
// (dsp.tcselector's programme rows; docs/modes/mu-67.md).
struct Network { float rel1, charge2, rel2, charge3, rel3; };
constexpr Network kTc5Network { 2000.f, 3000.f, 7700.f, 0.f, 0.f };              // 2 s peaks / 10 s multiple peaks
constexpr Network kTc6Network { 300.f, 1000.f, 8000.f, 8000.f, 16000.f };        // .3 s / 10 s / 25 s programme

constexpr Step kIn21[] = { { -20, "−20" }, { -19, "−19" }, { -18, "−18" }, { -17, "−17" }, { -16, "−16" },
                           { -15, "−15" }, { -14, "−14" }, { -13, "−13" }, { -12, "−12" }, { -11, "−11" },
                           { -10, "−10" }, { -9, "−9" }, { -8, "−8" }, { -7, "−7" }, { -6, "−6" }, { -5, "−5" },
                           { -4, "−4" }, { -3, "−3" }, { -2, "−2" }, { -1, "−1" }, { 0, "0" } };   // 20 dB, 1 dB
constexpr Step kSt[]   = { { 0, "L/R", "LEFT/RIGHT" }, { 1, "LAT/VERT", "LATERAL/VERTICAL" } };   // AGC (D §2.3)
constexpr Step kLink[] = { { 0, "IND", "INDEPENDENT" }, { 1, "LINK", "LINKED" } };
constexpr Step kHpf[]  = { { 0, "OFF" }, { 50, "50" }, { 100, "100" }, { 200, "200" }, { 350, "350" } };   // Herchild
constexpr Step kTube[] = { { 0, "TUBE", "TUBE RECTIFIER" } };
constexpr Step kTubeVoice[] = { { 0, "TUBE", "6386 PUSH-PULL + TRANSFORMERS" } };

// The progressive curve's constants from DC THRESH (knee plain, dB) [H, fitted] (file comment).
constexpr float kOnsetAtZeroDb = 0.5f;           // o_c at DC 0
constexpr float kOnsetPerDb = 1.0f;              // o_c per dB of knee
constexpr float kRatioMaxAtZero = 20.0f;         // R_max at DC 0
constexpr float kRatioMaxPerDb = -0.25f;         // R_max per dB of knee (10:1 at DC 10)
constexpr float kNominalOverDb = 10.f;           // the nominal ratio is the local one at T_in + 10 dB
constexpr float kThrDefault = -18.f;             // [H, fitted] AC THRESH default: 0 VU (file comment)

constexpr float onsetDb(float knee) noexcept { return kOnsetAtZeroDb + kOnsetPerDb * knee; }
constexpr float maxSlope(float knee) noexcept { return 1.f - 1.f / (kRatioMaxAtZero + kRatioMaxPerDb * knee); }

// The static FB curve's local ratio at T_in + 10 dB for a DC THRESH (Mu67.h's scalar solve; no libm, 01 §2.2).
float nominalRatio(float knee) noexcept {
    const float k = stage::QuadKnee::loopGain(maxSlope(knee));
    const float oc = onsetDb(knee);
    return Mu67::localRatioAtOutput(Mu67::outputOverDb(kNominalOverDb, k, oc), k, oc);
}

// The nominal RATIO (S) for the current DC THRESH (file comment).
float muRatioNominal(const ParamView& v) noexcept {
    return 1.f - 1.f / nominalRatio(v[Pid::knee].plain);
}

std::size_t tcStep(const ParamView& v) noexcept {
    const int s = v[Pid::rel].step;
    return static_cast<std::size_t>(s < 0 ? 1 : s);
}

float tcAttack(const ParamView& v) noexcept { return kTcAttackMs[tcStep(v)]; }

float acDial (float thr) noexcept { return (thr + 40.f) / 6.4f; }   // AC THRESH 0…10 ↔ −40…+24 dBFS [H]; 10 = none
float acPlain(float d)   noexcept { return d * 6.4f - 40.f; }
float dcDial (float k)   noexcept { return k * 0.25f; }             // DC THRESH 0…10 ↔ knee 0…40 dB [H]
float dcPlain(float d)   noexcept { return d * 4.f; }

constexpr ParamTable kMu67Params = [] {
    ParamTable t = allNa("NOT ON THIS CIRCUIT");
    t[Pid::thr]    = { named(cont(-40, 24, kThrDefault), "AC THRESH", { &acDial, &acPlain, "", 1 }) };
    t[Pid::ratio]  = { prog(derived(&muRatioNominal, Pid::knee, "PROGRESSIVE",
                                    "RATIO RISES WITH LEVEL (REMOTE-CUTOFF TUBE)")) };
    t[Pid::knee]   = { named(cont(0, 40, 20), "DC THRESH", { &dcDial, &dcPlain, "", 1 }) };
    t[Pid::atk]    = { derived(&tcAttack, Pid::rel, "= TIME", "ATTACK IS SET BY THE TIME CONSTANT") };
    t[Pid::rel]    = { named(stepped(kTc, 800), "TIME") };
    t[Pid::tmode]  = { na(0, "THE TIME CONSTANT SWITCH SETS ATTACK AND RELEASE") };
    t[Pid::det]    = { locked(kTube, "TUBE SIDE-CHAIN RECTIFIER, SENSING THE OUTPUT (FEEDBACK)") };
    t[Pid::schpf]  = { ext(stepped(kHpf, 0)) };
    t[Pid::link]   = { stepped(kLink, 1) };
    t[Pid::stmode] = { stepped(kSt, 0) };                                        // LAT/VERT = the universal M/S code
    t[Pid::voice]  = { locked(kTubeVoice, "REMOTE-CUTOFF TUBES AND TRANSFORMERS") };
    t[Pid::drive]  = { named(stepped(kIn21, 0), "INPUT") };                      // character only (D §2.3 [V S3])
    t[Pid::makeup] = { ext(cont(-12, 12, 0)) };                                  // the original has none [U]
    t[Pid::mix]    = { extMix() };
    return t;
}();

void muPhysical(const ParamView& v, EngineParams& e) noexcept {
    const float knee = v[Pid::knee].plain;
    e.topo = kTopoFB;                                   // FeedbackZdf<ProgressiveKnee> (E §2.7)
    e.slope = maxSlope(knee);                           // the curve's asymptote S_max (file comment)
    e.m[Mu67::kOnsetSlot] = kOnsetAtZeroDb;             // o_c = m[0] + m[1] * kneeDb (ProgressiveKnee)
    e.m[Mu67::kOnsetPerDbSlot] = kOnsetPerDb;
    // TC5 / TC6: the programme network's stage times (TcSelector runs MultiStage3 on their tags)
    const std::size_t tc = tcStep(v);
    if (tc >= 4) {
        const Network& n = tc == 4 ? kTc5Network : kTc6Network;
        e.m[Mu67::kRel1Slot] = n.rel1;
        e.m[Mu67::kCharge2Slot] = n.charge2;
        e.m[Mu67::kRel2Slot] = n.rel2;
        e.m[Mu67::kCharge3Slot] = n.charge3;
        e.m[Mu67::kRel3Slot] = n.rel3;
    }
    // ADR-63: the published closed-loop attack -> the loop's open-loop tau (file comment)
    e.atkTauMs = kTcAttackMs[tc] * Mu67::attackOpenLoopFactor(stage::QuadKnee::loopGain(e.slope), onsetDb(knee));
}

DetectorLaw muLaw(const EngineParams&) noexcept { return DetectorLaw::custom; }

constexpr InternalSpec kMuInt[] = { { "BIAS", "V", 0, 20, 1, true }, { "EFF RATIO", "", 1, 30, 1, false },
                                    { "TC WEIGHT", "", 0, 1, 2, false } };

} // namespace

extern constexpr ModeDescriptor kMu67 {
    .key = "mu-67", .name = "MU 67", .group = Group::varimu, .introducedInStateVersion = 1, .revision = 1,
    .provisional = false,
    .topologyLine = "VARIABLE-MU TUBE · FEEDBACK · PROGRESSIVE",
    .specLine = "MU 67   VARIABLE-MU · PROGRESSIVE RATIO · TC 1–6: 0.3–25 S · LAT/VERT",
    .params = kMu67Params, .physical = &muPhysical,
    .stage2 = Stage2Kind::none, .linkLaw = LinkLaw::max, .detectorLaw = &muLaw,
    .hasColour = true, .colourStatic = false, .wantsLookahead = false,
    .rigor = Rigor::character, .family = CurveFamily::custom,
    .attackSpec = &attackFromView, .releaseSpec = &releaseFromView, .tailSeconds = &tailFromRelease,
    .ctBudgetNsPerSample = 60, .internals = kMuInt };

} // namespace fcdsp::modes

// Traits (01 §10.7, final; Mu67.h): PeakLog; FeedbackZdf<ProgressiveKnee>; LinkMax; TcSelector (TC1-4
// SmoothBranching, TC5/TC6 MultiStage3, max of roots, with the S10 sub-ulp carry); NoStage2; TubePushPull; Flat;
// kTopologies = FB. Every DC THRESH is monotone (fb.monotone; dsp.progressiveknee sweeps the knob).
