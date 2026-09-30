// The universal host-parameter superset and its maps (01 §3.1, §3.3). IDs, plain ranges, maps and defaults are
// v1-forever. Q5 (mix 0-200 %), Q9 (FET 76 relabels tmode as its GR switch: a Mode concern, the host range here is
// the universal 0..7) and Q10 (0 dBFS = +22 dBu, core/Units.h) are baked in at their recorded defaults.
//
// The maps run in double and return float. params/ may use libm (01 §2.2): these functions are not the per-sample
// path, and the resolver snaps their result.

#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/EngineParams.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace fcdsp {

namespace {
constexpr const char* kQualityChoices[] = { "ECO", "STD", "HQ" };          // Quality::{eco, std, hq}
constexpr const char* kBudgetChoices[]  = { "OFF", "5 MS", "20 MS" };      // LookaheadBudget::{off, ms5, ms20}
} // namespace

// 01 §3.1, in Pid order. Every v1 parameter is version hint (vh) 1; `output` (v1.2, ADR-88) is 2, so hosts that key on
// the hint (AU) see a later parameter. aut = automatable; pre = stored in presets (the
// Mode rides in presets as the modeId attribute, never as a parameter). Bypass is the host's bypass parameter
// (getBypassParameter), automatable like any other. n = numSteps (0 = continuous).
constexpr std::array<HostParam, kNumParams> kHostParams {{
    // pid          id          name                 unit      map           lo      hi     centre def   vh aut    pre    n
    { Pid::thr,     "thr",      "Threshold",         "DB",     Map::linear,  -60,    24,    0,     -18,  1, true,  true,  0,   nullptr },
    { Pid::ratio,   "ratio",    "Ratio",             "",       Map::ratio3,  0,      2,     0,     0.75, 1, true,  true,  0,   nullptr },
    { Pid::knee,    "knee",     "Knee",              "DB",     Map::power,   0,      72,    12,    6,    1, true,  true,  0,   nullptr },
    { Pid::range,   "range",    "Range",             "DB",     Map::linear,  0,      60,    0,     60,   1, true,  true,  0,   nullptr },
    { Pid::atk,     "atk",      "Attack",            "MS",     Map::log,     0.005f, 300,   0,     10,   1, true,  true,  0,   nullptr },
    { Pid::rel,     "rel",      "Release",           "MS",     Map::log,     1,      30000, 0,     200,  1, true,  true,  0,   nullptr },
    { Pid::tmode,   "tmode",    "Time Mode",         "",       Map::index,   0,      7,     0,     0,    1, true,  true,  8,   nullptr },
    { Pid::hold,    "hold",     "Hold",              "MS",     Map::power,   0,      500,   50,    0,    1, true,  true,  0,   nullptr },
    { Pid::look,    "look",     "Lookahead",         "MS",     Map::linear,  0,      20,    0,     0,    1, true,  true,  0,   nullptr },
    { Pid::det,     "det",      "Detector",          "",       Map::index,   0,      7,     0,     0,    1, true,  true,  8,   nullptr },
    { Pid::schpf,   "schpf",    "SC High-Pass",      "HZ",     Map::power,   0,      500,   80,    0,    1, true,  true,  0,   nullptr },
    { Pid::sce,     "sce",      "SC Emphasis",       "DB/OCT", Map::linear,  -6,     6,     0,     0,    1, true,  true,  0,   nullptr },
    { Pid::link,    "link",     "Stereo Link",       "%",      Map::linear,  0,      1,     0,     1,    1, true,  true,  0,   nullptr },
    { Pid::stmode,  "stmode",   "Stereo Mode",       "",       Map::index,   0,      7,     0,     0,    1, true,  true,  8,   nullptr },
    { Pid::voice,   "voice",    "Voice",             "",       Map::index,   0,      7,     0,     0,    1, true,  true,  8,   nullptr },
    { Pid::drive,   "drive",    "Drive",             "DB",     Map::linear,  -24,    24,    0,     0,    1, true,  true,  0,   nullptr },
    { Pid::makeup,  "makeup",   "Makeup",            "DB",     Map::linear,  -24,    36,    0,     0,    1, true,  true,  0,   nullptr },
    { Pid::automu,  "automu",   "Auto Makeup",       "",       Map::boolean, 0,      1,     0,     0,    1, true,  true,  2,   nullptr },
    { Pid::mix,     "mix",      "Mix",               "%",      Map::linear,  0,      2,     0,     1,    1, true,  true,  0,   nullptr },
    { Pid::s2thr,   "s2thr",    "Stage 2 Threshold", "DB",     Map::linear,  -40,    24,    0,     24,   1, true,  true,  0,   nullptr },
    { Pid::s2atk,   "s2atk",    "Stage 2 Attack",    "MS",     Map::log,     0.005f, 300,   0,     1,    1, true,  true,  0,   nullptr },
    { Pid::s2rel,   "s2rel",    "Stage 2 Release",   "MS",     Map::log,     1,      30000, 0,     100,  1, true,  true,  0,   nullptr },
    { Pid::mode,    "mode",     "Mode",              "",       Map::index,   0,      127,   0,     0,    1, false, false, 128, nullptr },
    { Pid::extkey,  "extkey",   "External Key",      "",       Map::boolean, 0,      1,     0,     0,    1, false, false, 2,   nullptr },
    { Pid::listen,  "listen",   "SC Listen",         "",       Map::boolean, 0,      1,     0,     0,    1, false, false, 2,   nullptr },
    { Pid::delta,   "delta",    "Delta",             "",       Map::boolean, 0,      1,     0,     0,    1, false, false, 2,   nullptr },
    { Pid::bypass,  "bypass",   "Bypass",            "",       Map::boolean, 0,      1,     0,     0,    1, true,  false, 2,   nullptr },
    { Pid::quality, "quality",  "Quality",           "",       Map::index,   0,      2,     0,     1,    1, false, false, 3,   kQualityChoices },
    { Pid::labudget,"labudget", "Lookahead Budget",  "",       Map::index,   0,      2,     0,     0,    1, false, false, 3,   kBudgetChoices },
    { Pid::output,  "output",   "Output",            "DB",     Map::linear,  -24,    24,    0,     0,    2, true,  false, 0,   nullptr },
}};

