// Bus 25 (slot 6, `bus-25`): the Mode's descriptor, physical() and specs, from 01 §10.7's sketch and D §2.4 / §5 (API
// 2500). Bus25.h holds the traits and Bus25.cpp FCDSP_DEFINE_MODE(Bus25); docs/modes/bus-25.md lists every [H]
// constant with its source.
//
// - External linkage (SPRINTS §7 D24): the traits header declares `extern const ModeDescriptor kBus25;`.
// - provisional = false (M6, S11): the real traits are installed, so the fidelity rows are blocking spec rows and the
//   goldens can be blessed. revision 1: the Mode has never shipped (tests/fixtures/modes-ever.tsv), so fitting the [H]
//   constants moves no released print hash (K2 #10).
// - VOICE switches the topology (TYPE NEW = feed-forward, OLD = feedback; D §2.4 [V S6]); physical() sets
//   EngineParams::topo, so the change is a kernel-key change and the host crossfades it (01 §5.5). The NEW text avoids
//   an ASCII '-' ("FEEDFORWARD"): value text carries no ASCII hyphen (dsp.format's rule; minus is U+2212).
// - Time constants (ADR-63): the TimeSpecs declare the published times, which are the closed-loop times in both TYPEs.
//   NEW is open loop; under OLD physical() converts the attack to the loop's open-loop tau, tau = t_published (1 + k),
//   k = QuadKnee::loopGain(slope) = R - 1 (the loop gain above the knee; FET 76's and Diode 609's conversion).
//   Releases are not converted: the level drops below the threshold and the loop opens (docs/modes/fet-76.md).
// - TIME MODE VAR exposes the continuous release pot (the dependent-list example of 01 §10.7): RELEASE is stepped
//   under FIXED and continuous 50-3000 ms under VAR (D §2.4 [V S6]).
// - THRUST is the host's side-chain tilt (`sce`, dB/oct pivoted at 1 kHz, E §8): NORM 0, MED 1.5 [C], LOUD 3.01
//   (10 dB/decade [V S6]). physical() leaves m[] at its neutral zeros: the Mode adds no emphasis of its own (S11 lead
//   revision 4: no double emphasis), and ScShape is Flat.
// - RANGE is the standard extension while NEW (feed-forward) and n/a while OLD, with FET 76 / Opto 2A's feedback
//   reason: range is an FF-only extension (ModeKit.h extRange) and OLD is a feedback loop. This is a second dependent
//   list (driver VOICE, which resolves before RANGE in kResolveOrder).
// - AUTO makeup (the AUTO word on MAKEUP, `automu`): the engine's r^(0 dBFS) at the current THRESHOLD, RATIO and KNEE
//   ("based on ratio and threshold", D §2.4 [V S6]; E §2.2 with k = 1 [H]).
// - Defaults: RATIO 4:1, ATTACK 1 ms, RELEASE 0.5 s (the sketch's), KNEE MED, THRESHOLD 0 dBu (-22 dBFS), LINK 100 %,
//   TYPE NEW, THRUST NORM. Step texts follow Bus G's style ("0.1 MS", "0.5 S"); the inf step speaks "infinity to 1".
// - Mode-local helpers (the dial map) live here in an unnamed namespace (ModeKit.h is frozen); their tables are
//   namespace-scope constants (no function-local statics in fcdsp, C D12).

#include "fcdsp/modes/bus-25/Bus25.h"

#include "fcdsp/engine/stages/gain/QuadKnee.h"
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

constexpr Step kVoice[] = { { 0, "NEW", "NEW (FEEDFORWARD)" }, { 1, "OLD", "OLD (FEEDBACK)" } };    // -> e.topo
constexpr Step kTm[]    = { { 0, "FIXED" }, { 1, "VAR", "VARIABLE", kTagVar } };
constexpr Step kRel[]   = { { 50, ".05", "50 MS" }, { 100, ".1", "0.1 S" }, { 200, ".2", "0.2 S" },
                            { 500, ".5", "0.5 S" }, { 1000, "1", "1 S" }, { 2000, "2", "2 S" } };   // D §2.4 [V S6]
constexpr Variant kRelByTm[] = { { 1, cont(50, 3000, 500) } };        // VAR exposes the continuous pot (D §2.4 [V S6])
constexpr Step kRatio[] = { { 1.0f - 1.0f / 1.5f, "1.5", "1.5:1" }, { 0.5f, "2", "2:1" },
                            { 1.0f - 1.0f / 3.0f, "3", "3:1" }, { 0.75f, "4", "4:1" },
                            { 1.0f - 1.0f / 6.0f, "6", "6:1" }, { 0.9f, "10", "10:1" },
                            { 1.0f, "∞", "∞:1", kTagNone, "infinity to 1" } };               // D §2.4 [V S6]
