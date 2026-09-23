// The one resolver (01 §4.4-4.5; E §4.2; K1 #8, #32; K2 #4): raw host-plain values -> ParamView (what the active Mode
// shows) and EngineParams (what the engine runs). Pure and thread-agnostic: no allocation, no lock, no static state;
// the audio thread calls resolve() once per block, the UI once per frame, the host text lambdas once per call.
//
// Choices where 01 is silent (S1 F2 handoff; the lead freezes the semantics at FZ1):
//   - snap(): a NaN raw value reads as the spec's defaultPlain. Nearest-step search compares against the midpoints of
//     adjacent candidates in the snap domain, so a tie (raw exactly on a midpoint) goes to the lower step and +-inf
//     reach the end steps. The log domain maps plain <= 0 to -inf.
//   - snap() of a derived spec returns `value` with step -1 (resolveView fills the real value in pass 2).
//   - stepCount(): the steps a value can resolve to: steps.size() for stepped, hybrid and locked (one-step span); 0 for
//     continuous (soft notches are marks, not detents), derived and n/a.
//   - The budget lock replaces the active spec of `look` by kBudgetOffSpec (locked, tag "OFF", the 01 §4.4 reason,
//     brief "BUDGET OFF"). ResolvedParam::tag stays 0: kTagOff means "circuit disabled" in EngineParams::tags. With a
//     budget, a stepped `look` above the cap takes the highest step at or below it (plain = the cap, step -1 when
//     none); a derived `look` is budgeted right after its derivation.
//   - physicalDefault(): det, voice and tmode get the step index (0 when the parameter has no step); stmode gets the
//     universal stereo-mode code, i.e. the plain value (01 §3.1: STEREO, M/S, MID, SIDE, M>S, S>M), because the
//     Mode-agnostic Router consumes it and Brickwall lists ST/MID/SIDE "as indices {0, 2, 3}" (01 §10.7).
//     EngineParams::flags is assigned (kEngAutoMakeup when automu >= 0.5), tags = ParamView::tags; preGainDb, topo and
//     m[] are left for desc.physical.

#include "fcdsp/params/Resolve.h"

#include "fcdsp/core/Units.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/HostParams.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace fcdsp {

