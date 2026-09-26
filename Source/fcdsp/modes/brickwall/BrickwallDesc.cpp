// Brickwall (slot 7, `brickwall`): the Mode's descriptor, physical() and specs, from 01 §10.7's sketch, D §2.8 / §5
// and K1 #27, K2 #21. Brickwall.h holds the traits (the lookahead limiter, M7 S11) and Brickwall.cpp
// FCDSP_DEFINE_MODE(Brickwall). Constants and their reasons: docs/modes/brickwall.md.
//
// - External linkage (SPRINTS §7 D24): the traits header declares `extern const ModeDescriptor kBrickwall;`.
// - provisional = false (M7, S11: the final traits are installed, so its fidelity rows are blocking and its goldens can
//   be blessed); revision 1 (the Mode has never shipped: no modes-ever.tsv row, so no bump).
// - LOOKAHEAD is continuous 0.5–20 ms, and the resolver owns the budget (01 §4.4, K1 #8): locked at 0 with the BUDGET
//   OFF reason while labudget = OFF, else clamped to the budget; ATTACK is derived from the budget-clamped value, so it
//   also reads 0 while the budget is OFF (zero latency, overshoot allowed; the footer hint is keyed on
//   wantsLookahead && budget == off, 02 §6.6, K1 #35). The engine's box is that lookahead (SlidingMaxBox.h).
// - CEILING (makeup relabelled) sets the output: physical() makes makeupDb = ceiling − thrDb, so the threshold lands on
//   the ceiling; AUTO MAKEUP is n/a and kEngAutoMakeup is never set (K1 #27: no makeup applied twice). VOICE LOUD adds
//   LoudClip::kHeadroomDb (20 log10(4/3) - 0.25 = 2.25 dB): its soft clipper rounds held peaks 3.52 dB under the
//   threshold, and this much of it comes back as loudness while a held peak's fundamental stays under the ceiling
//   (LoudClip.h, docs/modes/brickwall.md "VOICE LOUD").
// - DETECT TP sets kEngTruePeak; its interpolator delay D_tp = 12 reaches the host through scDelaySamples (K2 #21a). TP
//   without enough lookahead budget (L_la − look + D_up < D_tp) cannot align: the descriptor cannot lock TP on the
//   budget (a step list has no driver other than a Pid), so the footer carries it (01 §10.7). TIME MODE AUTO sets
//   kEngAutoRelease (CrestAuto).
// Choices where the sketch is silent:
// - Group limit (crossmode.no_off: limiter-only), family textbook (∞:1 over a quadratic knee).
// - Rigor character (ADR-65): a ceiling-referenced output is never unity below threshold (makeupDb = ceiling − thrDb
//   even at CEILING 0 dBFS), so the clean and modelled below-threshold null rows of dsp.null (D8 (b)) cannot hold; the
//   character rule judges the THD golden instead.
// - SC HPF, drive, range, hold, SC emphasis and stage 2 are n/a (01 §10.2 matrix: "–").
// - Internals (E §7, limiter): HELD PEAK (history), LOOK EFF, TP OVER (Brickwall.h says what each reads).

#include "fcdsp/engine/stages/colour/LoudClip.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/ModeKit.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include <cstdint>

