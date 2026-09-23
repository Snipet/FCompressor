// Bus G (slot 1, `bus-g`): the Mode's descriptor, physical() and specs, 01 §10.4 verbatim except as noted below.
// BusG.h holds the (provisional, generic) traits and BusG.cpp FCDSP_DEFINE_MODE(BusG).
//
// - External linkage (SPRINTS §7 D24): the traits header declares `extern const ModeDescriptor kBusG;`.
// - provisional = true (descriptor wave, ADR-30): generic traits until the Mode task; revision 1 is spelled out.
// - Mode-local helpers (the dial map, the knee law) live here in an unnamed namespace (ModeKit.h is frozen).
// - The detector-law and knee tables are namespace-scope constants, not function-local arrays (no function-local
//   statics in fcdsp, C D12).

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/ModeKit.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include <cstddef>
#include <cstdint>

namespace fcdsp::modes {

extern const ModeDescriptor kBusG;               // also declared by modes/bus-g/BusG.h

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

// [H] E §2.2 auto knee per ratio step (2 / 4 / 10); its dependence on the threshold is open (D §8.3).
constexpr float kKneeByRatio[] = { 10.f, 6.f, 3.f };

float kneeFromRatio(const ParamView& v) noexcept {
    const int s = v[Pid::ratio].step;
    return kKneeByRatio[static_cast<std::size_t>(s < 0 ? 1 : s)];
}

float thrDial (float p) noexcept { return p + 15.f; }  // dial −15…+15 ↔ −30…0 dBFS [H: dial 0 = −15 dBFS]
float thrPlain(float d) noexcept { return d - 15.f; }

constexpr ParamTable kBusGParams = [] {
    ParamTable t = allNa("NOT ON THIS CIRCUIT");
    t[Pid::thr]    = { named(cont(-30, 0, -15), nullptr, { &thrDial, &thrPlain, "", 1 }) };
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
    if ((v[Pid::rel].tag & kTagAuto) != 0) {        // [H] DualRelease τRf, τC, τRs (E §2.5b)
        e.m[0] = 100.f;
        e.m[1] = 300.f;
        e.m[2] = 1200.f;
    }
}

DetectorLaw busGLaw(const EngineParams&) noexcept { return DetectorLaw::peak; }

constexpr InternalSpec kBusGInt[] = { { "FAST ENV", "DB", 0, 30, 1, false }, { "SLOW ENV", "DB", 0, 30, 1, true },
                                      { "AUTO SLOW", "", 0, 1, 0, false } };

} // namespace

extern constexpr ModeDescriptor kBusG {
    .key = "bus-g", .name = "BUS G", .group = Group::vca, .introducedInStateVersion = 1, .revision = 1,
    .provisional = true,
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