namespace {

// 01 §4.4 (K1 #8): what `look` resolves to while the configured lookahead budget is OFF.
constexpr ParamSpec kBudgetOffSpec = [] {
    ParamSpec s;
    s.kind = Kind::locked;
    s.value = 0.f;
    s.defaultPlain = 0.f;
    s.tag = "OFF";
    s.reason = "LOOKAHEAD BUDGET IS OFF — SET 5 MS OR 20 MS (ADDS LATENCY)";
    s.brief = "BUDGET OFF";
    return s;
}();

// A plain value in the snap domain of `pid` (Pid.h snapDomain; E §4.2.3).
double domainOf(Pid pid, float plain) noexcept {
    switch (snapDomain(pid)) {
        case SnapDomain::linear: return static_cast<double>(plain);
        case SnapDomain::log:
            return plain > 0.f ? std::log(static_cast<double>(plain)) : -std::numeric_limits<double>::infinity();
        case SnapDomain::host:   return static_cast<double>(toNorm(pid, plain));
    }
    return static_cast<double>(plain);
}

// Index of the candidate nearest to x in the snap domain of `pid`, for n >= 1 candidates in increasing order:
// the first i whose upper midpoint lies at or above x. A tie goes to the LOWER candidate; no hysteresis.
template <class PlainAt>
std::size_t nearestIndex(Pid pid, float x, std::size_t n, const PlainAt& plainAt) noexcept {
    const double dx = domainOf(pid, x);
    double lower = domainOf(pid, plainAt(std::size_t{0}));
    for (std::size_t i = 0; i + 1 < n; ++i) {
        const double upper = domainOf(pid, plainAt(i + 1));
        if (dx <= 0.5 * (lower + upper))
            return i;
        lower = upper;
    }
    return n - 1;
}

constexpr float clampTo(float x, float lo, float hi) noexcept { return x < lo ? lo : (x > hi ? hi : x); }

constexpr int8_t stepIndex(std::size_t i) noexcept { return static_cast<int8_t>(i); }   // steps.size() <= 127 (lint)

SlotState stateOf(Kind kind, int8_t step) noexcept {
    switch (kind) {
        case Kind::continuous:    return SlotState::live;
        case Kind::stepped:       return SlotState::stepped;
        case Kind::hybrid:        return step >= 0 ? SlotState::stepped : SlotState::live;   // K1 #32
        case Kind::locked:        return SlotState::locked;
        case Kind::derived:       return SlotState::derived;
        case Kind::notApplicable: return SlotState::na;
    }
    return SlotState::na;
}

// 01 §4.4 step 1 (K1 #8): the configured lookahead budget, applied to `look` right after it is resolved.
void applyBudget(ParamView& view, LookaheadBudget budget) noexcept {
    const std::size_t i = idx(Pid::look);
    const ParamSpec& spec = *view.spec[i];
    ResolvedParam& r = view.p[i];
    if (spec.kind == Kind::notApplicable)
        return;
    if (budget == LookaheadBudget::off) {
        view.spec[i] = &kBudgetOffSpec;
        r.plain = kBudgetOffSpec.value;
        r.step = -1;
        r.state = SlotState::locked;
        r.flags = kBudgetOffSpec.flags;
        r.tag = kTagNone;
        return;
    }
    const float cap = budgetMs(budget);
    const bool ranged = spec.kind == Kind::continuous || spec.kind == Kind::hybrid;
    const float hiEff = ranged && spec.hi < cap ? spec.hi : cap;
    if (!(r.plain > hiEff))
        return;
    r.plain = hiEff;
    r.flags = static_cast<uint8_t>(r.flags | kClamped);
    if (spec.kind == Kind::stepped) {
        std::size_t best = spec.steps.size();
        for (std::size_t k = 0; k < spec.steps.size(); ++k)
            if (spec.steps[k].plain <= hiEff)
                best = k;
        if (best < spec.steps.size()) {
            r.plain = spec.steps[best].plain;
            r.step = stepIndex(best);
            r.tag = spec.steps[best].tag;
            return;
        }
    }
    r.step = -1;
    r.tag = kTagNone;
    if (r.state == SlotState::stepped)
        r.state = SlotState::live;
}

} // namespace

Snapped snap(Pid pid, const ParamSpec& s, float rawPlain) noexcept {
    const float x = std::isnan(rawPlain) ? s.defaultPlain : rawPlain;
    switch (s.kind) {
        case Kind::continuous:
            return { clampTo(x, s.lo, s.hi), -1, kTagNone, x < s.lo || x > s.hi };

        case Kind::stepped: {
            if (s.steps.empty())
                return { s.defaultPlain, -1, kTagNone, false };
            const std::size_t i = nearestIndex(pid, x, s.steps.size(),
                                               [&s](std::size_t k) noexcept { return s.steps[k].plain; });
            return { s.steps[i].plain, stepIndex(i), s.steps[i].tag, false };
        }

        case Kind::hybrid: {
            if (x >= s.lo && x <= s.hi)
                return { x, -1, kTagNone, false };
            // Candidates in increasing order: the steps below lo, lo, hi, the steps above hi (the steps lie outside
            // [lo, hi]). A range edge clamps; a step is that step.
            std::size_t below = 0;
            while (below < s.steps.size() && s.steps[below].plain < s.lo)
                ++below;
            const auto plainAt = [&s, below](std::size_t k) noexcept {
                return k < below ? s.steps[k].plain : k == below ? s.lo : k == below + 1 ? s.hi : s.steps[k - 2].plain;
            };
            const std::size_t k = nearestIndex(pid, x, s.steps.size() + 2, plainAt);
            if (k == below)
                return { s.lo, -1, kTagNone, true };
            if (k == below + 1)
                return { s.hi, -1, kTagNone, true };
            const std::size_t i = k < below ? k : k - 2;
            return { s.steps[i].plain, stepIndex(i), s.steps[i].tag, false };
        }

        case Kind::locked:
            if (s.steps.empty())
                return { s.value, -1, kTagNone, false };
            return { s.value, 0, s.steps[0].tag, false };

        case Kind::derived:
        case Kind::notApplicable:
            return { s.value, -1, kTagNone, false };
    }
    return { s.value, -1, kTagNone, false };
}