namespace {

// Compile-time audit of the table against the rules of 01 §3.1 and §3.3.
consteval bool tableIsWellFormed() {
    for (std::size_t i = 0; i < kNumParams; ++i) {
        const HostParam& h = kHostParams[i];
        const bool modeFiltered = i < kNumModeParams;
        if (idx(h.pid) != i || h.id == nullptr || h.name == nullptr || h.unit == nullptr) return false;
        if (!(h.lo < h.hi) || h.def < h.lo || h.def > h.hi || h.versionHint < 1) return false;
        if (modeFiltered != h.inPresets) return false;                         // presets: Mode-filtered params only
        if (modeFiltered && !h.automatable) return false;                      // every Mode-filtered param automates
        switch (h.map) {
            case Map::log:     if (!(h.lo > 0.f) || h.numSteps != 0) return false; break;
            case Map::power:   if (!(h.centre > h.lo && h.centre < h.hi) || h.numSteps != 0) return false; break;
            case Map::linear:
            case Map::ratio3:  if (h.numSteps != 0) return false; break;
            case Map::index:
            case Map::boolean:
                if (h.lo != 0.f || h.numSteps < 2 || h.hi != static_cast<float>(h.numSteps - 1)) return false;
                break;
        }
        if ((h.choices != nullptr) != (h.pid == Pid::quality || h.pid == Pid::labudget)) return false;
    }
    return true;
}
static_assert(tableIsWellFormed(), "kHostParams violates 01 §3.1/§3.3");
static_assert(kHostParams[idx(Pid::mix)].hi == 2.f, "Q5: mix runs 0-200 %");
static_assert(kHostParams[idx(Pid::range)].hi == kRangeOff && kHostParams[idx(Pid::s2thr)].hi == kS2Off,
              "the range and stage-2 sentinels are the host range ends (K2 #20)");

const HostParam* find(Pid p) noexcept {
    return idx(p) < kNumParams ? &kHostParams[idx(p)] : nullptr;
}

// Map::power exponent: plain = lo + (hi - lo)*v^k passes through `centre` at v = 0.5.
double powerExponent(const HostParam& h) noexcept {
    const double lo = static_cast<double>(h.lo);
    return std::log2((static_cast<double>(h.hi) - lo) / (static_cast<double>(h.centre) - lo));
}

// Map::ratio3 (E §4.3). expm1/log1p keep full precision near S = 0 (1:1), where 1 - 20^(-v/0.8) would cancel.
double ratio3Plain(double v) noexcept {
    if (v <= 0.8) return -std::expm1(-(v / 0.8) * std::log(20.0));           // 1:1 -> 20:1
    if (v <= 0.9) return 0.95 + 0.5 * (v - 0.8);                             // 20:1 -> inf:1
    return 1.0 + 10.0 * (v - 0.9);                                           // inf:1 -> -1:1
}
double ratio3Norm(double s) noexcept {
    if (s <= 0.95) return -0.8 * std::log1p(-s) / std::log(20.0);
    if (s <= 1.0) return 0.8 + (s - 0.95) / 0.5;
    return 0.9 + (s - 1.0) / 10.0;
}

} // namespace

