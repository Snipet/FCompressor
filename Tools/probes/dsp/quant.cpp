// FCMP_PROBE layer=dsp name=quant scope=mode timeout=60
//
// dsp.quant.<key> (F3, S2; D3: 03 §3.4, C §5.4; 01 §4.4-4.5 snap semantics; E §4.2.3; K1 #8; SPRINTS §7 D6): stepped-
// parameter quantisation through the ONE resolver, for every Mode-filtered parameter of the Mode, under every active
// spec (the base spec and each variant, with its driver set to that step). Spec rows only: every row is structural
// (the resolver's contract), so the rows are blocking even for a provisional Mode.
//
//   sweep       the host-normalised value 0 -> 1 in 1/4096 steps through toPlain and resolveView:
//     stepped     quant.<pid>.off_detent   the resolved plain is bit-equal to a declared step, always
//                 quant.<pid>.unreached    every step is reached
//                 quant.<pid>.nonmonotone  the step index never falls as the value rises
//                 quant.<pid>.boundary     the step equals this probe's own nearest-in-snap-domain choice (linear
//                                          plain, log ln(plain), host toNorm; ties to the LOWER step): the boundaries
//                                          sit at the declared midpoints, with no hysteresis
//     hybrid      inside [lo, hi] the value passes through (<= 1 ulp); outside, the nearest of {steps, lo, hi} wins
//     continuous  quant.<pid>.passthrough  clamp(raw, lo, hi) within 1 ulp; quant.<pid>.clamped_flag set exactly
//                                          when raw lies outside [lo, hi]
//     locked/n/a  quant.<pid>.fixed        the declared value, always
//   look        the lookahead budget clamp (K1 #8) for OFF / 5 ms / 20 ms: locked at 0 while OFF, else
//               min(clamp(raw), budget) with kClamped above it, and EngineParams::lookMs equal to it
//   D1 between detents (C §5.4 step 3): for each stepped parameter, at the Mode default's detent and its neighbour, a
//               short D1 (1 kHz at T - 10, T + 10, T + 20 dB) at raw values 25 % and 75 % of the way between the two
//               detents in the snap domain measures exactly what the snapped detent measures (bit-equal gains):
//               quant.d1.<pid>.lo_is_detent, quant.d1.<pid>.hi_is_detent
// A variant context adds .v<driver step> to <pid>; an unused spec kind (derived) is counted in a NOTE.
#include "ProbeRegistry.h"

#include "EngineRig.h"

