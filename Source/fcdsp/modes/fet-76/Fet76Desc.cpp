// FET 76 (slot 2, `fet-76`): the Mode's descriptor, physical() and specs, 01 §10.5 verbatim except as noted below.
// Fet76.h holds the traits and Fet76.cpp FCDSP_DEFINE_MODE(Fet76); docs/modes/fet-76.md lists every [H] constant with
// its source.
//
// - External linkage (SPRINTS §7 D24): the traits header declares `extern const ModeDescriptor kFet76;`.
// - provisional = false (M2, S9): the real traits are installed, so the fidelity rows are blocking spec rows and the
//   goldens can be blessed. revision 1: the Mode has never shipped (tests/fixtures/modes-ever.tsv), so fitting the [H]
//   constants below moves no released print hash (K2 #10).
// - kT0, fitted (01 §10.5 had -12 dBFS [H]): the fixed internal threshold at 4:1 is the operating level at unity
//   INPUT, 0 VU = +4 dBu = -18 dBFS (ADR-59, the calibration Bus G's dial zero uses). So the INPUT knob's unity mark
//   (24, "about 24 ... at 12 o'clock", D §2.1 [V S2]) is also the host default threshold (-18 dBFS) and Clean's: a
//   fresh FET 76, and a switch from Clean's default, read INPUT 24.
// - The dial law, fitted (01 §10.5 had 1 dB per dial unit [H]): INPUT and OUTPUT are both 0-48 dials with unity at 24,
//   and the unit's maximum gain, both at 48, is 45 dB (D §2.1 [V S2] "Gain is 45 dB +-1"). So each dial's upper half
//   spans 22.5 dB: 45/48 = 0.9375 dB per dial unit, linear in dB and symmetric about 24 (INPUT -22.5 ... +22.5 dB into
//   the fixed threshold, i.e. thr = kT0 -+ 22.5 dBFS; OUTPUT -22.5 ... +22.5 dB).
// - Q9 = yes (S4 base): the attack knob's OFF detent is the `tmode` switch relabelled GR, ON = 0 and OFF = 7, so every
//   other Mode's tmode (0 or 1) resolves to ON and crossmode.no_off holds (ADR-25, K2 #4). `atk` is continuous
//   0.02-0.8 ms, hardware-reversed; a raw attack above 0.8 ms clamps to the slowest attack, OFF's hardware neighbour.
//   OFF sets kEngGrOff: the GR ramps to 0 over 20 ms with ModeEngine's smootherstep (offAmt_), and the audio still
//   passes the colour stage (the FET pinched off: FetColour's FET residual vanishes with the GR, the amplifiers
//   remain).
// - Time constants (ADR-63): the descriptor's TimeSpec is the PUBLISHED closed-loop time; physical() converts the
//   attack to the open-loop tau of the loop's ballistics, tau_open = tau_published (1 + k), k = QuadKnee::loopGain(S)
//   (the linear-region closed loop runs 1 + k times faster than its one-pole, E §2.6). The published attack is the
//   knob's, raised to 40 us while LINKed (D §2.1 [V S2]: "the fastest attack time is doubled ... 40 microseconds") and
//   slowed by ALL's lag; fetAttackSpec declares that effective value. The release is not converted: a release measured
//   the hardware way (D2: the level drops below the threshold) runs with the loop open, i.e. at the one-pole's tau.
// - ALL (the all-buttons step, kTagAll): a monotone curve (K2 #5a) with a larger loop gain (S = 1: k = 99, the
//   plateau), its own threshold offset and the near-hard knee of 20:1, an attack kAllAttackLag times slower (the lag
//   that lets transients through, D §2.1), and more colour (FetColour: the shifted bias points).
// - Mode-local helpers (the dial maps, the knee and offset tables, the attack law) live here in an unnamed namespace
//   (ModeKit.h is frozen); the tables are namespace-scope constants (no function-local statics, C D12).

#include "fcdsp/modes/fet-76/Fet76.h"

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

