#pragma once

// stage::ColourSelect<C0, C1, ...>: the colour stage picked by EngineParams::voice (01 §5.2 combinators
// "ColourSelect<...> (by voice)"; 01 §10.3 Clean: ColourSelect<ColourNone, TubeSym, DiodeAsym, Bright> for OFF / TUBE /
// DIODE / BRIGHT; FET 76: ColourSelect<FetColour>). voice is part of the kernel key (01 §5.5), so the host crossfades
// between two engines when it changes; inside one engine the selection is constant and process() dispatches once per
// call (per channel and chunk), never per sample.
//
//   design    every alternative's Coeffs, and the index clamp(voice, 0, N - 1)
//   process   the selected stage on one channel. Should the index change inside one engine anyway, the newly selected
//             stage's state is reset first (it has not run), so it starts clean instead of from stale history
//   transfer  the selected stage's static curve (the COLOUR view)
//   reset     every alternative
// With the index on ColourNone (Clean voice OFF) the wet signal is untouched, bit for bit (D8 (b), rigor clean).

#include "fcdsp/core/Rt.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/params/EngineParams.h"

namespace fcdsp::stage {

namespace detail {

template <class... Cs> struct ColourList;

template <class C>
struct ColourList<C> {
    struct Coeffs { typename C::Coeffs head{}; };
    struct State { typename C::State head{}; };

    static void design(Coeffs& c, const EngineParams& p, const StageCtx& x) noexcept FCDSP_NONBLOCKING
    {
        C::design(c.head, p, x);
    }
    static void process(int, const Coeffs& c, State& s, float* x, const float* gr, int n, int ch) noexcept
        FCDSP_NONBLOCKING
    {
        C::process(c.head, s.head, x, gr, n, ch);
    }
    static float transfer(int, const Coeffs& c, float x, float gr) noexcept FCDSP_NONBLOCKING
    {
        return C::transfer(c.head, x, gr);
    }
    static void resetOne(int, State& s) noexcept FCDSP_NONBLOCKING { C::reset(s.head); }
    static void resetAll(State& s) noexcept FCDSP_NONBLOCKING { C::reset(s.head); }
};

template <class C, class C1, class... Cs>
struct ColourList<C, C1, Cs...> {
    using Tail = ColourList<C1, Cs...>;
    struct Coeffs { typename C::Coeffs head{}; typename Tail::Coeffs tail{}; };
    struct State { typename C::State head{}; typename Tail::State tail{}; };

    static void design(Coeffs& c, const EngineParams& p, const StageCtx& x) noexcept FCDSP_NONBLOCKING
    {
        C::design(c.head, p, x);
        Tail::design(c.tail, p, x);
    }
    static void process(int i, const Coeffs& c, State& s, float* x, const float* gr, int n, int ch) noexcept
        FCDSP_NONBLOCKING
    {
        if (i == 0)
            C::process(c.head, s.head, x, gr, n, ch);
        else
            Tail::process(i - 1, c.tail, s.tail, x, gr, n, ch);
    }
    static float transfer(int i, const Coeffs& c, float x, float gr) noexcept FCDSP_NONBLOCKING
    {
        return i == 0 ? C::transfer(c.head, x, gr) : Tail::transfer(i - 1, c.tail, x, gr);
    }
    static void resetOne(int i, State& s) noexcept FCDSP_NONBLOCKING
    {
        if (i == 0)
            C::reset(s.head);
        else
            Tail::resetOne(i - 1, s.tail);
    }
    static void resetAll(State& s) noexcept FCDSP_NONBLOCKING
    {
        C::reset(s.head);
        Tail::resetAll(s.tail);
    }
};

} // namespace detail

template <class... Cs>
struct ColourSelect {
    static_assert(sizeof...(Cs) >= 1, "ColourSelect needs at least one colour stage");
    static_assert((ColourPolicy<Cs> && ...), "ColourSelect: every alternative must be a ColourPolicy");
    using List = detail::ColourList<Cs...>;
    static constexpr int kCount = static_cast<int>(sizeof...(Cs));

    struct Coeffs {
        typename List::Coeffs sub{};
        int which = 0;                          // clamp(voice, 0, kCount - 1)
    };
    struct State {
        typename List::State sub{};
        int which = -1;                         // the stage this channel's state last ran (-1: none since reset)
    };

    static void design(Coeffs& c, const EngineParams& p, const StageCtx& x) noexcept FCDSP_NONBLOCKING
    {
        List::design(c.sub, p, x);
        c.which = p.voice < kCount ? static_cast<int>(p.voice) : kCount - 1;
    }

    static void process(const Coeffs& c, State& s, float* x, const float* grDb, int n, int channel) noexcept
        FCDSP_NONBLOCKING
    {
        if (c.which != s.which)
        {
            List::resetOne(c.which, s.sub);
            s.which = c.which;
        }
        List::process(c.which, c.sub, s.sub, x, grDb, n, channel);
    }

    static float transfer(const Coeffs& c, float x, float grDb) noexcept FCDSP_NONBLOCKING
    {
        return List::transfer(c.which, c.sub, x, grDb);
    }

    static void reset(State& s) noexcept FCDSP_NONBLOCKING
    {
        List::resetAll(s.sub);
        s.which = -1;
    }
};

} // namespace fcdsp::stage
