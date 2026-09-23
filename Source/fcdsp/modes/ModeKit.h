#pragma once

// The shared kit for writing descriptors (01 §10.1): ParamSpec builders and modifiers, shared step tables, the
// neutral table, the standard hardware extensions and the shared time-spec functions. Every helper is defined inline
// here, so descriptor tables in other TUs can use them in constant initialisers (K3 #20). Frozen per sprint;
// Mode-local helpers stay in modes/<key>/.

#include "fcdsp/core/Units.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include <span>

namespace fcdsp::kit {

constexpr ParamSpec cont(float lo, float hi, float def, TimeLaw law = TimeLaw::expDb) noexcept
    { ParamSpec s; s.kind = Kind::continuous; s.lo = lo; s.hi = hi; s.defaultPlain = def; s.law = law; return s; }
constexpr ParamSpec stepped(std::span<const Step> st, float def, TimeLaw law = TimeLaw::expDb) noexcept
    { ParamSpec s; s.kind = Kind::stepped; s.steps = st; s.defaultPlain = def; s.law = law; return s; }
constexpr ParamSpec hybrid(float lo, float hi, std::span<const Step> outside, float def, TimeLaw law = TimeLaw::expDb) noexcept
    { ParamSpec s = cont(lo, hi, def, law); s.kind = Kind::hybrid; s.steps = outside; return s; }
constexpr ParamSpec locked(float v, const char* reason, const char* tag = "FIXED") noexcept
    { ParamSpec s; s.kind = Kind::locked; s.value = s.defaultPlain = v; s.reason = reason; s.tag = tag; return s; }
constexpr ParamSpec locked(std::span<const Step> one, const char* reason) noexcept          // list params
    { ParamSpec s = locked(one[0].plain, reason, one[0].label); s.steps = one; return s; }
constexpr ParamSpec derived(float (*fn)(const ParamView&) noexcept, Pid from, const char* tag, const char* reason) noexcept
    { ParamSpec s; s.kind = Kind::derived; s.derive = fn; s.derivedFrom = from; s.tag = tag; s.reason = reason; return s; }
constexpr ParamSpec na(float neutral, const char* reason) noexcept
    { ParamSpec s; s.kind = Kind::notApplicable; s.value = s.defaultPlain = neutral; s.reason = reason; return s; }

// modifiers
constexpr ParamSpec named(ParamSpec s, const char* label, DisplayMap m = {}) noexcept { s.label = label; s.display = m; return s; }
constexpr ParamSpec ext  (ParamSpec s) noexcept { s.flags |= kFlagExtension;  return s; }
constexpr ParamSpec prog (ParamSpec s) noexcept { s.flags |= kFlagProgram;    return s; }
constexpr ParamSpec plotPlain(ParamSpec s) noexcept { s.flags |= kFlagPlotIsPlain; return s; }   // absolute handle drags
constexpr ParamSpec hwrev(ParamSpec s) noexcept { s.flags |= kFlagHwReversed; return s; }
constexpr ParamSpec brief(ParamSpec s, const char* b) noexcept { s.brief = b; return s; }
constexpr ParamSpec withLaw(ParamSpec s, TimeLaw l) noexcept { s.law = l; return s; }

// shared step tables
inline constexpr Step kOffOn[]    = { {0, "OFF"}, {1, "ON"} };
inline constexpr Step kDualLink[] = { {0, "DUAL", "DUAL MONO"}, {1, "LINK", "LINKED"} };
inline constexpr Step kStereoMs[] = { {0, "ST", "STEREO"}, {1, "M/S", "MID/SIDE"} };
inline constexpr Step kPeak[]     = { {0, "PEAK"} };

// the neutral table: every Mode param n/a with host-neutral values
constexpr ParamTable allNa(const char* reason) noexcept {
    ParamTable t;
    for (auto& en : t.e) en.spec = na(0, reason);
    t[Pid::ratio].spec  = na(0.75f, reason);   t[Pid::knee].spec = na(6, reason);
    t[Pid::range].spec  = na(60, reason);      t[Pid::atk].spec  = na(10, reason);   t[Pid::rel].spec = na(200, reason);
    t[Pid::link].spec   = na(1, reason);       t[Pid::mix].spec  = na(1, reason);
    t[Pid::s2thr].spec  = na(24, reason);      t[Pid::s2atk].spec = na(1, reason);   t[Pid::s2rel].spec = na(100, reason);
    t[Pid::thr].spec    = na(-18, reason);
    return t;
}

// standard extensions (D §5.1 hardware defaults; E §4.2.5)
constexpr ParamSpec extSchpf()  noexcept { return ext(cont(0, 500, 0)); }        // < 20 Hz = OFF
constexpr ParamSpec extLink()   noexcept { return ext(cont(0, 1, 1)); }
constexpr ParamSpec extMix()    noexcept { return ext(cont(0, 1, 1)); }          // hardware Modes: 0-100 %
constexpr ParamSpec extRange()  noexcept { return ext(cont(0, 60, 60)); }        // FF Modes only
constexpr ParamSpec extStereo() noexcept { return ext(stepped(kStereoMs, 0)); }
constexpr ParamSpec extDrive()  noexcept { return ext(cont(-24, 24, 0)); }

namespace detail {
// The published time of `pid` (atk or rel, host plain in ms) as a TimeSpec in seconds, in the active spec's law.
// `program` comes from the spec's kFlagProgram or the resolved step's kTagProgram. For a program value, [lo, hi] is
// the published range the D2 probe checks (C §5.3):
//   - on a switch position tagged program (Bus G AUTO, Mu 67 TC5/TC6): the span of the switch's other positions
//     (Bus G AUTO -> 0.1-1.2 s);
//   - otherwise (a locked or derived nominal): the nominal scaled by [bandLo, bandHi] (Opto 2A: attack 10 ms ->
//     5-20 ms, release 60 ms to 50 % -> 40-80 ms).
// A value that is not program-dependent leaves lo = hi = 0: the probe applies its Rigor tolerance around `seconds`.
inline TimeSpec timeFromView(const ParamView& v, Pid pid, float bandLo, float bandHi) noexcept {
    const ResolvedParam& r = v[pid];
    const ParamSpec* spec = v.spec[idx(pid)];
    TimeSpec t{ r.plain / 1000.f, spec != nullptr ? spec->law : TimeLaw::expDb };
    t.program = (spec != nullptr && (spec->flags & kFlagProgram) != 0) || (r.tag & kTagProgram) != 0;
    if (!t.program)
        return t;
    float lo = 0, hi = 0;
    bool any = false;
    if (spec != nullptr) {
        for (const Step& st : spec->steps) {
            if ((st.tag & kTagProgram) != 0)
                continue;
            if (!any || st.plain < lo) lo = st.plain;
            if (!any || st.plain > hi) hi = st.plain;
            any = true;
        }
    }
    if (any && lo < hi) {
        t.lo = lo / 1000.f;
        t.hi = hi / 1000.f;
    } else {
        t.lo = t.seconds * bandLo;
        t.hi = t.seconds * bandHi;
    }
    return t;
}
} // namespace detail

// shared time-spec functions (inline, bodies in the header): published value + the spec's law; `program` from
// kFlagProgram (or a kTagProgram step). Program ranges without a switch: attack x[1/2, 2], release x[2/3, 4/3].
inline TimeSpec attackFromView(const ParamView& v, const EngineParams&) noexcept {
    return detail::timeFromView(v, Pid::atk, 0.5f, 2.0f);
}
inline TimeSpec releaseFromView(const ParamView& v, const EngineParams&) noexcept {
    return detail::timeFromView(v, Pid::rel, 2.0f / 3.0f, 4.0f / 3.0f);
}
// 5 x the longest release tau, in seconds (stage 2's counts while stage 2 is on); the host adds its latency.
inline float tailFromRelease(const EngineParams& e) noexcept {
    const float tauMs = e.s2ThrDb < kS2Off && e.s2RelTauMs > e.relTauMs ? e.s2RelTauMs : e.relTauMs;
    return tauMs > 0.f ? 5.f * tauMs / 1000.f : 0.f;
}

} // namespace fcdsp::kit
