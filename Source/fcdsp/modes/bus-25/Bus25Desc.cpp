// Bus 25 (slot 6, `bus-25`): the Mode's descriptor, physical() and specs, from 01 §10.7's sketch and D §2.4 / §5
// (API 2500). Bus25.h holds the (provisional, generic) traits and Bus25.cpp FCDSP_DEFINE_MODE(Bus25).
//
// - External linkage (SPRINTS §7 D24): the traits header declares `extern const ModeDescriptor kBus25;`.
// - provisional = true (descriptor wave, ADR-30); revision 1 is spelled out.
// - VOICE switches the topology (NEW = feed-forward, OLD = feedback; D §2.4 [V S6]); physical() sets
//   EngineParams::topo, so the change is a kernel-key change and the host crossfades it (01 §5.5). The NEW text avoids
//   an ASCII '-' ("FEEDFORWARD"): value text carries no ASCII hyphen (dsp.format's rule; minus is U+2212).
// - TIME MODE VAR exposes the continuous release pot (the dependent-list example of 01 §10.7): RELEASE is stepped
//   under FIXED and continuous 50–3000 ms under VAR.
// Choices where the sketch is silent, [H] until the Mode task fits them:
// - Defaults: ratio 4:1, attack 1 ms, release 0.5 s (the sketch's), knee MED, threshold 0 dBu (−22 dBFS), link 100 %.
// - Step texts follow Bus G's style ("0.1 MS", "0.5 S"); the ∞ step speaks "infinity to 1".
// - RANGE is the standard extension while NEW (feed-forward) and n/a while OLD, with FET 76 / Opto 2A's feedback
//   reason: range is an FF-only extension (ModeKit.h extRange) and OLD is a feedback loop. This is a second
//   dependent list (driver VOICE, which resolves before RANGE in kResolveOrder).
// - Internals (E §7, internals by family: VCA, adapted to the CV-sum link): RMS DET, OWN CV, LINKED CV (history).

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/ModeKit.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include <cstdint>

