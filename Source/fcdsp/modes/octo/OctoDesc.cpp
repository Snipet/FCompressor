// Octo (slot 8, `octo`): the Mode's descriptor, physical() and specs (D §2.7 the hybrid VCA, M18; §5.2–5.6 row M18).
// Octo.h holds the traits and Octo.cpp FCDSP_DEFINE_MODE(Octo); docs/modes/octo.md lists every [H] constant with its
// source.
//
// - External linkage (SPRINTS §7 D24): the traits header declares `extern const ModeDescriptor kOcto;`.
// - provisional = false: the Mode's own traits are installed, so the fidelity rows are blocking spec rows and the
//   goldens can be blessed. revision 1: the Mode has never shipped (tests/fixtures/modes-ever.tsv).
// - INPUT drives a fixed threshold (D §2.7 [V S14]: "Input 0–10: drives into a fixed threshold"). kT0 = -18 dBFS, the
//   0 VU calibration every Mode uses (ADR-59), so the dial's centre (5) is also the host default threshold and Clean's.
//   The dial is linear in dB, kDialDbPerUnit = 4 dB per unit [H]: 0 … 10 = -20 … +20 dB into the threshold.
// - OUTPUT is a 0–10 dial with unity at 5 [H] (4 dB per unit, -20 … +20 dB). The hardware's "Output at 8" is its tape
//   level (+4 dBu), a calibration FCompressor does not reproduce: unity mid-dial keeps a fresh Octo at unity gain.
// - RATIO 1, 2, 3, 4, 6, 10 (OPTO), 20, NUKE (D §2.7 [V S14]); the knee follows the ratio (D §5.3 "P per ratio"):
//   2:1 and 3:1 very wide ("parabolic knees … won't typically go into hard limiting"), 4:1 and 6:1 "steeper", 10:1 a
//   "short knee", 20:1 and NUKE hard ("brick wall … within 1 dB or so"). 1:1 is S = 0: no gain reduction, only the
//   audio circuit ("just the warming circuits").
// - ATTACK 0.05 … 30 ms and RELEASE 0.05 … 3.5 s (D §2.7 [V S14]: the 0–10 knobs), shown in time units. 10:1 OPTO:
//   RELEASE reaches 20 s (the dependent range, a Variant on `ratio`, D §5.7 "M18 rel hi depends on ratio") and the
//   ballistics turn to DualRelease: a fast release of kOptoFastShare × RELEASE (at least 50 ms), a slow path charging
//   in kOptoChargeMs and releasing at RELEASE, so the GR falls quickly after a transient and slowly after sustained
//   program, as an opto cell's memory does (the program-dependent release the unit's opto mode emulates).
// - DETECT: the side-chain HP is `schpf` (OFF / HP 90 Hz; "cuts low frequencies in detector", ≈80–100 Hz [C]); BAND
//   EMPH is `sce` (OFF / BE: a +kEmphasisDb bell at 6 kHz, BandEmphasis.h; the host's own SC tilt stays neutral); LINK
//   (the unit's Link: "sums both inputs") links the two lanes' GR by their mean.
// - AUDIO is `voice`: CLEAN / HP / DIST 2 / DIST 2 + HP / DIST 3 / DIST 3 + HP (D §2.7 [V S14]), OctoDist.h. INPUT
//   drives the distortion generator as it drives the threshold; DRIVE (an extension, ±24 dB) adds to it.
// - Not modelled (docs/modes/octo.md): the British mode of the later model (an 1176 all-buttons emulation), NUKE's
//   logarithmic release and 20:1's "different release slope", and the special detector circuitry of 2:1, 10:1 and NUKE.
// - Mode-local helpers (the dial maps, the knee table) live here in an unnamed namespace (ModeKit.h is frozen); the
//   tables are namespace-scope constants (no function-local statics, C D12).

#include "fcdsp/modes/octo/Octo.h"

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

