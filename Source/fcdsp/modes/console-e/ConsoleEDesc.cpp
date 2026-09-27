// Console E (slot 9, `console-e`): the Mode's descriptor, physical() and specs (D §2.4 the SSL E/G channel dynamics,
// M10; D §5.2–5.6 row M10). ConsoleE.h holds the traits and ConsoleE.cpp FCDSP_DEFINE_MODE(ConsoleE);
// docs/modes/console-e.md lists every [H] constant with its source.
//
// - External linkage (SPRINTS §7 D24): the traits header declares `extern const ModeDescriptor kConsoleE;`.
// - provisional = false: the Mode's own traits are installed, so the fidelity rows are blocking spec rows and the
//   goldens can be blessed. revision 1: the Mode has never shipped (tests/fixtures/modes-ever.tsv).
// - THRESHOLD reads +10 … -20 on its dial (D §2.4 [V S26]) around the operating level, 0 VU = -18 dBFS (ADR-59), as
//   Bus G's dial does: dial 0 = -18 dBFS, the host default and Clean's, and the dial spans -38 … -8 dBFS.
// - RATIO is continuous, 1:1 … inf:1 (S 0 … 1; D §5.3 "C 1 … inf, inf = limiter"). KNEE is the E module's defeat
//   switch: OVEREASY (a kOverEasyDb-wide quadratic knee) or HARD (D §5.3).
// - ATTACK is AUTO (program-dependent, 3–30 ms, D §2.4 [V S27]) or FAST (the F.ATK button, 1 ms); RELEASE is 0.1–4 s,
//   and the time-mode slot is the E module's release switch: LOG (the Revision 4 logarithmic release, RELEASE = tau)
//   or LIN (a constant 10 dB per RELEASE, VcaChannel.h). Both go to the ballistics through m[] (ConsoleE.h slots).
// - DETECT: RMS (the E module's "true RMS" side chain, a 10 ms window [H]) or PEAK (the 9000 J/K switch; the strip
//   guide's "peak sensing" [C]); RMS by default. The channel's HPF into the side chain is the host's SC HPF (18 dB/oct
//   on the console [V S26]; its range [U]). LINK is the adjacent-channel link: the larger GR drives both (LinkMax).
// - AUTO makeup is locked on ("calculated from the Ratio and Threshold settings", D §2.4 [V S26]); MAKEUP is an
//   extension trim after it. The makeup is the static curve's GR at 0 VU (-18 dBFS at the detector, kMakeupRefDbfs
//   [H]), so a signal at the operating level keeps its level as THRESHOLD falls and RATIO rises (SSL: the output
//   "remains constant"). The engine's own AUTO law is r^(0 dBFS) (E §2.2; Bus 25, Clean), which would lift a 0 VU
//   signal by up to 18 (1 - 1/R) dB here; so physical() clears kEngAutoMakeup (Brickwall's pattern) and adds this
//   makeup to makeupDb, which the host smooths like any makeup.
// - No colour stage (hasColour = false; VOICE and DRIVE n/a): the channel VCA is clean and has no drive control. Bus
//   G's VcaBus at 0 dB drive adds H3 near -105 dB at 0 VU, nothing a colour slot could show, and a moving DRIVE's
//   per-tick steps (VoiceDrive) read above dsp.zipper's limit against this Mode's clean RMS-detected output
//   (docs/modes/console-e.md).
// - Not modelled (docs/modes/console-e.md): the expander/gate, the E module's SC EQ routing, and the E/G revision
//   difference ([U]).
// - Mode-local helpers live here in an unnamed namespace (ModeKit.h is frozen); the tables are namespace-scope
//   constants (no function-local statics, C D12).

#include "fcdsp/modes/console-e/ConsoleE.h"

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/ModeKit.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include <algorithm>
#include <cstdint>