float toPlain(Pid p, float norm) noexcept {
    const HostParam* h = find(p);
    if (h == nullptr) return 0.f;
    if (std::isnan(norm)) return h->def;
    const double v = std::clamp(static_cast<double>(norm), 0.0, 1.0);
    const double lo = static_cast<double>(h->lo), hi = static_cast<double>(h->hi);
    double plain = lo;
    switch (h->map) {
        case Map::linear:  plain = lo + (hi - lo) * v; break;
        case Map::log:     plain = lo * std::pow(hi / lo, v); break;
        case Map::power:   plain = lo + (hi - lo) * std::pow(v, powerExponent(*h)); break;
        case Map::ratio3:  plain = ratio3Plain(v); break;
        case Map::index:   plain = lo + std::round(v * (h->numSteps - 1)); break;
        case Map::boolean: plain = v >= 0.5 ? 1.0 : 0.0; break;
    }
    return static_cast<float>(std::clamp(plain, lo, hi));
}

float toNorm(Pid p, float plain) noexcept {
    const HostParam* h = find(p);
    if (h == nullptr) return 0.f;
    const double lo = static_cast<double>(h->lo), hi = static_cast<double>(h->hi);
    const double x = std::clamp(static_cast<double>(std::isnan(plain) ? h->def : plain), lo, hi);
    double v = 0.0;
    switch (h->map) {
        case Map::linear:  v = (x - lo) / (hi - lo); break;
        case Map::log:     v = std::log(x / lo) / std::log(hi / lo); break;
        case Map::power:   v = std::pow((x - lo) / (hi - lo), 1.0 / powerExponent(*h)); break;
        case Map::ratio3:  v = ratio3Norm(x); break;
        case Map::index:   v = std::round(x - lo) / (h->numSteps - 1); break;
        case Map::boolean: v = x >= 0.5 ? 1.0 : 0.0; break;
    }
    return static_cast<float>(std::clamp(v, 0.0, 1.0));
}

float legal(Pid p, float plain) noexcept {
    const HostParam* h = find(p);
    if (h == nullptr) return 0.f;
    if (std::isnan(plain)) return h->def;
    const float x = std::clamp(plain, h->lo, h->hi);
    switch (h->map) {
        case Map::index:   return h->lo + std::round(x - h->lo);
        case Map::boolean: return x >= 0.5f ? 1.f : 0.f;
        case Map::linear:
        case Map::log:
        case Map::power:
        case Map::ratio3:  break;
    }
    return x;
}

} // namespace fcdsp
