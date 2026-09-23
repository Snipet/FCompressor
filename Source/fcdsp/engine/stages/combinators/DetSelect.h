#pragma once

// stage::DetSelect<D0, D1, ...>: the detector picked by EngineParams::det (01 §5.2 combinators "DetSelect<...> (by det;
// kernel key)"; 01 §10.3 Clean: DetSelect<PeakLog, RmsLog, DualDet> for PEAK / RMS / PK+RMS). det is part of the kernel
// key (01 §5.5), so the host crossfades between two engines when it changes; inside one engine it is constant, and
// DetSelect runs only the selected detector (a per-sample branch on a value that does not change).
//
//   design   every alternative's Coeffs (so a switch starts from designed coefficients) and the index,
//            clamp(det, 0, N - 1)
//   tick     the selected detector. Should the index change inside one engine anyway (a Rig or a host that skips the
//            crossfade), the newly selected detector is first seeded from the old one's level, so the level continues
//            instead of restarting from a stale state
//   seed     every alternative (Carry::detDb is the outgoing engine's detector level; whichever is selected continues)
//   levelDb  the selected detector's level (Carry::detDb)
//   sub<I>   the I-th alternative's state and which(), for a Mode's internals hook (Clean: PEAK DET, RMS DET)

#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/params/EngineParams.h"

namespace fcdsp::stage {

namespace detail {

template <class... Ds> struct DetList;

template <class D>
struct DetList<D> {
    struct Coeffs { typename D::Coeffs head{}; };
    struct State { typename D::State head{}; };

    static void design(Coeffs& c, const EngineParams& p, const StageCtx& x) noexcept FCDSP_NONBLOCKING
    {
        D::design(c.head, p, x);
    }
    static simd::f32x4 tick(int, const Coeffs& c, State& s, simd::f32x4 v) noexcept FCDSP_NONBLOCKING
    {
        return D::tick(c.head, s.head, v);
    }
    static void seedAll(State& s, simd::f32x4 v) noexcept FCDSP_NONBLOCKING { D::seed(s.head, v); }
    static void seedOne(int, State& s, simd::f32x4 v) noexcept FCDSP_NONBLOCKING { D::seed(s.head, v); }
    static simd::f32x4 levelDb(int, const State& s) noexcept FCDSP_NONBLOCKING { return D::levelDb(s.head); }

    template <int I>
    static const auto& get(const State& s) noexcept FCDSP_NONBLOCKING
    {
        static_assert(I == 0, "DetSelect::sub<I>: index out of range");
        return s.head;
    }
};

template <class D, class D1, class... Ds>
struct DetList<D, D1, Ds...> {
    using Tail = DetList<D1, Ds...>;
    struct Coeffs { typename D::Coeffs head{}; typename Tail::Coeffs tail{}; };
    struct State { typename D::State head{}; typename Tail::State tail{}; };

    static void design(Coeffs& c, const EngineParams& p, const StageCtx& x) noexcept FCDSP_NONBLOCKING
    {
        D::design(c.head, p, x);
        Tail::design(c.tail, p, x);
    }
    static simd::f32x4 tick(int i, const Coeffs& c, State& s, simd::f32x4 v) noexcept FCDSP_NONBLOCKING
    {
        return i == 0 ? D::tick(c.head, s.head, v) : Tail::tick(i - 1, c.tail, s.tail, v);
    }
    static void seedAll(State& s, simd::f32x4 v) noexcept FCDSP_NONBLOCKING
    {
        D::seed(s.head, v);
        Tail::seedAll(s.tail, v);
    }
    static void seedOne(int i, State& s, simd::f32x4 v) noexcept FCDSP_NONBLOCKING
    {
        if (i == 0)
            D::seed(s.head, v);
        else
            Tail::seedOne(i - 1, s.tail, v);
    }
    static simd::f32x4 levelDb(int i, const State& s) noexcept FCDSP_NONBLOCKING
    {
        return i == 0 ? D::levelDb(s.head) : Tail::levelDb(i - 1, s.tail);
    }

    template <int I>
    static const auto& get(const State& s) noexcept FCDSP_NONBLOCKING
    {
        if constexpr (I == 0)
            return s.head;
        else
            return Tail::template get<I - 1>(s.tail);
    }
};

} // namespace detail

template <class... Ds>
struct DetSelect {
    static_assert(sizeof...(Ds) >= 1, "DetSelect needs at least one detector");
    static_assert((DetectorPolicy<Ds> && ...), "DetSelect: every alternative must be a DetectorPolicy");
    using List = detail::DetList<Ds...>;
    static constexpr int kCount = static_cast<int>(sizeof...(Ds));

    struct Coeffs {
        typename List::Coeffs sub{};
        int which = 0;                          // clamp(det, 0, kCount - 1)
    };
    struct State {
        typename List::State sub{};
        int which = 0;                          // the detector the state last ran (levelDb, a switch's seed)
    };

    static void design(Coeffs& c, const EngineParams& p, const StageCtx& x) noexcept FCDSP_NONBLOCKING
    {
        List::design(c.sub, p, x);
        c.which = p.det < kCount ? static_cast<int>(p.det) : kCount - 1;
    }

    static simd::f32x4 tick(const Coeffs& c, State& s, simd::f32x4 v) noexcept FCDSP_NONBLOCKING
    {
        if (c.which != s.which)
        {
            List::seedOne(c.which, s.sub, List::levelDb(s.which, s.sub));
            s.which = c.which;
        }
        return List::tick(c.which, c.sub, s.sub, v);
    }

    static void seed(State& s, simd::f32x4 levelDb) noexcept FCDSP_NONBLOCKING { List::seedAll(s.sub, levelDb); }

    static simd::f32x4 levelDb(const State& s) noexcept FCDSP_NONBLOCKING { return List::levelDb(s.which, s.sub); }

    static int which(const State& s) noexcept FCDSP_NONBLOCKING { return s.which; }

    template <int I>
    static const auto& sub(const State& s) noexcept FCDSP_NONBLOCKING
    {
        return List::template get<I>(s.sub);
    }
};

} // namespace fcdsp::stage
