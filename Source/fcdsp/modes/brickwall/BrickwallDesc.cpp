// Brickwall (slot 7, `brickwall`): the Mode's descriptor, physical() and specs, from 01 §10.7's sketch, D §2.8 / §5
// and K1 #27, K2 #21. Brickwall.h holds the (provisional, generic) traits and Brickwall.cpp
// FCDSP_DEFINE_MODE(Brickwall).
//
// - External linkage (SPRINTS §7 D24): the traits header declares `extern const ModeDescriptor kBrickwall;`.
// - provisional = true (descriptor wave, ADR-30); revision 1 is spelled out.
// - LOOKAHEAD is continuous 0.5–20 ms, and the resolver owns the budget (01 §4.4, K1 #8): locked at 0 with the BUDGET
//   OFF reason while labudget = OFF, else clamped to the budget; ATTACK is derived from the budget-clamped value, so it
//   also reads 0 while the budget is OFF (zero latency, overshoot allowed; the footer hint is keyed on
//   wantsLookahead && budget == off, 02 §6.6, K1 #35).
// - CEILING (makeup relabelled) sets the output: physical() makes makeupDb = ceiling − thrDb, so the threshold lands on
//   the ceiling; AUTO MAKEUP is n/a and kEngAutoMakeup is never set (K1 #27: no makeup applied twice).
// - DETECT TP sets kEngTruePeak; its interpolator delay D_tp reaches the host through scDelaySamples (K2 #21a). TP
//   without a lookahead budget cannot align: the descriptor cannot lock TP on the budget (a step list has no driver
//   other than a Pid), so the footer carries it (01 §10.7). TIME MODE AUTO sets kEngAutoRelease.
// Choices where the sketch is silent:
// - Group limit (crossmode.no_off: limiter-only), family textbook (∞:1 over a quadratic knee).
// - Rigor character: a ceiling-referenced output is never unity below threshold (makeupDb = ceiling − thrDb even at
//   CEILING 0 dBFS), so the clean and modelled below-threshold null rows of dsp.null (D8 (b)) cannot hold; the
//   character rule judges the THD golden instead. Revisit with the Mode task (handoff: schema/probe request).
// - SC HPF, drive, range, hold, SC emphasis and stage 2 are n/a (01 §10.2 matrix: "–").
// - Internals (E §7, limiter): HELD PEAK (history), LOOK EFF, TP OVER.

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
    .provisional = true,
    .topologyLine = "LOOKAHEAD LIMITER · FEED-FORWARD · PEAK/TRUE PEAK",
    .specLine = "BRICKWALL   LOOKAHEAD LIMITER · ∞:1 · 0.5–20 MS LOOKAHEAD · 1–1000 MS · CEILING −12…0",
    .params = kBrickwallParams, .physical = &brickwallPhysical,
    .stage2 = Stage2Kind::none, .linkLaw = LinkLaw::max, .detectorLaw = &brickwallLaw,
    .hasColour = true, .colourStatic = true, .wantsLookahead = true,
    .rigor = Rigor::character, .family = CurveFamily::textbook,
    .attackSpec = &attackFromView, .releaseSpec = &releaseFromView, .tailSeconds = &tailFromRelease,
    .ctBudgetNsPerSample = 50, .internals = kBrickwallInt };

} // namespace fcdsp::modes

// Owner specs (K2 #21, the Mode task): HQ 4×-reconstructed output peak ≤ CEILING + 0.1 dB; STD overshoot is
// documented, not clipped (≤ CEILING + 1.0 dB true peak, spec row); the box average re-sums exactly every 4096 samples
// (a 10-minute below-threshold soak row in dsp.null requires GR exactly 0.0); automating look slews the SC read
// position by ≤ 1 sample per tick and changes the box length only at ticks.
