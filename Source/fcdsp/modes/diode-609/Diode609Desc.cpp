// Diode 609 (slot 5, `diode-609`): the Mode's descriptor, physical() and specs, from 01 §10.7's sketch and D §2.5 / §5
// (Neve 33609, with the 2254's steps where the 33609's are unverified [U]). Diode609.h holds the (provisional,
// generic) traits and Diode609.cpp FCDSP_DEFINE_MODE(Diode609).
//
// - External linkage (SPRINTS §7 D24): the traits header declares `extern const ModeDescriptor kDiode609;`.
// - provisional = true (descriptor wave, ADR-30); revision 1 is spelled out.
// - THRESHOLD and LIMIT THRESHOLD are dBu switches: plain dBFS = dBu − 22 (0 dBFS = +22 dBu, 01 §3.1), shown in DBU
//   through a DisplayMap; stage 2 is OFF above +15 dBu, at the host range end 24 (kS2Off, K2 #20).
// - A1 / A2 (the self-adjusting recoveries) are RELEASE steps tagged kTagAuto / kTagAuto2 (01 §10.2 rule: AUTO
//   positions on a hardware release switch are rel steps), plus kTagProgram (their times are nominal, as Bus G's
//   AUTO). Both release switches carry them, and EngineParams::tags is one OR over every parameter, so physical()
//   routes each switch's AUTO variant through m[] (m[1] compressor, m[2] limiter) where the policies tell them apart.
// - ATTACK SLOW also adds the 33609's reduced sensitivity below 100 Hz: m[0] = 100 (Hz) for the Mode-internal SC
//   high-pass (ScShape SlowHp, the Mode task); the host SC HPF stays the standard extension.
// Choices where the sketch is silent, [H] until the Mode task fits them:
// - Defaults: THRESHOLD +4 dBu (−18 dBFS, 0 VU), RATIO 2:1, ATTACK FAST, RELEASE 400 ms (the sketch's), GAIN 0 dB,
//   LINK STEREO (the sketch's), LIMIT OFF (the sketch's), LIMIT ATTACK FAST, LIMIT RELEASE 100 ms (the sketch's).
// - The LIMIT THRESHOLD OFF step does NOT carry kTagOff, although 01 §10.7's sketch and ParamSpec.h's StepTag comment
//   name Stage 2 OFF as a kTagOff position: dsp.registry's crossmode.no_off forbids a kTagOff step wherever another
//   Mode's defaults land (K2 #4 ii), and every other Mode leaves s2thr at its host default 24 = OFF. Stage 2 off is
//   carried by the value itself (s2ThrDb >= kS2Off, ramped by s2On_, 01 §5.1). Handoff: schema request.
// - Rigor character (a diode bridge with transformer colour and program-dependent recovery), family custom (the
//   progressive knee: the true ratio only above ~5 dB over, D §2.5). Link law max (the side chains are combined:
//   "compressed by the same amount", D §2.5).
// - Internals (E §7, two-stage): COMP GR, LIMIT GR (history), LIMIT WINS.

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/ModeKit.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include <cstdint>

