// Opto 2A (slot 3, `opto-2a`): the Mode's descriptor, physical() and specs, 01 §10.6 verbatim except as noted below.
// Opto2A.h holds the (provisional, generic) traits and Opto2A.cpp FCDSP_DEFINE_MODE(Opto2A).
//
// - External linkage (SPRINTS §7 D24): the traits header declares `extern const ModeDescriptor kOpto2A;`.
// - provisional = true (descriptor wave, ADR-30); revision 1 is spelled out.
// - The time specs are the kit's: attack {0.010 s, expDb, 5–20 ms, program} and release {0.060 s, t50, 40–80 ms,
//   program} (kit::detail::timeFromView's bands for a locked program nominal).

#include "fcdsp/core/Units.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/ModeKit.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include <cstdint>

namespace fcdsp::modes {

extern const ModeDescriptor kOpto2A;             // also declared by modes/opto-2a/Opto2A.h

namespace {

using namespace kit;

float prDial   (float thr) noexcept { return (24.f - thr) / 0.64f; }   // PEAK RED. 0…100 ↔ +24…−40 dBFS [H]
float prPlain  (float d)   noexcept { return 24.f - 0.64f * d; }
float gainDial (float mu)  noexcept { return (mu + 10.f) / 0.4f; }     // GAIN 0…100 ↔ −10…+30 dB [H]
float gainPlain(float d)   noexcept { return -10.f + 0.4f * d; }
float emDial   (float s)   noexcept { return s * (10.f / 6.f); }        // R37 set-screw 0…10
float emPlain  (float d)   noexcept { return d * 0.6f; }

// COMP / LIMIT: nominal 3:1 / 10:1 [C: 4:1] (D §2.2).
constexpr Step kRatio[] = { { 0.6667f, "COMP", "COMPRESS" }, { 0.90f, "LIMIT", "LIMIT" } };
constexpr Step kT4[]    = { { 0, "T4", "T4 CELL" } };
constexpr Step kTube[]  = { { 0, "TUBE", "TUBE + TRANSFORMERS" } };

constexpr ParamTable kOpto2AParams = [] {
    ParamTable t = allNa("NOT ON THIS CIRCUIT");
    t[Pid::thr]    = { named(cont(-40, 24, -1.6f /*PR 40*/), "PEAK RED.",
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
    e.topo = kTopoFB;                               // FeedbackDelayed: safe at τ ≈ 10 ms (E §2.7), guarded in prepare()
    e.m[0] = v[Pid::sce].plain * (10.f / 6.f);      // R37: LF desensitisation 0…10 dB below 1 kHz (D §2.2) [H]
    e.m[1] = v[Pid::ratio].step == 1 ? 1.f : 0.f;   // LIMIT: EL-panel drive law change [H]
    // OptoCell constants (E §2.7 [H]): w = 0.5, τon 10 ms, τoff,f 87 ms, τoff,s(m) = 0.3 + 3.2·m s,
    // memory 5 s up / 20 s down
}

DetectorLaw optoLaw(const EngineParams&) noexcept { return DetectorLaw::custom; }
float optoTail(const EngineParams&) noexcept { return 15.f; }        // memory: up to 15 s (E §5.2)

constexpr InternalSpec kOptoInt[] = { { "LIGHT", "", 0, 1, 2, false }, { "G FAST", "", 0, 1, 2, false },
                                      { "G SLOW", "", 0, 1, 2, false }, { "MEMORY", "%", 0, 100, 0, true },
                                      { "LDR", "KOHM", 0, 1000, 0, false } };

} // namespace

extern constexpr ModeDescriptor kOpto2A {
    .key = "opto-2a", .name = "OPTO 2A", .group = Group::opto, .introducedInStateVersion = 1, .revision = 1,
    .provisional = true,
    .topologyLine = "OPTICAL · FEEDBACK · PROGRAM-DEPENDENT",
    .specLine = "OPTO 2A   T4 CELL · COMP/LIMIT · ~10 MS · 60 MS→50 %, THEN 1–15 S",
    .params = kOpto2AParams, .physical = &optoPhysical,
    .stage2 = Stage2Kind::none, .linkLaw = LinkLaw::max, .detectorLaw = &optoLaw,
    .hasColour = true, .colourStatic = true, .wantsLookahead = false,
    .rigor = Rigor::character, .family = CurveFamily::custom,
    .attackSpec = &attackFromView,       // {0.010 s, expDb, lo 0.005, hi 0.020, program}
    .releaseSpec = &releaseFromView,     // {0.060 s, t50,   lo 0.040, hi 0.080, program}
    .tailSeconds = &optoTail, .ctBudgetNsPerSample = 50, .internals = kOptoInt };

} // namespace fcdsp::modes

// Traits (01 §10.6, final): Detector OptoSense (rectified, emphasis-shaped output); Computer OptoCellCurve (steady
// state of law::LdrShunt, used by staticGr and the telemetry target); Ballistics OptoCell, fused in
// FeedbackDelayed<OptoCellCurve> — prepare() checks k ≤ α/(1−α) at the actual fs and falls back to FeedbackZdf if
// violated (01 §5.3, K2 #5c); ScShape R37Shelf; Colour TubeTransformer; kTopologies = FB.
