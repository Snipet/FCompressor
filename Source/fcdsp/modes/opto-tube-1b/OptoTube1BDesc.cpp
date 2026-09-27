// Opto Tube 1B (slot 13, `opto-tube-1b`): the Mode's descriptor, physical() and specs (D §2.2 the Tube-Tech CL 1B, M04;
// D §5.2–5.6 row M04). OptoTube1B.h holds the traits and OptoTube1B.cpp FCDSP_DEFINE_MODE(OptoTube1B);
// docs/modes/opto-tube-1b.md lists every [H] constant with its source.
//
// - External linkage (SPRINTS §7 D24): the traits header declares `extern const ModeDescriptor kOptoTube1B;`.
// - provisional = false: the Mode's own traits are installed, so the fidelity rows are blocking spec rows and the
//   goldens can be blessed. revision 1: the Mode has never shipped (tests/fixtures/modes-ever.tsv).
// - THRESHOLD reads +20 … -40 on its dial (D §2.2 [V S13]) around 0 VU (-18 dBFS, ADR-59), plus OFF: a hybrid, -58 …
//   +2 dBFS continuous and the OFF step outside it (+24 dBFS, where nothing compresses). RATIO 2 … 10:1 continuous
//   (S 0.5 … 0.9). KNEE is the opto cell's soft knee, locked at kKneeDb [H] (D §5.3 "soft opto" [U]).
// - ATTACK/RELEASE SELECT (`tmode`, D §2.2 [V S13]): FIXED is attack 1 ms and release 50 ms (the knobs lock there);
//   MANUAL the knobs, ATTACK 0.5 … 300 ms and RELEASE 0.05 … 10 s; FIX/MAN a fast fixed attack (1 ms), after a short
//   peak the fixed fast release, and "the Attack knob becomes a delay before the manual Release takes over, and this
//   applies only when the peak is shorter than the Attack setting": DualRelease with the fixed 50 ms as the fast path,
//   the ATTACK knob (relabelled DELAY) setting the slow path's charge, and RELEASE as its release (OptoTube1B.h slots).
//   The charge is a one-pole over kDelayChargeShare x DELAY [H, fitted]: it also charges while the fast path falls, so
//   at 1 x DELAY a 30 ms peak under a 100 ms DELAY kept enough to hold the release for 284 ms; at 2 x it releases
//   fast, and a peak held well past DELAY hands the GR to the manual release (dsp.optotube1b).
// - DETECT is the optical cell (OptoSense, Opto 2A's sensor; DetectorLaw custom); SC LOW CUT (the Mk II's, frequency
//   [U]) is the host's SC HPF; LINK is the side-chain bus link; MIX the Mk II's (native, 0–100 %).
// - GAIN 0 … +30 dB (the original's "off" position is not modelled). VOICE locked TUBE: TubeTransformer (Opto 2A's
//   tube and transformer stage) driven kTubeTrimDb under Opto 2A's calibration: the CL 1B's tube stage is clean at
//   working levels, and at Opto 2A's level its small level-dependent gain, run at the OS rate, moved a 0.5 s
//   release by 2.2 samples between ECO and HQ (dsp.time's quality spread, <= 1.5); DRIVE is the standard extension.
// - Mode-local helpers live here in an unnamed namespace (ModeKit.h is frozen); the tables are namespace-scope constants
//   (no function-local statics, C D12).

#include "fcdsp/modes/opto-tube-1b/OptoTube1B.h"

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

constexpr float kDialZeroDbfs = -18.f;           // [H] dial 0 = 0 VU (ADR-59)
constexpr float kKneeDb = 10.f;                  // [H] the opto cell's soft knee
constexpr float kFixedAttackMs = 1.f, kFixedReleaseMs = 50.f;   // FIXED (D §2.2 [V S13]); FIX/MAN's fast path
constexpr float kProgramHiShare = 1.5f;          // FIX/MAN's release spec: the cascade's 1/e runs past RELEASE
constexpr float kDelayChargeShare = 2.0f;        // [H, fitted] FIX/MAN: the slow path charges over 2 x DELAY
constexpr int kFixedStep = 1, kFixManStep = 2;
constexpr float kTubeTrimDb = -12.f;             // [H] the tube stage's drive under Opto 2A's calibration (file comment)

float thrDial (float p) noexcept { return p - kDialZeroDbfs; }   // dial +20 … -40 <-> +2 … -58 dBFS
float thrPlain(float d) noexcept { return d + kDialZeroDbfs; }

constexpr Step kThrOff[] = { { 24, "OFF", "OFF (NO COMPRESSION)" } };
// MANUAL first: `tmode` 0 is every Mode's manual timing (Clean, Brickwall: 1 is their AUTO), so a Mode switch keeps it.
constexpr Step kSel[]    = { { 0, "MAN", "MANUAL" }, { 1, "FIX", "FIXED" }, { 2, "F/M", "FIX/MAN" } };
constexpr Step kOpto[]   = { { 0, "OPTO", "OPTICAL CELL" } };
constexpr Step kTube[]   = { { 0, "TUBE", "TUBE + TRANSFORMERS" } };

constexpr ParamSpec thresholdSpec() noexcept {
    ParamSpec s = named(hybrid(kDialZeroDbfs - 40.f, kDialZeroDbfs + 20.f, kThrOff, kDialZeroDbfs), nullptr,
                        { &thrDial, &thrPlain, "", 1 });
    s.reason = "+20 … −40 DB, OR OFF";
    return s;
}

