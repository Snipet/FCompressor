// FCMP_PROBE layer=dsp name=resolve scope=global timeout=60
//
// dsp.resolve (S1 F2; SPRINTS S1.2; 01 §4.4-4.5; E §4.2; K1 #8, #32; K2 #4): the one resolver on every ParamSpec kind.
// A probe-local synthetic descriptor (kSynth: every Kind, a variant, derived specs, a DisplayMap, the budget lock,
// Bus-G-style 2/4/10 ratio steps, log- and host-domain step lists, FET 76's GR switch at 0/7) and Clean's table
// (01 §10.3) are driven through snap, the step helpers, activeSpec, resolveView, resolve (+ physicalDefault and
// desc.physical) and modeDefaults, then swept 0 -> 1 in 1/4096 steps of the host map (the D3 method of 03 §3.4, global
// until Clean is registered in S2: SPRINTS §7 D6). Spec rows only (no golden file): the snap boundaries print as SPEC
// rows, e.g. `resolve.synth.ratio.boundary.2_4 got 0.625`.
//
// kSynth has external linkage (like a Mode descriptor, SPRINTS §7 D24) so that dsp.format tests the same table.
#include "ProbeRegistry.h"

#include "fcdsp/core/Units.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/ModeKit.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/params/Setup.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace fcdsp::modes
{
    extern const ModeDescriptor kClean;         // modes/clean/CleanDesc.cpp
}

// ---- the synthetic descriptor ------------------------------------------------------------------------------------------
namespace fcmp::probe::synth
{
    extern const fcdsp::ModeDescriptor kSynth;

    namespace
    {
        using namespace fcdsp;
        using namespace fcdsp::kit;

        constexpr Step kRatio[] = { { 0.50f, "2", "2:1" }, { 0.75f, "4", "4:1" }, { 0.90f, "10", "10:1" } };  // S 0.625/0.825
        constexpr Step kAtkOutside[] = { { 0.01f, "MIN", "10 µS" }, { 30, "SLOW", "SLOW 30 MS", kTagProgram },
                                         { 100, "MAX" } };                     // hybrid: one step below lo, two above hi
        constexpr Step kRel[] = { { 100, ".1", "0.1 S" }, { 300, ".3", "0.3 S" }, { 600, ".6", "0.6 S" },
                                  { 1200, "1.2", "1.2 S" }, { 2400, "AUTO", "AUTO", kTagAuto | kTagProgram } };
        constexpr Step kGr[] = { { 0, "ON", "GAIN REDUCTION ON" },
                                 { 7, "OFF", "ATTACK OFF (NO GAIN REDUCTION, COLOUR ONLY)", kTagOff, "gain reduction off" } };
        constexpr Step kHpf[] = { { 0, "OFF" }, { 50, "50" }, { 100, "100" }, { 200, "200" }, { 350, "350" } };
        constexpr Step kSt[] = { { 0, "ST", "STEREO" }, { 2, "MID", "MID ONLY" }, { 3, "SIDE", "SIDE ONLY" } };
        constexpr Step kVoice[] = { { 0, "VCA", "VCA + CONSOLE" }, { 1, "CLEAN" } };
        constexpr Step kS2[] = { { -18, "4" }, { -14, "8" }, { -10, "12" }, { 24, "OFF", "LIMITER OUT", kTagOff } };
        constexpr Variant kDriveByVoice[] = { { 1, na(0, "CLEAN VOICE HAS NO COLOUR STAGE") } };

        float inDial(float thr) noexcept { return 24.f + (-12.f - thr); }       // FET-style INPUT dial, inverted
        float inPlain(float d) noexcept { return -12.f - (d - 24.f); }
        float emDial(float s) noexcept { return s * (10.f / 6.f); }
        float emPlain(float d) noexcept { return d * 0.6f; }
        float gainDial(float mu) noexcept { return (mu + 10.f) / 0.4f; }
        float gainPlain(float d) noexcept { return -10.f + 0.4f * d; }
        float toDbu(float p) noexcept { return p + 22.f; }
        float fromDbu(float d) noexcept { return d - 22.f; }

        float kneeFromRatio(const ParamView& v) noexcept
        {
            constexpr float w[] = { 10.f, 6.f, 3.f };
            const int s = v[Pid::ratio].step;
            return w[s < 0 ? 1 : s];
        }
        float holdFromLook(const ParamView& v) noexcept { return 0.16f * v[Pid::look].plain; }

        constexpr ParamTable kParams = []
        {
            ParamTable t = allNa("NOT IN THE SYNTHETIC MODE");
            t[Pid::thr]    = { named(cont(-36, 12, -18), "INPUT", { &inDial, &inPlain, "", 0, true }) };
            t[Pid::ratio]  = { stepped(kRatio, 0.75f) };
            t[Pid::knee]   = { derived(&kneeFromRatio, Pid::ratio, "= RATIO", "KNEE FOLLOWS RATIO") };
            t[Pid::range]  = { na(60, "NO RANGE IN THIS LOOP") };
            t[Pid::atk]    = { hybrid(0.1f, 10, kAtkOutside, 1) };
            t[Pid::rel]    = { stepped(kRel, 300) };
            t[Pid::tmode]  = { named(stepped(kGr, 0), "GR") };
            t[Pid::hold]   = { derived(&holdFromLook, Pid::look, "= LOOKAHEAD", "HOLD FOLLOWS THE LOOKAHEAD") };
            t[Pid::look]   = { cont(0.5f, 20, 5) };
            t[Pid::det]    = { locked(kPeak, "PEAK DETECTOR") };
            t[Pid::schpf]  = { ext(stepped(kHpf, 0)) };
            t[Pid::sce]    = { named(cont(0, 6, 0), "EMPHASIS", { &emDial, &emPlain, "", 1 }) };
            t[Pid::link]   = { stepped(kDualLink, 1) };
            t[Pid::stmode] = { stepped(kSt, 0) };
            t[Pid::voice]  = { stepped(kVoice, 0) };
            t[Pid::drive]  = { ext(cont(-12, 12, 0)), Pid::voice, kDriveByVoice };
            t[Pid::makeup] = { named(cont(-10, 30, 6), "GAIN", { &gainDial, &gainPlain, "", 0 }) };
            t[Pid::automu] = { stepped(kOffOn, 0) };
            t[Pid::mix]    = { extMix() };
            t[Pid::s2thr]  = { named(stepped(kS2, 24), nullptr, { &toDbu, &fromDbu, "DBU", 0 }) };
            t[Pid::s2atk]  = { locked(10, "STAGE-2 ATTACK IS FIXED") };
            t[Pid::s2rel]  = { prog(withLaw(locked(60, "60 MS TO 50 %, THEN BY PROGRAM", "AUTO"), TimeLaw::t50)) };
            return t;
        }();