int stepCount(const ParamSpec& s) noexcept {
    switch (s.kind) {
        case Kind::stepped:
        case Kind::hybrid:
        case Kind::locked:
            return static_cast<int>(s.steps.size());
        case Kind::continuous:
        case Kind::derived:
        case Kind::notApplicable:
            return 0;
    }
    return 0;
}

float stepPlain(const ParamSpec& s, int i) noexcept {
    const int n = stepCount(s);
    if (n <= 0)
        return s.kind == Kind::locked || s.kind == Kind::notApplicable ? s.value : s.defaultPlain;
    const int k = i < 0 ? 0 : (i >= n ? n - 1 : i);
    return s.steps[static_cast<std::size_t>(k)].plain;
}

int stepIndexOf(Pid pid, const ParamSpec& s, float plain) noexcept {
    return snap(pid, s, plain).step;
}

const ParamSpec& activeSpec(const ParamEntry& e, const ParamView& partial) noexcept {
    if (e.driver == kNoPid || e.variants.empty() || idx(e.driver) >= kNumModeParams)
        return e.spec;
    const int8_t driverStep = partial.p[idx(e.driver)].step;
    for (const Variant& v : e.variants)
        if (v.driverStep == driverStep)
            return v.spec;
    return e.spec;
}

void resolveView(const ModeDescriptor& desc, const RawParams& raw, ParamView& out) noexcept {
    out.desc = &desc;
    out.slot = raw.modeSlot;
    out.tags = 0;
    out.p.fill(ResolvedParam{});
    out.spec.fill(nullptr);

    // 1. Every non-derived parameter, in resolve order (a variant's driver is resolved before it; registry lint).
    for (const Pid pid : kResolveOrder) {
        const std::size_t i = idx(pid);
        const ParamSpec& spec = activeSpec(desc.params.e[i], out);
        out.spec[i] = &spec;
        if (spec.kind == Kind::derived)
            continue;
        const Snapped sn = snap(pid, spec, raw.v[i]);
        ResolvedParam& r = out.p[i];
        r.plain = sn.plain;
        r.step = sn.step;
        r.tag = sn.tag;
        r.flags = static_cast<uint8_t>(spec.flags | (sn.clamped ? kClamped : 0u));
        r.state = stateOf(spec.kind, sn.step);
        if (pid == Pid::look)
            applyBudget(out, raw.budget);
    }

    // 2. The derived parameters: a derive() sees every non-derived value, `look` already budget-clamped.
    for (const Pid pid : kResolveOrder) {
        const std::size_t i = idx(pid);
        const ParamSpec& spec = *out.spec[i];
        if (spec.kind != Kind::derived)
            continue;
        ResolvedParam& r = out.p[i];
        r.plain = spec.derive != nullptr ? spec.derive(out) : spec.value;
        r.step = -1;
        r.tag = kTagNone;
        r.flags = spec.flags;
        r.state = SlotState::derived;
        if (pid == Pid::look)
            applyBudget(out, raw.budget);
    }

    // 3. The Mode's display scale and the OR of the step tags.
    for (std::size_t i = 0; i < kNumModeParams; ++i) {
        const DisplayMap& m = out.spec[i]->display;
        ResolvedParam& r = out.p[i];
        r.display = m.toDisplay != nullptr ? m.toDisplay(r.plain) : r.plain;
        out.tags |= r.tag;
    }
}