// FIXED locks the knobs; FIX/MAN turns ATTACK into the DELAY before the manual release.
constexpr Variant kAtkBySel[] = { { kFixedStep, locked(kFixedAttackMs, "FIXED: 1 MS ATTACK") },
                                  { kFixManStep, named(cont(0.5f, 300, 30), "DELAY") } };
constexpr Variant kRelBySel[] = { { kFixedStep, locked(kFixedReleaseMs, "FIXED: 50 MS RELEASE") } };

bool isFixMan(const ParamView& v) noexcept { return v[Pid::tmode].step == kFixManStep; }

constexpr ParamTable kOptoTube1BParams = [] {
    ParamTable t = allNa("NOT ON THIS CIRCUIT");
    t[Pid::thr]    = { thresholdSpec() };
    t[Pid::ratio]  = { cont(0.5f, 0.9f, 0.75f) };                  // S: 2:1 … 10:1
    t[Pid::knee]   = { locked(kKneeDb, "THE OPTICAL CELL'S SOFT KNEE") };
    t[Pid::range]  = { extRange() };
    t[Pid::atk]    = { cont(0.5f, 300, 30), Pid::tmode, kAtkBySel };
    t[Pid::rel]    = { cont(50, 10000, 500), Pid::tmode, kRelBySel };
    t[Pid::tmode]  = { named(stepped(kSel, 0), "A/R SELECT") };
    t[Pid::det]    = { locked(kOpto, "THE OPTICAL CELL'S LIGHT") };
    t[Pid::schpf]  = { named(cont(0, 300, 0), "SC LOW CUT") };    // < 20 Hz = OFF
    t[Pid::link]   = { stepped(kDualLink, 1) };
    t[Pid::stmode] = { extStereo() };
    t[Pid::voice]  = { locked(kTube, "TUBE AMPLIFIER AND TRANSFORMERS") };
    t[Pid::drive]  = { extDrive() };
    t[Pid::makeup] = { named(cont(0, 30, 0), "GAIN") };
    t[Pid::mix]    = { cont(0, 1, 1) };                             // the Mk II's
    return t;
}();

void optoTubePhysical(const ParamView& v, EngineParams& e) noexcept {
    e.topo = kTopoFF;
    e.driveDb += kTubeTrimDb;                                       // the clean tube stage (file comment)
    if (isFixMan(v)) {                                              // DualRelease: fast, DELAY, manual (file comment)
        e.m[OptoTube1B::kFixManSlot] = 1.f;
        e.m[OptoTube1B::kFastReleaseSlot] = kFixedReleaseMs;
        e.m[OptoTube1B::kDelaySlot] = kDelayChargeShare * e.atkTauMs;   // the ATTACK knob, relabelled DELAY
        e.m[OptoTube1B::kManualReleaseSlot] = e.relTauMs;
        e.atkTauMs = kFixedAttackMs;                                // the fixed fast attack
    }
}

DetectorLaw optoTubeLaw(const EngineParams&) noexcept { return DetectorLaw::custom; }

// FIX/MAN: the fixed fast attack.
TimeSpec optoTubeAttack(const ParamView& v, const EngineParams& e) noexcept {
    TimeSpec t = attackFromView(v, e);
    if (isFixMan(v))
        t = TimeSpec{ kFixedAttackMs / 1000.f, TimeLaw::expDb };
    return t;
}

// FIX/MAN: program-dependent, from the fixed fast release (a short peak) to past RELEASE (a long one).
TimeSpec optoTubeRelease(const ParamView& v, const EngineParams& e) noexcept {
    TimeSpec t = releaseFromView(v, e);
    if (isFixMan(v)) {
        t.program = true;
        t.lo = kFixedReleaseMs / 1000.f;
        const float hi = v[Pid::rel].plain * kProgramHiShare / 1000.f;
        t.hi = hi > 1.5f * t.lo ? hi : 1.5f * t.lo;
    }
    return t;
}

constexpr InternalSpec kOptoTubeInt[] = { { "FAST ENV", "DB", 0, 30, 1, false }, { "SLOW ENV", "DB", 0, 30, 1, true },
                                          { "MAN HOLD", "", 0, 1, 0, false } };

} // namespace

extern constexpr ModeDescriptor kOptoTube1B {
    .key = "opto-tube-1b", .name = "OPTO TUBE 1B", .group = Group::opto, .introducedInStateVersion = 1,
    .revision = 1, .provisional = false,
    .topologyLine = "OPTICAL · TUBE · FEED-FORWARD",
    .specLine = "OPTO TUBE 1B   OPTO + TUBE · 2–10:1 · .5–300 MS · .05–10 S · FIXED/MANUAL/FIX-MAN",
    .params = kOptoTube1BParams, .physical = &optoTubePhysical,
    .stage2 = Stage2Kind::none, .linkLaw = LinkLaw::max, .detectorLaw = &optoTubeLaw,
    .hasColour = true, .colourStatic = true, .wantsLookahead = false,
    .rigor = Rigor::character, .family = CurveFamily::textbook,
    .attackSpec = &optoTubeAttack, .releaseSpec = &optoTubeRelease, .tailSeconds = &tailFromRelease,
    .ctBudgetNsPerSample = 50, .internals = kOptoTubeInt };

} // namespace fcdsp::modes
