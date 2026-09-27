// Mu Mastering (slot 12, `mu-mastering`): the Mode's descriptor, physical() and specs (D §2.3 the Manley Variable Mu,
// Mastering version, M06; D §5.2–5.6 row M06). MuMastering.h holds the traits and MuMastering.cpp
// FCDSP_DEFINE_MODE(MuMastering); docs/modes/mu-mastering.md lists every [H] constant with its source.
//
// - External linkage (SPRINTS §7 D24): the traits header declares `extern const ModeDescriptor kMuMastering;`.
// - provisional = false: the Mode's own traits are installed, so the fidelity rows are blocking spec rows and the
//   goldens can be blessed. revision 1: the Mode has never shipped (tests/fixtures/modes-ever.tsv).
// - COMPRESS is 1.5:1 with a soft knee; LIMIT is 4:1, and "at greater than 12 dB of limiting the ratio increases (up to
//   20:1)", "like a compressor followed by a limiter" (D §2.3 [V S12]). Stage 1 is QuadKnee in feedback at the switch's
//   ratio with the knee derived from it (the output-domain W: 15 dB at the input at 1.5:1, 7.5 dB at 4:1 [H]); in
//   LIMIT, stage 2 (SharedElementMaxT, 20:1 with a 3 dB knee) shares the element kRiseOnsetDb above the threshold at
//   the output, so the element's GR leaves the 4:1 curve for the 20:1 one past about 12 dB of GR (fitted, measured by
//   dsp.mumastering: 4.00:1 at 6 dB of GR, 8:1 passed at 12.25 dB, 18.8:1 at 25 dB; docs/modes/mu-mastering.md). Stage 2 runs on the same side chain's times (its s2 slots are n/a) and is out in
//   COMPRESS.
// - THRESHOLD: the Mastering version's 21 steps, "calibrated to LIMIT mode", about 12 dB in LIMIT (D §2.3 [V S12]):
//   the dial 0 ... 20 reads -12 ... -24 dBFS, 0.6 dB a step. The unit's narrower span in COMPRESS (about 6 dB) is not
//   modelled: the input threshold must move 1:1 with the parameter (K1 #9, dsp.registry's thr.slope; the TRANSFER
//   handle's drag). INPUT (`drive`, -10 ... +10 dB [H]) is the input attenuator: a gain into the threshold and the tubes
//   alike ("pushing Input and pulling Output gives gentle tube distortion"); the tubes run kTubeTrimDb under Mu 67's
//   calibration (the unit is under 0.1 % THD at 1 kHz, D §2.3 [V S12], and at 1.5:1 the wet signal is hotter: meter
//   truth). OUTPUT is the output attenuator, shown as its dial with unity at -11.5 (the Mastering version's 0.5 dB steps
//   are continuous here).
// - ATTACK: 25–70 ms continuous on the standard unit, 11 positions "slightly extended in both directions" on the
//   Mastering version [U values]: 20 ... 80 ms [H]. RECOVERY 0.2 / 0.4 / 0.6 / 4 / 8 s (D §2.3 [V S12]). ADR-63: the
//   attack is the closed loop's at the switch's ratio, converted to the open-loop tau t (1 + k); the rise runs the same
//   open-loop times (one side chain, one RC), so it closes faster by its own loop gain.
// - SC HPF: the unit's own switch, OFF / 100 Hz (-3 dB at 100 Hz, D §2.3 [V S12]), the host SC HPF. LINK: SEP / LINK,
//   the DC control voltages combined (LinkMax). STEREO / M/S: the Mastering version's M/S encode and decode.
// - VOICE is locked 5670 (TubePushPull, Mu 67's push-pull tube stage); the T-Bar tube option is not modelled.
// - Mode-local helpers live here in an unnamed namespace (ModeKit.h is frozen); the tables are namespace-scope constants
//   (no function-local statics, C D12).

#include "fcdsp/modes/mu-mastering/MuMastering.h"

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