// [H] The knee widths W (dB, QuadKnee's input-domain width in NEW; at the output in OLD, where the loop widens it by
// (1 + R) / 2 at the input: "FB is smoother, softer", D §2.4 [V S6]). The 2500 publishes the three positions, not
// their widths (docs/modes/bus-25.md): HARD is QuadKnee's hard knee, MED the host default width (so a Mode switch
// from Clean's default lands on MED), SOFT twice it.
constexpr Step kKnee[]  = { { 0, "HARD" }, { 6, "MED", "MEDIUM" }, { 12, "SOFT" } };
constexpr Step kAtk[]   = { { 0.03f, ".03", "0.03 MS" }, { 0.1f, ".1", "0.1 MS" }, { 0.3f, ".3", "0.3 MS" },
                            { 1, "1", "1 MS" }, { 3, "3", "3 MS" }, { 10, "10", "10 MS" },
                            { 30, "30", "30 MS" } };                                               // D §2.4 [V S6]
// THRUST on the host SC tilt, dB/oct: LOUD = 10 dB/decade [V S6]; MED [C] D §8.5 (half of LOUD; SOS reads 2 / 4).
constexpr Step kThrust[] = { { 0, "NORM", "NORMAL" }, { 1.5f, "MED", "MEDIUM" }, { 3.01f, "LOUD" } };
constexpr Step kLink[]  = { { 0, "IND", "INDEPENDENT" }, { 0.5f, "50" }, { 0.6f, "60" }, { 0.7f, "70" }, { 0.8f, "80" },
                            { 0.9f, "90" }, { 1, "100" } };                                        // D §2.4 [V S6]
constexpr Step kAutoMu[] = { { 0, "MAN", "MANUAL" }, { 1, "AUTO" } };                      // drawn as the AUTO word
constexpr Step kRms[]   = { { 0, "RMS" } };
constexpr Variant kRangeByVoice[] = { { 1, na(60, "NO GAIN-REDUCTION LIMIT INSIDE THE FEEDBACK LOOP (OLD)") } };

// The TYPE switch's OLD position (VOICE step 1): the feedback loop.
constexpr int kOldStep = 1;

float thrDbu  (float p) noexcept { return p + 22.f; }     // dBu at the fixed calibration 0 dBFS = +22 dBu (01 §3.1)
float thrPlain(float d) noexcept { return d - 22.f; }

constexpr ParamTable kBus25Params = [] {
    ParamTable t = allNa("NOT ON THIS CIRCUIT");
    t[Pid::thr]    = { named(cont(-42, -2, -22), nullptr, { &thrDbu, &thrPlain, "DBU", 1 }) };   // -20 ... +20 dBu
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
    const bool old = v[Pid::voice].step == kOldStep;
    e.topo = old ? kTopoFB : kTopoFF;       // OLD (FB) links with LinkCvSum inside the loop (Bus25Ballistics.h)
    if (old)                                // ADR-63: the published (closed-loop) attack -> the loop's open-loop tau
        e.atkTauMs *= 1.f + stage::QuadKnee::loopGain(e.slope);
    // m[] stays neutral (file comment: THRUST is the host tilt alone)
}

DetectorLaw bus25Law(const EngineParams&) noexcept { return DetectorLaw::rms; }

constexpr InternalSpec kBus25Int[] = { { "RMS DET", "DB", -60, 6, 1, false }, { "OWN CV", "DB", 0, 30, 1, false },
                                       { "LINKED CV", "DB", 0, 30, 1, true } };

} // namespace

extern constexpr ModeDescriptor kBus25 {
    .key = "bus-25", .name = "BUS 25", .group = Group::vca, .introducedInStateVersion = 1, .revision = 1,
    .provisional = false,
    .topologyLine = "VCA · NEW FEED-FORWARD / OLD FEEDBACK · RMS",
    .specLine = "BUS 25   VCA · RMS · 1.5–∞ · .03–30 MS · .05–2 S + VAR · THRUST · CV-SUM LINK",
    .params = kBus25Params, .physical = &bus25Physical,
    .stage2 = Stage2Kind::none, .linkLaw = LinkLaw::cvSum, .detectorLaw = &bus25Law,
    .hasColour = true, .colourStatic = true, .wantsLookahead = false,
    .rigor = Rigor::modelled, .family = CurveFamily::textbook,
    .attackSpec = &attackFromView, .releaseSpec = &releaseFromView, .tailSeconds = &tailFromRelease,
    .ctBudgetNsPerSample = 40, .internals = kBus25Int };   // S13 H1b: measured (docs/modes/budgets.md)

} // namespace fcdsp::modes

// Traits (Bus25.h, 01 §10.7): bus25::RmsCatch (RMS, window tau_R / 50, 20 dB jump catch); QuadKnee (3 W values);
// LinkCvSum; bus25::CvSumBranching (SmoothBranching, the CV-sum link solved inside the OLD loop); NoStage2; ColourNone;
// Flat; kTopologies = FF | FB (branch per chunk on e.topo).
