// Source/editor/SlotModel.cpp — see SlotModel.h. U1a builds the view straight from the resolved ParamView; U1s (S6)
// adds the live sub-readouts (DET, EFF, AUTO, DRY: 02 §6.4, quantised into key()), the CLAMPED FROM text and the slot
// probes.
#include "editor/SlotModel.h"

#include "editor/Layout.h"

#include "fcdsp/analysis/Analysis.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/Text.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace fcmp::ui
{
    namespace
    {
        constexpr uint64_t kFnvOffset = 1469598103934665603ull;
        constexpr uint64_t kFnvPrime  = 1099511628211ull;

        void mix(uint64_t& h, uint64_t v) noexcept
        {
            for (int i = 0; i < 8; ++i)
            {
                h ^= (v >> (8 * i)) & 0xFFu;
                h *= kFnvPrime;
            }
        }

        void mixFloat(uint64_t& h, float f) noexcept { mix(h, std::bit_cast<uint32_t>(f)); }

        void mixPtr(uint64_t& h, const void* p) noexcept { mix(h, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(p))); }

        template <std::size_t N>
        void copyText(char (&dst)[N], const char* a, const char* b = nullptr) noexcept
        {
            std::size_t n = 0;
            for (const char* s : { a, b })
                for (; s != nullptr && *s != '\0' && n + 1 < N; ++s)
                    dst[n++] = *s;
            dst[n] = '\0';
        }

        funkgui::ValueState stateOf(const fcdsp::ParamSpec& s, const fcdsp::ResolvedParam& r) noexcept
        {
            switch (r.state)
            {
                case fcdsp::SlotState::live:    return funkgui::ValueState::continuous;
                case fcdsp::SlotState::stepped: return s.kind == fcdsp::Kind::hybrid ? funkgui::ValueState::continuous
                                                                                     : funkgui::ValueState::stepped;
                case fcdsp::SlotState::locked:  return funkgui::ValueState::locked;
                case fcdsp::SlotState::derived: return funkgui::ValueState::derived;
                case fcdsp::SlotState::na:      return funkgui::ValueState::na;
            }
            return funkgui::ValueState::na;
        }

        const char* spokenOf(const fcdsp::Step& st) noexcept
        {
            return st.spoken != nullptr ? st.spoken : st.text != nullptr ? st.text : st.label;
        }
    }

    SlotModel::SlotModel(ProcessorFacade& facade, const FrameState& frame, fcdsp::Pid pid)
        : frame_(frame), pid_(pid), port_(facade.port(pid))
    {
    }

    const fcdsp::ParamSpec* SlotModel::spec() const noexcept
    {
        return frame_.entry != nullptr ? frame_.res.view.spec[fcdsp::idx(pid_)] : nullptr;
    }

    const fcdsp::ResolvedParam& SlotModel::resolved() const noexcept { return frame_.res.view.p[fcdsp::idx(pid_)]; }

    const char* SlotModel::universalLabel() const noexcept { return layout::universalLabel(pid_); }

    funkgui::ParamPort* SlotModel::port() { return &port_; }

    uint64_t SlotModel::key() const
    {
        uint64_t h = kFnvOffset;
        const fcdsp::ParamView& v = frame_.res.view;
        mix(h, v.slot);
        mixPtr(h, v.desc);
        mixPtr(h, spec());
        mixFloat(h, frame_.raw.v[fcdsp::idx(pid_)]);
        if (v.desc != nullptr)
            if (const fcdsp::Pid driver = v.desc->params[pid_].driver; driver != fcdsp::kNoPid)
                mix(h, static_cast<uint64_t>(static_cast<int64_t>(v[driver].step)));
        const fcdsp::ResolvedParam& r = resolved();
        mixFloat(h, r.plain);
        mixFloat(h, r.display);
        mix(h, static_cast<uint64_t>(static_cast<int64_t>(r.step)));
        mix(h, static_cast<uint64_t>(r.state));
        mix(h, r.flags);
        mix(h, r.tag);
        return h;
    }

    SlotModel::Range SlotModel::trackRange() const noexcept
    {
        const fcdsp::ParamSpec* s = spec();
        if (s != nullptr && s->lo < s->hi)
            return { fcdsp::toNorm(pid_, s->lo), fcdsp::toNorm(pid_, s->hi) };
        return { 0.0f, 1.0f };                                   // locked, derived, n/a: the host range
    }

    float SlotModel::trackOf(float plain) const noexcept
    {
        const Range r = trackRange();
        const float span = r.n1 - r.n0;
        const float u = span != 0.0f ? (fcdsp::toNorm(pid_, plain) - r.n0) / span : 0.0f;
        const fcdsp::ParamSpec* s = spec();
        return s != nullptr && s->display.invert ? 1.0f - u : u;
    }

    void SlotModel::view(funkgui::ValueView& v) const
    {
        v = funkgui::ValueView{};
        const char* universal = universalLabel();
        const fcdsp::ParamSpec* s = spec();
        if (s == nullptr)
        {
            v.state = funkgui::ValueState::na;
            v.label = universal;
            v.reason = "NO MODE IS REGISTERED";
            copyText(v.text.value, "\xE2\x80\x93");                   // U+2013, 01 §4.6
            copyText(v.text.spoken, "not applicable");
            return;
        }
        const fcdsp::ResolvedParam& r = resolved();
        const bool invert = s->display.invert;
        v.state  = stateOf(*s, r);
        v.label  = s->label != nullptr ? s->label : universal;
        v.aka    = s->label != nullptr ? universal : nullptr;
        v.reason = s->reason;
        if (r.state == fcdsp::SlotState::locked || r.state == fcdsp::SlotState::derived)
            v.tag = s->tag;
        else if (r.state != fcdsp::SlotState::na && (s->flags & fcdsp::kFlagExtension) != 0)
            v.tag = "+";
        if (const layout::SlotPlace* place = layout::slotOf(pid_))
            v.bipolar = place->bipolar;

        v.clamped   = (r.flags & fcdsp::kClamped) != 0;
        v.atDefault = r.plain == s->defaultPlain;
        if (r.state != fcdsp::SlotState::na)
        {
            v.track = trackOf(r.plain);
            v.trackDefault = trackOf(s->defaultPlain);
            if (r.state == fcdsp::SlotState::locked && (v.track < 0.0f || v.track > 1.0f))
                v.clamped = true;                                // the fixed value lies outside the track: no notch
            v.track = std::clamp(v.track, 0.0f, 1.0f);
            v.trackDefault = std::clamp(v.trackDefault, 0.0f, 1.0f);
        }

        const int nSteps = std::min(static_cast<int>(s->steps.size()), kMaxDetents);
        if (s->kind == fcdsp::Kind::stepped && r.state == fcdsp::SlotState::stepped)
        {
            // Index space, laid out so moving right raises the displayed quantity (02 §8.1: the track follows the
            // display; with an inverting DisplayMap the steps run right to left).
            for (int i = 0; i < nSteps; ++i)
            {
                const fcdsp::Step& st = s->steps[static_cast<std::size_t>(invert ? nSteps - 1 - i : i)];
                detents_[static_cast<std::size_t>(i)] = { fcdsp::toNorm(pid_, st.plain), st.label, spokenOf(st) };
            }
            v.nDetents = nSteps;
            v.detents = detents_.data();
            v.detent = r.step < 0 ? -1 : (invert ? nSteps - 1 - r.step : r.step);
        }
        else if (s->kind == fcdsp::Kind::continuous && r.state == fcdsp::SlotState::live)
        {
            for (int i = 0; i < nSteps; ++i)                     // soft notches: the value is never moved
                notches_[static_cast<std::size_t>(i)] = std::clamp(trackOf(s->steps[static_cast<std::size_t>(i)].plain),
                                                                   0.0f, 1.0f);
            v.nNotches = nSteps;
            v.notches = nSteps > 0 ? notches_.data() : nullptr;
        }
        else if (s->kind == fcdsp::Kind::hybrid)
        {
            // End cells (02 §8.1): the steps below lo on the low side, above hi on the high side, outermost first on
            // the low side and last on the high side; an inverting map swaps the sides.
            int nLo = 0, nHi = 0;
            for (int i = 0; i < nSteps; ++i)
                (s->steps[static_cast<std::size_t>(i)].plain < s->lo ? nLo : nHi) += 1;
            const int nLeft = invert ? nHi : nLo;
            int activeEnd = 0;
            for (int i = 0; i < nSteps; ++i)
            {
                const fcdsp::Step& st = s->steps[static_cast<std::size_t>(i)];
                const bool low = st.plain < s->lo;
                const bool left = low != invert;
                // plain order: low steps ascending (outermost first), high steps ascending (outermost last)
                const int ordinalInSide = low ? i : i - nLo;
                const int sideCount = low ? nLo : nHi;
                const int k = left != low ? sideCount - 1 - ordinalInSide : ordinalInSide;   // mirrored when swapped
                const int slotIndex = left ? k : nLeft + k;
                detents_[static_cast<std::size_t>(slotIndex)] = { fcdsp::toNorm(pid_, st.plain), st.label, spokenOf(st) };
                if (r.step == i)
                    activeEnd = left ? -(k + 1) : k + 1;
            }
            v.nEndLo = nLeft;
            v.nEndHi = nSteps - nLeft;
            v.endLo = nLeft > 0 ? detents_.data() : nullptr;
            v.endHi = nSteps - nLeft > 0 ? detents_.data() + nLeft : nullptr;
            v.activeEnd = r.state == fcdsp::SlotState::stepped ? activeEnd : 0;
        }

        fcdsp::FormattedValue f;
        fcdsp::formatParts(frame_.res.view, pid_, f);
        copyText(v.text.value, f.prefix == '~' ? "~" : nullptr, f.value);
        copyText(v.text.unit, f.unit);
        copyText(v.text.spoken, f.spoken);
        if (r.state == fcdsp::SlotState::locked)
            copyText(v.text.sub, s->brief != nullptr ? s->brief : s->tag);
        else if (r.state == fcdsp::SlotState::derived && s->derivedFrom != fcdsp::kNoPid
                 && fcdsp::idx(s->derivedFrom) < fcdsp::kNumModeParams)
            copyText(v.text.sub, "FOLLOWS ", layout::universalLabel(s->derivedFrom));
    }

    float SlotModel::host01FromTrack(float t) const
    {
        const Range r = trackRange();
        const fcdsp::ParamSpec* s = spec();
        float u = std::clamp(t, 0.0f, 1.0f);
        if (s != nullptr && s->display.invert)
            u = 1.0f - u;
        return r.n0 + u * (r.n1 - r.n0);
    }

    float SlotModel::defaultHost01() const
    {
        const fcdsp::ParamSpec* s = spec();
        return s != nullptr ? fcdsp::toNorm(pid_, s->defaultPlain) : port_.default01();
    }

    bool SlotModel::plotToHost01(float plotValue, float& host01) const
    {
        const fcdsp::ParamSpec* s = spec();
        const fcdsp::ResolvedParam& r = resolved();
        if (s == nullptr || (r.state != fcdsp::SlotState::live && r.state != fcdsp::SlotState::stepped))
            return false;
        const bool ranged = s->lo < s->hi;
        const auto inRange = [&](float plain) { return ranged ? std::clamp(plain, s->lo, s->hi) : plain; };
        switch (pid_)
        {
            case fcdsp::Pid::thr:
            {
                // T_in = thrDb − preGainDb is affine in thr with slope 1 (registry lint thr.slope), so the drag writes
                // thr_new = thr_cur + (T_target − T_cur): right for FET 76's offsets and inverted dials alike.
                const float tCur = fcdsp::analysis::inputThresholdDb(frame_.res.eng);
                host01 = fcdsp::toNorm(pid_, inRange(r.plain + (plotValue - tCur)));
                return true;
            }
            case fcdsp::Pid::knee:
            case fcdsp::Pid::range:
                if ((s->flags & fcdsp::kFlagPlotIsPlain) == 0 || s->display.toDisplay != nullptr)
                    return false;                                // relative drags (02 §6.5)
                host01 = fcdsp::toNorm(pid_, inRange(plotValue));
                return true;
            case fcdsp::Pid::atk:
            case fcdsp::Pid::rel:
                if (s->kind != fcdsp::Kind::continuous)
                    return false;                                // stepped markers snap to detents
                host01 = fcdsp::toNorm(pid_, inRange(plotValue * 1000.0f));   // the marker sits at seconds = plain/1000
                return true;
            case fcdsp::Pid::schpf:
                host01 = fcdsp::toNorm(pid_, plotValue < layout::chars::kScOffHz ? 0.0f : inRange(plotValue));
                return true;
            case fcdsp::Pid::ratio:  case fcdsp::Pid::tmode: case fcdsp::Pid::hold:  case fcdsp::Pid::look:
            case fcdsp::Pid::det:    case fcdsp::Pid::sce:   case fcdsp::Pid::link:  case fcdsp::Pid::stmode:
            case fcdsp::Pid::voice:  case fcdsp::Pid::drive: case fcdsp::Pid::makeup: case fcdsp::Pid::automu:
            case fcdsp::Pid::mix:    case fcdsp::Pid::s2thr: case fcdsp::Pid::s2atk: case fcdsp::Pid::s2rel:
            case fcdsp::Pid::mode:   case fcdsp::Pid::extkey: case fcdsp::Pid::listen: case fcdsp::Pid::delta:
            case fcdsp::Pid::bypass: case fcdsp::Pid::quality: case fcdsp::Pid::labudget: case fcdsp::Pid::kCount:
                return false;
        }
        return false;
    }
}
