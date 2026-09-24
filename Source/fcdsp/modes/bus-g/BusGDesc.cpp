// Bus G (slot 1, `bus-g`): the Mode's descriptor, physical() and specs, 01 §10.4 verbatim except as noted below.
// BusG.h holds the traits and BusG.cpp FCDSP_DEFINE_MODE(BusG); docs/modes/bus-g.md lists every [H] constant with its
// source.
//
// - External linkage (SPRINTS §7 D24): the traits header declares `extern const ModeDescriptor kBusG;`.
// - provisional = false (M1, S7): the real traits are installed, so the fidelity rows are blocking spec rows and the
//   goldens can be blessed. revision 1: the Mode has never shipped (tests/fixtures/modes-ever.tsv), so fitting the [H]
//   constants below moves no released print hash (K2 #10).
// - THRESHOLD dial offset, fitted (the card's [H]; 01 §10.4 had dial 0 = -15 dBFS): the SSL G dial reads -15 ... +15 dB
//   around the console's operating level (D §2.4 [V~ S5]), and ADR-59 fixes that level, 0 VU = +4 dBu, at -18 dBFS.
//   So dial 0 = -18 dBFS and the dial spans -33 ... -3 dBFS; the default is the dial's centre, which is also the host
//   default (-18 dBFS) and Clean's, so a fresh Bus G and a switch from Clean's default both read dial 0.
// - RELEASE AUTO's DualRelease times go to m[0..2] only on the AUTO position (01 §10.4); off AUTO the AutoSwitch runs
//   SmoothBranching and lands DualRelease on them when AUTO engages (AutoSwitch.h).
// - Mode-local helpers (the dial map, the knee law) live here in an unnamed namespace (ModeKit.h is frozen).
// - The knee table is a namespace-scope constant, not a function-local array (no function-local statics in fcdsp,
//   C D12).

#include "fcdsp/modes/bus-g/BusG.h"

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

constexpr Step kRatio[] = { { 0.50f, "2", "2:1" }, { 0.75f, "4", "4:1" }, { 0.90f, "10", "10:1" } };  // .625/.825
constexpr Step kAtk[]   = { { 0.1f, ".1", "0.1 MS" }, { 0.3f, ".3", "0.3 MS" }, { 1, "1", "1 MS" },
                            { 3, "3", "3 MS" }, { 10, "10", "10 MS" }, { 30, "30", "30 MS" } };        // D §2.4 [V~ S5]
constexpr Step kRel[]   = { { 100, ".1", "0.1 S" }, { 300, ".3", "0.3 S" }, { 600, ".6", "0.6 S" },
                            { 1200, "1.2", "1.2 S" },
                            { 2400, "AUTO", "AUTO", kTagAuto | kTagProgram } };                 // AUTO = 5th position
constexpr Step kVoice[] = { { 0, "VCA", "VCA + CONSOLE" }, { 1, "CLEAN" } };           // Waves "Analog" precedent
constexpr Variant kDriveByVoice[] = { { 1, na(0, "CLEAN VOICE HAS NO COLOUR STAGE") } };

// [H] E §2.2 auto knee per ratio step (2 / 4 / 10): soft, medium, hard (D §2.4 "moderately soft"); its dependence on
// the threshold is unpublished (D §8.3) and not modelled.
constexpr float kKneeByRatio[] = { 10.f, 6.f, 3.f };

// [H] THRESHOLD dial: 0 on the dial = the operating level, 0 VU = -18 dBFS (ADR-59; file comment).
constexpr float kDialZeroDbfs = -18.f;

// [H] RELEASE AUTO (E §2.5b: "a fast one near the fastest release setting (0.1 s) and a slow one near the slowest
// (1.2 s)"), fitted so AUTO's release, measured on the applied GR in the switch's own law (tau, to 1/e), spans the
// manual positions: after a transient the slow path is empty and the GR releases at tau_Rf = 0.1 s, the fastest
// position; after sustained program both paths are charged and the GR follows the two-pole cascade r_f -> r_s, whose
// 1/e time is 1.105 s at tau_Rs = 1.0 s (1.305 s at 1.2 s would overrun the switch's 1.2 s end: the lag of r_f adds
// about tau_Rf). The slow path charges from the fast one in tau_C = 0.3 s, so a program peak must last about that long
// before the release turns slow ("dependent upon the duration of the program peak", D §2.4). docs/modes/bus-g.md.
constexpr float kAutoFastReleaseMs = 100.f, kAutoChargeMs = 300.f, kAutoSlowReleaseMs = 1000.f;