constexpr float kT0 = -18.f;                     // [H] the fixed threshold: 0 VU (ADR-59)
constexpr float kDialCentre = 5.f;               // INPUT / OUTPUT dial unity
constexpr float kDialDbPerUnit = 4.f;            // [H] both dials, linear in dB
constexpr float kDialHalfSpanDb = kDialCentre * kDialDbPerUnit;   // 20 dB
constexpr float kEmphasisDb = 10.f;              // [H] BAND EMPH: +10 dB at 6 kHz in the side chain
constexpr float kOptoFastShare = 0.2f;           // [H] OPTO: the fast release is a fifth of RELEASE ...
constexpr float kOptoFastMinMs = 50.f;           // ... and never faster than the shortest release
constexpr float kOptoChargeMs = 800.f;           // [H] OPTO: the slow path (the cell's memory) charges in 0.8 s
constexpr float kOptoReleaseHiShare = 1.5f;      // OPTO release spec: the cascade's 1/e time runs past RELEASE

float inDial  (float thr) noexcept { return kDialCentre + (kT0 - thr) / kDialDbPerUnit; }
float inPlain (float d)   noexcept { return kT0 - (d - kDialCentre) * kDialDbPerUnit; }
float outDial (float mu)  noexcept { return kDialCentre + mu / kDialDbPerUnit; }
float outPlain(float d)   noexcept { return (d - kDialCentre) * kDialDbPerUnit; }

constexpr int kOptoStep = 5;
constexpr Step kRatio[] = { { 0.0f, "1", "1:1 (NO COMPRESSION)" }, { 0.5f, "2", "2:1" }, { 0.66667f, "3", "3:1" },
                            { 0.75f, "4", "4:1" }, { 0.83333f, "6", "6:1" },
                            { 0.9f, "10", "10:1 OPTO", 0, "ten to one, opto" },
                            { 0.95f, "20", "20:1" },
                            { 1.0f, "NUKE", "NUKE (BRICK WALL)", 0, "nuke" } };
// [H] D §5.3 "P per ratio": knee width (dB) per ratio step 1 / 2 / 3 / 4 / 6 / 10 / 20 / NUKE.
constexpr float kKneeByRatio[] = { 6.f, 18.f, 14.f, 10.f, 8.f, 5.f, 2.f, 0.f };
constexpr Step kHp[]    = { { 0, "OFF" }, { 90, "HP", "DETECTOR HP 90 HZ" } };
constexpr Step kBe[]    = { { 0, "OFF" }, { 1, "BE", "6 KHZ BAND EMPHASIS" } };
constexpr Step kAudio[] = { { 0, "CLEAN" }, { 1, "HP", "AUDIO HP (65 HZ, 18 DB/OCT)" }, { 2, "D2", "DIST 2" },
                            { 3, "D2 HP", "DIST 2 + HP" }, { 4, "D3", "DIST 3" }, { 5, "D3 HP", "DIST 3 + HP" } };
constexpr Step kVcaDet[] = { { 0, "VCA", "VCA DETECTOR (PEAK)" } };
constexpr Variant kRelByRatio[] = { { kOptoStep, cont(50, 20000, 500) } };   // 10:1 OPTO: up to 20 s

std::size_t ratioStep(const ParamView& v) noexcept {
    const int s = v[Pid::ratio].step;
    return static_cast<std::size_t>(s < 0 ? 3 : s);
}
bool isOpto(const ParamView& v) noexcept { return v[Pid::ratio].step == kOptoStep; }

float kneeFromRatio(const ParamView& v) noexcept { return kKneeByRatio[ratioStep(v)]; }

float optoFastMs(float relMs) noexcept {
    const float f = relMs * kOptoFastShare;
    return f > kOptoFastMinMs ? f : kOptoFastMinMs;
}