#include "fcdsp/analysis/Analysis.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/params/Setup.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace
{
    using namespace fcdsp;
    using funkgui::test::Probe;

    constexpr int kSweep = 4096;
    constexpr float kFs = 48000.0f;

    constexpr std::array<const char*, kNumModeParams> kPidNames{
        "thr", "ratio", "knee", "range", "atk", "rel", "tmode", "hold", "look", "det", "schpf",
        "sce", "link", "stmode", "voice", "drive", "makeup", "automu", "mix", "s2thr", "s2atk", "s2rel" };

    // The snap domain of 01 §3.2 (snapDomain), computed here independently of Resolve.cpp's.
    double toDomain(Pid pid, float plain)
    {
        switch (snapDomain(pid))
        {
            case SnapDomain::log:    return std::log(static_cast<double>(plain));
            case SnapDomain::host:   return static_cast<double>(toNorm(pid, plain));
            case SnapDomain::linear: return static_cast<double>(plain);
        }
        return static_cast<double>(plain);
    }

    float fromDomain(Pid pid, double d)
    {
        switch (snapDomain(pid))
        {
            case SnapDomain::log:    return static_cast<float>(std::exp(d));
            case SnapDomain::host:   return toPlain(pid, static_cast<float>(d));
            case SnapDomain::linear: return static_cast<float>(d);
        }
        return static_cast<float>(d);
    }

    // The nearest candidate in the snap domain, ties to the lower index (candidates ascending).
    std::size_t nearest(Pid pid, const std::vector<float>& candidates, float raw)
    {
        const double d = toDomain(pid, raw);
        std::size_t best = 0;
        double bestDist = std::fabs(d - toDomain(pid, candidates[0]));
        for (std::size_t i = 1; i < candidates.size(); ++i)
        {
            const double dist = std::fabs(d - toDomain(pid, candidates[i]));
            if (dist < bestDist)
            {
                best = i;
                bestDist = dist;
            }
        }
        return best;
    }

    bool withinUlp(float got, float want)
    {
        if (got == want)
            return true;
        const float up = std::nextafter(want, std::numeric_limits<float>::infinity());
        const float down = std::nextafter(want, -std::numeric_limits<float>::infinity());
        return got == up || got == down;
    }

    struct Context
    {
        const ParamSpec* spec;
        Pid driver = kNoPid;
        float driverPlain = 0;
        std::string label;
    };

    // The spec contexts of one entry: each variant (driver at its step), and the base spec with the driver at a step
    // no variant claims (none: the base spec is never active and is skipped).
    std::vector<Context> contexts(const ModeDescriptor& d, const RawParams& base, Pid pid)
    {
        const ParamEntry& e = d.params.e[idx(pid)];
        const std::string name = kPidNames[idx(pid)];
        if (e.driver == kNoPid || e.variants.empty())
            return { Context{ &e.spec, kNoPid, 0, name } };
        ParamView v;
        resolveView(d, base, v);
        const ParamSpec* driverSpec = v.spec[idx(e.driver)];
        const int n = driverSpec != nullptr ? stepCount(*driverSpec) : 0;
        std::vector<Context> out;
        for (const Variant& var : e.variants)
            if (var.driverStep >= 0 && var.driverStep < n)
                out.push_back({ &var.spec, e.driver, stepPlain(*driverSpec, var.driverStep),
                                name + ".v" + std::to_string(var.driverStep) });
        for (int s = 0; s < n; ++s)
        {
            bool claimed = false;
            for (const Variant& var : e.variants)
                claimed = claimed || var.driverStep == s;
            if (!claimed)
            {
                out.push_back({ &e.spec, e.driver, stepPlain(*driverSpec, s), name + ".base" });
                break;
            }
        }
        return out;
    }

    // A short D1: the left channel's single-bin gains at T - 10, T + 10 and T + 20 dB.
    std::vector<double> shortD1(const ModeEntry& en, const EngineParams& e)
    {
        const double t = analysis::inputThresholdDb(e);
        const double levels[] = { t - 10.0, t + 10.0, t + 20.0 };
        fcmp::probe::EngineRig rig(en, e, kFs);
        const fcmp::probe::CurveRun run =
            fcmp::probe::runSineStaircase(rig, levels, fcmp::probe::peakOffsetDb(en, e), 1000.0, 0.3, 0.1);
        std::vector<double> g;
        for (const fcmp::probe::CurvePoint& p : run.points)
            g.push_back(p.gainDb);
        return g;
    }
} // namespace

