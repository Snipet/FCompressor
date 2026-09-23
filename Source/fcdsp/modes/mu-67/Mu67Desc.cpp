// Mu 67 (slot 4, `mu-67`): the Mode's descriptor, physical() and specs, from 01 §10.7's sketch, K1 #26 / ADR-56 and
// D §2.3 / §5 (Fairchild 660/670). Mu67.h holds the (provisional, generic) traits and Mu67.cpp FCDSP_DEFINE_MODE(Mu67).
//
// - External linkage (SPRINTS §7 D24): the traits header declares `extern const ModeDescriptor kMu67;`.
// - provisional = true (descriptor wave, ADR-30); revision 1 is spelled out.
// - TIME (the TC switch) is a `rel` list (01 §10.2 rule: the Fairchild TC goes on rel, whose plain values are strictly
//   increasing); ATTACK is derived from it. TC5 / TC6 are program-dependent (kTagTc5 / kTagTc6 | kTagProgram).
// - RATIO is derived from DC THRESH (K1 #26): the nominal slope of the progressive curve (E §2.2,
//   r^(o) = S_max·(o − o_c·(1 − e^(−o/o_c))), local slope S_max·(1 − e^(−o/o_c))) at 10 dB over the threshold, with
//   kFlagProgram so the slot prints the live EFF ratio (02 §8.1). physical() hands the curve constants to the
//   ProgressiveKnee through m[0] (o_c, dB) and m[1] (S_max), 01 §10.7 [H].
// Choices where the sketch is silent or self-contradictory:
// - AC THRESH: 01 §10.7 writes "dial 0…10, invert" next to "(10 = no compression, D §2.3 [V S4])". Dial 10 = no
//   compression is the highest threshold, so the dial rises with the threshold and the registry lint (invert must match
//   the map's slope) needs invert = false; the verified hardware scale wins: dial = (thr + 40) / 6.4, not inverted.
//   Handoff: the 01 §10.2 matrix cell "inverted" should read "0–10 (10 = none)".
// - DC THRESH dial 0…10 over knee 0…40 dB (dial = knee / 4); fully anticlockwise (0) is the hard-knee peak limiter,
//   fully clockwise the gentle ratio and knee (D §2.3 [V S4]).
// - INPUT (on drive) is the 20 dB input attenuator in 1 dB steps, −20 … 0 dB, default 0 (D §2.3 [V S3]); character
//   only (level-compensated, like every drive).
// - SC HPF is the Herchild-precedent extension OFF / 50 / 100 / 200 / 350 Hz (D §5.5), default OFF.
// - Link law max (the AGC link ties the two channels' control voltages), rigor character, family custom, detector law
//   custom (the axis is labelled with the active det step, TUBE: K1 #28).
// - Internals (E §7, vari-mu): BIAS (history), EFF RATIO, TC WEIGHT.

#include "fcdsp/core/FastMath.h"
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
constexpr Step kIn21[] = { { -20, "−20" }, { -19, "−19" }, { -18, "−18" }, { -17, "−17" }, { -16, "−16" },
                           { -15, "−15" }, { -14, "−14" }, { -13, "−13" }, { -12, "−12" }, { -11, "−11" },
                           { -10, "−10" }, { -9, "−9" }, { -8, "−8" }, { -7, "−7" }, { -6, "−6" }, { -5, "−5" },
                           { -4, "−4" }, { -3, "−3" }, { -2, "−2" }, { -1, "−1" }, { 0, "0" } };   // 20 dB, 1 dB
constexpr Step kSt[]   = { { 0, "L/R", "LEFT/RIGHT" }, { 1, "LAT/VERT", "LATERAL/VERTICAL" } };   // AGC (D §2.3)
constexpr Step kLink[] = { { 0, "IND", "INDEPENDENT" }, { 1, "LINK", "LINKED" } };
constexpr Step kHpf[]  = { { 0, "OFF" }, { 50, "50" }, { 100, "100" }, { 200, "200" }, { 350, "350" } };   // Herchild
constexpr Step kTube[] = { { 0, "TUBE", "TUBE RECTIFIER" } };
constexpr Step kTubeVoice[] = { { 0, "TUBE", "6386 PUSH-PULL + TRANSFORMERS" } };

