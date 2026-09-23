// FCMP_PROBE layer=dsp name=hostparams scope=global timeout=60
//
// dsp.hostparams (S1 F2; SPRINTS S1.2; 01 §3.1-3.3; K2 #9, #14): the universal host-parameter superset and its maps.
//   - toPlain/toNorm round trip at 4,097 points (v = k/4096) for all 29 parameters: toPlain(toNorm(toPlain(v))) is
//     within 1e-6 of toPlain(v), relative to max(|plain|, |lo|, |hi|) (the linear maps cross zero); for the continuous
//     maps toNorm(toPlain(v)) is within 1e-6 of v. Index and boolean maps round trip exactly.
//   - the maps are monotone, hit their end points exactly, pass the power-map centres at v = 0.5 and the ratio3
//     breakpoints (S 0.95 at v 0.8, 1 at 0.9), map NaN to the default and clamp out-of-range arguments;
//   - legal() clamps to [lo, hi], rounds index and boolean values, maps NaN to the default, is idempotent and leaves
//     every toPlain() output unchanged;
//   - every default lies in its range, is legal and survives toPlain(toNorm(def));
//   - kApvtsOrder is a permutation of the 29 Pids, the 29 IDs are distinct [a-z0-9]+, kHostParams is in Pid order,
//     exactly the 22 Mode-filtered parameters are automatable preset parameters (01 §3.1 rules), and kSnapDomain is
//     generated from snapDomain().
// Spec rows only. The maps use libm (01 §3.3), so any golden row fed by them would need absrel (K2 #14); there is none.
#include "ProbeRegistry.h"

#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/Pid.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <set>
#include <string>
#include <string_view>

namespace
{
    using namespace fcdsp;

    bool continuousMap(Map m) { return m == Map::linear || m == Map::log || m == Map::power || m == Map::ratio3; }

    std::string key(std::string_view a, std::string_view b, std::string_view c = {})
    {
        std::string s(a);
        s.append(".").append(b);
        if (!c.empty())
            s.append(".").append(c);
        return s;
    }

    bool validId(std::string_view id)
    {
        return !id.empty() && std::all_of(id.begin(), id.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); });
    }
} // namespace