FCMP_PROBE(dsp, quant)
{
    const ModeEntry& en = fcmp::probe::modeEntry(C.key);
    const ModeDescriptor& desc = *en.desc;
    // The generic sweep runs with the 20 ms budget, so `look` is live; the budget rows below cover OFF and 5 ms.
    const RawParams base = fcmp::probe::modeRaw(en, LookaheadBudget::ms20);

    std::int64_t derivedSkipped = 0;
    for (std::size_t pi = 0; pi < kNumModeParams; ++pi)
    {
        const Pid pid = static_cast<Pid>(pi);
        for (const Context& ctx : contexts(desc, base, pid))
        {
            const ParamSpec& s = *ctx.spec;
            const std::string k = "quant." + ctx.label;
            RawParams raw = base;
            if (ctx.driver != kNoPid)
                raw[ctx.driver] = ctx.driverPlain;

            std::vector<float> steps;
            for (const Step& st : s.steps)
                steps.push_back(st.plain);
            std::vector<float> hybridCands = steps;         // hybrid: {steps, lo, hi}, ascending
            if (s.kind == Kind::hybrid)
            {
                hybridCands.push_back(s.lo);
                hybridCands.push_back(s.hi);
                std::sort(hybridCands.begin(), hybridCands.end());
            }

            std::int64_t specMismatch = 0, offDetent = 0, nonmonotone = 0, boundary = 0, passthrough = 0;
            std::int64_t clampedFlag = 0, fixed = 0;
            std::vector<bool> reached(steps.size(), false);
            int prevStep = -1;
            ParamView v;
            for (int q = 0; q <= kSweep; ++q)
            {
                const float r = toPlain(pid, static_cast<float>(q) / static_cast<float>(kSweep));
                raw[pid] = r;
                resolveView(desc, raw, v);
                const ResolvedParam& rp = v[pid];
                specMismatch += v.spec[pi] == &s ? 0 : 1;
                switch (s.kind)
                {
                    case Kind::stepped:
                    {
                        bool on = false;
                        for (const float p : steps)
                            on = on || std::memcmp(&p, &rp.plain, sizeof p) == 0;
                        offDetent += on ? 0 : 1;
                        if (rp.step >= 0 && static_cast<std::size_t>(rp.step) < steps.size())
                            reached[static_cast<std::size_t>(rp.step)] = true;
                        nonmonotone += rp.step < prevStep ? 1 : 0;
                        prevStep = rp.step;
                        boundary += static_cast<std::size_t>(rp.step) == nearest(pid, steps, r) ? 0 : 1;
                        break;
                    }
                    case Kind::hybrid:
                        if (s.lo <= r && r <= s.hi)
                            passthrough += withinUlp(rp.plain, r) && rp.step < 0 ? 0 : 1;
                        else
                        {
                            const float want = hybridCands[nearest(pid, hybridCands, r)];
                            boundary += rp.plain == want ? 0 : 1;
                        }
                        break;
                    case Kind::continuous:
                    {
                        const float want = std::clamp(r, s.lo, s.hi);
                        passthrough += withinUlp(rp.plain, want) && rp.step < 0 ? 0 : 1;
                        const bool outside = r < s.lo || r > s.hi;
                        clampedFlag += ((rp.flags & kClamped) != 0) == outside ? 0 : 1;
                        break;
                    }
                    case Kind::locked:
                    case Kind::notApplicable:
                        fixed += rp.plain == s.value ? 0 : 1;
                        break;
                    case Kind::derived:
                        break;
                }
            }
            P.eq(k + ".spec_mismatch", specMismatch, 0);
            switch (s.kind)
            {
                case Kind::stepped:
                {
                    std::int64_t unreached = 0;
                    for (const bool b : reached)
                        unreached += b ? 0 : 1;
                    P.eq(k + ".off_detent", offDetent, 0);
                    P.eq(k + ".unreached", unreached, 0);
                    P.eq(k + ".nonmonotone", nonmonotone, 0);
                    P.eq(k + ".boundary", boundary, 0);
                    break;
                }
                case Kind::hybrid:
                    P.eq(k + ".passthrough", passthrough, 0);
                    P.eq(k + ".boundary", boundary, 0);
                    break;
                case Kind::continuous:
                    P.eq(k + ".passthrough", passthrough, 0);
                    P.eq(k + ".clamped_flag", clampedFlag, 0);
                    break;
                case Kind::locked:
                case Kind::notApplicable:
                    P.eq(k + ".fixed", fixed, 0);
                    break;
                case Kind::derived:
                    ++derivedSkipped;
                    break;
            }
        }
    }
    if (derivedSkipped > 0)
        std::printf("NOTE     %lld derived spec(s): their value follows derive(), not the sweep\n",
                    static_cast<long long>(derivedSkipped));

    // ---- look: the lookahead budget clamp (K1 #8) -------------------------------------------------------------------
    {
        const LookaheadBudget budgets[] = { LookaheadBudget::off, LookaheadBudget::ms5, LookaheadBudget::ms20 };
        const char* names[] = { "off", "5ms", "20ms" };
        for (int b = 0; b < 3; ++b)
        {
            RawParams raw = fcmp::probe::modeRaw(en, budgets[b]);
            const ParamSpec* s = &desc.params[Pid::look].spec;
            if (s->kind == Kind::notApplicable && b == 0)
                std::printf("NOTE     look is n/a in this Mode; the budget rows check the neutral value\n");
            std::int64_t bad = 0;
            for (int q = 0; q <= kSweep; ++q)
            {
                const float r = toPlain(Pid::look, static_cast<float>(q) / static_cast<float>(kSweep));
                raw[Pid::look] = r;
                const Resolution res = fcmp::probe::resolveRaw(en, raw);
                const ResolvedParam& rp = res.view[Pid::look];
                if (s->kind == Kind::notApplicable)
                    bad += rp.plain == s->value ? 0 : 1;
                else if (budgets[b] == LookaheadBudget::off)
                    bad += rp.plain == 0.0f && rp.state == SlotState::locked && res.eng.lookMs == 0.0f ? 0 : 1;
                else if (s->kind == Kind::continuous || s->kind == Kind::hybrid)
                {
                    const float hiEff = std::min(s->hi, budgetMs(budgets[b]));
                    const float want = std::min(std::clamp(r, s->lo, s->hi), hiEff);
                    const bool clamped = r > hiEff || r < s->lo;
                    bad += withinUlp(rp.plain, want) && ((rp.flags & kClamped) != 0) == clamped
                                   && res.eng.lookMs == rp.plain
                               ? 0 : 1;
                }
                else
                    bad += rp.plain <= budgetMs(budgets[b]) && res.eng.lookMs == rp.plain ? 0 : 1;
            }
            P.eq(std::string("quant.look.budget_") + names[b] + ".violations", bad, 0);
        }
    }

    // ---- D1 between two detents (C §5.4 step 3) ---------------------------------------------------------------------
    {
        const RawParams dflt = fcmp::probe::modeRaw(en);
        ParamView v;
        resolveView(desc, dflt, v);
        for (std::size_t pi = 0; pi < kNumModeParams; ++pi)
        {
            const Pid pid = static_cast<Pid>(pi);
            const ParamSpec* s = v.spec[pi];
            if (s == nullptr || s->kind != Kind::stepped || s->steps.size() < 2)
                continue;
            const int n = static_cast<int>(s->steps.size());
            const int at = v[pid].step < 0 ? 0 : v[pid].step;
            const int i = at + 1 < n ? at : at - 1, j = i + 1;
            const double di = toDomain(pid, s->steps[static_cast<std::size_t>(i)].plain);
            const double dj = toDomain(pid, s->steps[static_cast<std::size_t>(j)].plain);
            const std::string k = std::string("quant.d1.") + kPidNames[pi];

            const auto run = [&](float plain, int wantStep, std::vector<double>& gains, EngineParams& eng) {
                RawParams raw = dflt;
                raw[pid] = plain;
                const Resolution res = fcmp::probe::resolveRaw(en, raw);
                eng = res.eng;
                gains = shortD1(en, res.eng);
                return res.view[pid].step == wantStep;
            };
            std::vector<double> gi, gj, glo, ghi;
            EngineParams ei{}, ej{}, elo{}, ehi{};
            run(s->steps[static_cast<std::size_t>(i)].plain, i, gi, ei);
            run(s->steps[static_cast<std::size_t>(j)].plain, j, gj, ej);
            const bool loSnaps = run(fromDomain(pid, di + 0.25 * (dj - di)), i, glo, elo);
            const bool hiSnaps = run(fromDomain(pid, di + 0.75 * (dj - di)), j, ghi, ehi);
            const bool loSame = loSnaps && glo == gi && std::memcmp(&elo, &ei, sizeof ei) == 0;
            const bool hiSame = hiSnaps && ghi == gj && std::memcmp(&ehi, &ej, sizeof ej) == 0;
            P.eq(k + ".lo_is_detent", loSame ? 1 : 0, 1);
            P.eq(k + ".hi_is_detent", hiSame ? 1 : 0, 1);
            double diff = 0.0;
            for (std::size_t q = 0; q < gi.size() && q < gj.size(); ++q)
                diff = std::max(diff, std::fabs(gi[q] - gj[q]));
            std::printf("NOTE     %s: detents %d and %d (%s, %s) differ by %.4g dB in D1\n", k.c_str(), i, j,
                        s->steps[static_cast<std::size_t>(i)].label, s->steps[static_cast<std::size_t>(j)].label, diff);
        }
    }
    return P.finish();
}