// The progressive curve's constants from DC THRESH (knee plain, dB) [H] (01 §10.7).
constexpr float curveOnsetDb(float knee) noexcept { return 2.f + 0.35f * knee; }    // o_c
constexpr float curveMaxSlope(float knee) noexcept { return 0.98f - 0.002f * knee; }   // S_max
constexpr float kNominalOverDb = 10.f;                  // the nominal ratio is the local one at T_in + 10 dB
constexpr float kLog2E = 1.44269504f;

// The nominal RATIO (S) for the current DC THRESH: S_max·(1 − e^(−10 / o_c)), through fcdsp::exp2 (no libm, 01 §2.2).
float muRatioNominal(const ParamView& v) noexcept {
    const float knee = v[Pid::knee].plain;
    return curveMaxSlope(knee) * (1.f - fcdsp::exp2(-kNominalOverDb * kLog2E / curveOnsetDb(knee)));
}

float tcAttack(const ParamView& v) noexcept {
    const int s = v[Pid::rel].step;
    return kTcAttackMs[static_cast<std::size_t>(s < 0 ? 1 : s)];
}

float acDial (float thr) noexcept { return (thr + 40.f) / 6.4f; }   // AC THRESH 0…10 ↔ −40…+24 dBFS [H]; 10 = none
float acPlain(float d)   noexcept { return d * 6.4f - 40.f; }
float dcDial (float k)   noexcept { return k * 0.25f; }             // DC THRESH 0…10 ↔ knee 0…40 dB [H]
float dcPlain(float d)   noexcept { return d * 4.f; }

constexpr ParamTable kMu67Params = [] {
    ParamTable t = allNa("NOT ON THIS CIRCUIT");
    t[Pid::thr]    = { named(cont(-40, 24, 0), "AC THRESH", { &acDial, &acPlain, "", 1 }) };
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
    e.m[0] = curveOnsetDb(knee);                        // o_c, dB [H]
    e.m[1] = curveMaxSlope(knee);                       // S_max [H]
    // TC5 / TC6 (kTagTc5 / kTagTc6 in e.tags) select TcSelector's MultiStage3 branches (01 §10.7).
}

DetectorLaw muLaw(const EngineParams&) noexcept { return DetectorLaw::custom; }

constexpr InternalSpec kMuInt[] = { { "BIAS", "V", 0, 20, 1, true }, { "EFF RATIO", "", 1, 30, 1, false },
                                    { "TC WEIGHT", "", 0, 1, 2, false } };

} // namespace

extern constexpr ModeDescriptor kMu67 {
    .key = "mu-67", .name = "MU 67", .group = Group::varimu, .introducedInStateVersion = 1, .revision = 1,
    .provisional = true,
    .topologyLine = "VARIABLE-MU TUBE · FEEDBACK · PROGRESSIVE",
    .specLine = "MU 67   VARIABLE-MU · PROGRESSIVE RATIO · TC 1–6: 0.3–25 S · LAT/VERT",
    .params = kMu67Params, .physical = &muPhysical,
    .stage2 = Stage2Kind::none, .linkLaw = LinkLaw::max, .detectorLaw = &muLaw,
    .hasColour = true, .colourStatic = false, .wantsLookahead = false,
    .rigor = Rigor::character, .family = CurveFamily::custom,
    .attackSpec = &attackFromView, .releaseSpec = &releaseFromView, .tailSeconds = &tailFromRelease,
    .ctBudgetNsPerSample = 60, .internals = kMuInt };

} // namespace fcdsp::modes

// Traits (01 §10.7, final): FeedbackZdf<ProgressiveKnee>; TcSelector exposes TC1–4 as SmoothBranching {α·r1, 1−α} and
// TC5/TC6 as MultiStage3 branches, solved as the max of roots (01 §5.2); fb.monotone must hold for every DC THRESH;
// VarimuColour (odd-dominant, growing with GR); kTopologies = FB. Open item (S3 lead revision 4): an FB release cannot
// carry sub-ulp steps through FbAffine yet (≈ 0.07 dB stall on a 3 s FB release at 48 kHz): extend FbAffine with a base
// GR before this Mode's task.