namespace fcdsp::modes {

namespace {

using namespace kit;
using Ballistics = ConsoleE::Ballistics;

constexpr float kDialZeroDbfs = -18.f;           // [H] dial 0 = 0 VU (ADR-59)
constexpr float kOverEasyDb = 10.f;              // [H] the OVEREASY knee width (dB)
constexpr float kFastMs = 1.f;                   // F.ATK (D §2.4 [V S26])
constexpr float kAutoNominalMs = 10.f;           // AUTO's nominal value (the geometric middle of 3–30 ms)
constexpr int kLinStep = 1;                      // the time-mode slot's LIN position
constexpr float kMakeupRefDbfs = -18.f;          // [H] AUTO makeup's reference: 0 VU (ADR-59)

float thrDial (float p) noexcept { return p - kDialZeroDbfs; }     // dial -20 … +10 <-> -38 … -8 dBFS
float thrPlain(float d) noexcept { return d + kDialZeroDbfs; }

constexpr Step kKnee[]    = { { 0, "HARD", "HARD KNEE" }, { kOverEasyDb, "EASY", "OVEREASY", 0, "over easy" } };
constexpr Step kAtk[]     = { { kFastMs, "FAST", "FAST 1 MS" },
                              { kAutoNominalMs, "AUTO", "AUTO 3–30 MS", kTagAuto | kTagProgram, "auto" } };
constexpr Step kRelLaw[]  = { { 0, "LOG" }, { 1, "LIN", "LINEAR" } };
constexpr Step kDet[]     = { { 0, "RMS" }, { 1, "PEAK" } };
constexpr Step kAutoOn[]  = { { 1, "AUTO", "AUTO MAKEUP" } };

bool isAuto(const ParamView& v) noexcept { return (v[Pid::atk].tag & kTagAuto) != 0; }
bool isLin(const ParamView& v) noexcept { return v[Pid::tmode].step == kLinStep; }

constexpr ParamTable kConsoleEParams = [] {
    ParamTable t = allNa("NOT ON THIS CIRCUIT");
    t[Pid::thr]    = { named(cont(kDialZeroDbfs - 20.f, kDialZeroDbfs + 10.f, kDialZeroDbfs), nullptr,
                             { &thrDial, &thrPlain, "", 1 }) };
    t[Pid::ratio]  = { cont(0, 1, 0.75f) };                         // S: 1:1 … inf:1
    t[Pid::knee]   = { stepped(kKnee, kOverEasyDb) };
    t[Pid::range]  = { extRange() };
    t[Pid::atk]    = { stepped(kAtk, kAutoNominalMs) };
    t[Pid::rel]    = { cont(100, 4000, 300) };
    t[Pid::tmode]  = { named(stepped(kRelLaw, 0), "REL CURVE") };
    t[Pid::det]    = { stepped(kDet, 0) };
    t[Pid::schpf]  = { cont(0, 350, 0) };                           // < 20 Hz = OFF
    t[Pid::link]   = { stepped(kDualLink, 1) };
    t[Pid::stmode] = { extStereo() };
    t[Pid::voice]  = { na(0, "THE CHANNEL VCA IS CLEAN") };
    t[Pid::drive]  = { na(0, "THE CHANNEL HAS NO DRIVE CONTROL") };
    t[Pid::makeup] = { ext(cont(-24, 12, 0)) };                     // a trim after the automatic makeup
    t[Pid::automu] = { locked(kAutoOn, "THE CHANNEL'S MAKEUP IS ALWAYS AUTOMATIC") };
    t[Pid::mix]    = { extMix() };
    return t;
}();

// QuadKnee's static GR at the detector level x (QuadKnee.h), RANGE's clamp included.
float curveGrDb(const EngineParams& e, float x) noexcept {
    const float w = std::max(e.kneeDb, stage::QuadKnee::kMinKneeDb);
    const float o = x - e.thrDb;
    const float q = std::clamp(o + 0.5f * w, 0.f, w);
    return std::min(e.slope * (q * q / (2.f * w) + std::max(0.f, o - 0.5f * w)), e.rangeDb);
}

void consoleEPhysical(const ParamView& v, EngineParams& e) noexcept {
    e.topo = kTopoFF;
    e.m[ConsoleE::kAutoAttackSlot] = isAuto(v) ? 1.f : 0.f;
    e.m[ConsoleE::kLinReleaseSlot] = isLin(v) ? 1.f : 0.f;
    e.flags = static_cast<uint8_t>(e.flags & ~kEngAutoMakeup);            // this Mode's own AUTO (file comment)
    e.makeupDb += curveGrDb(e, kMakeupRefDbfs + e.preGainDb);
}

DetectorLaw consoleELaw(const EngineParams& e) noexcept { return e.det == 0 ? DetectorLaw::rms : DetectorLaw::peak; }

// AUTO: program-dependent over the published 3–30 ms.
TimeSpec consoleEAttackSpec(const ParamView& v, const EngineParams& e) noexcept {
    TimeSpec t = attackFromView(v, e);
    if (isAuto(v)) {
        t.program = true;
        t.lo = Ballistics::kAutoFastMs / 1000.f;
        t.hi = Ballistics::kAutoSlowMs / 1000.f;
    }
    return t;
}

// LIN: a slew rate, kLinDb per RELEASE (dB/s).
TimeSpec consoleEReleaseSpec(const ParamView& v, const EngineParams& e) noexcept {
    TimeSpec t = releaseFromView(v, e);
    if (isLin(v) && t.seconds > 0.f) {
        t.law = TimeLaw::rateDbPerS;
        t.seconds = Ballistics::kLinDb / t.seconds;
    }
    return t;
}

// LOG: 5 tau; LIN: 60 dB (RANGE's end) at kLinDb per RELEASE.
float consoleETail(const EngineParams& e) noexcept {
    const float n = e.m[ConsoleE::kLinReleaseSlot] > 0.5f ? 60.f / Ballistics::kLinDb : 5.f;
    return e.relTauMs > 0.f ? n * e.relTauMs / 1000.f : 0.f;
}

constexpr InternalSpec kConsoleEInt[] = { { "ATK EFF", "MS", 0, 30, 1, false } };

} // namespace

extern constexpr ModeDescriptor kConsoleE {
    .key = "console-e", .name = "CONSOLE E", .group = Group::vca, .introducedInStateVersion = 1, .revision = 1,
    .provisional = false,
    .topologyLine = "VCA CHANNEL · FEED-FORWARD · RMS/PEAK",
    .specLine = "CONSOLE E   VCA CHANNEL · 1–∞ · AUTO/1 MS · .1–4 S LOG/LIN · RMS/PEAK",
    .params = kConsoleEParams, .physical = &consoleEPhysical,
    .stage2 = Stage2Kind::none, .linkLaw = LinkLaw::max, .detectorLaw = &consoleELaw,
    .hasColour = false, .colourStatic = true, .wantsLookahead = false,
    .rigor = Rigor::character, .family = CurveFamily::textbook,
    .attackSpec = &consoleEAttackSpec, .releaseSpec = &consoleEReleaseSpec, .tailSeconds = &consoleETail,
    .ctBudgetNsPerSample = 45, .internals = kConsoleEInt };

} // namespace fcdsp::modes