constexpr float kThrTopDbfs = -12.f;             // THRESHOLD dial 0 (LIMIT) ...
constexpr float kThrStepDb = 0.6f;               // ... 0.6 dB a step: dial 20 = -24 dBFS
constexpr float kThrRefDbfs = -18.f;             // the dial's centre (10), 0 VU: the default
constexpr float kTubeTrimDb = -8.f;              // [H] the tubes' drive against Mu 67's calibration (file comment)
constexpr float kCompressKneeDb = 12.f;          // [H] the output-domain knees: 15 dB at the input at 1.5:1 (soft)
constexpr float kLimitKneeDb = 3.0f;             // [H, fitted] ... and 7.5 dB at 4:1 (4:1 from about 5 dB of GR)
constexpr float kRiseOnsetDb = 3.6f;             // [H, fitted] LIMIT's 20:1 section above the threshold (output)
constexpr float kUnityDial = -11.5f;             // OUTPUT's dial at unity (the Mastering version)
constexpr int kLimitStep = 1;

float thrDial (float p) noexcept { return (kThrTopDbfs - p) / kThrStepDb; }   // dial 0 ... 20 <-> -12 ... -24 dBFS
float thrPlain(float d) noexcept { return kThrTopDbfs - d * kThrStepDb; }
float outDial (float mu) noexcept { return mu + kUnityDial; }
float outPlain(float d)  noexcept { return d - kUnityDial; }

constexpr Step kThr[] = { { -24.0f, "20" }, { -23.4f, "19" }, { -22.8f, "18" }, { -22.2f, "17" }, { -21.6f, "16" },
                          { -21.0f, "15" }, { -20.4f, "14" }, { -19.8f, "13" }, { -19.2f, "12" }, { -18.6f, "11" },
                          { -18.0f, "10" }, { -17.4f, "9" },  { -16.8f, "8" },  { -16.2f, "7" },  { -15.6f, "6" },
                          { -15.0f, "5" },  { -14.4f, "4" },  { -13.8f, "3" },  { -13.2f, "2" },  { -12.6f, "1" },
                          { -12.0f, "0" } };
constexpr Step kRatio[] = { { 1.0f - 1.0f / 1.5f, "COMP", "COMPRESS 1.5:1" },
                            { 0.75f, "LIMIT", "LIMIT 4:1, TO 20:1" } };
constexpr float kKneeByStep[] = { kCompressKneeDb, kLimitKneeDb };
constexpr Step kAtk[]   = { { 20, "20" }, { 25, "25" }, { 30, "30" }, { 35, "35" }, { 40, "40" }, { 45, "45" },
                            { 50, "50" }, { 55, "55" }, { 60, "60" }, { 70, "70" }, { 80, "80" } };
constexpr Step kRel[]   = { { 200, ".2", "0.2 S" }, { 400, ".4", "0.4 S" }, { 600, ".6", "0.6 S" }, { 4000, "4", "4 S" },
                            { 8000, "8", "8 S" } };
constexpr Step kHpf[]   = { { 0, "OFF" }, { 100, "100", "100 HZ" } };
constexpr Step kLink[]  = { { 0, "SEP", "SEPARATE" }, { 1, "LINK", "LINKED" } };
constexpr Step kTube[]  = { { 0, "TUBE", "TUBE SIDE CHAIN" } };
constexpr Step kVoice[] = { { 0, "5670", "5670 TUBES + TRANSFORMERS" } };

bool isLimit(const ParamView& v) noexcept { return v[Pid::ratio].step == kLimitStep; }

float kneeFromSwitch(const ParamView& v) noexcept {
    const int s = v[Pid::ratio].step;
    return kKneeByStep[s == kLimitStep ? 1 : 0];
}