float kneeFromRatio(const ParamView& v) noexcept {
    const int s = v[Pid::ratio].step;
    return kKneeByRatio[static_cast<std::size_t>(s < 0 ? 1 : s)];
}

float thrDial (float p) noexcept { return p - kDialZeroDbfs; }  // dial -15 ... +15 <-> -33 ... -3 dBFS
float thrPlain(float d) noexcept { return d + kDialZeroDbfs; }

constexpr ParamTable kBusGParams = [] {
    ParamTable t = allNa("NOT ON THIS CIRCUIT");
    t[Pid::thr]    = { named(cont(kDialZeroDbfs - 15.f, kDialZeroDbfs + 15.f, kDialZeroDbfs), nullptr,
                             { &thrDial, &thrPlain, "", 1 }) };
    t[Pid::ratio]  = { stepped(kRatio, 0.75f) };
    t[Pid::knee]   = { derived(&kneeFromRatio, Pid::ratio, "= RATIO",
                               "KNEE FOLLOWS RATIO: SOFT AT 2, MEDIUM AT 4, HARD AT 10") };
    t[Pid::range]  = { extRange() };
    t[Pid::atk]    = { stepped(kAtk, 10) };
    t[Pid::rel]    = { stepped(kRel, 300) };
    t[Pid::tmode]  = { na(0, "AUTO IS THE FIFTH RELEASE POSITION") };
    t[Pid::det]    = { locked(kPeak, "PEAK DETECTOR, FIXED IN THIS CIRCUIT") };   // [U] D §8.3
    t[Pid::schpf]  = { extSchpf() };
    t[Pid::link]   = { extLink() };
    t[Pid::stmode] = { extStereo() };
    t[Pid::voice]  = { stepped(kVoice, 0) };
    t[Pid::drive]  = { ext(cont(-12, 12, 0)), Pid::voice, kDriveByVoice };
    t[Pid::makeup] = { cont(-5, 15, 0) };
    t[Pid::mix]    = { extMix() };
    return t;
}();

void busGPhysical(const ParamView& v, EngineParams& e) noexcept {
    e.topo = kTopoFF;
    if ((v[Pid::rel].tag & kTagAuto) != 0) {        // DualRelease tau_Rf, tau_C, tau_Rs (E §2.5b; BusG.h slots)
        e.m[BusG::kFastReleaseSlot] = kAutoFastReleaseMs;
        e.m[BusG::kChargeSlot] = kAutoChargeMs;
        e.m[BusG::kSlowReleaseSlot] = kAutoSlowReleaseMs;
    }
}

DetectorLaw busGLaw(const EngineParams&) noexcept { return DetectorLaw::peak; }

constexpr InternalSpec kBusGInt[] = { { "FAST ENV", "DB", 0, 30, 1, false }, { "SLOW ENV", "DB", 0, 30, 1, true },
                                      { "AUTO SLOW", "", 0, 1, 0, false } };

} // namespace

extern constexpr ModeDescriptor kBusG {
    .key = "bus-g", .name = "BUS G", .group = Group::vca, .introducedInStateVersion = 1, .revision = 1,
    .provisional = false,
    .topologyLine = "VCA BUS · FEED-FORWARD · PEAK",
    .specLine = "BUS G   VCA BUS · 2/4/10 · .1–30 MS · .1–1.2 S + AUTO",
    .params = kBusGParams, .physical = &busGPhysical,
    .stage2 = Stage2Kind::none, .linkLaw = LinkLaw::max, .detectorLaw = &busGLaw,
    .hasColour = true, .colourStatic = true, .wantsLookahead = false,
    .rigor = Rigor::modelled, .family = CurveFamily::textbook,
    .attackSpec = &attackFromView, .releaseSpec = &releaseFromView, .tailSeconds = &tailFromRelease,
    .ctBudgetNsPerSample = 40, .internals = kBusGInt };

} // namespace fcdsp::modes

// Release spec: AUTO -> TimeSpec{program, lo 0.1 s, hi 1.2 s} (kit::releaseFromView: the span of the switch's other
// positions; C §5.3 character-style range check).
