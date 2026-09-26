// Opto 2A (slot 3, `opto-2a`): the Mode's descriptor, physical() and specs, 01 §10.6 except as noted below. Opto2A.h
// holds the traits and the internals hook, Opto2A.cpp FCDSP_DEFINE_MODE(Opto2A); every [H] constant, its source and its
// fit are in docs/modes/opto-2a.md.
//
// - External linkage (SPRINTS §7 D24): the traits header declares `extern const ModeDescriptor kOpto2A;`.
// - provisional = false (M3, S9): the real traits run, the fidelity rows are blocking. Revision 1: the Mode has not
//   shipped (no modes-ever.tsv row), so fitting it moves no revision.
// - PEAK RED. (fitted, 01 §12 #5): 0...100 <-> +20...-50 dBFS, 0.7 dB per unit (01 §10.6 had +24...-40, 0.64 [H]). PR 0
//   leaves a 0 dBFS peak at 0.1 dB of GR ("0 = no GR", D §5.2) and the default PR 40 (-8 dBFS) holds 0 VU program's
//   -6 dBFS peaks near 4 dB of GR, the LA-2A's usual working range; 01 §10.6's PR 40 (-1.6 dBFS) held them under 2 dB.
// - physical() (S9 lead revision 4): EMPHASIS is the Mode's own R37 shelf (m[0]); the host's `sce` tilt gets a neutral
//   0 dB/oct (it used to receive the same value: a double emphasis). The published times are converted to the cell's
//   open-loop ones (ADR-63): the attack by the loop's speed-up, the fast release by the panel's hold delay. m[1] is the
//   LIMIT flag of 01 §10.6; the EL-panel drive law change it names is the curve's exponent, the loop gain k of the
//   LIMIT step (OptoCellCurve.h), so no policy reads it.
// - The time specs are the kit's: attack {0.010 s, expDb, 5-20 ms, program} and release {0.060 s, t50, 40-80 ms,
//   program} (kit::detail::timeFromView's bands for a locked program nominal).
// - tailSeconds: five slow-release time constants (the memory's tail), 25 s (01 §10.6 had 15 s).

#include "fcdsp/modes/opto-2a/Opto2A.h"

#include "fcdsp/core/Units.h"
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

// PEAK RED. 0...100 <-> +20...-50 dBFS [H, fitted: header], GAIN 0...100 <-> -10...+30 dB [H], R37 set-screw 0...10.
constexpr float kPrTopDb = 20.0f, kPrDbPerUnit = 0.7f;
float prDial   (float thr) noexcept { return (kPrTopDb - thr) / kPrDbPerUnit; }
float prPlain  (float d)   noexcept { return kPrTopDb - kPrDbPerUnit * d; }
float gainDial (float mu)  noexcept { return (mu + 10.f) / 0.4f; }
float gainPlain(float d)   noexcept { return -10.f + 0.4f * d; }
float emDial   (float s)   noexcept { return s * (10.f / 6.f); }
float emPlain  (float d)   noexcept { return d * 0.6f; }

// The T4 cell (docs/modes/opto-2a.md): the slow part's share, its charge and its release (OptoCell's m[2..4]).
constexpr float kSlowShare = 0.5f;              // [H] beta: E §2.7's w = 0.5 ("50 % in 60 ms")
constexpr float kMemoryChargeMs = 5000.0f;      // [H] tau_m: E §2.7's memory, "5 s up"
constexpr float kSlowReleaseMs = 5000.0f;       // [H] tau_s: fitted to "then 1-15 s" (D §2.2)
// ADR-63: the published attack is the closed loop's; the cell runs tau_on = published x (1 + kAttackLoop k).
constexpr float kAttackLoop = 1.0f;             // [H] fitted: dsp.time's attack at 10 ms (COMP)
// The published release (60 ms to 50 %) includes the panel's hold (OptoSense): tau_f = published tau x kReleaseFit.
constexpr float kReleaseFit = 0.85f;            // [H] fitted: dsp.time's release at 60 ms to 50 %

// COMP / LIMIT: nominal 3:1 / 10:1 [C: 4:1] (D §2.2).
constexpr Step kRatio[] = { { 0.6667f, "COMP", "COMPRESS" }, { 0.90f, "LIMIT", "LIMIT" } };
constexpr Step kT4[]    = { { 0, "T4", "T4 CELL" } };
constexpr Step kTube[]  = { { 0, "TUBE", "TUBE + TRANSFORMERS" } };