namespace fcdsp::modes {

extern const ModeDescriptor kBus25;              // also declared by modes/bus-25/Bus25.h

namespace {

using namespace kit;

constexpr Step kVoice[] = { { 0, "NEW", "NEW (FEEDFORWARD)" }, { 1, "OLD", "OLD (FEEDBACK)" } };    // -> e.topo
constexpr Step kTm[]    = { { 0, "FIXED" }, { 1, "VAR", "VARIABLE", kTagVar } };
constexpr Step kRel[]   = { { 50, ".05", "50 MS" }, { 100, ".1", "0.1 S" }, { 200, ".2", "0.2 S" },
                            { 500, ".5", "0.5 S" }, { 1000, "1", "1 S" }, { 2000, "2", "2 S" } };   // D §2.4 [V S6]
constexpr Variant kRelByTm[] = { { 1, cont(50, 3000, 500) } };        // VAR exposes the continuous pot (D §2.4 [V S6])
constexpr Step kRatio[] = { { 1.0f - 1.0f / 1.5f, "1.5", "1.5:1" }, { 0.5f, "2", "2:1" },
                            { 1.0f - 1.0f / 3.0f, "3", "3:1" }, { 0.75f, "4", "4:1" },
                            { 1.0f - 1.0f / 6.0f, "6", "6:1" }, { 0.9f, "10", "10:1" },
                            { 1.0f, "∞", "∞:1", kTagNone, "infinity to 1" } };
constexpr Step kKnee[]  = { { 0, "HARD" }, { 6, "MED", "MEDIUM" }, { 12, "SOFT" } };             // [H widths]
constexpr Step kAtk[]   = { { 0.03f, ".03", "0.03 MS" }, { 0.1f, ".1", "0.1 MS" }, { 0.3f, ".3", "0.3 MS" },
                            { 1, "1", "1 MS" }, { 3, "3", "3 MS" }, { 10, "10", "10 MS" },
                            { 30, "30", "30 MS" } };                                               // D §2.4 [V S6]
// THRUST on the host SC tilt, dB/oct: LOUD = 10 dB/decade [V S6]; MED [C] D §8.5.
constexpr Step kThrust[] = { { 0, "NORM", "NORMAL" }, { 1.5f, "MED", "MEDIUM" }, { 3.01f, "LOUD" } };
constexpr Step kLink[]  = { { 0, "IND", "INDEPENDENT" }, { 0.5f, "50" }, { 0.6f, "60" }, { 0.7f, "70" }, { 0.8f, "80" },
                            { 0.9f, "90" }, { 1, "100" } };
constexpr Step kAutoMu[] = { { 0, "MAN", "MANUAL" }, { 1, "AUTO" } };                      // drawn as the AUTO word
constexpr Step kRms[]   = { { 0, "RMS" } };
constexpr Variant kRangeByVoice[] = { { 1, na(60, "NO GAIN-REDUCTION LIMIT INSIDE THE FEEDBACK LOOP (OLD)") } };

float thrDbu  (float p) noexcept { return p + 22.f; }     // dBu at the fixed calibration 0 dBFS = +22 dBu (01 §3.1)
float thrPlain(float d) noexcept { return d - 22.f; }

constexpr ParamTable kBus25Params = [] {
    ParamTable t = allNa("NOT ON THIS CIRCUIT");
    t[Pid::thr]    = { named(cont(-42, -2, -22), nullptr, { &thrDbu, &thrPlain, "DBU", 1 }) };   // −20…+20 dBu
    t[Pid::ratio]  = { stepped(kRatio, 0.75f) };
    t[Pid::knee]   = { stepped(kKnee, 6) };
    t[Pid::range]  = { extRange(), Pid::voice, kRangeByVoice };
    t[Pid::atk]    = { stepped(kAtk, 1) };
    t[Pid::rel]    = { stepped(kRel, 500), Pid::tmode, kRelByTm };              // the dependent-list example
    t[Pid::tmode]  = { stepped(kTm, 0) };                                       // its own slot, TIME MODE (02 §6.4)
    t[Pid::det]    = { locked(kRms, "RMS DETECTOR (THAT 2252), FIXED IN THIS CIRCUIT") };
    t[Pid::schpf]  = { extSchpf() };
    t[Pid::sce]    = { named(stepped(kThrust, 0), "THRUST") };                  // the host SC tilt, dB/oct
    t[Pid::link]   = { stepped(kLink, 1) };
    t[Pid::stmode] = { extStereo() };
    t[Pid::voice]  = { stepped(kVoice, 0) };
    t[Pid::drive]  = { extDrive() };
    t[Pid::makeup] = { cont(0, 24, 0) };
    t[Pid::automu] = { stepped(kAutoMu, 0) };
    t[Pid::mix]    = { cont(0, 1, 1) };                                         // crossfade law (D §2.4)
    return t;
}();

void bus25Physical(const ParamView& v, EngineParams& e) noexcept {
    e.topo = v[Pid::voice].step == 1 ? kTopoFB : kTopoFF;   // OLD (FB) links with LinkCvSum after the per-lane solve
}

DetectorLaw bus25Law(const EngineParams&) noexcept { return DetectorLaw::rms; }

constexpr InternalSpec kBus25Int[] = { { "RMS DET", "DB", -60, 6, 1, false }, { "OWN CV", "DB", 0, 30, 1, false },
                                       { "LINKED CV", "DB", 0, 30, 1, true } };

} // namespace

extern constexpr ModeDescriptor kBus25 {
    .key = "bus-25", .name = "BUS 25", .group = Group::vca, .introducedInStateVersion = 1, .revision = 1,
    .provisional = true,
    .topologyLine = "VCA · NEW FEED-FORWARD / OLD FEEDBACK · RMS",
    .specLine = "BUS 25   VCA · RMS · 1.5–∞ · .03–30 MS · .05–2 S + VAR · THRUST · CV-SUM LINK",
    .params = kBus25Params, .physical = &bus25Physical,
    .stage2 = Stage2Kind::none, .linkLaw = LinkLaw::cvSum, .detectorLaw = &bus25Law,
    .hasColour = true, .colourStatic = true, .wantsLookahead = false,
    .rigor = Rigor::modelled, .family = CurveFamily::textbook,
    .attackSpec = &attackFromView, .releaseSpec = &releaseFromView, .tailSeconds = &tailFromRelease,
    .ctBudgetNsPerSample = 45, .internals = kBus25Int };

} // namespace fcdsp::modes

// Traits (01 §10.7, final): RmsLog; QuadKnee (3 W values); LinkCvSum; SmoothBranching; NoStage2; the 2510/2520 +
// output-transformer colour; kTopologies = FF | FB (branch per chunk on e.topo). OLD (FB) links with LinkCvSum after
// the per-lane solve (01 §5.2).
