// FET 76 (slot 2, `fet-76`): the Mode's descriptor, physical() and specs, 01 §10.5 verbatim except as noted below.
// Fet76.h holds the (provisional, generic) traits and Fet76.cpp FCDSP_DEFINE_MODE(Fet76).
//
// - External linkage (SPRINTS §7 D24): the traits header declares `extern const ModeDescriptor kFet76;`.
// - provisional = true (descriptor wave, ADR-30); revision 1 is spelled out.
// - Q9 = yes (S4 base): the attack knob's OFF detent is the `tmode` switch relabelled GR, ON = 0 and OFF = 7, so every
//   other Mode's tmode (0 or 1) resolves to ON and crossmode.no_off holds (ADR-25, K2 #4). `atk` is continuous
//   0.02–0.8 ms, hardware-reversed; a raw attack above 0.8 ms clamps to the slowest attack, OFF's hardware neighbour.
// - The knee and threshold-offset tables are namespace-scope constants (no function-local statics, C D12).

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/ModeKit.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include <cstddef>
#include <cstdint>

namespace fcdsp::modes {

extern const ModeDescriptor kFet76;              // also declared by modes/fet-76/Fet76.h

namespace {

using namespace kit;

constexpr float kT0 = -12.f;                     // [H] internal threshold at 4:1, dBFS (D §8.1)

// INPUT dial 0…48, unity 24 (D §2.1 [V S2]); gain = dial − 24 dB [H]; host thr = effective input threshold at 4:1
float inDial  (float thr) noexcept { return 24.f + (kT0 - thr); }
float inPlain (float d)   noexcept { return kT0 - (d - 24.f); }
float outDial (float mu)  noexcept { return mu + 24.f; }          // OUTPUT dial 0…48, unity 24
float outPlain(float d)   noexcept { return d - 24.f; }

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

// [H] D §5.3: 4:1 softer … 20:1 near-hard; per ratio step 4 / 8 / 12 / 20 / ALL.
constexpr float kKneeByRatio[] = { 8.f, 5.f, 4.f, 2.f, 2.f };
// [H] "higher ratios raise the threshold" (D §2.1 [V S2]), dB over kT0 per ratio step.
constexpr float kThrOffsetByRatio[] = { 0.f, 2.f, 4.f, 6.f, 4.f };

std::size_t ratioStep(const ParamView& v) noexcept {
    const int s = v[Pid::ratio].step;
    return static_cast<std::size_t>(s < 0 ? 0 : s);
}

float kneeFromRatio(const ParamView& v) noexcept { return kKneeByRatio[ratioStep(v)]; }

constexpr ParamTable kFet76Params = [] {
    ParamTable t = allNa("NOT ON THIS CIRCUIT");
    t[Pid::thr]    = { named(cont(-36, 12, -18), "INPUT", { &inDial, &inPlain, "", 0, /*invert*/true }) };
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
    t[Pid::makeup] = { named(cont(-24, 24, 0), "OUTPUT", { &outDial, &outPlain, "", 0 }) };
    t[Pid::mix]    = { extMix() };
    return t;
}();

void fetPhysical(const ParamView& v, EngineParams& e) noexcept {
    e.preGainDb = kT0 - v[Pid::thr].plain;                        // the INPUT knob's gain; also drives the FET colour
    e.thrDb     = kT0 + kThrOffsetByRatio[ratioStep(v)];          // fixed internal threshold (detector domain)
    e.topo      = kTopoFB;
    if ((v[Pid::tmode].tag & kTagOff) != 0)                       // audio still passes the colour stage; GR ramps
        e.flags = static_cast<uint8_t>(e.flags | kEngGrOff);      // to 0 over 20 ms (offAmt_, 01 §5.1), never steps
    if (v[Pid::link].step == 1 && e.atkTauMs < 0.04f)             // linked: 40 µs minimum (D §2.1 [V S2])
        e.atkTauMs = 0.04f;
    // ALL: custom curve and slower attack come from kTagAll inside the policies (E §2.7 [H]).
}

DetectorLaw fetLaw(const EngineParams&) noexcept { return DetectorLaw::peak; }

constexpr InternalSpec kFetInt[] = { { "LOOP CV", "V", 0, 10, 2, true }, { "FET R", "KOHM", 0, 100, 1, false },
                                     { "H2", "DB", -100, 0, 0, false }, { "H3", "DB", -100, 0, 0, false } };

} // namespace

extern constexpr ModeDescriptor kFet76 {
    .key = "fet-76", .name = "FET 76", .group = Group::fet, .introducedInStateVersion = 1, .revision = 1,
    .provisional = true,
    .topologyLine = "FEEDBACK FET · PEAK",
    .specLine = "FET 76   FEEDBACK FET · PEAK · 20–800 µS · 4/8/12/20/ALL · INPUT DRIVES A FIXED THRESHOLD",
    .params = kFet76Params, .physical = &fetPhysical,
    .stage2 = Stage2Kind::none, .linkLaw = LinkLaw::max, .detectorLaw = &fetLaw,
    .hasColour = true, .colourStatic = false, .wantsLookahead = false,
    .rigor = Rigor::character, .family = CurveFamily::custom,
    .attackSpec = &attackFromView, .releaseSpec = &releaseFromView, .tailSeconds = &tailFromRelease,
    .ctBudgetNsPerSample = 60, .internals = kFetInt };

} // namespace fcdsp::modes

// Traits (01 §10.5, final): PeakLog; QuadKnee (solveFb closed form, E §2.6); LinkMax; SmoothBranching; NoStage2;
// ColourSelect<FetColour> (voice = revision constants); law::FetVcr [H]; kTopologies = FB. At ECO the FET colour runs
// ADAA-1 at base rate (01 §5.6); STD or HQ is recommended (D F14) but nothing is refused (K1 #35).