namespace {

using namespace kit;

constexpr float kT0 = -18.f;                     // [H, fitted] internal threshold at 4:1 = 0 VU at unity INPUT, dBFS
constexpr float kDialUnity = 24.f;               // INPUT / OUTPUT dial unity mark (D §2.1 [V S2])
constexpr float kDialDbPerUnit = 45.f / 48.f;    // [H, fitted] 0.9375 dB per dial unit: 45 dB over both upper halves
constexpr float kDialHalfSpanDb = kDialUnity * kDialDbPerUnit;   // 22.5 dB
constexpr float kLinkedMinAttackMs = 0.04f;      // LINKed: 40 us fastest attack (D §2.1 [V S2])
constexpr float kAllAttackLag = 3.f;             // [H] ALL: the attack runs 3 x slower ("a lag time on the attack")

// INPUT dial 0...48, unity 24; gain = (dial - 24) x 0.9375 dB; host thr = the effective input threshold at 4:1.
float inDial  (float thr) noexcept { return kDialUnity + (kT0 - thr) / kDialDbPerUnit; }
float inPlain (float d)   noexcept { return kT0 - (d - kDialUnity) * kDialDbPerUnit; }
float outDial (float mu)  noexcept { return kDialUnity + mu / kDialDbPerUnit; }        // OUTPUT dial 0...48, unity 24
float outPlain(float d)   noexcept { return (d - kDialUnity) * kDialDbPerUnit; }

constexpr Step kRatio[]  = { { 0.75f, "4", "4:1" }, { 0.875f, "8", "8:1" }, { 0.91667f, "12", "12:1" },
                             { 0.95f, "20", "20:1" },
                             { 1.0f, "ALL", "ALL BUTTONS", kTagAll | kTagProgram, "all buttons" } };
// The attack knob's CCW OFF detent (D §2.1 [V S2]) lives on tmode, relabelled GR (K2 #4): ON = 0, OFF = 7, boundary
// 3.5, so every other Mode's tmode ∈ {0, 1} resolves to ON.
constexpr Step kGrSwitch[] = { { 0, "ON", "GAIN REDUCTION ON" },
                               { 7, "OFF", "ATTACK OFF (NO GAIN REDUCTION, COLOUR ONLY)", kTagOff,
                                 "gain reduction off" } };
constexpr Step kVoice[]  = { { 0, "LN", "REV D/E LN" }, { 1, "A", "REV A" }, { 2, "F", "REV F/H" } };   // D §2.1
constexpr Step kPeakFb[] = { { 0, "PEAK", "PEAK · FEEDBACK" } };

// [H] D §5.3: 4:1 softer ... 20:1 near-hard; per ratio step 4 / 8 / 12 / 20 / ALL (the FB knee is at the output).
constexpr float kKneeByRatio[] = { 8.f, 5.f, 4.f, 2.f, 2.f };
// [H] "higher ratios raise the threshold" (D §2.1 [V S2]), dB over kT0 per ratio step; ALL sits between 8 and 20.
constexpr float kThrOffsetByRatio[] = { 0.f, 2.f, 4.f, 6.f, 4.f };

std::size_t ratioStep(const ParamView& v) noexcept {
    const int s = v[Pid::ratio].step;
    return static_cast<std::size_t>(s < 0 ? 0 : s);
}

float kneeFromRatio(const ParamView& v) noexcept { return kKneeByRatio[ratioStep(v)]; }

// The published (closed-loop) attack the Mode runs, ms: the knob, the linked minimum, ALL's lag (file comment).
float publishedAttackMs(const ParamView& v) noexcept {
    float a = v[Pid::atk].plain;
    if (v[Pid::link].step == 1 && a < kLinkedMinAttackMs)
        a = kLinkedMinAttackMs;
    if ((v[Pid::ratio].tag & kTagAll) != 0)
        a *= kAllAttackLag;
    return a;
}

constexpr ParamTable kFet76Params = [] {
    ParamTable t = allNa("NOT ON THIS CIRCUIT");
    t[Pid::thr]    = { named(cont(kT0 - kDialHalfSpanDb, kT0 + kDialHalfSpanDb, kT0), "INPUT",
                             { &inDial, &inPlain, "", 0, /*invert*/true }) };
    t[Pid::ratio]  = { stepped(kRatio, 0.75f) };
    t[Pid::knee]   = { derived(&kneeFromRatio, Pid::ratio, "= RATIO",
                               "KNEE IS SET BY THE RATIO BUTTON (FEEDBACK FET)") };
    t[Pid::range]  = { na(60, "NO GAIN-REDUCTION LIMIT INSIDE THIS FEEDBACK LOOP") };
    t[Pid::atk]    = { hwrev(cont(0.02f, 0.8f, 0.2f)) };    // raw > 0.8 clamps to the slowest attack (OFF's neighbour)
    t[Pid::rel]    = { hwrev(cont(50, 1100, 400)) };
    t[Pid::tmode]  = { named(stepped(kGrSwitch, 0), "GR") };  // B4 slot shows "GR"; OFF = the knob's OFF detent
    t[Pid::det]    = { locked(kPeakFb, "PEAK SENSING AFTER THE GAIN ELEMENT (FEEDBACK)") };
    t[Pid::schpf]  = { extSchpf() };
    t[Pid::link]   = { stepped(kDualLink, 1) };
    t[Pid::stmode] = { extStereo() };
    t[Pid::voice]  = { stepped(kVoice, 0) };
    t[Pid::drive]  = { na(0, "INPUT DRIVES THE FET STAGE; THERE IS NO SEPARATE DRIVE") };
    t[Pid::makeup] = { named(cont(-kDialHalfSpanDb, kDialHalfSpanDb, 0), "OUTPUT", { &outDial, &outPlain, "", 0 }) };
    t[Pid::mix]    = { extMix() };
    return t;
}();

void fetPhysical(const ParamView& v, EngineParams& e) noexcept {
    e.preGainDb = kT0 - v[Pid::thr].plain;                        // the INPUT knob's gain; also drives the FET colour
    e.thrDb     = kT0 + kThrOffsetByRatio[ratioStep(v)];          // fixed internal threshold (detector domain)
    e.topo      = kTopoFB;
    if ((v[Pid::tmode].tag & kTagOff) != 0)                       // audio still passes the colour stage; GR ramps
        e.flags = static_cast<uint8_t>(e.flags | kEngGrOff);      // to 0 over 20 ms (offAmt_, 01 §5.1), never steps
    // ADR-63: the published closed-loop attack -> the loop's open-loop tau (file comment). S = 1 (ALL) gives k = 99.
    e.atkTauMs  = publishedAttackMs(v) * (1.f + stage::QuadKnee::loopGain(e.slope));
}

DetectorLaw fetLaw(const EngineParams&) noexcept { return DetectorLaw::peak; }

// The attack the Mode runs, as published (closed loop): the knob with the linked minimum and ALL's lag (ADR-63).
TimeSpec fetAttackSpec(const ParamView& v, const EngineParams& e) noexcept {
    TimeSpec t = attackFromView(v, e);
    t.seconds = publishedAttackMs(v) / 1000.f;
    return t;
}

constexpr InternalSpec kFetInt[] = { { "LOOP CV", "V", 0, 10, 2, true }, { "FET R", "KOHM", 0, 100, 1, false },
                                     { "H2", "DB", -100, 0, 0, false }, { "H3", "DB", -100, 0, 0, false } };

} // namespace