constexpr ParamTable kOpto2AParams = [] {
    ParamTable t = allNa("NOT ON THIS CIRCUIT");
    t[Pid::thr]    = { named(cont(-50, 20, -8.f /*PR 40*/), "PEAK RED.",
                             { &prDial, &prPlain, "", 0, /*invert*/true }) };
    t[Pid::ratio]  = { stepped(kRatio, 0.6667f) };
    t[Pid::knee]   = { na(6, "THE KNEE IS THE T4 CELL'S; SEE THE CURVE") };
    t[Pid::range]  = { na(60, "NO GAIN-REDUCTION LIMIT INSIDE THIS FEEDBACK LOOP") };
    t[Pid::atk]    = { prog(locked(10, "T4 CELL: ABOUT 10 MS ON AVERAGE, PROGRAM-DEPENDENT")) };
    t[Pid::rel]    = { prog(withLaw(locked(60, "T4 CELL: 60 MS TO 50 %, THEN 1–15 S BY PROGRAM HISTORY", "AUTO"),
                                    TimeLaw::t50)) };
    t[Pid::tmode]  = { na(0, "NO TIME CONTROLS ON THIS CIRCUIT") };
    t[Pid::det]    = { locked(kT4, "THE OPTICAL CELL SENSES THE OUTPUT (FEEDBACK)") };
    t[Pid::schpf]  = { extSchpf() };
    t[Pid::sce]    = { named(cont(0, 6, 0), "EMPHASIS", { &emDial, &emPlain, "", 1 }) };   // D §2.2 [V S31] R37
    t[Pid::link]   = { stepped(kDualLink, 1) };
    t[Pid::stmode] = { extStereo() };
    t[Pid::voice]  = { locked(kTube, "TUBE LINE AMP AND TRANSFORMERS") };
    t[Pid::drive]  = { extDrive() };
    t[Pid::makeup] = { named(cont(-10, 30, 6.f /*GAIN 40*/), "GAIN", { &gainDial, &gainPlain, "", 0 }) };
    t[Pid::mix]    = { extMix() };
    return t;
}();

void optoPhysical(const ParamView& v, EngineParams& e) noexcept {
    e.topo = kTopoFB;                               // FeedbackDelayed: safe at the cell's tau (E §2.7), guarded in prepare()
    e.m[Opto2A::kEmphasisSlot] = v[Pid::sce].plain * (10.f / 6.f);   // R37: 0...10 dB below 1 kHz (D §2.2 [V S31]) [H]
    e.sceDbOct = 0.f;                               // the host SC tilt stays neutral: the emphasis is R37's alone
    e.m[Opto2A::kLimitSlot] = v[Pid::ratio].step == 1 ? 1.f : 0.f;   // LIMIT (the drive law: the loop gain k)
    e.m[Opto2A::kShareSlot] = kSlowShare;
    e.m[Opto2A::kChargeSlot] = kMemoryChargeMs;
    e.m[Opto2A::kSlowSlot] = kSlowReleaseMs;
    // ADR-63: the cell's open-loop times. k = S / (1 - S), the loop gain of the ratio step (2 COMP, 9 LIMIT).
    const float k = e.slope < 0.99f ? e.slope / (1.0f - e.slope) : 99.0f;
    e.atkTauMs *= 1.0f + kAttackLoop * (k > 0.0f ? k : 0.0f);
    e.relTauMs *= kReleaseFit;
}

DetectorLaw optoLaw(const EngineParams&) noexcept { return DetectorLaw::custom; }
float optoTail(const EngineParams& e) noexcept                     // the memory's tail: 5 tau_s (s)
{
    return 5.0f * e.m[Opto2A::kSlowSlot] / 1000.0f;
}

constexpr InternalSpec kOptoInt[] = { { "LIGHT", "", 0, 1, 2, false }, { "G FAST", "", 0, 1, 2, false },
                                      { "G SLOW", "", 0, 1, 2, false }, { "MEMORY", "%", 0, 100, 0, true },
                                      { "LDR", "KOHM", 0, 1000, 0, false } };

} // namespace

extern constexpr ModeDescriptor kOpto2A {
    .key = "opto-2a", .name = "OPTO 2A", .group = Group::opto, .introducedInStateVersion = 1, .revision = 1,
    .provisional = false,
    .topologyLine = "OPTICAL · FEEDBACK · PROGRAM-DEPENDENT",
    .specLine = "OPTO 2A   T4 CELL · COMP/LIMIT · ~10 MS · 60 MS→50 %, THEN 1–15 S",
    .params = kOpto2AParams, .physical = &optoPhysical,
    .stage2 = Stage2Kind::none, .linkLaw = LinkLaw::max, .detectorLaw = &optoLaw,
    .hasColour = true, .colourStatic = true, .wantsLookahead = false,
    .rigor = Rigor::character, .family = CurveFamily::custom,
    .attackSpec = &attackFromView,       // {0.010 s, expDb, lo 0.005, hi 0.020, program}
    .releaseSpec = &releaseFromView,     // {0.060 s, t50,   lo 0.040, hi 0.080, program}
    .tailSeconds = &optoTail, .ctBudgetNsPerSample = 60, .internals = kOptoInt };   // S13 H1b (budgets.md)

} // namespace fcdsp::modes