        // physicalDefault runs first (m[0] = its atkTauMs), then this (m[1] = 1).
        void synthPhysical(const ParamView& v, EngineParams& e) noexcept
        {
            e.m[0] = e.atkTauMs;
            e.m[1] = 1.f;
            e.topo = kTopoFB;
            if ((v[Pid::tmode].tag & kTagOff) != 0)
                e.flags = static_cast<uint8_t>(e.flags | kEngGrOff);
        }
        DetectorLaw synthLaw(const EngineParams&) noexcept { return DetectorLaw::peak; }
    } // namespace

    extern constexpr fcdsp::ModeDescriptor kSynth {
        .key = "synth", .name = "SYNTH", .group = fcdsp::Group::other, .introducedInStateVersion = 1, .revision = 1,
        .provisional = true, .topologyLine = "SYNTHETIC · EVERY KIND", .specLine = "SYNTH   dsp.resolve / dsp.format",
        .params = kParams, .physical = &synthPhysical,
        .stage2 = fcdsp::Stage2Kind::none, .linkLaw = fcdsp::LinkLaw::max, .detectorLaw = &synthLaw,
        .hasColour = false, .colourStatic = true, .wantsLookahead = true,
        .rigor = fcdsp::Rigor::clean, .family = fcdsp::CurveFamily::textbook,
        .attackSpec = &fcdsp::kit::attackFromView, .releaseSpec = &fcdsp::kit::releaseFromView,
        .tailSeconds = &fcdsp::kit::tailFromRelease, .ctBudgetNsPerSample = 40, .internals = {} };
} // namespace fcmp::probe::synth

namespace
{
    using namespace fcdsp;

    const char* pidName(Pid p) { return kHostParams[idx(p)].id; }

    ModeEntry entryOf(const ModeDescriptor& d) { return { &d, nullptr, 0, 0, nullptr, nullptr, nullptr }; }

    RawParams hostDefaults(LookaheadBudget b)
    {
        RawParams r;
        for (std::size_t i = 0; i < kNumModeParams; ++i)
            r.v[i] = kHostParams[i].def;
        r.modeSlot = 0;
        r.budget = b;
        return r;
    }

    RawParams modeDefaultRaw(const ModeDescriptor& d, LookaheadBudget b)
    {
        RawParams r = hostDefaults(b);
        modeDefaults(d, r);
        return r;
    }

    std::string k(const char* a, const char* b = nullptr, const char* c = nullptr, const char* d = nullptr)
    {
        std::string s = a;
        for (const char* part : { b, c, d })
            if (part != nullptr)
                s.append(".").append(part);
        return s;
    }

    int b2i(bool b) { return b ? 1 : 0; }

    // The largest float in [lo, hi) for which `holds` is true, when it holds at lo and not at hi (lo < hi) and flips
    // once: a snap boundary, ties included.
    template <class Pred>
    float lastFloatWhere(float lo, float hi, const Pred& holds)
    {
        for (;;)
        {
            const auto m = static_cast<float>(0.5 * (static_cast<double>(lo) + static_cast<double>(hi)));
            if (!(m > lo && m < hi))
                return lo;
            if (holds(m))
                lo = m;
            else
                hi = m;
        }
    }

    // The largest float in [lo, hi) that snaps to `step`.
    float lastFloatOf(Pid pid, const ParamSpec& spec, int step, float lo, float hi)
    {
        return lastFloatWhere(lo, hi, [&](float x) { return snap(pid, spec, x).step == step; });
    }

    // Snap-domain value (Pid.h snapDomain), for the probe's own midpoints.
    double domain(Pid pid, float plain)
    {
        switch (snapDomain(pid))
        {
            case SnapDomain::linear: return static_cast<double>(plain);
            case SnapDomain::log:
                return plain > 0.f ? std::log(static_cast<double>(plain)) : -std::numeric_limits<double>::infinity();
            case SnapDomain::host: return static_cast<double>(toNorm(pid, plain));
        }
        return static_cast<double>(plain);
    }

    bool sameResolved(const ResolvedParam& a, const ResolvedParam& b)
    {
        return std::memcmp(&a.plain, &b.plain, sizeof a.plain) == 0 && std::memcmp(&a.display, &b.display, sizeof a.display) == 0
            && a.step == b.step && a.state == b.state && a.flags == b.flags && a.tag == b.tag;
    }

    // ---- the 1/4096 sweep (D3) over one descriptor ---------------------------------------------------------------------
    struct SweepCounts
    {
        long points = 0, offDetent = 0, unreached = 0, nonmonotone = 0, boundaryMisplaced = 0, continuousMismatch = 0,
             clampFlagMismatch = 0, fixedMismatch = 0, stateMismatch = 0;
        int notes = 0;                                  // diagnostic NOTE lines printed (capped)
    };