void resolve(const ModeEntry& entry, const RawParams& raw, Resolution& out) noexcept {
    out.eng = EngineParams{};
    if (entry.desc == nullptr) {
        out.view = ParamView{};
        return;
    }
    resolveView(*entry.desc, raw, out.view);
    kit::physicalDefault(out.view, out.eng);
    if (entry.desc->physical != nullptr)
        entry.desc->physical(out.view, out.eng);
}

void modeDefaults(const ModeDescriptor& desc, RawParams& inOut) noexcept {
    // Walk the resolve order so that a variant sees its driver's NEW value (Clean: VOICE OFF makes DRIVE n/a, so DRIVE
    // keeps its raw value). Only live and stepped specs (continuous, stepped, hybrid) are written.
    ParamView partial;
    partial.desc = &desc;
    partial.slot = inOut.modeSlot;
    for (const Pid pid : kResolveOrder) {
        const std::size_t i = idx(pid);
        const ParamSpec& spec = activeSpec(desc.params.e[i], partial);
        if (spec.kind == Kind::continuous || spec.kind == Kind::stepped || spec.kind == Kind::hybrid)
            inOut.v[i] = spec.defaultPlain;
        partial.p[i].step = spec.kind == Kind::derived ? int8_t{-1} : snap(pid, spec, inOut.v[i]).step;
    }
}

namespace kit {

void physicalDefault(const ParamView& v, EngineParams& e) noexcept {
    const auto plain = [&v](Pid p) noexcept { return v.p[idx(p)].plain; };
    const auto tauMs = [&v](Pid p) noexcept {         // published time -> tau through the active spec's law (E §2.3)
        const ParamSpec* s = v.spec[idx(p)];
        return v.p[idx(p)].plain / lawFactor(s != nullptr ? s->law : TimeLaw::expDb);
    };
    const auto stepOf = [&v](Pid p) noexcept {
        const int8_t s = v.p[idx(p)].step;
        return static_cast<uint8_t>(s < 0 ? 0 : s);
    };

    e.thrDb = plain(Pid::thr);
    e.slope = plain(Pid::ratio);
    e.kneeDb = plain(Pid::knee);
    e.rangeDb = plain(Pid::range) >= kRangeOff ? kRangeOff : plain(Pid::range);
    e.atkTauMs = tauMs(Pid::atk);
    e.relTauMs = tauMs(Pid::rel);
    e.holdMs = plain(Pid::hold);
    e.lookMs = plain(Pid::look);
    e.driveDb = plain(Pid::drive);
    e.makeupDb = plain(Pid::makeup);
    e.mix = plain(Pid::mix);
    e.scHpfHz = plain(Pid::schpf) < 20.f ? 0.f : plain(Pid::schpf);
    e.sceDbOct = plain(Pid::sce);
    e.link = plain(Pid::link);
    e.s2ThrDb = plain(Pid::s2thr) >= kS2Off ? kS2Off : plain(Pid::s2thr);
    e.s2AtkTauMs = tauMs(Pid::s2atk);
    e.s2RelTauMs = tauMs(Pid::s2rel);

    e.det = stepOf(Pid::det);
    e.voice = stepOf(Pid::voice);
    e.tmode = stepOf(Pid::tmode);
    const float st = plain(Pid::stmode);                // the universal code (header comment)
    e.stmode = static_cast<uint8_t>(st > 0.f ? (st < 7.f ? std::lround(st) : 7L) : 0L);

    e.flags = plain(Pid::automu) >= 0.5f ? static_cast<uint8_t>(kEngAutoMakeup) : uint8_t{0};
    e.tags = v.tags;
}

} // namespace kit

} // namespace fcdsp