extern constexpr ModeDescriptor kFet76 {
    .key = "fet-76", .name = "FET 76", .group = Group::fet, .introducedInStateVersion = 1, .revision = 1,
    .provisional = false,
    .topologyLine = "FEEDBACK FET · PEAK",
    .specLine = "FET 76   FEEDBACK FET · PEAK · 20–800 µS · 4/8/12/20/ALL · INPUT DRIVES A FIXED THRESHOLD",
    .params = kFet76Params, .physical = &fetPhysical,
    .stage2 = Stage2Kind::none, .linkLaw = LinkLaw::max, .detectorLaw = &fetLaw,
    .hasColour = true, .colourStatic = false, .wantsLookahead = false,
    .rigor = Rigor::character, .family = CurveFamily::custom,
    .attackSpec = &fetAttackSpec, .releaseSpec = &releaseFromView, .tailSeconds = &tailFromRelease,
    .ctBudgetNsPerSample = 60, .internals = kFetInt };

} // namespace fcdsp::modes

// Traits (01 §10.5, final): PeakLog; QuadKnee (solveFb closed form, E §2.6) + law::FetVcr; LinkMax; SmoothBranching;
// NoStage2; ColourSelect<FetColour> (voice = revision constants); Flat; kTopologies = FB. At ECO the FET colour runs
// ADAA-1 at base rate (01 §5.6); STD or HQ is recommended (D F14) but nothing is refused (K1 #35).