    void sweepPid(const ModeDescriptor& d, RawParams base, Pid pid, SweepCounts& c)
    {
        const std::size_t i = idx(pid);
        std::vector<int> reached;
        int prevStep = -1;
        float prevRaw = 0.f;
        const ParamSpec* prevSpec = nullptr;
        for (int n = 0; n <= 4096; ++n)
        {
            const float raw = toPlain(pid, static_cast<float>(n) / 4096.f);
            base.v[i] = raw;
            ParamView v;
            resolveView(d, base, v);
            const ParamSpec& s = *v.spec[i];
            const ResolvedParam& r = v.p[i];
            ++c.points;
            if (&s != prevSpec)
            {
                const bool detents = s.kind == Kind::stepped || s.kind == Kind::hybrid;   // not soft notches or spans
                reached.assign(detents ? s.steps.size() : 0, 0);
                prevStep = -1;
                prevSpec = &s;
            }
            long bad = 0;
            switch (s.kind)
            {
                case Kind::continuous:
                {
                    const float want = raw < s.lo ? s.lo : (raw > s.hi ? s.hi : raw);
                    if (std::memcmp(&r.plain, &want, sizeof want) != 0 || r.step != -1)
                        ++c.continuousMismatch, ++bad;
                    if (((r.flags & kClamped) != 0) != (raw < s.lo || raw > s.hi))
                        ++c.clampFlagMismatch, ++bad;
                    if (r.state != SlotState::live)
                        ++c.stateMismatch, ++bad;
                    break;
                }
                case Kind::stepped:
                {
                    if (r.step < 0 || static_cast<std::size_t>(r.step) >= s.steps.size()
                        || r.plain != s.steps[static_cast<std::size_t>(r.step)].plain)
                    {
                        ++c.offDetent, ++bad;
                        break;
                    }
                    reached[static_cast<std::size_t>(r.step)] = 1;
                    if (r.state != SlotState::stepped)
                        ++c.stateMismatch, ++bad;
                    if (prevStep >= 0 && r.step < prevStep)
                        ++c.nonmonotone, ++bad;
                    // every boundary crossed since the previous point lies between the two raw values (ties lower)
                    for (int b = prevStep; prevStep >= 0 && b < r.step; ++b)
                    {
                        const double mid = 0.5 * (domain(pid, s.steps[static_cast<std::size_t>(b)].plain)
                                                  + domain(pid, s.steps[static_cast<std::size_t>(b) + 1].plain));
                        if (!(domain(pid, prevRaw) <= mid && mid < domain(pid, raw)))
                            ++c.boundaryMisplaced, ++bad;
                    }
                    prevStep = r.step;
                    break;
                }
                case Kind::hybrid:
                {
                    const bool inside = raw >= s.lo && raw <= s.hi;
                    bool onStep = r.step >= 0 && static_cast<std::size_t>(r.step) < s.steps.size()
                               && r.plain == s.steps[static_cast<std::size_t>(r.step)].plain;
                    if (inside ? (r.plain != raw || r.step != -1 || r.state != SlotState::live)
                               : !(onStep || ((r.plain == s.lo || r.plain == s.hi) && r.step == -1)))
                        ++c.offDetent, ++bad;
                    if (onStep)
                        reached[static_cast<std::size_t>(r.step)] = 1;
                    break;
                }
                case Kind::locked:
                case Kind::notApplicable:
                    if (r.plain != s.value)
                        ++c.fixedMismatch, ++bad;
                    break;
                case Kind::derived:
                    break;
            }
            if (bad != 0 && c.notes++ < 20)
                std::printf("NOTE     sweep %s %s raw %.9g -> plain %.9g step %d\n", std::string(d.key).c_str(), pidName(pid),
                            static_cast<double>(raw), static_cast<double>(r.plain), r.step);
            prevRaw = raw;
        }
        for (const int hit : reached)
            if (hit == 0)
            {
                ++c.unreached;
                std::printf("NOTE     sweep %s %s: a step is never reached\n", std::string(d.key).c_str(), pidName(pid));
            }
    }

    void sweep(funkgui::test::Probe& P, const ModeDescriptor& d)
    {
        const std::string key(d.key);
        SweepCounts c;
        for (const Pid pid : kResolveOrder)
        {
            const RawParams base = modeDefaultRaw(d, LookaheadBudget::ms20);
            if (d.params[pid].driver != kNoPid)
            {
                // a dependent list: sweep it under every step of its driver
                const ParamSpec& drv = d.params[d.params[pid].driver].spec;
                for (int s = 0; s < stepCount(drv); ++s)
                {
                    RawParams b = base;
                    b[d.params[pid].driver] = stepPlain(drv, s);
                    sweepPid(d, b, pid, c);
                }
            }
            else
                sweepPid(d, base, pid, c);
        }
        P.ge(k("sweep", key.c_str(), "points"), static_cast<double>(c.points), 22.0 * 4097.0);
        P.eq(k("sweep", key.c_str(), "off_detent"), c.offDetent, 0);
        P.eq(k("sweep", key.c_str(), "unreached"), c.unreached, 0);
        P.eq(k("sweep", key.c_str(), "nonmonotone"), c.nonmonotone, 0);
        P.eq(k("sweep", key.c_str(), "boundary_misplaced"), c.boundaryMisplaced, 0);
        P.eq(k("sweep", key.c_str(), "continuous_passthrough_mismatch"), c.continuousMismatch, 0);
        P.eq(k("sweep", key.c_str(), "clamp_flag_mismatch"), c.clampFlagMismatch, 0);
        P.eq(k("sweep", key.c_str(), "fixed_value_mismatch"), c.fixedMismatch, 0);
        P.eq(k("sweep", key.c_str(), "state_mismatch"), c.stateMismatch, 0);
    }

