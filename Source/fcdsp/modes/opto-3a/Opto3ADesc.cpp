// Opto 3A (slot 10, `opto-3a`): the Mode's descriptor, physical() and specs (D §2.2 the UREI LA-3A, M03; D §5.2–5.6
// row M03). Opto3A.h holds the traits and the internals hook, Opto3A.cpp FCDSP_DEFINE_MODE(Opto3A); every [H] constant,
// its source and its fit are in docs/modes/opto-3a.md. The cell, the laws and the dials are Opto 2A's
// (Opto2ADesc.cpp); what differs is below.
//
// - External linkage (SPRINTS §7 D24): the traits header declares `extern const ModeDescriptor kOpto3A;`.
// - provisional = false: the Mode's own traits are installed, so the fidelity rows are blocking spec rows and the
//   goldens can be blessed. revision 1: the Mode has never shipped (tests/fixtures/modes-ever.tsv).
// - ATTACK: "1.5 ms or less, program dependent" (D §2.2 [V S32]); the kit's band for a locked program nominal gives
//   0.75–3 ms. RELEASE: stage 1 is 60 ms to 50 % as on the LA-2A; after that "quick with occasional light compression
//   ... slower when driven hard continuously" (D §2.2 [V~ S32]): the memory charges in 2 s and lets go in 2 s [H]
//   (Opto 2A: 5 s and 5 s), so a short passage leaves little of it and a long, deep one a tail of a few seconds.
// - COMP / LIMIT: the two "sound virtually indistinguishable unless very heavy compression is used" (D §2.2 [V S32]).
//   The cell's law ties the knee to the exponent k (OptoCellCurve.h), so LIMIT is 4:1 (k 3) against COMP's 3:1 (k 2)
//   [H]: 0.52 dB apart at the threshold and 0.41 dB at T + 10, parting only under heavy GR (2.37 dB at T + 30; Opto
//   2A's LIMIT is 10:1).
// - HF SENS is the rear side-chain pot (D §2.2 [V S32]), the R37 shelf in m[0] (0–10 dB below 1 kHz); the host's `sce`
//   tilt stays neutral, as in Opto 2A.
// - VOICE is locked CLASS A + TRANSFORMERS, ClassATransformer (TubeTransformer.h: the tube voice's odd term, a smaller
//   even term); DRIVE is the standard extension.
// - Not modelled: the reissue's -20 dB input pad (PEAK RED.'s range covers it), the meter's OUTPUT position.
// - Mode-local helpers live here in an unnamed namespace (ModeKit.h is frozen); the tables are namespace-scope constants
//   (no function-local statics, C D12).

#include "fcdsp/modes/opto-3a/Opto3A.h"

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

// PEAK RED. 0...100 <-> +20...-50 dBFS, GAIN 0...100 <-> -10...+30 dB, HF SENS 0...10: Opto 2A's dials.
constexpr float kPrTopDb = 20.0f, kPrDbPerUnit = 0.7f;
float prDial   (float thr) noexcept { return (kPrTopDb - thr) / kPrDbPerUnit; }
float prPlain  (float d)   noexcept { return kPrTopDb - kPrDbPerUnit * d; }
float gainDial (float mu)  noexcept { return (mu + 10.f) / 0.4f; }
float gainPlain(float d)   noexcept { return -10.f + 0.4f * d; }
float hfDial   (float s)   noexcept { return s * (10.f / 6.f); }
float hfPlain  (float d)   noexcept { return d * 0.6f; }

// The T4 cell (docs/modes/opto-3a.md): the slow part's share, its charge and its release (OptoCell's m[2..4]).
constexpr float kSlowShare = 0.5f;              // [H] beta: as Opto 2A (the GR passes 50 % on the fast path)
constexpr float kMemoryChargeMs = 2000.0f;      // [H] tau_m: only continuous GR builds the memory
constexpr float kSlowReleaseMs = 2000.0f;       // [H] tau_s: the LA-3A's quicker tail
// ADR-63: the published attack is the closed loop's; the cell runs tau_on = published x (1 + kAttackLoop k).
constexpr float kAttackLoop = 1.0f;             // [H] Opto 2A's fit (dsp.time's attack)
// The published release (60 ms to 50 %) includes the panel's hold (OptoSense): tau_f = published tau x kReleaseFit.
constexpr float kReleaseFit = 0.85f;            // [H] Opto 2A's fit (dsp.time's release)

constexpr int kLimitStep = 1;
constexpr Step kRatio[]  = { { 0.6667f, "COMP", "COMPRESS" }, { 0.75f, "LIMIT", "LIMIT" } };
constexpr Step kT4[]     = { { 0, "T4", "T4 CELL" } };
constexpr Step kClassA[] = { { 0, "CLASS A", "CLASS A + TRANSFORMERS" } };