constexpr ParamTable kMuMasteringParams = [] {
    ParamTable t = allNa("NOT ON THIS CIRCUIT");
    t[Pid::thr]    = { named(stepped(kThr, kThrRefDbfs), nullptr, { &thrDial, &thrPlain, "", 0, /*invert*/true }) };
    t[Pid::ratio]  = { stepped(kRatio, 1.0f - 1.0f / 1.5f) };
    t[Pid::knee]   = { derived(&kneeFromSwitch, Pid::ratio, "= RATIO",
                               "SOFT IN COMPRESS; IN LIMIT THE KNEE SOFTENS INTO 20:1 PAST 12 DB") };
    t[Pid::range]  = { na(60, "NO GAIN-REDUCTION LIMIT INSIDE THIS FEEDBACK LOOP") };
    t[Pid::atk]    = { stepped(kAtk, 50) };
    t[Pid::rel]    = { named(stepped(kRel, 600), "RECOVERY") };
    t[Pid::tmode]  = { na(0, "NO TIME MODE ON THIS CIRCUIT") };
    t[Pid::det]    = { locked(kTube, "TUBE SIDE CHAIN, SENSING THE OUTPUT (FEEDBACK)") };
    t[Pid::schpf]  = { stepped(kHpf, 0) };
    t[Pid::link]   = { stepped(kLink, 1) };
    t[Pid::stmode] = { stepped(kStereoMs, 0) };
    t[Pid::voice]  = { locked(kVoice, "REMOTE-CUTOFF TUBES AND TRANSFORMERS") };
    t[Pid::drive]  = { named(cont(-10, 10, 0), "INPUT") };
    t[Pid::makeup] = { named(cont(-12, 12, 0), "OUTPUT", { &outDial, &outPlain, "", 1 }) };
    t[Pid::mix]    = { extMix() };
    t[Pid::s2thr]  = { na(24, "THE LIMIT CURVE'S RISE TO 20:1 IS PART OF THE LIMIT SWITCH") };
    t[Pid::s2atk]  = { na(1, "THE RISE RUNS ON THE SAME SIDE CHAIN'S ATTACK") };
    t[Pid::s2rel]  = { na(100, "THE RISE RUNS ON THE SAME SIDE CHAIN'S RECOVERY") };
    return t;
}();

void muMasteringPhysical(const ParamView& v, EngineParams& e) noexcept {
    e.topo = kTopoFB;                                       // the gain control chain is feedback (D §2.3 [V S12])
    const bool limit = isLimit(v);
    e.preGainDb = v[Pid::drive].plain;                      // INPUT: into the threshold and the tubes
    e.driveDb = v[Pid::drive].plain + kTubeTrimDb;
    // ADR-63: the published (closed-loop) attack -> the loop's open-loop tau. The rise is the same side chain (one RC):
    // it runs the same open-loop times, so its closed loop is faster by its larger loop gain, as the unit's is.
    e.atkTauMs *= 1.f + stage::QuadKnee::loopGain(e.slope);
    if (limit) {
        e.s2ThrDb = e.thrDb + kRiseOnsetDb;
        e.s2AtkTauMs = e.atkTauMs;
        e.s2RelTauMs = e.relTauMs;
    } else {
        e.s2ThrDb = kS2Off;
    }
}

DetectorLaw muMasteringLaw(const EngineParams&) noexcept { return DetectorLaw::peak; }

constexpr InternalSpec kMuMasteringInt[] = { { "COMP GR", "DB", 0, 30, 1, false }, { "LIMIT GR", "DB", 0, 30, 1, true },
                                             { "LIMIT WINS", "", 0, 1, 0, false } };

} // namespace

extern constexpr ModeDescriptor kMuMastering {
    .key = "mu-mastering", .name = "MU MASTERING", .group = Group::varimu, .introducedInStateVersion = 1,
    .revision = 1, .provisional = false,
    .topologyLine = "VARIABLE MU · FEEDBACK · COMPRESS/LIMIT",
    .specLine = "MU MASTERING   VARI-MU · 1.5:1 / 4:1→20:1 · 20–80 MS · .2–8 S · M/S",
    .params = kMuMasteringParams, .physical = &muMasteringPhysical,
    .stage2 = Stage2Kind::sharedElementMax, .linkLaw = LinkLaw::max, .detectorLaw = &muMasteringLaw,
    .hasColour = true, .colourStatic = false, .wantsLookahead = false,
    .rigor = Rigor::character, .family = CurveFamily::custom,
    .attackSpec = &attackFromView, .releaseSpec = &releaseFromView, .tailSeconds = &tailFromRelease,
    .ctBudgetNsPerSample = 70, .internals = kMuMasteringInt };

} // namespace fcdsp::modes
