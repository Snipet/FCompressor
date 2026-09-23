#pragma once

// Parameter identities, the host layout order, the resolve order and the snap domains (01 §3.1-3.2; K2 #9).
// Pid order is internal: sprint-frozen, free to regroup between sprints, never persisted and never sent to a host.
// kApvtsOrder is v1-forever: the host layout order, into which later parameters are appended.

#include <array>
#include <cstddef>
#include <cstdint>

namespace fcdsp {

// Internal order: the Mode-filtered block first (ModeDescriptor covers exactly it), then the globals.
// Sprint-frozen, free to regroup between sprints; never persisted and never sent to a host (K2 #9).
enum class Pid : uint8_t {
    thr, ratio, knee, range, atk, rel, tmode, hold, look, det,
    schpf, sce, link, stmode, voice, drive, makeup, automu, mix,
    s2thr, s2atk, s2rel,
    kModeCount,                       // 22: the Mode-filtered set (ModeDescriptor covers exactly these)
    mode = kModeCount, extkey, listen, delta, bypass, quality, labudget,
    kCount                            // 29
};
inline constexpr std::size_t kNumModeParams = static_cast<std::size_t>(Pid::kModeCount);
inline constexpr std::size_t kNumParams     = static_cast<std::size_t>(Pid::kCount);
inline constexpr Pid kNoPid = Pid::kCount;
constexpr std::size_t idx(Pid p) noexcept { return static_cast<std::size_t>(p); }

// Host (APVTS) layout order: v1-forever. v1 equals the 01 §3.1 table; a v2 parameter is added to the Pid block it
// belongs to AND appended here. dsp.registry (and the static_assert below) check that this is a permutation of every
// Pid.
inline constexpr std::array<Pid, kNumParams> kApvtsOrder {
    Pid::thr, Pid::ratio, Pid::knee, Pid::range, Pid::atk, Pid::rel, Pid::tmode, Pid::hold, Pid::look, Pid::det,
    Pid::schpf, Pid::sce, Pid::link, Pid::stmode, Pid::voice, Pid::drive, Pid::makeup, Pid::automu, Pid::mix,
    Pid::s2thr, Pid::s2atk, Pid::s2rel,
    Pid::mode, Pid::extkey, Pid::listen, Pid::delta, Pid::bypass, Pid::quality, Pid::labudget /* v2 appends go here */ };

// Resolve order: a ParamEntry's `driver` must appear EARLIER here (registry lint). Derived params are
// evaluated in a second pass after every non-derived param, so "derived from" may point anywhere non-derived.
inline constexpr std::array<Pid, kNumModeParams> kResolveOrder {
    Pid::voice, Pid::tmode, Pid::det, Pid::stmode, Pid::automu,
    Pid::ratio, Pid::knee, Pid::thr, Pid::range, Pid::atk, Pid::rel, Pid::hold, Pid::look,
    Pid::drive, Pid::makeup, Pid::mix, Pid::schpf, Pid::sce, Pid::link,
    Pid::s2thr, Pid::s2atk, Pid::s2rel };

// Snap domain for stepped values (E §4.2.3): midpoints in this domain; ties go to the LOWER step.
//   linear  plain (thr, ratio as S, knee, ...: every Pid not named below)
//   log     ln(plain): the four times atk, rel, s2atk, s2rel
//   host    toNorm(pid, plain): schpf (its power map)
// FZ0 errata (R-F0 #4): keyed by NAME, so regrouping Pid can never move a parameter into another domain. It replaces
// the positional kSnapDomain table; kSnapDomain remains as a table GENERATED from snapDomain() (snap() may index it).
enum class SnapDomain : uint8_t { linear, log, host };
constexpr SnapDomain snapDomain(Pid p) noexcept {
    // Not a switch: -Wswitch-enum (JUCE's warning list, R-B0 #6) rejects a default over 29 enumerators.
    if (p == Pid::atk || p == Pid::rel || p == Pid::s2atk || p == Pid::s2rel) return SnapDomain::log;
    if (p == Pid::schpf)                                                      return SnapDomain::host;
    return SnapDomain::linear;
}
inline constexpr std::array<SnapDomain, kNumModeParams> kSnapDomain = [] {   // kSnapDomain[idx(p)] == snapDomain(p)
    std::array<SnapDomain, kNumModeParams> t{};
    for (std::size_t i = 0; i < kNumModeParams; ++i)
        t[i] = snapDomain(static_cast<Pid>(i));
    return t;
}();

namespace detail {
// True when `order` names each of the first N Pids exactly once.
template <std::size_t N>
constexpr bool isPidPermutation(const std::array<Pid, N>& order) noexcept {
    std::array<bool, N> seen{};
    for (const Pid p : order) {
        const std::size_t i = idx(p);
        if (i >= N || seen[i])
            return false;
        seen[i] = true;
    }
    return true;
}
} // namespace detail

static_assert(detail::isPidPermutation(kApvtsOrder), "kApvtsOrder must list every Pid exactly once (K2 #9)");
static_assert(detail::isPidPermutation(kResolveOrder), "kResolveOrder must list every Mode-filtered Pid exactly once");

} // namespace fcdsp