namespace fcdsp::modes {

extern const ModeDescriptor kDiode609;           // also declared by modes/diode-609/Diode609.h

namespace {

using namespace kit;

// THRESHOLD −20 … +10 dBu in 2 dB steps (2254 [V S11]; the 33609's steps are [U]) as dBFS = dBu − 22.
constexpr Step kThr[] = { { -42, "−20" }, { -40, "−18" }, { -38, "−16" }, { -36, "−14" }, { -34, "−12" },
                          { -32, "−10" }, { -30, "−8" }, { -28, "−6" }, { -26, "−4" }, { -24, "−2" }, { -22, "0" },
                          { -20, "2" }, { -18, "4" }, { -16, "6" }, { -14, "8" }, { -12, "10" } };
constexpr Step kRatio[] = { { 1.0f - 1.0f / 1.5f, "1.5", "1.5:1" }, { 0.5f, "2", "2:1" },
                            { 1.0f - 1.0f / 3.0f, "3", "3:1" }, { 0.75f, "4", "4:1" },
                            { 1.0f - 1.0f / 6.0f, "6", "6:1" } };                              // D §2.5 [V~]
constexpr Step kAtk[]   = { { 3, "FAST", "FAST ~3 MS" }, { 6, "SLOW", "SLOW ~6 MS" } };         // SLOW: + SC high-pass
constexpr Step kRel[]   = { { 100, ".1", "0.1 S" }, { 400, ".4", "0.4 S" }, { 800, ".8", "0.8 S" },
                            { 1500, "1.5", "1.5 S" },
                            { 2000, "A1", "AUTO 1 100 MS/2 S", kTagAuto | kTagProgram },
                            { 5000, "A2", "AUTO 2 50 MS/5 S", kTagAuto2 | kTagProgram } };      // D §2.5 [V S10]
constexpr Step kGain[]  = { { 0, "0" }, { 2, "2" }, { 4, "4" }, { 6, "6" }, { 8, "8" }, { 10, "10" }, { 12, "12" },
                            { 14, "14" }, { 16, "16" }, { 18, "18" }, { 20, "20" } };           // 2254 [V S11]
constexpr Step kLink[]  = { { 0, "DUAL", "DUAL MONO" }, { 1, "STEREO", "STEREO (SIDE CHAINS LINKED)" } };
// LIMIT THRESHOLD +4 … +15 dBu in 1 dB steps [U steps] as dBFS = dBu − 22, then OFF at the host range end.
constexpr Step kS2Thr[] = { { -18, "4" }, { -17, "5" }, { -16, "6" }, { -15, "7" }, { -14, "8" }, { -13, "9" },
                            { -12, "10" }, { -11, "11" }, { -10, "12" }, { -9, "13" }, { -8, "14" }, { -7, "15" },
                            { 24, "OFF", "LIMITER OUT" } };
constexpr Step kS2Atk[] = { { 2, "FAST", "FAST ~2 MS" }, { 4, "SLOW", "SLOW ~4 MS" } };         // D §2.5 [V S10]
constexpr Step kS2Rel[] = { { 50, ".05", "50 MS" }, { 100, ".1", "0.1 S" }, { 200, ".2", "0.2 S" },
                            { 800, ".8", "0.8 S" },
                            { 2000, "A1", "AUTO 1 100 MS/2 S", kTagAuto | kTagProgram },
                            { 5000, "A2", "AUTO 2 50 MS/5 S", kTagAuto2 | kTagProgram } };      // [U list] D §8.6
constexpr Step kPeak[]  = { { 0, "PEAK", "PEAK · FEEDBACK" } };
constexpr Step kDiode[] = { { 0, "DIODE", "DIODE BRIDGE + TRANSFORMERS" } };

float dbu     (float p) noexcept { return p + 22.f; }       // 0 dBFS = +22 dBu (01 §3.1)
float dbuPlain(float d) noexcept { return d - 22.f; }

constexpr ParamTable kDiode609Params = [] {
    ParamTable t = allNa("NOT ON THIS CIRCUIT");
    t[Pid::thr]    = { named(stepped(kThr, -18), nullptr, { &dbu, &dbuPlain, "DBU", 0 }) };
    t[Pid::ratio]  = { stepped(kRatio, 0.5f) };
    t[Pid::knee]   = { brief(locked(5, "PROGRESSIVE: TRUE RATIO >5 DB OVER"), "PROGRESSIVE KNEE") };
    t[Pid::atk]    = { stepped(kAtk, 3) };
    t[Pid::rel]    = { stepped(kRel, 400) };
    t[Pid::tmode]  = { na(0, "A1 AND A2 ARE RELEASE POSITIONS") };
    t[Pid::det]    = { locked(kPeak, "PEAK SENSING AFTER THE GAIN ELEMENT (FEEDBACK)") };
    t[Pid::schpf]  = { extSchpf() };
    t[Pid::link]   = { stepped(kLink, 1) };
    t[Pid::stmode] = { extStereo() };
    t[Pid::voice]  = { locked(kDiode, "DIODE-BRIDGE GAIN ELEMENT AND TRANSFORMERS") };
    t[Pid::drive]  = { extDrive() };
    t[Pid::makeup] = { stepped(kGain, 0) };
    t[Pid::mix]    = { extMix() };
    t[Pid::s2thr]  = { named(stepped(kS2Thr, 24), nullptr, { &dbu, &dbuPlain, "DBU", 0 }) };
    t[Pid::s2atk]  = { stepped(kS2Atk, 2) };
    t[Pid::s2rel]  = { stepped(kS2Rel, 100) };
    return t;
}();

float autoVariant(uint16_t tag) noexcept {                  // 0 manual, 1 A1, 2 A2
    return (tag & kTagAuto2) != 0 ? 2.f : (tag & kTagAuto) != 0 ? 1.f : 0.f;
}

void diodePhysical(const ParamView& v, EngineParams& e) noexcept {
    e.topo = kTopoFB;                                       // both stages are feedback (D §2.5 [V S11])
    e.m[0] = v[Pid::atk].step == 1 ? 100.f : 0.f;           // SLOW: Mode-internal SC high-pass corner, Hz (D §2.5)
    e.m[1] = autoVariant(v[Pid::rel].tag);                  // compressor A1/A2: DualRelease in the FB solve
    e.m[2] = autoVariant(v[Pid::s2rel].tag);                // limiter A1/A2
}

DetectorLaw diodeLaw(const EngineParams&) noexcept { return DetectorLaw::peak; }

constexpr InternalSpec kDiodeInt[] = { { "COMP GR", "DB", 0, 30, 1, false }, { "LIMIT GR", "DB", 0, 30, 1, true },
                                       { "LIMIT WINS", "", 0, 1, 0, false } };

} // namespace

extern constexpr ModeDescriptor kDiode609 {
    .key = "diode-609", .name = "DIODE 609", .group = Group::diode, .introducedInStateVersion = 1, .revision = 1,
    .provisional = true,
    .topologyLine = "DIODE BRIDGE · FEEDBACK · COMPRESSOR + LIMITER",
    .specLine = "DIODE 609   DIODE BRIDGE · 1.5–6 · FAST/SLOW · .1–1.5 S + A1/A2 · LIMITER +4…+15 DBU",
    .params = kDiode609Params, .physical = &diodePhysical,
    .stage2 = Stage2Kind::sharedElementMax, .linkLaw = LinkLaw::max, .detectorLaw = &diodeLaw,
    .hasColour = true, .colourStatic = false, .wantsLookahead = false,
    .rigor = Rigor::character, .family = CurveFamily::custom,
    .attackSpec = &attackFromView, .releaseSpec = &releaseFromView, .tailSeconds = &tailFromRelease,
    .ctBudgetNsPerSample = 70, .internals = kDiodeInt };

} // namespace fcdsp::modes

// Traits (01 §10.7, final): PeakLog; QuadKnee in FB (the progressive 5 dB knee); LinkMax; SmoothBranching, with
// DualRelease for A1/A2 inside the FB solve (max of the fast and slow roots, 01 §5.2); Stage2
// SharedElementMax<PeakLog, SmoothBranching> on the aux lanes (E §3.3), switched on and off through the 20 ms s2On_
// ramp (01 §5.1), never a step; ScShape SlowHp (m[0]); Colour DiodeBridge; kTopologies = FB.