FCMP_PROBE(dsp, hostparams)
{
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
    constexpr int kPoints = 4096;

    // ---- 1. the maps, per parameter ---------------------------------------------------------------------------------
    long worstPlainParam = -1, worstNormParam = -1;
    double worstPlain = 0, worstNorm = 0;
    for (std::size_t i = 0; i < kNumParams; ++i)
    {
        const HostParam& h = kHostParams[i];
        const Pid pid = h.pid;
        const double scale = std::max({ 1e-30, std::fabs(static_cast<double>(h.lo)), std::fabs(static_cast<double>(h.hi)) });
        double plainErr = 0, normErr = 0;
        long nonmonotone = 0, illegal = 0, outOfRange = 0;
        float prevPlain = -std::numeric_limits<float>::infinity(), prevNorm = -1.f;
        for (int k = 0; k <= kPoints; ++k)
        {
            const float v = static_cast<float>(k) / static_cast<float>(kPoints);
            const float p = toPlain(pid, v);
            const float n = toNorm(pid, p);
            const float p2 = toPlain(pid, n);
            const double rel = std::fabs(static_cast<double>(p2) - static_cast<double>(p))
                             / std::max(scale, std::fabs(static_cast<double>(p)));
            plainErr = std::max(plainErr, rel);
            if (continuousMap(h.map))
                normErr = std::max(normErr, std::fabs(static_cast<double>(n) - static_cast<double>(v)));
            if (p < prevPlain)
                ++nonmonotone;
            prevPlain = p;
            if (!(p >= h.lo && p <= h.hi))
                ++outOfRange;
            if (legal(pid, p) != p)
                ++illegal;
            const float nn = toNorm(pid, h.lo + (h.hi - h.lo) * v);          // toNorm over a linear plain grid
            if (nn < prevNorm || !(nn >= 0.f && nn <= 1.f))
                ++nonmonotone;
            prevNorm = nn;
        }
        if (plainErr > worstPlain)
            worstPlain = plainErr, worstPlainParam = static_cast<long>(i);
        if (normErr > worstNorm)
            worstNorm = normErr, worstNormParam = static_cast<long>(i);
        P.le(key("map", h.id, "plain_round_trip_rel"), plainErr, 1e-6);
        if (continuousMap(h.map))
            P.le(key("map", h.id, "norm_round_trip_abs"), normErr, 1e-6);
        else
            P.eq(key("map", h.id, "plain_round_trip_exact"), plainErr == 0.0 ? 1 : 0, 1);
        P.eq(key("map", h.id, "nonmonotone"), nonmonotone, 0);
        P.eq(key("map", h.id, "out_of_range"), outOfRange, 0);
        P.eq(key("map", h.id, "toplain_not_legal"), illegal, 0);

        // end points, NaN, out-of-range arguments
        P.eq(key("map", h.id, "ends_exact"),
             toPlain(pid, 0.f) == h.lo && toPlain(pid, 1.f) == h.hi && toNorm(pid, h.lo) == 0.f && toNorm(pid, h.hi) == 1.f ? 1 : 0, 1);
        P.eq(key("map", h.id, "nan_is_default"),
             toPlain(pid, kNaN) == h.def && toNorm(pid, kNaN) == toNorm(pid, h.def) && legal(pid, kNaN) == h.def ? 1 : 0, 1);
        P.eq(key("map", h.id, "arguments_clamp"),
             toPlain(pid, -1.f) == h.lo && toPlain(pid, 2.f) == h.hi && toNorm(pid, h.lo - 1000.f) == 0.f
                     && toNorm(pid, h.hi + 1000.f) == 1.f ? 1 : 0, 1);

        // legal(): clamp, round, idempotent
        const float mid = 0.5f * (h.lo + h.hi) + 0.3f;
        const float lg = legal(pid, mid);
        const bool rounds = h.map == Map::index ? lg == std::round(std::min(std::max(mid, h.lo), h.hi))
                          : h.map == Map::boolean ? (lg == 0.f || lg == 1.f)
                                                  : lg == std::min(std::max(mid, h.lo), h.hi);
        P.eq(key("legal", h.id, "value"), rounds ? 1 : 0, 1);
        P.eq(key("legal", h.id, "clamps"), legal(pid, h.lo - 1000.f) == h.lo && legal(pid, h.hi + 1000.f) == h.hi ? 1 : 0, 1);
        P.eq(key("legal", h.id, "idempotent"), legal(pid, lg) == lg ? 1 : 0, 1);

        // defaults
        P.in(key("default", h.id, "in_range"), h.def, h.lo, h.hi);
        P.eq(key("default", h.id, "legal"), legal(pid, h.def) == h.def ? 1 : 0, 1);
        P.near(key("default", h.id, "survives_norm"), toPlain(pid, toNorm(pid, h.def)), h.def, 1e-6 * static_cast<double>(std::max(std::fabs(h.lo), std::fabs(h.hi))));
    }
    if (worstPlainParam >= 0)
        std::printf("NOTE     worst plain round trip %.3g (%s); worst norm round trip %.3g (%s)\n", worstPlain,
                    kHostParams[static_cast<std::size_t>(worstPlainParam)].id, worstNorm,
                    worstNormParam >= 0 ? kHostParams[static_cast<std::size_t>(worstNormParam)].id : "-");

    // ---- 2. the map shapes of 01 §3.3 --------------------------------------------------------------------------------
    P.near("shape.knee.centre", toPlain(Pid::knee, 0.5f), 12.0, 0.0, 1e-5);
    P.near("shape.hold.centre", toPlain(Pid::hold, 0.5f), 50.0, 0.0, 1e-5);
    P.near("shape.schpf.centre", toPlain(Pid::schpf, 0.5f), 80.0, 0.0, 1e-5);
    P.near("shape.ratio3.v0_8_is_20_to_1", toPlain(Pid::ratio, 0.8f), 0.95, 0.0, 1e-6);
    P.near("shape.ratio3.v0_9_is_infinity", toPlain(Pid::ratio, 0.9f), 1.0, 0.0, 1e-6);
    P.near("shape.ratio3.v0_4", toPlain(Pid::ratio, 0.4f), 1.0 - std::pow(20.0, -0.5), 0.0, 1e-6);
    P.near("shape.ratio3.default_4_to_1", toPlain(Pid::ratio, toNorm(Pid::ratio, 0.75f)), 0.75, 0.0, 1e-6);
    P.near("shape.atk.log_midpoint", toPlain(Pid::atk, 0.5f), std::sqrt(0.005 * 300.0), 0.0, 1e-5);
    P.near("shape.rel.log_midpoint", toPlain(Pid::rel, 0.5f), std::sqrt(30000.0), 0.0, 1e-5);
    P.near("shape.thr.linear_midpoint", toPlain(Pid::thr, 0.5f), -18.0, 1e-5);
    P.eq("shape.index.rounds", toPlain(Pid::det, 0.5f) == 4.f && toPlain(Pid::mode, 1.f / 127.f) == 1.f ? 1 : 0, 1);
    P.eq("shape.boolean.threshold", toPlain(Pid::bypass, 0.49f) == 0.f && toPlain(Pid::bypass, 0.5f) == 1.f ? 1 : 0, 1);
    P.eq("legal.index.half_rounds_away", legal(Pid::det, 2.5f) == 3.f && legal(Pid::det, 2.4f) == 2.f ? 1 : 0, 1);
    P.eq("legal.boolean.half_is_on", legal(Pid::automu, 0.5f) == 1.f && legal(Pid::automu, 0.49f) == 0.f ? 1 : 0, 1);

    // ---- 3. the table: IDs, order, rules ---------------------------------------------------------------------------------
    {
        std::array<int, kNumParams> seen{};
        for (const Pid p : kApvtsOrder)
            if (idx(p) < kNumParams)
                ++seen[idx(p)];
        int once = 0;
        for (const int s : seen)
            once += s == 1 ? 1 : 0;
        P.eq("table.apvts_order.size", static_cast<int64_t>(kApvtsOrder.size()), 29);
        P.eq("table.apvts_order.each_pid_once", once, 29);

        std::set<std::string_view> ids;
        int badIds = 0, pidOrder = 0, presetRule = 0, steps = 0, choices = 0;
        for (std::size_t i = 0; i < kNumParams; ++i)
        {
            const HostParam& h = kHostParams[i];
            ids.insert(h.id);
            badIds += validId(h.id) ? 0 : 1;
            pidOrder += idx(h.pid) == i ? 0 : 1;
            const bool modeFiltered = i < kNumModeParams;
            presetRule += (h.inPresets == modeFiltered && (!modeFiltered || h.automatable)) ? 0 : 1;
            const bool discrete = h.map == Map::index || h.map == Map::boolean;
            steps += discrete == (h.numSteps > 0) ? 0 : 1;
            choices += (h.choices != nullptr) == (h.pid == Pid::quality || h.pid == Pid::labudget) ? 0 : 1;
        }
        P.eq("table.ids.distinct", static_cast<int64_t>(ids.size()), 29);
        P.eq("table.ids.valid", badIds, 0);
        P.eq("table.pid_order", pidOrder, 0);
        P.eq("table.preset_and_automation_rules", presetRule, 0);
        P.eq("table.steps_iff_discrete", steps, 0);
        P.eq("table.choices_only_for_setup", choices, 0);
        P.eq("table.index_lists_hold_8", kHostParams[idx(Pid::det)].numSteps == 8 && kHostParams[idx(Pid::tmode)].numSteps == 8
                                             && kHostParams[idx(Pid::stmode)].numSteps == 8 && kHostParams[idx(Pid::voice)].numSteps == 8 ? 1 : 0, 1);
        P.eq("table.mode_slots_128", kHostParams[idx(Pid::mode)].numSteps, 128);
        P.eq("table.non_automatable_globals",
             !kHostParams[idx(Pid::mode)].automatable && !kHostParams[idx(Pid::extkey)].automatable
                     && !kHostParams[idx(Pid::listen)].automatable && !kHostParams[idx(Pid::delta)].automatable
                     && !kHostParams[idx(Pid::quality)].automatable && !kHostParams[idx(Pid::labudget)].automatable
                     && kHostParams[idx(Pid::bypass)].automatable ? 1 : 0, 1);

        int domain = 0;
        for (std::size_t i = 0; i < kNumModeParams; ++i)
            domain += kSnapDomain[i] == snapDomain(static_cast<Pid>(i)) ? 0 : 1;
        P.eq("table.snap_domain_generated", domain, 0);
        P.eq("table.snap_domain.times_log",
             snapDomain(Pid::atk) == SnapDomain::log && snapDomain(Pid::rel) == SnapDomain::log && snapDomain(Pid::s2atk) == SnapDomain::log
                     && snapDomain(Pid::s2rel) == SnapDomain::log && snapDomain(Pid::schpf) == SnapDomain::host
                     && snapDomain(Pid::ratio) == SnapDomain::linear ? 1 : 0, 1);
    }

    return P.finish();
}