constexpr ParamTable kOpto3AParams = [] {
    ParamTable t = allNa("NOT ON THIS CIRCUIT");
    t[Pid::thr]    = { named(cont(-50, 20, -8.f /*PR 40*/), "PEAK RED.",
                             { &prDial, &prPlain, "", 0, /*invert*/true }) };
    t[Pid::ratio]  = { stepped(kRatio, 0.6667f) };
    t[Pid::knee]   = { na(6, "THE KNEE IS THE T4 CELL'S; SEE THE CURVE") };
    t[Pid::range]  = { na(60, "NO GAIN-REDUCTION LIMIT INSIDE THIS FEEDBACK LOOP") };
    t[Pid::atk]    = { prog(locked(1.5f, "T4 CELL: 1.5 MS OR LESS, PROGRAM-DEPENDENT")) };
    t[Pid::rel]    = { prog(withLaw(locked(60, "T4 CELL: 60 MS TO 50 %, THEN QUICK OR SLOW BY PROGRAM", "AUTO"),
                                    TimeLaw::t50)) };
    t[Pid::tmode]  = { na(0, "NO TIME CONTROLS ON THIS CIRCUIT") };
    t[Pid::det]    = { locked(kT4, "THE OPTICAL CELL SENSES THE OUTPUT (FEEDBACK)") };
    t[Pid::schpf]  = { extSchpf() };
    t[Pid::sce]    = { named(cont(0, 6, 0), "HF SENS", { &hfDial, &hfPlain, "", 1 }) };      // the rear pot
    t[Pid::link]   = { stepped(kDualLink, 1) };
    t[Pid::stmode] = { extStereo() };
    t[Pid::voice]  = { locked(kClassA, "CLASS A SOLID STATE AND TRANSFORMERS") };
    t[Pid::drive]  = { extDrive() };
    t[Pid::makeup] = { named(cont(-10, 30, 6.f /*GAIN 40*/), "GAIN", { &gainDial, &gainPlain, "", 0 }) };
    t[Pid::mix]    = { extMix() };
    return t;
}();

void opto3aPhysical(const ParamView& v, EngineParams& e) noexcept {
    e.topo = kTopoFB;                               // FeedbackDelayed, guarded in prepare() (Opto 2A)
    e.m[Opto3A::kHfSensSlot] = v[Pid::sce].plain * (10.f / 6.f);     // 0...10 dB below 1 kHz [H]
    e.sceDbOct = 0.f;                               // the host SC tilt stays neutral: HF SENS is the shelf's alone
    e.m[Opto3A::kLimitSlot] = v[Pid::ratio].step == kLimitStep ? 1.f : 0.f;
    e.m[Opto3A::kShareSlot] = kSlowShare;
    e.m[Opto3A::kChargeSlot] = kMemoryChargeMs;
    e.m[Opto3A::kSlowSlot] = kSlowReleaseMs;
    // ADR-63: the cell's open-loop times. k = S / (1 - S), the loop gain of the ratio step (2 COMP, 3 LIMIT).
    const float k = e.slope < 0.99f ? e.slope / (1.0f - e.slope) : 99.0f;
    e.atkTauMs *= 1.0f + kAttackLoop * (k > 0.0f ? k : 0.0f);
    e.relTauMs *= kReleaseFit;
}

DetectorLaw opto3aLaw(const EngineParams&) noexcept { return DetectorLaw::custom; }
float opto3aTail(const EngineParams& e) noexcept                   // the memory's tail: 5 tau_s (s)
{
    return 5.0f * e.m[Opto3A::kSlowSlot] / 1000.0f;
}

constexpr InternalSpec kOpto3AInt[] = { { "LIGHT", "", 0, 1, 2, false }, { "G FAST", "", 0, 1, 2, false },
                                        { "G SLOW", "", 0, 1, 2, false }, { "MEMORY", "%", 0, 100, 0, true },
                                        { "LDR", "KOHM", 0, 1000, 0, false } };

} // namespace

extern constexpr ModeDescriptor kOpto3A {
    .key = "opto-3a", .name = "OPTO 3A", .group = Group::opto, .introducedInStateVersion = 1, .revision = 1,
    .provisional = false,
    .topologyLine = "OPTICAL · FEEDBACK · SOLID STATE",
    .specLine = "OPTO 3A   T4 CELL · COMP/LIMIT · ≤1.5 MS · 60 MS→50 %, THEN BY PROGRAM · HF SENS",
    .params = kOpto3AParams, .physical = &opto3aPhysical,
    .stage2 = Stage2Kind::none, .linkLaw = LinkLaw::max, .detectorLaw = &opto3aLaw,
    .hasColour = true, .colourStatic = true, .wantsLookahead = false,
    .rigor = Rigor::character, .family = CurveFamily::custom,
    .attackSpec = &attackFromView,       // {0.0015 s, expDb, lo 0.00075, hi 0.003, program}
    .releaseSpec = &releaseFromView,     // {0.060 s, t50,   lo 0.040,   hi 0.080, program}
    .tailSeconds = &opto3aTail, .ctBudgetNsPerSample = 60, .internals = kOpto3AInt };

} // namespace fcdsp::modes