constexpr ParamTable kOctoParams = [] {
    ParamTable t = allNa("NOT ON THIS CIRCUIT");
    t[Pid::thr]    = { named(cont(kT0 - kDialHalfSpanDb, kT0 + kDialHalfSpanDb, kT0), "INPUT",
                             { &inDial, &inPlain, "", 1, /*invert*/true }) };
    t[Pid::ratio]  = { stepped(kRatio, 0.75f) };
    t[Pid::knee]   = { derived(&kneeFromRatio, Pid::ratio, "= RATIO",
                               "EACH RATIO HAS ITS OWN CURVE: WIDE AT 2 AND 3, HARD AT 20 AND NUKE") };
    t[Pid::range]  = { extRange() };
    t[Pid::atk]    = { cont(0.05f, 30.f, 1.f) };
    t[Pid::rel]    = { cont(50, 3500, 500), Pid::ratio, kRelByRatio };
    t[Pid::tmode]  = { na(0, "10:1 IS THE OPTO RELEASE; THERE IS NO TIME MODE") };
    t[Pid::det]    = { locked(kVcaDet, "THE VCA'S OWN PEAK DETECTOR") };
    t[Pid::schpf]  = { stepped(kHp, 0) };
    t[Pid::sce]    = { named(stepped(kBe, 0), "BAND EMPH") };
    t[Pid::link]   = { stepped(kDualLink, 1) };
    t[Pid::stmode] = { extStereo() };
    t[Pid::voice]  = { named(stepped(kAudio, 0), "AUDIO") };
    t[Pid::drive]  = { extDrive() };                             // more drive into the AUDIO distortion (extension)
    t[Pid::makeup] = { named(cont(-kDialHalfSpanDb, kDialHalfSpanDb, 0), "OUTPUT", { &outDial, &outPlain, "", 1 }) };
    t[Pid::mix]    = { extMix() };
    return t;
}();

void octoPhysical(const ParamView& v, EngineParams& e) noexcept {
    e.topo = kTopoFF;
    e.preGainDb = kT0 - v[Pid::thr].plain;                        // the INPUT knob's gain: into the threshold and the
    e.thrDb = kT0;                                                // distortion generator alike
    e.m[Octo::kEmphasisSlot] = v[Pid::sce].step == 1 ? kEmphasisDb : 0.f;
    e.sceDbOct = 0.f;                                             // the band emphasis is BandEmphasis's alone
    if (isOpto(v)) {                                              // DualRelease: fast, charge, slow (file comment)
        const float rel = v[Pid::rel].plain;
        e.m[Octo::kOptoSlot] = 1.f;
        e.m[Octo::kFastReleaseSlot] = optoFastMs(rel);
        e.m[Octo::kChargeSlot] = kOptoChargeMs;
        e.m[Octo::kSlowReleaseSlot] = rel;
    }
}

DetectorLaw octoLaw(const EngineParams&) noexcept { return DetectorLaw::peak; }

// OPTO: program-dependent, from the fast release after a transient to past RELEASE after sustained program.
TimeSpec octoReleaseSpec(const ParamView& v, const EngineParams& e) noexcept {
    TimeSpec t = releaseFromView(v, e);
    if (isOpto(v)) {
        t.program = true;
        t.lo = optoFastMs(v[Pid::rel].plain) / 1000.f;
        t.hi = v[Pid::rel].plain * kOptoReleaseHiShare / 1000.f;
    }
    return t;
}

float octoTail(const EngineParams& e) noexcept {                  // 5 x the longest release tau (s)
    const float tau = e.m[Octo::kOptoSlot] > 0.5f && e.m[Octo::kSlowReleaseSlot] > e.relTauMs
                    ? e.m[Octo::kSlowReleaseSlot] : e.relTauMs;
    return tau > 0.f ? 5.f * tau / 1000.f : 0.f;
}

constexpr InternalSpec kOctoInt[] = { { "FAST ENV", "DB", 0, 30, 1, false }, { "SLOW ENV", "DB", 0, 30, 1, true },
                                      { "OPTO SLOW", "", 0, 1, 0, false } };

} // namespace

extern constexpr ModeDescriptor kOcto {
    .key = "octo", .name = "OCTO", .group = Group::vca, .introducedInStateVersion = 1, .revision = 1,
    .provisional = false,
    .topologyLine = "HYBRID VCA · FEED-FORWARD · PEAK",
    .specLine = "OCTO   HYBRID VCA · 1–NUKE · .05–30 MS · .05–3.5 S (OPTO 20 S) · DIST 2/3",
    .params = kOctoParams, .physical = &octoPhysical,
    .stage2 = Stage2Kind::none, .linkLaw = LinkLaw::mean, .detectorLaw = &octoLaw,
    .hasColour = true, .colourStatic = true, .wantsLookahead = false,
    .rigor = Rigor::character, .family = CurveFamily::custom,
    .attackSpec = &attackFromView, .releaseSpec = &octoReleaseSpec, .tailSeconds = &octoTail,
    .ctBudgetNsPerSample = 50, .internals = kOctoInt };

} // namespace fcdsp::modes
