// Source/editor/SlotModel.cpp — see SlotModel.h. U1a built the view straight from the resolved ParamView; U1s (S6) adds
// the fit of every text to the slot (value abbreviation, the sub-line priority), the live sub-readouts of the primary
// row (DET, EFF, the knee span, AUTO, DRY: 02 §6.4, quantised into key() through the text they print), CLAMPED FROM,
// the remap sub-lines and the universal-unit helper the slot grid's spec lines use. UF1a (S11): the operating point the
// live readouts read (DET, EFF ratio, a derived program ratio) is the TRANSFER dot's, the 10 ms peak envelope of
// views/Telemetry.h over the ring's newest columns, so THRESHOLD's DET no longer jitters with the waveform's phase.
#include "editor/SlotModel.h"

#include "editor/Layout.h"
#include "editor/views/Telemetry.h"

#include "fcdsp/analysis/Analysis.h"
#include "fcdsp/core/Units.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/Text.h"
#include "fcdsp/telemetry/UiFrame.h"

#include <funkgui/core/TypeScale.h>
#include <funkgui/text/FontService.h>
#include <funkgui/text/TextFit.h>
#include <funkgui/widgets/RuleSlider.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace fcmp::ui
{
    namespace
    {
        constexpr uint64_t kFnvOffset = 1469598103934665603ull;
        constexpr uint64_t kFnvPrime  = 1099511628211ull;

        constexpr const char* kMinus    = "\xE2\x88\x92";        // U+2212: every negative number (02 §4.6)
        constexpr const char* kEllipsis = "\xE2\x80\xA6";        // U+2026: the knee span, fitEllipsis
        constexpr const char* kAlmost   = "\xE2\x89\x88 ";       // U+2248: "≈ −28.0 DB" (FunkGui's glyph for remaps)
        constexpr float kFloorDb = -199.0f;                      // UiFrame meters floor at −200 dBFS

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

        void mixText(uint64_t& h, const char* s) noexcept
        {
            for (; s != nullptr && *s != '\0'; ++s)
            {
                h ^= static_cast<unsigned char>(*s);
                h *= kFnvPrime;
            }
            h ^= 0xFFu;                                          // terminator: "A" + "B" differs from "AB" + ""
            h *= kFnvPrime;
        }

        // A NUL-terminated line in a fixed buffer. Whole codepoints only: the first one that does not fit ends the line.
        template <std::size_t N>
        struct Line
        {
            char        s[N] {};
            std::size_t n = 0;
            bool        full = false;

            Line& add(const char* t) noexcept
            {
                while (!full && t != nullptr && *t != '\0')
                {
                    const auto lead = static_cast<unsigned char>(*t);
                    std::size_t len = lead < 0x80u ? 1 : lead >= 0xF0u ? 4 : lead >= 0xE0u ? 3 : 2;
                    for (std::size_t k = 1; k < len; ++k)
                        if (t[k] == '\0')
                            len = k;                             // a truncated sequence: take what is there
                    if (n + len + 1 > N)
                    {
                        full = true;
                        break;
                    }
                    std::memcpy(s + n, t, len);
                    n += len;
                    t += len;
                }
                s[n] = '\0';
                return *this;
            }
            bool empty() const noexcept { return n == 0; }
        };

        template <std::size_t N>
        void copyText(char (&dst)[N], const char* a, const char* b = nullptr) noexcept
        {
            Line<N> l;
            l.add(a).add(b);
            std::memcpy(dst, l.s, N);
        }

        bool same(const char* a, const char* b) noexcept
        {
            return a != nullptr && b != nullptr && std::strcmp(a, b) == 0;
        }

        // "−14.2" / "+4.1" (plus = true) / "0.0": one decimal, U+2212, no "−0.0".
        template <std::size_t N>
        void addDb1(Line<N>& l, float db, bool plus) noexcept
        {
            const long long t = std::llround(static_cast<double>(db) * 10.0);
            const long long a = t < 0 ? -t : t;
            if (t < 0)
                l.add(kMinus);
            else if (plus && t > 0)
                l.add("+");
            char digits[24];
            std::snprintf(digits, sizeof digits, "%lld.%lld", a / 10, a % 10);
            l.add(digits);
        }

        // A whole number of dB ("−21"), for the knee span.
        template <std::size_t N>
        void addDb0(Line<N>& l, float db) noexcept
        {
            const long long t = std::llround(static_cast<double>(db));
            if (t < 0)
                l.add(kMinus);
            char digits[24];
            std::snprintf(digits, sizeof digits, "%lld", t < 0 ? -t : t);
            l.add(digits);
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

        // 01 §3.1's universal text of a plain value: formatParts over a continuous spec spanning the host range, with
        // no DisplayMap (so no Mode scale, no step text).
        void universalParts(fcdsp::Pid pid, float plain, fcdsp::FormattedValue& out) noexcept
        {
            const fcdsp::HostParam& h = fcdsp::kHostParams[fcdsp::idx(pid)];
            fcdsp::ParamSpec spec;
            spec.kind = fcdsp::Kind::continuous;
            spec.lo = h.lo;
            spec.hi = h.hi;
            fcdsp::ParamView view;
            view.spec[fcdsp::idx(pid)] = &spec;
            fcdsp::ResolvedParam& r = view.p[fcdsp::idx(pid)];
            r.plain = plain;
            r.display = plain;
            r.state = fcdsp::SlotState::live;
            fcdsp::formatParts(view, pid, out);
        }

        template <std::size_t N>
        void addUniversal(Line<N>& l, fcdsp::Pid pid, float plain) noexcept
        {
            fcdsp::FormattedValue f;
            universalParts(pid, plain, f);
            l.add(f.value);
            if (f.unit[0] != '\0')
                l.add(" ").add(f.unit);
        }

        const funkgui::FontAtlasSdf& atlas() { return funkgui::FontService::get().atlas(); }

        float widthOf(const char* s, const funkgui::TextStyle& st) { return funkgui::text::width(atlas(), s, st); }

        // The Mode's name of a parameter (its label, else the universal one): "FOLLOWS DC THRESH".
        const char* modeName(const fcdsp::ParamView& v, fcdsp::Pid p) noexcept
        {
            if (fcdsp::idx(p) >= fcdsp::kNumModeParams)
                return "";
            const fcdsp::ParamSpec* s = v.spec[fcdsp::idx(p)];
            return s != nullptr && s->label != nullptr ? s->label : layout::universalLabel(p);
        }

        bool programTime(const fcdsp::ParamSpec& s, const fcdsp::ResolvedParam& r) noexcept
        {
            return (s.flags & fcdsp::kFlagProgram) != 0 || (r.tag & fcdsp::kTagProgram) != 0;
        }

        // The EFF value of a program-dependent time (attackNowMs / releaseNowMs are taus): published through the
        // spec's law, so it reads in the same units as the slot's value (Opto 2A's release is a 50 % time).
        float effTimeMs(const fcdsp::ParamSpec& s, float tauMs) noexcept { return tauMs * fcdsp::lawFactor(s.law); }

        // The live EFF ratio (analysis::localRatio at the operating point), as the host plain S of the ratio map.
        float effRatioS(const FrameState& f, float cx) noexcept
        {
            const float r = fcdsp::analysis::localRatio(*f.entry, f.eng, cx);
            if (!std::isfinite(r) || std::fabs(r) >= 1.0e4f)
                return 1.0f;                                     // ∞:1
            return r != 0.0f ? 1.0f - 1.0f / r : 0.0f;
        }
    }

    // ---- the live readouts (02 §6.4), shared by key() and view() so a slider re-reads when a printed value changes ----

    namespace
    {
        struct LiveOut
        {
            Line<40> sub;                                        // the primary sub-line readout ("DET −14.2")
            bool     derivedValue = false;                       // a derived kFlagProgram slot's live EFF value
            fcdsp::FormattedValue value{};
        };

        // Fills `out` for Pid `pid` of `frame` when the telemetry is live and a readout applies (else leaves it empty).
        // The operating point's x is the dot's (the 10 ms peak envelope over `ring`'s newest columns, Telemetry.h).
        void liveReadout(const FrameState& frame, const fcdsp::HistoryRing& ring, fcdsp::Pid pid, bool primary,
                         LiveOut& out) noexcept
        {
            if (!frame.live || frame.entry == nullptr || fcdsp::idx(pid) >= fcdsp::kNumModeParams)
                return;
            const fcdsp::ParamSpec* s = frame.res.view.spec[fcdsp::idx(pid)];
            if (s == nullptr)
                return;
            const fcdsp::ResolvedParam& r = frame.res.view.p[fcdsp::idx(pid)];
            const fcdsp::UiFrame& ui = frame.ui;
            const int lane = telemetry::grLane(ui);
            const bool usesX = pid == fcdsp::Pid::thr || pid == fcdsp::Pid::ratio;   // DET, EFF, a derived ratio
            const float cx = usesX ? telemetry::operatingX(ui, ring) : ui.curveXDb[lane];
            const bool writable = r.state == fcdsp::SlotState::live || r.state == fcdsp::SlotState::stepped;

            // A derived program value reads live (02 §8.1 derived value: "the live EFF value when kFlagProgram").
            if (r.state == fcdsp::SlotState::derived && (s->flags & fcdsp::kFlagProgram) != 0)
            {
                bool has = true;
                if (pid == fcdsp::Pid::ratio && cx > kFloorDb)
                    universalParts(pid, effRatioS(frame, cx), out.value);
                else if (pid == fcdsp::Pid::atk)
                    universalParts(pid, effTimeMs(*s, ui.attackNowMs[lane]), out.value);
                else if (pid == fcdsp::Pid::rel)
                    universalParts(pid, effTimeMs(*s, ui.releaseNowMs[lane]), out.value);
                else
                    has = false;
                if (has)
                {
                    out.derivedValue = true;
                    out.value.prefix = 0;                        // a measured value, not the nominal "~" one
                }
            }

            if (!primary || r.state == fcdsp::SlotState::na)
                return;
            switch (pid)
            {
                case fcdsp::Pid::thr:
                    if (writable && cx > kFloorDb)
                    {
                        out.sub.add("DET ");
                        addDb1(out.sub, cx, false);
                    }
                    break;
                case fcdsp::Pid::ratio:
                    if (writable && cx > kFloorDb)
                    {
                        out.sub.add("EFF ");
                        addUniversal(out.sub, pid, effRatioS(frame, cx));
                    }
                    break;
                case fcdsp::Pid::knee:
                    if (writable)
                    {
                        const float tIn = fcdsp::analysis::inputThresholdDb(frame.eng);
                        const float half = 0.5f * std::max(0.0f, frame.eng.kneeDb);
                        const long long a = std::llround(static_cast<double>(tIn - half));
                        const long long b = std::llround(static_cast<double>(tIn + half));
                        if (a == b)
                            out.sub.add("AT ");
                        addDb0(out.sub, tIn - half);
                        if (a != b)
                        {
                            out.sub.add(kEllipsis);
                            addDb0(out.sub, tIn + half);
                        }
                        out.sub.add(" DB");
                    }
                    break;
                case fcdsp::Pid::atk:
                case fcdsp::Pid::rel:
                    if (programTime(*s, r) && r.state != fcdsp::SlotState::derived)
                    {
                        out.sub.add("EFF ");
                        const float tau = pid == fcdsp::Pid::atk ? ui.attackNowMs[lane] : ui.releaseNowMs[lane];
                        addUniversal(out.sub, pid, effTimeMs(*s, tau));
                    }
                    break;
                case fcdsp::Pid::makeup:
                    if (writable && (frame.res.eng.flags & fcdsp::kEngAutoMakeup) != 0)
                    {
                        out.sub.add("AUTO ");
                        addDb1(out.sub, ui.makeupEffDb - r.plain, true);
                    }
                    break;
                case fcdsp::Pid::mix:
                    if (writable)
                    {
                        const float dry = 1.0f - std::clamp(frame.eng.mix, 0.0f, 1.0f);
                        out.sub.add("DRY ");
                        if (dry <= 1.0e-5f)
                            out.sub.add(kMinus).add("INF");
                        else
                            addDb1(out.sub, 20.0f * std::log10(dry), false);
                        out.sub.add(" DB");
                    }
                    break;
                case fcdsp::Pid::range:  case fcdsp::Pid::tmode:  case fcdsp::Pid::hold:   case fcdsp::Pid::look:
                case fcdsp::Pid::det:    case fcdsp::Pid::schpf:  case fcdsp::Pid::sce:    case fcdsp::Pid::link:
                case fcdsp::Pid::stmode: case fcdsp::Pid::voice:  case fcdsp::Pid::drive:  case fcdsp::Pid::automu:
                case fcdsp::Pid::s2thr:  case fcdsp::Pid::s2atk:  case fcdsp::Pid::s2rel:  case fcdsp::Pid::mode:
                case fcdsp::Pid::extkey: case fcdsp::Pid::listen: case fcdsp::Pid::delta:  case fcdsp::Pid::bypass:
                case fcdsp::Pid::quality: case fcdsp::Pid::labudget: case fcdsp::Pid::kCount:
                    break;
            }
        }

        bool isPrimary(fcdsp::Pid pid) noexcept
        {
            const layout::SlotPlace* place = layout::slotOf(pid);
            return place != nullptr && place->row == layout::SlotRow::p;
        }

        // Whether the slot's attached word is visible (AUTO is hidden while automu is n/a; EXT and LISTEN never are).
        bool wordVisible(const FrameState& frame, fcdsp::Pid pid) noexcept
        {
            const layout::SlotPlace* place = layout::slotOf(pid);
            if (place == nullptr || place->word == fcdsp::kNoPid)
                return false;
            if (fcdsp::idx(place->word) >= fcdsp::kNumModeParams)
                return true;
            return frame.entry != nullptr && frame.res.view.p[fcdsp::idx(place->word)].state != fcdsp::SlotState::na;
        }
    }

    // ---- SlotModel ------------------------------------------------------------------------------------------------------

    SlotModel::SlotModel(ProcessorFacade& facade, const FrameState& frame, fcdsp::Pid pid)
        : frame_(frame), pid_(pid), port_(facade.port(pid)), ring_(facade.history())
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
        mix(h, wordVisible(frame_, pid_) ? 1u : 0u);             // a tag moves to the sub line beside a visible word
        LiveOut live;
        liveReadout(frame_, ring_, pid_, isPrimary(pid_), live);  // the quantised live value is the text it prints
        mixText(h, live.sub.s);
        if (live.derivedValue)
        {
            mixText(h, live.value.value);
            mixText(h, live.value.unit);
        }
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

    float SlotModel::trackPosition(float plain) const noexcept { return trackOf(plain); }

    int SlotModel::universalText(fcdsp::Pid pid, float plain, char* out, int cap) noexcept
    {
        if (out == nullptr || cap <= 0)
            return 0;
        if (fcdsp::idx(pid) >= fcdsp::kNumModeParams)
        {
            out[0] = '\0';
            return 0;
        }
        Line<64> l;
        addUniversal(l, pid, plain);
        std::size_t n = std::min(l.n, static_cast<std::size_t>(cap - 1));
        while (n > 0 && n < l.n && (static_cast<unsigned char>(l.s[n]) & 0xC0u) == 0x80u)
            --n;
        std::memcpy(out, l.s, n);
        out[n] = '\0';
        return static_cast<int>(n);
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
        const bool locked = r.state == fcdsp::SlotState::locked;
        const bool derived = r.state == fcdsp::SlotState::derived;
        const bool na = r.state == fcdsp::SlotState::na;
        v.state  = stateOf(*s, r);
        v.label  = s->label != nullptr ? s->label : universal;
        v.aka    = s->label != nullptr ? universal : nullptr;
        v.reason = s->reason;
        if (locked || derived)
            v.tag = s->tag;
        else if (!na && (s->flags & fcdsp::kFlagExtension) != 0)
            v.tag = "+";
        if (const layout::SlotPlace* place = layout::slotOf(pid_))
            v.bipolar = place->bipolar;

        v.clamped   = (r.flags & fcdsp::kClamped) != 0;
        v.atDefault = r.plain == s->defaultPlain;
        if (!na)
        {
            v.track = trackOf(r.plain);
            v.trackDefault = trackOf(s->defaultPlain);
            if (locked && (v.track < 0.0f || v.track > 1.0f))
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

        // ---- text ------------------------------------------------------------------------------------------------------
        const bool primary = isPrimary(pid_) || pid_ == fcdsp::Pid::automu;
        const funkgui::TextStyle& valueStyle = primary ? funkgui::type::kValueP : funkgui::type::kValueS;
        const funkgui::TextStyle& unitStyle = primary ? funkgui::type::kLabel : funkgui::type::kMicro;
        const float w = layout::kSlotW;

        LiveOut live;
        liveReadout(frame_, ring_, pid_, isPrimary(pid_), live);
        fcdsp::FormattedValue f;
        fcdsp::formatParts(frame_.res.view, pid_, f);
        if (live.derivedValue)
        {
            f = live.value;
            v.live = true;
        }
        copyText(v.text.value, f.prefix == '~' ? "~" : nullptr, f.value);
        copyText(v.text.unit, f.unit);
        copyText(v.text.spoken, f.spoken);

        // The value fit (02 §8.1, S5 carry-forward): value + 6 + unit within the slot at the row's value size; else the
        // resolved step's own label; only a text without one is cut.
        const auto valueWidth = [&]() {
            const float wu = v.text.unit[0] != '\0' ? kUnitGap + widthOf(v.text.unit, unitStyle) : 0.0f;
            return widthOf(v.text.value, valueStyle) + wu;
        };
        bool abbreviated = false;
        if (!na && valueWidth() > w)
        {
            const fcdsp::Step* st = r.step >= 0 && static_cast<std::size_t>(r.step) < s->steps.size()
                                  ? &s->steps[static_cast<std::size_t>(r.step)] : nullptr;
            if (st != nullptr && st->label != nullptr && st->label[0] != '\0' && !live.derivedValue)
            {
                copyText(v.text.value, f.prefix == '~' ? "~" : nullptr, st->label);
                v.text.unit[0] = '\0';
                abbreviated = true;
            }
            if (valueWidth() > w)
            {
                const float room = w - (v.text.unit[0] != '\0' ? kUnitGap + widthOf(v.text.unit, unitStyle) : 0.0f);
                char cut[sizeof v.text.value];
                funkgui::text::fitEllipsis(atlas(), v.text.value, valueStyle, room, cut, sizeof cut);
                copyText(v.text.value, cut);
            }
        }
        // A locked list's tag repeats its step label ("PEAK" over PEAK): it says nothing the value does not.
        if (locked && v.tag != nullptr && (same(v.tag, v.text.value) || same(v.tag, f.value)))
            v.tag = nullptr;
        // An extension "+" that a visible word pushes to the head of the detent line must not cost the detent labels
        // their place (RuleSlider draws labels only with room for the moved tag): the labels win, the footer still
        // says EXTENSION (SlotGrid's spec line). Mu 67's SC HPF OFF·50·100·200·350 keeps its labels (02 §8.3).
        if (v.state == funkgui::ValueState::stepped && same(v.tag, "+") && wordVisible(frame_, pid_) && nSteps > 0
            && funkgui::RuleSlider::detentLabelsFit(atlas(), v.detents, v.nDetents, w))
        {
            const float cell = w / static_cast<float>(v.nDetents);
            const float firstLeft = 0.5f * cell - 0.5f * widthOf(v.detents[0].label, funkgui::type::kMicro);
            if (firstLeft < widthOf(v.tag, funkgui::type::kMicro) + 4.0f)
                v.tag = nullptr;
        }

        // ---- the sub / detent line -------------------------------------------------------------------------------------
        const bool tagOnSub = v.tag != nullptr && v.tag[0] != '\0' && !na && wordVisible(frame_, pid_);
        const float room = w - (tagOnSub ? widthOf(v.tag, funkgui::type::kMicro) + kTagGap : 0.0f);
        const auto fits = [&](const char* t) { return widthOf(t, funkgui::type::kMicro) <= room; };
        Line<96> sub;
        Line<96> shorter;                                        // a fallback when `sub` does not fit
        if (na)
        {
            // nothing: n/a draws only its dash
        }
        else if (v.clamped && (r.flags & fcdsp::kClamped) != 0)
        {
            Line<48> rawText;
            addUniversal(rawText, pid_, frame_.raw.v[fcdsp::idx(pid_)]);
            sub.add("CLAMPED FROM ").add(rawText.s);
            shorter.add("CLAMPED ").add(rawText.s);
            if (!fits(sub.s) && !fits(shorter.s))
            {
                sub = Line<96>{};
                sub.add("CLAMPED");
            }
        }
        else if (!live.sub.empty())
        {
            sub.add(live.sub.s);
            v.live = true;
        }
        else if (locked)
        {
            const fcdsp::Step* st = r.step >= 0 && static_cast<std::size_t>(r.step) < s->steps.size()
                                  ? &s->steps[static_cast<std::size_t>(r.step)] : nullptr;
            // brief, else the full text the value abbreviates; never the tag again (it already shows in the label
            // row, or at the head of this line beside a word).
            if (s->brief != nullptr)
                sub.add(s->brief);
            else if (abbreviated && st != nullptr && st->text != nullptr && fits(st->text))
                sub.add(st->text);
        }
        else if (derived)
        {
            if (s->derivedFrom != fcdsp::kNoPid && fcdsp::idx(s->derivedFrom) < fcdsp::kNumModeParams)
                sub.add("FOLLOWS ").add(modeName(frame_.res.view, s->derivedFrom));
        }
        else if (s->display.toDisplay != nullptr)
        {
            // A Mode scale: the universal equivalent, named when the Mode renames the slot ("THRESHOLD −28.0 DB"
            // under INPUT), bare under the universal name itself (Bus G's dial: "−18.0 DB" under THRESHOLD).
            Line<48> u;
            addUniversal(u, pid_, r.plain);
            if (v.aka != nullptr)
                sub.add(v.aka).add(" ");
            sub.add(u.s);
            shorter.add(kAlmost).add(u.s);
        }
        else if (v.aka != nullptr)
        {
            sub.add(v.aka);
        }
        if (!sub.empty() && !fits(sub.s) && !shorter.empty() && fits(shorter.s))
            sub = shorter;
        if (!sub.empty() && !fits(sub.s))
        {
            char cut[96];
            funkgui::text::fitEllipsis(atlas(), sub.s, funkgui::type::kMicro, room, cut, sizeof cut);
            copyText(v.text.sub, cut);
        }
        else
        {
            copyText(v.text.sub, sub.s);
        }
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
                if (s->kind != fcdsp::Kind::continuous && s->kind != fcdsp::Kind::hybrid)
                    return false;                                // stepped markers snap to detents
                // the marker sits at seconds = plain/1000; a hybrid's drag stays in its range (its steps are the
                // slot's end cells: Diode 54's 5 MS attack, v1.2)
                host01 = fcdsp::toNorm(pid_, inRange(plotValue * 1000.0f));
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
