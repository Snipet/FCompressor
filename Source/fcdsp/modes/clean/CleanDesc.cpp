// Clean (slot 0, `clean`): the Mode's descriptor, physical() and specs, 01 §10.3 verbatim except as noted below.
// Clean.h (the traits, F3) and Clean.cpp (FCDSP_DEFINE_MODE(Clean)) arrive in S2; until F3 activates slot 0 in
// Modes.def nothing registers this descriptor, and the S1 probes reach it through the extern declaration below.
//
// - External linkage (SPRINTS §7 D24): the traits header and the probes declare `extern const ModeDescriptor kClean;`.
// - provisional = true until F9 (S3) fits Clean and clears it (golden.py adopt refuses its modes/clean/ rows; 01 §4.3).
// - revision is spelled out (= 1, the default) so the designated initialiser names every field.

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/ModeKit.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"

namespace fcdsp::modes {

extern const ModeDescriptor kClean;              // also declared by modes/clean/Clean.h (F3)

namespace {

using namespace kit;

constexpr Step kTm[]    = { { 0, "MAN", "MANUAL" }, { 1, "AUTO", "AUTO RELEASE", kTagAuto } };   // CrestAuto (E §2.5a)
constexpr Step kDet[]   = { { 0, "PEAK" }, { 1, "RMS" }, { 2, "PK+RMS", "PEAK + RMS" } };
constexpr Step kSt[]    = { { 0, "ST", "STEREO" }, { 1, "M/S", "MID/SIDE" }, { 2, "MID", "MID ONLY" },
                            { 3, "SIDE", "SIDE ONLY" }, { 4, "M>S", "MID KEYS SIDE" }, { 5, "S>M", "SIDE KEYS MID" } };
constexpr Step kVoice[] = { { 0, "OFF" }, { 1, "TUBE" }, { 2, "DIODE" }, { 3, "BRIGHT" } };  // Pro-C 3 precedent (D §2.8)
constexpr Variant kDriveByVoice[] = { { 0, na(0, "NO COLOUR STAGE WHILE VOICE IS OFF") } };

constexpr ParamTable kCleanParams = [] {
    ParamTable t = allNa("NOT USED BY CLEAN");
    t[Pid::thr]    = { plotPlain(cont(-60, 0, -18)) };
    t[Pid::ratio]  = { cont(0, 1, 0.75f) };                 // S: 1:1 ... inf:1 (no negative ratios in Clean)
    t[Pid::knee]   = { plotPlain(cont(0, 72, 6)) };         // absolute knee/range handle drags (02 §6.5)
    t[Pid::range]  = { plotPlain(cont(0, 60, 60)) };
    t[Pid::atk]    = { cont(0.005f, 250, 10) };
    t[Pid::rel]    = { cont(5, 5000, 200) };
    t[Pid::tmode]  = { stepped(kTm, 0) };                    // its own slot, TIME MODE (02 §6.4)
    t[Pid::hold]   = { cont(0, 500, 0) };
    t[Pid::look]   = { cont(0, 20, 0) };                     // the resolver clamps to the budget; locked 0 when OFF
    t[Pid::det]    = { stepped(kDet, 0) };
    t[Pid::schpf]  = { cont(0, 500, 0) };
    t[Pid::sce]    = { named(cont(-6, 6, 0), "SC TILT") };
    t[Pid::link]   = { cont(0, 1, 1) };
    t[Pid::stmode] = { stepped(kSt, 0) };
    t[Pid::voice]  = { stepped(kVoice, 0) };
    t[Pid::drive]  = { cont(-24, 24, 0), Pid::voice, kDriveByVoice };
    t[Pid::makeup] = { cont(-24, 24, 0) };
    t[Pid::automu] = { stepped(kOffOn, 0) };                 // drawn as the AUTO word on MAKEUP
    t[Pid::mix]    = { cont(0, 2, 1) };                      // up to 200 % (D §2.8, E §10.6)
    return t;
}();

void cleanPhysical(const ParamView& v, EngineParams& e) noexcept {
    e.topo = kTopoFF;
    if ((v[Pid::tmode].tag & kTagAuto) != 0)
        e.flags = static_cast<uint8_t>(e.flags | kEngAutoRelease);
}

DetectorLaw cleanLaw(const EngineParams& e) noexcept { return e.det == 1 ? DetectorLaw::rms : DetectorLaw::peak; }

constexpr InternalSpec kCleanInt[] = { { "REL EFF", "MS", 1, 5000, 0, true }, { "CREST", "DB", 0, 30, 1, false },
                                       { "PEAK DET", "DB", -60, 6, 1, false }, { "RMS DET", "DB", -60, 6, 1, false } };

} // namespace

extern constexpr ModeDescriptor kClean {
    .key = "clean", .name = "CLEAN", .group = Group::modern, .introducedInStateVersion = 1, .revision = 1,
    .provisional = true,
    .topologyLine = "FEED-FORWARD · LOG DOMAIN",
    .specLine = "CLEAN   FEED-FORWARD · PEAK/RMS · 5 µS–250 MS · 1:1–∞ · 0–200 % MIX",
    .params = kCleanParams, .physical = &cleanPhysical,
    .stage2 = Stage2Kind::none, .linkLaw = LinkLaw::max, .detectorLaw = &cleanLaw,
    .hasColour = true, .colourStatic = true, .wantsLookahead = false,
    .rigor = Rigor::clean, .family = CurveFamily::textbook,
    .attackSpec = &attackFromView, .releaseSpec = &releaseFromView, .tailSeconds = &tailFromRelease,
    .ctBudgetNsPerSample = 40, .internals = kCleanInt };

} // namespace fcdsp::modes

// Traits (Clean.h, F3): Detector DetSelect<PeakLog, RmsLog, DualDet>; Computer QuadKnee; Link LinkMax;
// Ballistics Hold<CrestAuto<SmoothBranching>>; Stage2 NoStage2; Colour ColourSelect<ColourNone, TubeSym, DiodeAsym,
// Bright>; ScShape Flat; kTopologies = FF. Rigor clean => D8 (b) bit-exact below-threshold null when voice = OFF.