    // The boundaries of a stepped spec, as SPEC rows: the last float of each step against the domain midpoint.
    void boundaries(funkgui::test::Probe& P, const char* key, Pid pid, const ParamSpec& s, double relTol)
    {
        for (std::size_t b = 0; b + 1 < s.steps.size(); ++b)
        {
            const float lo = s.steps[b].plain, hi = s.steps[b + 1].plain;
            const float last = lastFloatOf(pid, s, static_cast<int>(b), lo, hi);
            double want = 0;
            switch (snapDomain(pid))
            {
                case SnapDomain::linear: want = 0.5 * (static_cast<double>(lo) + static_cast<double>(hi)); break;
                case SnapDomain::log:    want = std::sqrt(static_cast<double>(lo) * static_cast<double>(hi)); break;
                case SnapDomain::host:
                    want = static_cast<double>(toPlain(pid, static_cast<float>(
                        0.5 * (static_cast<double>(toNorm(pid, lo)) + static_cast<double>(toNorm(pid, hi))))));
                    break;
            }
            const std::string name = std::string(s.steps[b].label) + "_" + s.steps[b + 1].label;
            std::string lower;
            for (const char ch : name)
                lower += (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : (ch == '.' ? 'p' : ch);
            P.near(k("resolve", key, "boundary", lower.c_str()), static_cast<double>(last), want, 0.0, relTol);
            P.eq(k("resolve", key, "boundary", (lower + ".next_is_upper").c_str()),
                 snap(pid, s, std::nextafter(last, std::numeric_limits<float>::infinity())).step, static_cast<int64_t>(b) + 1);
        }
    }
} // namespace

FCMP_PROBE(dsp, resolve)
{
    using fcmp::probe::synth::kSynth;
    const ModeDescriptor& clean = fcdsp::modes::kClean;
    const ModeEntry synthEntry = entryOf(kSynth), cleanEntry = entryOf(clean);
    const auto& T = kSynth.params;
    constexpr float kInf = std::numeric_limits<float>::infinity();
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

    // ---- 1. snap(), per kind -------------------------------------------------------------------------------------------
    {
        const ParamSpec& thr = T[Pid::thr].spec;                       // continuous -36 ... 12, default -18
        const Snapped below = snap(Pid::thr, thr, -40.f), above = snap(Pid::thr, thr, 20.f);
        const Snapped inside = snap(Pid::thr, thr, -3.25f), nan = snap(Pid::thr, thr, kNaN);
        P.near("snap.continuous.below_lo", below.plain, -36.0, 0.0);
        P.eq("snap.continuous.below_lo.clamped", b2i(below.clamped), 1);
        P.near("snap.continuous.above_hi", above.plain, 12.0, 0.0);
        P.eq("snap.continuous.above_hi.clamped", b2i(above.clamped), 1);
        P.near("snap.continuous.inside_exact", inside.plain, -3.25, 0.0);
        P.eq("snap.continuous.inside.clamped", b2i(inside.clamped), 0);
        P.eq("snap.continuous.step", inside.step, -1);
        P.near("snap.continuous.nan_is_default", nan.plain, -18.0, 0.0);
        P.near("snap.continuous.plus_inf", snap(Pid::thr, thr, kInf).plain, 12.0, 0.0);
        P.near("snap.continuous.minus_inf", snap(Pid::thr, thr, -kInf).plain, -36.0, 0.0);

        // soft notches never move a continuous value
        ParamSpec notched = thr;
        notched.steps = T[Pid::ratio].spec.steps;
        P.near("snap.continuous.soft_notches_ignored", snap(Pid::thr, notched, 0.74f).plain, 0.74f, 0.0);

        const ParamSpec& ratio = T[Pid::ratio].spec;                   // stepped 2/4/10, linear in S
        P.eq("snap.stepped.exact_step", snap(Pid::ratio, ratio, 0.75f).step, 1);
        P.eq("snap.stepped.below_all", snap(Pid::ratio, ratio, 0.f).step, 0);
        P.eq("snap.stepped.above_all", snap(Pid::ratio, ratio, 2.f).step, 2);
        P.eq("snap.stepped.plus_inf", snap(Pid::ratio, ratio, kInf).step, 2);
        P.eq("snap.stepped.minus_inf", snap(Pid::ratio, ratio, -kInf).step, 0);
        P.eq("snap.stepped.nan_is_default", snap(Pid::ratio, ratio, kNaN).step, 1);
        P.eq("snap.stepped.never_clamped", b2i(snap(Pid::ratio, ratio, 2.f).clamped), 0);
        P.near("snap.stepped.plain_is_canonical", snap(Pid::ratio, ratio, 0.8f).plain, 0.75f, 0.0);
        P.eq("snap.stepped.tie_goes_lower.0_625", snap(Pid::ratio, ratio, 0.625f).step, 0);
        P.eq("snap.stepped.tie_goes_lower.0_825", snap(Pid::ratio, ratio, 0.825f).step, 1);
        P.eq("snap.stepped.deterministic", b2i(snap(Pid::ratio, ratio, 0.7f).step == snap(Pid::ratio, ratio, 0.7f).step), 1);

        const ParamSpec& rel = T[Pid::rel].spec;                       // stepped, log domain
        P.eq("snap.stepped.log.tag", snap(Pid::rel, rel, 9000.f).tag, kTagAuto | kTagProgram);
        P.eq("snap.stepped.log.zero_is_lowest", snap(Pid::rel, rel, 0.f).step, 0);

        const ParamSpec& gr = T[Pid::tmode].spec;                      // FET 76 GR: ON 0, OFF 7 (K2 #4)
        int onFromOtherModes = 0, offTags = 0;
        for (int t = 0; t <= 7; ++t)
        {
            const Snapped sn = snap(Pid::tmode, gr, static_cast<float>(t));
            onFromOtherModes += (t <= 1 && sn.step == 0) ? 1 : 0;       // every other Mode writes tmode 0 or 1
            offTags += (sn.tag & kTagOff) != 0 ? 1 : 0;
        }
        P.eq("snap.gr.tmode_0_and_1_resolve_on", onFromOtherModes, 2);
        P.eq("snap.gr.index_values_resolving_off", offTags, 4);        // 4, 5, 6, 7

        const ParamSpec& atk = T[Pid::atk].spec;                       // hybrid 0.1 ... 10 + MIN 0.01, SLOW 30, MAX 100
        const Snapped live = snap(Pid::atk, atk, 2.5f), toLo = snap(Pid::atk, atk, 0.05f), toMin = snap(Pid::atk, atk, 0.02f);
        const Snapped toHi = snap(Pid::atk, atk, 15.f), toSlow = snap(Pid::atk, atk, 20.f), toMax = snap(Pid::atk, atk, 300.f);
        P.eq("snap.hybrid.inside.live", b2i(live.step == -1 && live.plain == 2.5f && !live.clamped), 1);
        P.eq("snap.hybrid.below_nearer_lo.clamps", b2i(toLo.step == -1 && toLo.plain == 0.1f && toLo.clamped), 1);
        P.eq("snap.hybrid.below_nearer_step", b2i(toMin.step == 0 && toMin.plain == 0.01f && !toMin.clamped), 1);
        P.eq("snap.hybrid.above_nearer_hi.clamps", b2i(toHi.step == -1 && toHi.plain == 10.f && toHi.clamped), 1);
        P.eq("snap.hybrid.above_nearer_step", b2i(toSlow.step == 1 && toSlow.plain == 30.f), 1);
        P.eq("snap.hybrid.step_tag", toSlow.tag, kTagProgram);
        P.eq("snap.hybrid.far_above", toMax.step, 2);
        // the step/edge boundaries sit at the geometric means (log domain)
        P.near("snap.hybrid.boundary.min_lo", lastFloatOf(Pid::atk, atk, 0, 0.01f, 0.1f),
               std::sqrt(static_cast<double>(0.01f) * static_cast<double>(0.1f)), 0.0, 1e-6);
        P.near("snap.hybrid.boundary.hi_slow",
               lastFloatWhere(10.f, 30.f, [&](float x) { return snap(Pid::atk, atk, x).plain == 10.f; }),
               std::sqrt(300.0), 0.0, 1e-6);

        const ParamSpec& s2atk = T[Pid::s2atk].spec, &det = T[Pid::det].spec, &range = T[Pid::range].spec;
        const Snapped lk = snap(Pid::s2atk, s2atk, 123.f), span = snap(Pid::det, det, 5.f), na = snap(Pid::range, range, 5.f);
        P.eq("snap.locked.value", b2i(lk.plain == 10.f && lk.step == -1 && !lk.clamped), 1);
        P.eq("snap.locked.one_step_span", b2i(span.plain == 0.f && span.step == 0), 1);
        P.eq("snap.na.value", b2i(na.plain == 60.f && na.step == -1), 1);
        const Snapped dv = snap(Pid::knee, T[Pid::knee].spec, 3.f);
        P.eq("snap.derived.placeholder", b2i(dv.step == -1 && dv.plain == T[Pid::knee].spec.value), 1);
    }

    // ---- 2. the snap boundaries: linear (Bus G 2/4/10 in S), log (times), host (schpf), FET GR ---------------------------
    boundaries(P, "synth.ratio", Pid::ratio, T[Pid::ratio].spec, 0.0);       // exactly 0.625 and 0.825 (ties lower)
    boundaries(P, "synth.rel", Pid::rel, T[Pid::rel].spec, 1e-6);           // geometric means
    boundaries(P, "synth.schpf", Pid::schpf, T[Pid::schpf].spec, 1e-5);     // midpoints of toNorm
    boundaries(P, "synth.tmode", Pid::tmode, T[Pid::tmode].spec, 0.0);      // 3.5 (K2 #4)
    boundaries(P, "synth.s2thr", Pid::s2thr, T[Pid::s2thr].spec, 0.0);      // negative plains
    boundaries(P, "clean.det", Pid::det, clean.params[Pid::det].spec, 0.0);

    // ---- 3. stepCount / stepPlain / stepIndexOf ---------------------------------------------------------------------------
    {
        long roundTrip = 0, clampIdx = 0, specs = 0;
        for (const ModeDescriptor* d : { &kSynth, &clean })
            for (const Pid pid : kResolveOrder)
            {
                const ParamSpec& s = d->params[pid].spec;
                const int n = stepCount(s);
                ++specs;
                for (int i = 0; i < n; ++i)
                    if (stepIndexOf(pid, s, stepPlain(s, i)) != i)
                        ++roundTrip;
                if (n > 0 && (stepPlain(s, -1) != stepPlain(s, 0) || stepPlain(s, n) != stepPlain(s, n - 1)))
                    ++clampIdx;
            }
        P.eq("steps.index_round_trip_failures", roundTrip, 0);
        P.eq("steps.out_of_range_index_clamps_failures", clampIdx, 0);
        P.eq("steps.count.stepped", stepCount(T[Pid::rel].spec), 5);
        P.eq("steps.count.hybrid_outside_steps", stepCount(T[Pid::atk].spec), 3);
        P.eq("steps.count.locked_span", stepCount(T[Pid::det].spec), 1);
        P.eq("steps.count.locked_value", stepCount(T[Pid::s2atk].spec), 0);
        P.eq("steps.count.continuous", stepCount(T[Pid::thr].spec), 0);
        P.near("steps.plain.continuous_is_default", stepPlain(T[Pid::thr].spec, 0), -18.0, 0.0);
        P.near("steps.plain.locked_is_value", stepPlain(T[Pid::s2atk].spec, 0), 10.0, 0.0);
        P.ge("steps.specs_checked", static_cast<double>(specs), 44.0);
    }

    // ---- 4. activeSpec and variants ---------------------------------------------------------------------------------------
    {
        RawParams raw = modeDefaultRaw(kSynth, LookaheadBudget::ms5);
        ParamView v;
        raw[Pid::voice] = 0.f;                                         // VCA: the base spec
        resolveView(kSynth, raw, v);
        P.eq("variant.synth.base_spec", b2i(v.spec[idx(Pid::drive)] == &T[Pid::drive].spec), 1);
        P.eq("variant.synth.base_live", b2i(v[Pid::drive].state == SlotState::live), 1);
        raw[Pid::voice] = 1.f;                                         // CLEAN: the n/a variant
        raw[Pid::drive] = 5.f;
        resolveView(kSynth, raw, v);
        P.eq("variant.synth.variant_spec", b2i(v.spec[idx(Pid::drive)] == &T[Pid::drive].variants[0].spec), 1);
        P.eq("variant.synth.variant_na", b2i(v[Pid::drive].state == SlotState::na && v[Pid::drive].plain == 0.f), 1);
        P.eq("variant.active_spec_direct", b2i(&activeSpec(T[Pid::drive], v) == &T[Pid::drive].variants[0].spec), 1);
        P.eq("variant.no_driver_is_base", b2i(&activeSpec(T[Pid::thr], v) == &T[Pid::thr].spec), 1);

        RawParams c = modeDefaultRaw(clean, LookaheadBudget::off);
        ParamView cv;
        resolveView(clean, c, cv);
        P.eq("variant.clean.voice_off_drive_na", b2i(cv[Pid::drive].state == SlotState::na), 1);
        c[Pid::voice] = 2.f;
        resolveView(clean, c, cv);
        P.eq("variant.clean.voice_diode_drive_live", b2i(cv[Pid::drive].state == SlotState::live), 1);
    }

    // ---- 5. resolveView: states, tags, display, flags, derived pass, purity -------------------------------------------
    {
        const RawParams raw = modeDefaultRaw(kSynth, LookaheadBudget::ms5);
        RawParams copy = raw;
        ParamView v;
        resolveView(kSynth, raw, v);
        P.eq("view.raw_never_written", b2i(std::memcmp(&raw, &copy, sizeof raw) == 0), 1);
        P.eq("view.desc", b2i(v.desc == &kSynth), 1);
        int nullSpecs = 0;
        for (const ParamSpec* s : v.spec)
            nullSpecs += s == nullptr ? 1 : 0;
        P.eq("view.every_spec_set", nullSpecs, 0);

        struct Want { Pid pid; SlotState state; };
        constexpr Want kStates[] = {
            { Pid::thr, SlotState::live }, { Pid::ratio, SlotState::stepped }, { Pid::knee, SlotState::derived },
            { Pid::range, SlotState::na }, { Pid::atk, SlotState::live }, { Pid::rel, SlotState::stepped },
            { Pid::tmode, SlotState::stepped }, { Pid::hold, SlotState::derived }, { Pid::look, SlotState::live },
            { Pid::det, SlotState::locked }, { Pid::schpf, SlotState::stepped }, { Pid::sce, SlotState::live },
            { Pid::link, SlotState::stepped }, { Pid::stmode, SlotState::stepped }, { Pid::voice, SlotState::stepped },
            { Pid::drive, SlotState::live }, { Pid::makeup, SlotState::live }, { Pid::automu, SlotState::stepped },
            { Pid::mix, SlotState::live }, { Pid::s2thr, SlotState::stepped }, { Pid::s2atk, SlotState::locked },
            { Pid::s2rel, SlotState::locked } };
        for (const Want& w : kStates)
            P.eq(k("view.synth.state", pidName(w.pid)), static_cast<int64_t>(v[w.pid].state), static_cast<int64_t>(w.state));

        P.near("view.derived.knee_at_4", v[Pid::knee].plain, 6.0, 0.0);
        P.near("view.derived.hold_from_look", v[Pid::hold].plain, 0.8, 1e-6);
        P.near("view.display.inverted_dial", v[Pid::thr].display, 30.0, 0.0);          // INPUT 30 = thr -18
        P.near("view.display.gain_dial", v[Pid::makeup].display, 40.0, 1e-5);
        P.near("view.display.dbu", v[Pid::s2thr].display, 46.0, 0.0);                   // OFF step (24) + 22
        P.near("view.display.identity", v[Pid::look].display, 5.0, 0.0);
        P.eq("view.flags.extension", v[Pid::drive].flags & kFlagExtension, kFlagExtension);
        P.eq("view.tags.default_or", static_cast<int64_t>(v.tags), kTagOff);             // only s2thr OFF
        P.eq("view.slot", v.slot, raw.modeSlot);

        RawParams r2 = raw;
        r2[Pid::ratio] = 0.95f;
        r2[Pid::rel] = 3000.f;
        r2[Pid::tmode] = 7.f;
        r2[Pid::thr] = -50.f;
        r2[Pid::atk] = 0.02f;
        r2.modeSlot = 3;
        resolveView(kSynth, r2, v);
        P.near("view.derived.knee_follows_ratio_10", v[Pid::knee].plain, 3.0, 0.0);
        P.eq("view.tags.or", static_cast<int64_t>(v.tags), kTagAuto | kTagProgram | kTagOff);
        P.eq("view.flags.clamped", v[Pid::thr].flags & kClamped, kClamped);
        P.eq("view.hybrid.on_step_is_stepped", b2i(v[Pid::atk].state == SlotState::stepped && v[Pid::atk].step == 0), 1);
        P.eq("view.slot_follows_raw", v.slot, 3);

        ParamView again;
        resolveView(kSynth, r2, again);
        int differ = 0;
        for (std::size_t i = 0; i < kNumModeParams; ++i)
            differ += sameResolved(v.p[i], again.p[i]) && v.spec[i] == again.spec[i] ? 0 : 1;
        P.eq("view.deterministic", differ, 0);
    }

    // ---- 6. the lookahead budget (K1 #8) ----------------------------------------------------------------------------------
    {
        RawParams raw = modeDefaultRaw(kSynth, LookaheadBudget::off);
        raw[Pid::look] = 10.f;
        Resolution res;
        resolve(synthEntry, raw, res);
        const ResolvedParam& look = res.view[Pid::look];
        const ParamSpec* ls = res.view.spec[idx(Pid::look)];
        P.eq("budget.off.locked", b2i(look.state == SlotState::locked && look.plain == 0.f && look.step == -1), 1);
        P.eq("budget.off.tag_off", b2i(ls != nullptr && ls->tag != nullptr && std::strcmp(ls->tag, "OFF") == 0), 1);
        P.eq("budget.off.reason",
             b2i(ls != nullptr && ls->reason != nullptr
                 && std::strcmp(ls->reason, "LOOKAHEAD BUDGET IS OFF — SET 5 MS OR 20 MS (ADDS LATENCY)") == 0), 1);
        P.eq("budget.off.brief", b2i(ls != nullptr && ls->brief != nullptr && std::strcmp(ls->brief, "BUDGET OFF") == 0), 1);
        P.eq("budget.off.no_step_tag", look.tag, 0);
        P.near("budget.off.derived_sees_zero", res.view[Pid::hold].plain, 0.0, 0.0);
        P.near("budget.off.engine_look", res.eng.lookMs, 0.0, 0.0);

        raw.budget = LookaheadBudget::ms5;
        resolve(synthEntry, raw, res);
        P.eq("budget.5ms.clamped", b2i(res.view[Pid::look].plain == 5.f && (res.view[Pid::look].flags & kClamped) != 0
                                       && res.view[Pid::look].state == SlotState::live), 1);
        P.near("budget.5ms.derived_sees_clamp", res.view[Pid::hold].plain, 0.8, 1e-6);
        P.near("budget.5ms.engine_look", res.eng.lookMs, 5.0, 0.0);
        raw[Pid::look] = 3.f;
        resolve(synthEntry, raw, res);
        P.eq("budget.5ms.inside_unclamped", b2i(res.view[Pid::look].plain == 3.f && (res.view[Pid::look].flags & kClamped) == 0), 1);
        raw[Pid::look] = 0.2f;
        resolve(synthEntry, raw, res);
        P.eq("budget.5ms.below_lo_clamped_to_lo", b2i(res.view[Pid::look].plain == 0.5f && (res.view[Pid::look].flags & kClamped) != 0), 1);

        raw.budget = LookaheadBudget::ms20;
        raw[Pid::look] = 20.f;
        resolve(synthEntry, raw, res);
        P.eq("budget.20ms.at_hi_unclamped", b2i(res.view[Pid::look].plain == 20.f && (res.view[Pid::look].flags & kClamped) == 0), 1);

        // an n/a look is never locked by the budget; a stepped look takes the highest step at or below the cap
        ModeDescriptor d = kSynth;
        d.params[Pid::look] = { kit::na(0, "NO LOOKAHEAD") };
        raw.budget = LookaheadBudget::off;
        ParamView v;
        resolveView(d, raw, v);
        P.eq("budget.off.na_stays_na", b2i(v[Pid::look].state == SlotState::na && v.spec[idx(Pid::look)] == &d.params[Pid::look].spec), 1);
        static constexpr Step kLookSteps[] = { { 1, "1" }, { 5, "5" }, { 10, "10" }, { 20, "20" } };
        d.params[Pid::look] = { kit::stepped(kLookSteps, 5) };
        raw.budget = LookaheadBudget::ms5;
        raw[Pid::look] = 12.f;
        resolveView(d, raw, v);
        P.eq("budget.5ms.stepped_highest_below_cap",
             b2i(v[Pid::look].plain == 5.f && v[Pid::look].step == 1 && (v[Pid::look].flags & kClamped) != 0), 1);

        // Clean: look cont(0, 20) locked at 0 while OFF, clamped to 5 at 5 MS
        RawParams c = modeDefaultRaw(clean, LookaheadBudget::off);
        c[Pid::look] = 20.f;
        resolveView(clean, c, v);
        P.eq("budget.clean.off_locked_zero", b2i(v[Pid::look].state == SlotState::locked && v[Pid::look].plain == 0.f), 1);
        c.budget = LookaheadBudget::ms5;
        resolveView(clean, c, v);
        P.eq("budget.clean.5ms_clamped", b2i(v[Pid::look].plain == 5.f && (v[Pid::look].flags & kClamped) != 0), 1);
        long sweepBad = 0;
        for (const LookaheadBudget b : { LookaheadBudget::off, LookaheadBudget::ms5, LookaheadBudget::ms20 })
            for (int n = 0; n <= 4096; ++n)
            {
                c.budget = b;
                c[Pid::look] = toPlain(Pid::look, static_cast<float>(n) / 4096.f);
                resolveView(clean, c, v);
                const float cap = budgetMs(b);
                const float want = b == LookaheadBudget::off ? 0.f : (c[Pid::look] > cap ? cap : c[Pid::look]);
                const bool clampWant = b != LookaheadBudget::off && c[Pid::look] > cap;
                if (v[Pid::look].plain != want || ((v[Pid::look].flags & kClamped) != 0) != clampWant)
                    ++sweepBad;
            }
        P.eq("budget.clean.sweep_mismatches", sweepBad, 0);
    }

    // ---- 7. resolve(): physicalDefault, then desc.physical --------------------------------------------------------------
    {
        Resolution res;
        RawParams raw = modeDefaultRaw(kSynth, LookaheadBudget::ms5);
        resolve(synthEntry, raw, res);
        const EngineParams& e = res.eng;
        P.near("resolve.synth.thr_db", e.thrDb, -18.0, 0.0);
        P.near("resolve.synth.slope", e.slope, 0.75, 0.0);
        P.near("resolve.synth.knee_db_derived", e.kneeDb, 6.0, 0.0);
        P.near("resolve.synth.range_na_is_off", e.rangeDb, kRangeOff, 0.0);
        P.near("resolve.synth.atk_tau_expdb", e.atkTauMs, 1.0, 0.0);
        P.near("resolve.synth.rel_tau", e.relTauMs, 300.0, 0.0);
        P.near("resolve.synth.hold_derived", e.holdMs, 0.8, 1e-6);
        P.near("resolve.synth.look", e.lookMs, 5.0, 0.0);
        P.near("resolve.synth.makeup", e.makeupDb, 6.0, 0.0);
        P.near("resolve.synth.schpf_off_is_zero", e.scHpfHz, 0.0, 0.0);
        P.near("resolve.synth.s2thr_off_sentinel", e.s2ThrDb, kS2Off, 0.0);
        P.near("resolve.synth.s2atk_locked_tau", e.s2AtkTauMs, 10.0, 0.0);
        P.near("resolve.synth.s2rel_t50_tau", e.s2RelTauMs, 60.0 / 0.693147181, 0.0, 1e-6);
        P.eq("resolve.synth.physical_after_default", b2i(e.m[0] == e.atkTauMs && e.m[1] == 1.f && e.topo == kTopoFB), 1);
        P.eq("resolve.synth.tags", static_cast<int64_t>(e.tags), static_cast<int64_t>(res.view.tags));
        P.eq("resolve.synth.flags_default", e.flags, 0);

        raw[Pid::tmode] = 7.f;
        raw[Pid::stmode] = 2.f;
        raw[Pid::automu] = 1.f;
        raw[Pid::schpf] = 100.f;
        raw[Pid::s2thr] = -14.f;
        raw[Pid::voice] = 1.f;
        resolve(synthEntry, raw, res);
        P.eq("resolve.synth.tmode_step_index", res.eng.tmode, 1);                   // OFF is step 1 (plain 7)
        P.eq("resolve.synth.gr_off_flag", res.eng.flags & kEngGrOff, kEngGrOff);
        P.eq("resolve.synth.automu_flag", res.eng.flags & kEngAutoMakeup, kEngAutoMakeup);
        P.eq("resolve.synth.stmode_universal_code", res.eng.stmode, 2);              // MID: step 1, code 2
        P.eq("resolve.synth.voice_step", res.eng.voice, 1);
        P.near("resolve.synth.schpf_hz", res.eng.scHpfHz, 100.0, 0.0);
        P.near("resolve.synth.s2thr_db", res.eng.s2ThrDb, -14.0, 0.0);
        P.eq("resolve.synth.tag_off", static_cast<int64_t>(res.eng.tags & kTagOff), kTagOff);

        RawParams c = modeDefaultRaw(clean, LookaheadBudget::off);
        resolve(cleanEntry, c, res);
        const EngineParams& ce = res.eng;
        const bool defaults = ce.thrDb == -18.f && ce.slope == 0.75f && ce.kneeDb == 6.f && ce.rangeDb == kRangeOff
                           && ce.atkTauMs == 10.f && ce.relTauMs == 200.f && ce.holdMs == 0.f && ce.lookMs == 0.f
                           && ce.driveDb == 0.f && ce.makeupDb == 0.f && ce.mix == 1.f && ce.scHpfHz == 0.f
                           && ce.sceDbOct == 0.f && ce.link == 1.f && ce.s2ThrDb == kS2Off && ce.s2AtkTauMs == 1.f
                           && ce.s2RelTauMs == 100.f && ce.det == 0 && ce.stmode == 0 && ce.voice == 0 && ce.tmode == 0
                           && ce.flags == 0 && ce.tags == 0 && ce.topo == kTopoFF && ce.preGainDb == 0.f;
        P.eq("resolve.clean.defaults", b2i(defaults), 1);
        c[Pid::tmode] = 1.f;
        c[Pid::det] = 1.f;
        c[Pid::range] = 30.f;
        c[Pid::schpf] = 10.f;
        resolve(cleanEntry, c, res);
        P.eq("resolve.clean.auto_release", res.eng.flags & kEngAutoRelease, kEngAutoRelease);
        P.eq("resolve.clean.tag_auto", static_cast<int64_t>(res.eng.tags), kTagAuto);
        P.eq("resolve.clean.detector_law_rms", static_cast<int64_t>(clean.detectorLaw(res.eng)), static_cast<int64_t>(DetectorLaw::rms));
        P.near("resolve.clean.range_db", res.eng.rangeDb, 30.0, 0.0);
        P.near("resolve.clean.schpf_below_20_off", res.eng.scHpfHz, 0.0, 0.0);

        const ModeEntry none{ nullptr, nullptr, 0, 0, nullptr, nullptr, nullptr };
        resolve(none, c, res);
        P.eq("resolve.null_descriptor_is_neutral", b2i(res.view.desc == nullptr && res.eng.thrDb == -18.f), 1);
    }

    // ---- 8. modeDefaults: live and stepped only, variant-aware ----------------------------------------------------------
    {
        RawParams c = hostDefaults(LookaheadBudget::ms20);
        for (std::size_t i = 0; i < kNumModeParams; ++i)
            c.v[i] = kHostParams[i].hi;                                // every raw value away from Clean's defaults
        modeDefaults(clean, c);
        const bool live = c[Pid::thr] == -18.f && c[Pid::ratio] == 0.75f && c[Pid::knee] == 6.f && c[Pid::range] == 60.f
                       && c[Pid::atk] == 10.f && c[Pid::rel] == 200.f && c[Pid::tmode] == 0.f && c[Pid::hold] == 0.f
                       && c[Pid::look] == 0.f && c[Pid::det] == 0.f && c[Pid::schpf] == 0.f && c[Pid::sce] == 0.f
                       && c[Pid::link] == 1.f && c[Pid::stmode] == 0.f && c[Pid::voice] == 0.f && c[Pid::makeup] == 0.f
                       && c[Pid::automu] == 0.f && c[Pid::mix] == 1.f;
        P.eq("defaults.clean.live_and_stepped_written", b2i(live), 1);
        P.eq("defaults.clean.na_untouched",
             b2i(c[Pid::s2thr] == kHostParams[idx(Pid::s2thr)].hi && c[Pid::s2atk] == kHostParams[idx(Pid::s2atk)].hi
                 && c[Pid::s2rel] == kHostParams[idx(Pid::s2rel)].hi), 1);
        P.eq("defaults.clean.variant_na_untouched", b2i(c[Pid::drive] == kHostParams[idx(Pid::drive)].hi), 1);

        RawParams s = hostDefaults(LookaheadBudget::ms20);
        for (std::size_t i = 0; i < kNumModeParams; ++i)
            s.v[i] = kHostParams[i].lo;
        modeDefaults(kSynth, s);
        P.eq("defaults.synth.locked_derived_na_untouched",
             b2i(s[Pid::knee] == 0.f && s[Pid::range] == 0.f && s[Pid::hold] == kHostParams[idx(Pid::hold)].lo
                 && s[Pid::det] == 0.f
                 && s[Pid::s2atk] == kHostParams[idx(Pid::s2atk)].lo && s[Pid::s2rel] == kHostParams[idx(Pid::s2rel)].lo), 1);
        P.eq("defaults.synth.hybrid_and_variant_base_written",
             b2i(s[Pid::atk] == 1.f && s[Pid::drive] == 0.f && s[Pid::s2thr] == 24.f && s[Pid::thr] == -18.f), 1);

        // at its defaults no parameter is clamped, and every live default resolves to itself
        for (const ModeDescriptor* d : { &clean, &kSynth })
        {
            const RawParams r = modeDefaultRaw(*d, LookaheadBudget::ms20);
            ParamView v;
            resolveView(*d, r, v);
            int clamped = 0, moved = 0;
            for (std::size_t i = 0; i < kNumModeParams; ++i)
            {
                clamped += (v.p[i].flags & kClamped) != 0 ? 1 : 0;
                const SlotState st = v.p[i].state;
                if ((st == SlotState::live || st == SlotState::stepped) && v.p[i].plain != r.v[i])
                    ++moved;
            }
            const std::string key(d->key);
            P.eq(k("defaults", key.c_str(), "clamped"), clamped, 0);
            P.eq(k("defaults", key.c_str(), "moved_by_resolve"), moved, 0);
        }
    }

    // ---- 9. Clean's state matrix at its defaults (01 §10.2, labudget OFF) -------------------------------------------------
    {
        ParamView v;
        resolveView(clean, modeDefaultRaw(clean, LookaheadBudget::off), v);
        std::array<int, 5> n{};
        for (const ResolvedParam& r : v.p)
            ++n[static_cast<std::size_t>(r.state)];
        P.eq("clean.states.live", n[static_cast<std::size_t>(SlotState::live)], 12);
        P.eq("clean.states.stepped", n[static_cast<std::size_t>(SlotState::stepped)], 5);   // tmode det stmode voice automu
        P.eq("clean.states.locked", n[static_cast<std::size_t>(SlotState::locked)], 1);     // look, budget OFF
        P.eq("clean.states.derived", n[static_cast<std::size_t>(SlotState::derived)], 0);
        P.eq("clean.states.na", n[static_cast<std::size_t>(SlotState::na)], 4);             // s2thr s2atk s2rel, drive
        P.eq("clean.plot_is_plain", b2i((v[Pid::thr].flags & kFlagPlotIsPlain) != 0 && (v[Pid::knee].flags & kFlagPlotIsPlain) != 0
                                        && (v[Pid::range].flags & kFlagPlotIsPlain) != 0), 1);
        P.eq("clean.provisional", b2i(clean.provisional), 1);
        P.eq("clean.key", b2i(clean.key == "clean" && clean.name == "CLEAN"), 1);
        P.eq("clean.not_registered_yet", b2i(fcdsp::byKey("clean") == nullptr || fcdsp::byKey("clean")->desc == &clean), 1);
    }

    // ---- 10. the 1/4096 sweeps (D3) -------------------------------------------------------------------------------------
    sweep(P, clean);
    sweep(P, kSynth);

    return P.finish();
}