namespace fcdsp::modes {

extern const ModeDescriptor kBrickwall;          // also declared by modes/brickwall/Brickwall.h

namespace {

using namespace kit;

constexpr Step kTm[]    = { { 0, "MAN", "MANUAL" }, { 1, "AUTO", "AUTO RELEASE", kTagAuto } };
constexpr Step kDet[]   = { { 0, "PEAK", "SAMPLE PEAK" }, { 1, "TP", "TRUE PEAK" } };                 // -> kEngTruePeak
constexpr Step kVoice[] = { { 0, "CLEAN" }, { 1, "LOUD", "LOUD (SOFT-CLIP PRE-STAGE)" } };
constexpr Step kSt[]    = { { 0, "ST", "STEREO" }, { 2, "MID", "MID ONLY" }, { 3, "SIDE", "SIDE ONLY" } };  // 01 §3.1

constexpr int kVoiceLoud = 1;                    // kVoice's LOUD step (Brickwall::kVoiceLoud, the ColourSelect index)

float lookAttack(const ParamView& v) noexcept { return v[Pid::look].plain; }   // the budget-clamped lookahead (ms)

constexpr ParamTable kBrickwallParams = [] {
    ParamTable t = allNa("NOT ON THIS LIMITER");
    t[Pid::thr]    = { plotPlain(cont(-30, 0, -6)) };
    t[Pid::ratio]  = { locked(1.0f, "A LIMITER: RATIO IS INFINITE", "∞") };
    t[Pid::knee]   = { plotPlain(cont(0, 6, 0)) };
    t[Pid::atk]    = { derived(&lookAttack, Pid::look, "= LOOKAHEAD", "ATTACK HAPPENS INSIDE THE LOOKAHEAD WINDOW") };
    t[Pid::rel]    = { cont(1, 1000, 50) };
    t[Pid::tmode]  = { stepped(kTm, 0) };                                       // its own slot, TIME MODE (02 §6.4)
    t[Pid::look]   = { cont(0.5f, 20, 5) };                                     // budget-clamped; locked 0 when OFF
    t[Pid::det]    = { stepped(kDet, 0) };
    t[Pid::schpf]  = { na(0, "A LIMITER DETECTS THE FULL BAND") };
    t[Pid::link]   = { cont(0, 1, 1) };
    t[Pid::stmode] = { stepped(kSt, 0) };
    t[Pid::voice]  = { stepped(kVoice, 0) };
    t[Pid::makeup] = { named(cont(-12, 0, -0.3f), "CEILING") };
    t[Pid::automu] = { na(0, "OUTPUT IS SET BY THE CEILING") };                 // no AUTO word (K1 #27)
    t[Pid::mix]    = { locked(1, "A LIMITER IS NEVER BLENDED", "100 %") };
    return t;
}();

void brickwallPhysical(const ParamView& v, EngineParams& e) noexcept {
    e.topo = kTopoFF;
    e.makeupDb = v[Pid::makeup].plain - e.thrDb;                    // the threshold lands on the ceiling
    if (e.voice == kVoiceLoud)
        e.makeupDb += stage::LoudClip::kHeadroomDb;                 // LOUD: the clipper's headroom, back as loudness
    uint8_t flags = static_cast<uint8_t>(e.flags & ~kEngAutoMakeup);  // never auto makeup (K1 #27)
    if (v[Pid::det].step == 1)
        flags = static_cast<uint8_t>(flags | kEngTruePeak);
    if ((v[Pid::tmode].tag & kTagAuto) != 0)
        flags = static_cast<uint8_t>(flags | kEngAutoRelease);
    e.flags = flags;
}

DetectorLaw brickwallLaw(const EngineParams& e) noexcept {
    return (e.flags & kEngTruePeak) != 0 ? DetectorLaw::truePeak : DetectorLaw::peak;
}

constexpr InternalSpec kBrickwallInt[] = { { "HELD PEAK", "DB", -60, 6, 1, true },
                                           { "LOOK EFF", "MS", 0, 20, 1, false },
                                           { "TP OVER", "DB", 0, 3, 2, false } };

} // namespace

extern constexpr ModeDescriptor kBrickwall {
    .key = "brickwall", .name = "BRICKWALL", .group = Group::limit, .introducedInStateVersion = 1, .revision = 1,
    .provisional = false,
    .topologyLine = "LOOKAHEAD LIMITER · FEED-FORWARD · PEAK/TRUE PEAK",
    .specLine = "BRICKWALL   LOOKAHEAD LIMITER · ∞:1 · 0.5–20 MS LOOKAHEAD · 1–1000 MS · CEILING −12…0",
    .params = kBrickwallParams, .physical = &brickwallPhysical,
    .stage2 = Stage2Kind::none, .linkLaw = LinkLaw::max, .detectorLaw = &brickwallLaw,
    .hasColour = true, .colourStatic = true, .wantsLookahead = true,
    .rigor = Rigor::character, .family = CurveFamily::textbook,
    .attackSpec = &attackFromView, .releaseSpec = &releaseFromView, .tailSeconds = &tailFromRelease,
    .ctBudgetNsPerSample = 40, .internals = kBrickwallInt };   // S13 H1b: measured (docs/modes/budgets.md)

} // namespace fcdsp::modes

// Owner specs (K2 #21; dsp.slidingmax holds them): HQ 4×-reconstructed output peak ≤ CEILING + 0.1 dB (DETECT TP with a
// lookahead budget); STD overshoot is documented, not clipped (≤ CEILING + 1.0 dB true peak, spec row); the box average
// re-sums exactly every 4096 samples (the 10-minute below-threshold soak row in dsp.null requires GR exactly 0.0);
// automating look slews the SC read position by ≤ 1 sample per tick and changes the box length only at ticks.
