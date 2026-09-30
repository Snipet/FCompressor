// Source/editor/views/SlotGrid.cpp — the 21 slots and their words (see SlotGrid.h; 02 §6.4, §8.1–§8.4, §8.7, §8.9).
#include "editor/views/SlotGrid.h"

#include "editor/Layout.h"
#include "editor/SlotModel.h"
#include "editor/Tags.h"
#include "editor/views/Telemetry.h"

#include "fcdsp/analysis/Analysis.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/params/Text.h"
#include "fcdsp/telemetry/UiFrame.h"

#include <funkgui/canvas/Canvas.h>
#include <funkgui/core/Ease.h>
#include <funkgui/core/Theme.h>
#include <funkgui/core/TypeScale.h>
#include <funkgui/panel/HostServices.h>
#include <funkgui/params/GestureController.h>
#include <funkgui/params/ParamPort.h>
#include <funkgui/widgets/ValueModel.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>

namespace fcmp::ui
{
    namespace
    {
        constexpr const char* kNoBus = "NO SIDECHAIN BUS CONNECTED";   // 02 §6.4
        constexpr float kFloorDb = -199.0f;                              // UiFrame levels floor at −200 dBFS
        constexpr float kRangeOffDb = 60.0f;                             // fcdsp::kRangeOff: the end of RANGE's track

        bool sameText(const char* a, const char* b) noexcept
        {
            if (a == nullptr || b == nullptr)
                return a == b;
            return std::strcmp(a, b) == 0;
        }

        // A line in a caller's buffer; whole codepoints only (the first that does not fit ends it).
        struct Out
        {
            char*       s;
            std::size_t cap;
            std::size_t n = 0;
            bool        full = false;

            Out(char* buf, std::size_t c, bool keep = false) noexcept : s(buf), cap(c)
            {
                if (s == nullptr || cap == 0)
                    return;
                if (keep)                                          // continue the NUL-terminated text already there
                    while (n + 1 < cap && s[n] != '\0')
                        ++n;
                s[n] = '\0';
            }

            Out& add(const char* t) noexcept
            {
                if (s == nullptr || cap == 0)
                    return *this;
                while (!full && t != nullptr && *t != '\0')
                {
                    const auto lead = static_cast<unsigned char>(*t);
                    std::size_t len = lead < 0x80u ? 1 : lead >= 0xF0u ? 4 : lead >= 0xE0u ? 3 : 2;
                    for (std::size_t k = 1; k < len; ++k)
                        if (t[k] == '\0')
                            len = k;
                    if (n + len + 1 > cap)
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

            Out& upper(const char* t) noexcept                     // ASCII upper case ("Auto Makeup" -> "AUTO MAKEUP")
            {
                for (; t != nullptr && *t != '\0' && !full; ++t)
                {
                    const char c[2] = { *t >= 'a' && *t <= 'z' ? static_cast<char>(*t - 'a' + 'A') : *t, '\0' };
                    add(c);
                }
                return *this;
            }
        };

        const fcdsp::HostParam& hostParam(fcdsp::Pid p) noexcept { return fcdsp::kHostParams[fcdsp::idx(p)]; }

        // A list or switch parameter: its raw value is an index that means nothing outside its Mode.
        bool indexParam(fcdsp::Pid p) noexcept
        {
            const fcdsp::Map m = hostParam(p).map;
            return m == fcdsp::Map::index || m == fcdsp::Map::boolean;
        }

        // The a11y display scale of a plain value (02 §8.9: "Mode track in display units"): the Mode's DisplayMap, else
        // the universal one (percent × 100, the ratio map's S as R = 1/(1 − S), capped at 1000 for ∞; the rest as is).
        constexpr double kRatioInf = 1000.0;

        double toDisplayUnits(fcdsp::Pid pid, const fcdsp::ParamSpec& s, float plain) noexcept
        {
            if (s.display.toDisplay != nullptr)
                return static_cast<double>(s.display.toDisplay(plain));
            const fcdsp::HostParam& h = hostParam(pid);
            if (h.map == fcdsp::Map::ratio3)
                return plain >= 0.999f ? kRatioInf : 1.0 / (1.0 - static_cast<double>(plain));
            if (std::strcmp(h.unit, "%") == 0)
                return static_cast<double>(plain) * 100.0;
            return static_cast<double>(plain);
        }

        float fromDisplayUnits(fcdsp::Pid pid, const fcdsp::ParamSpec& s, double d) noexcept
        {
            if (s.display.toPlain != nullptr)
                return s.display.toPlain(static_cast<float>(d));
            const fcdsp::HostParam& h = hostParam(pid);
            if (h.map == fcdsp::Map::ratio3)
                return d >= kRatioInf ? 1.0f : (d != 0.0 ? static_cast<float>(1.0 - 1.0 / d) : 0.0f);
            if (std::strcmp(h.unit, "%") == 0)
                return static_cast<float>(d / 100.0);
            return static_cast<float>(d);
        }

        struct PlainRange { float a, b; };

        // The plain span the slot's track draws (02 §9.4): the spec's [lo, hi], else the host range.
        PlainRange trackSpan(fcdsp::Pid pid, const fcdsp::ParamSpec& s) noexcept
        {
            if (s.lo < s.hi)
                return { s.lo, s.hi };
            return { hostParam(pid).lo, hostParam(pid).hi };
        }
    }

    // ---- the words (02 §6.4, K1 #23) ------------------------------------------------------------------------------------

    class SlotGrid::Word final : public funkgui::WordModel
    {
    public:
        Word(PanelContext& ctx, fcdsp::Pid pid) : ctx_(ctx), pid_(pid), port_(ctx.facade.port(pid)) {}

        Word(const Word&) = delete;
        Word& operator=(const Word&) = delete;

        fcdsp::Pid pid() const noexcept { return pid_; }
        bool modeFiltered() const noexcept { return fcdsp::idx(pid_) < fcdsp::kNumModeParams; }

        // The pid a hand offer names: PanelContext::slot takes Mode-filtered Pids only.
        fcdsp::Pid handPid() const noexcept { return modeFiltered() ? pid_ : fcdsp::kNoPid; }

        // EXT: fresh telemetry says the key is on and no key bus is active (kUiExtKeyActive = extKey AND a bus).
        bool noBus() const noexcept
        {
            return pid_ == fcdsp::Pid::extkey && ctx_.frame.fresh && port_.value01() >= 0.5f
                && (ctx_.frame.ui.flags & fcdsp::kUiExtKeyActive) == 0;
        }

        // While drawing, EXT without a bus reads disabled (02 §6.4 "drawn disabled"); every write path stays open.
        void present(bool on) const noexcept { presenting_ = on; }

        bool on() const override
        {
            if (modeFiltered())
            {
                const fcdsp::ResolvedParam& r = ctx_.slot(pid_).resolved();
                return r.state != fcdsp::SlotState::na && r.plain >= 0.5f;
            }
            return port_.value01() >= 0.5f;
        }

        bool visible() const override
        {
            return !modeFiltered() || ctx_.slot(pid_).resolved().state != fcdsp::SlotState::na;
        }

        bool enabled() const override
        {
            if (modeFiltered())
            {
                const fcdsp::SlotState s = ctx_.slot(pid_).resolved().state;
                return s == fcdsp::SlotState::live || s == fcdsp::SlotState::stepped;
            }
            return !(presenting_ && noBus());
        }

        const char* reason() const override
        {
            if (modeFiltered())
            {
                const fcdsp::ParamSpec* s = ctx_.slot(pid_).spec();
                return s != nullptr ? s->reason : nullptr;
            }
            return noBus() ? kNoBus : nullptr;
        }

        void set(bool v, funkgui::GestureController& g) override
        {
            g.tap(port_, fcdsp::toNorm(pid_, v ? 1.0f : 0.0f));  // the canonical detent of the switch
            if (modeFiltered())
                ctx_.touch(pid_);
        }

        funkgui::ParamPort* port() override { return &port_; }

    private:
        PanelContext&       ctx_;
        fcdsp::Pid          pid_;
        funkgui::ParamPort& port_;
        mutable bool        presenting_ = false;
    };

    // ---- construction ---------------------------------------------------------------------------------------------------

    SlotGrid::SlotGrid(PanelContext& ctx) : ctx_(ctx)
    {
        std::size_t w = 0;
        for (std::size_t i = 0; i < layout::kSlots.size(); ++i)
        {
            const layout::SlotPlace& place = layout::kSlots[i];
            const funkgui::SlotGeom g = layout::slotGeom(place);
            sliders_[i].emplace(ctx_.slot(place.pid), g, a11yId(ViewIndex::slotGrid, static_cast<uint32_t>(1 + i)));
            if (place.word != fcdsp::kNoPid && w < words_.size())
            {
                wordModels_[w] = std::make_unique<Word>(ctx_, place.word);
                words_[w].emplace(static_cast<funkgui::WordModel&>(*wordModels_[w]), g, place.wordLabel,
                                  a11yId(ViewIndex::slotGrid, kWordIdBase + static_cast<uint32_t>(i)));
                wordSlot_[w] = static_cast<int>(i);
                sliders_[i]->setWord(&*words_[w]);
                wordShown_[w] = words_[w]->visible();
                ++w;
            }
        }
        modeSerial_ = ctx_.frame.modeSerial;
    }

    SlotGrid::~SlotGrid() = default;

    // ---- lookups --------------------------------------------------------------------------------------------------------

    int SlotGrid::sliderAt(funkgui::Point p) const noexcept
    {
        for (std::size_t i = 0; i < sliders_.size(); ++i)
            if (sliders_[i]->contains(p))
                return static_cast<int>(i);
        return -1;
    }

    int SlotGrid::wordAt(funkgui::Point p) const noexcept
    {
        for (std::size_t k = 0; k < words_.size(); ++k)
            if (words_[k] && words_[k]->visible() && words_[k]->contains(p))
                return static_cast<int>(k);
        return -1;
    }

    int SlotGrid::sliderOf(uint32_t id) const noexcept
    {
        for (std::size_t i = 0; i < sliders_.size(); ++i)
            if (sliders_[i]->a11yId() == id)
                return static_cast<int>(i);
        return -1;
    }

    int SlotGrid::wordOf(uint32_t id) const noexcept
    {
        for (std::size_t k = 0; k < words_.size(); ++k)
            if (words_[k] && words_[k]->a11yId() == id)
                return static_cast<int>(k);
        return -1;
    }

    bool SlotGrid::focused(uint32_t id) const noexcept { return ctx_.focusVisible && ctx_.focus == id; }

    bool SlotGrid::writable(int i) const noexcept
    {
        const funkgui::ValueState s = sliders_[static_cast<std::size_t>(i)]->view().state;
        return s == funkgui::ValueState::continuous || s == funkgui::ValueState::stepped;
    }

    // ---- per frame ------------------------------------------------------------------------------------------------------

    void SlotGrid::land()
    {
        // 02 §8.7: every caret eases to the new Mode's position (τ 90 ms); a slot whose state, label or tag changed
        // flashes its label. Nothing is written (K2 #4): the views are re-read, not the ports.
        for (std::size_t i = 0; i < sliders_.size(); ++i)
        {
            const funkgui::ValueView before = sliders_[i]->view();
            funkgui::ValueView after;
            ctx_.slot(layout::kSlots[i].pid).view(after);
            const bool changed = before.state != after.state || !sameText(before.label, after.label)
                              || !sameText(before.tag, after.tag);
            sliders_[i]->flashLabel(changed ? layout::landing::kFlashS * funkgui::ease::timeScale() : 0.0f);   // ADR-90
        }
        modeSerial_ = ctx_.frame.modeSerial;
    }

    void SlotGrid::tick(float dt)
    {
        if (ctx_.frame.modeSerial != modeSerial_)
        {
            closeEntry();                                        // ADR-89: the Mode changed under an open field
            land();
        }
        entry_.tick(dt);
        for (std::size_t k = 0; k < words_.size(); ++k)
        {
            if (!words_[k])
                continue;
            const bool shown = words_[k]->visible();
            if (shown != wordShown_[k])
            {
                wordShown_[k] = shown;
                ++revision_;                                     // an a11y item appeared or disappeared
                if (!shown && hoveredWord_ == static_cast<int>(k))
                    hoveredWord_ = -1;
                if (!shown && capturedWord_ == static_cast<int>(k))
                    capturedWord_ = -1;
            }
        }
        for (std::size_t i = 0; i < sliders_.size(); ++i)
            sliders_[i]->tick(dt, ctx_.pointerIn && hovered_ == static_cast<int>(i), focused(sliders_[i]->a11yId()),
                              ctx_.alwaysChrome);
        for (std::size_t k = 0; k < words_.size(); ++k)
            if (words_[k])
                words_[k]->tick(dt, ctx_.pointerIn && hoveredWord_ == static_cast<int>(k));
        offerHands();
    }

    void SlotGrid::offerHands()
    {
        char line[sizeof ctx_.handNext.spec];
        const auto slotPid = [](int i) { return layout::kSlots[static_cast<std::size_t>(i)].pid; };
        if (entry_.open() && entrySlider_ >= 0)                  // ADR-89: the field's own line, over any other
        {
            const funkgui::RuleSlider& s = *sliders_[static_cast<std::size_t>(entrySlider_)];
            std::snprintf(line, sizeof line, "%s   TYPE A VALUE   RETURN SETS IT   ESC CANCELS",
                          s.view().label != nullptr ? s.view().label : "");
            ctx_.offerHand(slotPid(entrySlider_), HandKind::drag, s.a11yId(), line);
            return;
        }
        if (captured_ >= 0 && ctx_.pointerPressed)
        {
            specLine(captured_, line, sizeof line);
            ctx_.offerHand(slotPid(captured_), HandKind::drag, sliders_[static_cast<std::size_t>(captured_)]->a11yId(),
                           line);
        }
        if (capturedWord_ >= 0 && ctx_.pointerPressed)
        {
            const auto k = static_cast<std::size_t>(capturedWord_);
            wordSpecLine(capturedWord_, line, sizeof line);
            ctx_.offerHand(wordModels_[k]->handPid(), HandKind::drag, words_[k]->a11yId(), line);
        }
        if (ctx_.pointerIn && hovered_ >= 0)
        {
            specLine(hovered_, line, sizeof line);
            ctx_.offerHand(slotPid(hovered_), HandKind::hover, sliders_[static_cast<std::size_t>(hovered_)]->a11yId(),
                           line);
        }
        if (ctx_.pointerIn && hoveredWord_ >= 0)
        {
            const auto k = static_cast<std::size_t>(hoveredWord_);
            wordSpecLine(hoveredWord_, line, sizeof line);
            ctx_.offerHand(wordModels_[k]->handPid(), HandKind::hover, words_[k]->a11yId(), line);
        }
        if (ctx_.focusVisible && ctx_.focus != 0)
        {
            if (const int i = sliderOf(ctx_.focus); i >= 0)
            {
                specLine(i, line, sizeof line);
                ctx_.offerHand(slotPid(i), HandKind::focus, ctx_.focus, line);
            }
            else if (const int k = wordOf(ctx_.focus); k >= 0 && words_[static_cast<std::size_t>(k)]->visible())
            {
                wordSpecLine(k, line, sizeof line);
                ctx_.offerHand(wordModels_[static_cast<std::size_t>(k)]->handPid(), HandKind::focus, ctx_.focus, line);
            }
        }
    }

    bool SlotGrid::wantsFullRate() const
    {
        if (!entry_.settled())
            return true;
        for (const auto& s : sliders_)
            if (!s->settled())
                return true;
        for (const auto& w : words_)
            if (w && w->visible() && !w->settled())
                return true;
        return false;
    }

    // ---- the footer lines (02 §6.6, §8.1) ------------------------------------------------------------------------------

    void SlotGrid::specLine(int i, char* out, std::size_t n) const
    {
        const funkgui::RuleSlider& slider = *sliders_[static_cast<std::size_t>(i)];
        const fcdsp::Pid pid = layout::kSlots[static_cast<std::size_t>(i)].pid;
        if (writable(i))
        {
            slider.specLine(out, n);                             // STEPS …, hints, EXTENSION, CLAMPED FROM
            // RuleSlider names an extension by its "+" tag; SlotModel drops that tag when it would push a word's
            // detent labels off their line, so the footer says it here instead.
            const fcdsp::ParamSpec* s = ctx_.slot(pid).spec();
            const char* tag = slider.view().tag;
            if (s != nullptr && (s->flags & fcdsp::kFlagExtension) != 0 && !(tag != nullptr && std::strcmp(tag, "+") == 0))
            {
                Out line(out, n, true);
                line.add("   EXTENSION \xE2\x80\x94 NOT ON THE ORIGINAL UNIT \xE2\x80\x94 NEUTRAL AT DEFAULT");
            }
            return;
        }
        // Locked, derived, n/a: "KNEE   = RATIO   FOLLOWS THE RATIO SWITCH …   STORED 6.0 DB (USED BY OTHER MODES)".
        const funkgui::ValueView& v = slider.view();
        Out line(out, n);
        line.add(v.label);
        if (v.aka != nullptr && v.aka[0] != '\0')
            line.add(" (").add(v.aka).add(")");
        if (v.tag != nullptr && v.tag[0] != '\0' && std::strcmp(v.tag, "+") != 0)
            line.add("   ").add(v.tag);
        if (v.reason != nullptr && v.reason[0] != '\0')
            line.add("   ").add(v.reason);
        if (!indexParam(pid) && ctx_.frame.entry != nullptr)
        {
            char stored[48];
            SlotModel::universalText(pid, ctx_.frame.raw.v[fcdsp::idx(pid)], stored, static_cast<int>(sizeof stored));
            line.add("   STORED ").add(stored).add(" (USED BY OTHER MODES)");
        }
    }

    void SlotGrid::wordSpecLine(int k, char* out, std::size_t n) const
    {
        const Word& m = *wordModels_[static_cast<std::size_t>(k)];
        const layout::SlotPlace& place = layout::kSlots[static_cast<std::size_t>(wordSlot_[static_cast<std::size_t>(k)])];
        Out line(out, n);
        line.add(place.wordLabel).add(" (").upper(hostParam(m.pid()).name).add(")");
        line.add(m.on() ? "   ON" : "   OFF");
        if (!m.enabled())
        {
            if (const char* r = m.reason())
                line.add("   ").add(r);
            return;
        }
        line.add("   CLICK / RETURN TOGGLES");
        if (m.noBus())
            line.add("   ").add(kNoBus);
    }

    // ---- drawing --------------------------------------------------------------------------------------------------------

    void SlotGrid::draw(funkgui::Canvas& c, const funkgui::Theme& th) const
    {
        const FrameState& f = ctx_.frame;
        for (std::size_t i = 0; i < sliders_.size(); ++i)
        {
            const funkgui::RuleSlider& slider = *sliders_[i];
            if (slider.view().state == funkgui::ValueState::derived)
            {
                // ADR-74 (v1.1): a linked slot follows another parameter and is not edited on its own; it sits in a
                // box (the slot's hit rectangle) so that reads at a glance, under the tag that names what it follows.
                const funkgui::Rect b = slider.geom().hit();
                const funkgui::Canvas::Scope scope(c, tag::linkedBox, false);
                c.rrect(b.x, b.y, b.w, b.h, layout::kLinkedRadius, th.ink16.withAlpha(layout::kLinkedFillAlpha), 1.0f,
                        th.ink32);
            }
            slider.draw(c, th, focused(slider.a11yId()));

            const fcdsp::Pid pid = layout::kSlots[i].pid;
            if (!f.live || f.entry == nullptr || slider.view().state != funkgui::ValueState::continuous)
                continue;
            const SlotModel& m = ctx_.slot(pid);
            const funkgui::SlotGeom& g = slider.geom();
            const float cx = pid == fcdsp::Pid::thr ? telemetry::operatingPoint(f.ui, ctx_.history).x : kFloorDb;
            if (pid == fcdsp::Pid::thr && cx > kFloorDb)
            {
                // THRESHOLD's detector tick (02 §6.4): the operating point on the threshold's own scale — the dot's, the
                // 10 ms peak envelope (Telemetry.h), so it does not jitter with the waveform's phase (UF1a). T_in is
                // affine in thr with slope 1, so the level x sits where thr = thr_now + (x − T_in).
                const float at = m.resolved().plain + (cx - fcdsp::analysis::inputThresholdDb(f.eng));
                const float t = m.trackPosition(at);
                if (t >= 0.0f && t <= 1.0f)
                {
                    const funkgui::Canvas::Scope scope(c, tag::detTick, true);
                    c.hairlineV(g.x + t * g.w, g.trackY() + 1.0f, 4.0f, th.ink52);
                }
            }
            else if (pid == fcdsp::Pid::range)
            {
                // RANGE's GR-used bar (02 §6.4): 1 px signal on the track from 0 dB to the applied GR.
                const float gr = std::clamp(std::max(f.ui.appliedGrDb[0], f.ui.appliedGrDb[1]), 0.0f, kRangeOffDb);
                const float x0 = g.x + std::clamp(m.trackPosition(0.0f), 0.0f, 1.0f) * g.w;
                const float x1 = g.x + std::clamp(m.trackPosition(gr), 0.0f, 1.0f) * g.w;
                if (std::fabs(x1 - x0) >= 0.5f)
                {
                    const funkgui::Canvas::Scope scope(c, tag::rangeBar, true);
                    c.hairlineH(std::min(x0, x1), g.trackY(), std::fabs(x1 - x0), th.signal);
                }
            }
        }
        for (std::size_t k = 0; k < words_.size(); ++k)
        {
            if (!words_[k] || !words_[k]->visible())
                continue;
            wordModels_[k]->present(true);
            words_[k]->draw(c, th, focused(words_[k]->a11yId()));
            wordModels_[k]->present(false);
        }
        entry_.draw(c, th);                                      // ADR-89: over its slot's value
    }

    bool SlotGrid::hit(funkgui::Point p) const { return layout::kSlotGrid.contains(p); }

    funkgui::Cursor SlotGrid::cursor(funkgui::Point p) const
    {
        if (captured_ >= 0)                                      // a drag keeps its cursor off the slot
            return sliders_[static_cast<std::size_t>(captured_)]->cursorAt(downAt_);
        if (const int k = wordAt(p); k >= 0)
            return words_[static_cast<std::size_t>(k)]->cursorAt(p);
        if (const int i = sliderAt(p); i >= 0)
            return sliders_[static_cast<std::size_t>(i)]->cursorAt(p);
        return funkgui::Cursor::normal;
    }

    // ---- pointer --------------------------------------------------------------------------------------------------------

    void SlotGrid::pointerMove(const funkgui::PointerEvent& e)
    {
        const funkgui::Point p{ e.x, e.y };
        const int k = wordAt(p);
        const int i = k >= 0 ? -1 : sliderAt(p);
        if (i != hovered_ && hovered_ >= 0)
            sliders_[static_cast<std::size_t>(hovered_)]->pointerExit();
        hovered_ = i;
        hoveredWord_ = k;
        if (i >= 0)
            sliders_[static_cast<std::size_t>(i)]->pointerMove(e);
    }

    void SlotGrid::pointerExit()
    {
        if (hovered_ >= 0)
            sliders_[static_cast<std::size_t>(hovered_)]->pointerExit();
        hovered_ = -1;
        hoveredWord_ = -1;
    }

    void SlotGrid::pointerDown(const funkgui::PointerEvent& e)
    {
        captured_ = capturedWord_ = -1;
        if (ctx_.gestures == nullptr || ctx_.host == nullptr)
            return;
        const funkgui::Point p{ e.x, e.y };
        if (const int k = wordAt(p); k >= 0)
        {
            capturedWord_ = k;
            words_[static_cast<std::size_t>(k)]->pointerDown(e, *ctx_.gestures);
            return;
        }
        if (const int i = sliderAt(p); i >= 0)
        {
            captured_ = i;
            downAt_ = p;
            ctx_.focus = sliders_[static_cast<std::size_t>(i)]->a11yId();   // ADR-89: a number typed next goes here
            sliders_[static_cast<std::size_t>(i)]->pointerDown(e, *ctx_.gestures, *ctx_.host);
        }
    }

    void SlotGrid::pointerDrag(const funkgui::PointerEvent& e)
    {
        if (ctx_.gestures == nullptr)
            return;
        if (capturedWord_ >= 0)
            words_[static_cast<std::size_t>(capturedWord_)]->pointerDrag(e);
        else if (captured_ >= 0)
            sliders_[static_cast<std::size_t>(captured_)]->pointerDrag(e, *ctx_.gestures);
    }

    void SlotGrid::pointerUp(const funkgui::PointerEvent& e)
    {
        const int k = capturedWord_, i = captured_;
        capturedWord_ = captured_ = -1;
        if (ctx_.gestures == nullptr)
            return;
        if (k >= 0)
            words_[static_cast<std::size_t>(k)]->pointerUp(e, *ctx_.gestures);
        else if (i >= 0)
            sliders_[static_cast<std::size_t>(i)]->pointerUp(e, *ctx_.gestures);
    }

    void SlotGrid::doubleClick(const funkgui::PointerEvent& e)
    {
        if (ctx_.gestures == nullptr || capturedWord_ >= 0)
            return;                                              // a word toggles on each click, never "resets"
        const int i = captured_ >= 0 ? captured_ : sliderAt({ e.x, e.y });
        if (i < 0)
            return;
        sliders_[static_cast<std::size_t>(i)]->doubleClick(*ctx_.gestures);   // the Mode default (02 §8.4.6)
        if (writable(i))
            ctx_.touch(layout::kSlots[static_cast<std::size_t>(i)].pid);
    }

    bool SlotGrid::wheel(const funkgui::WheelEvent& e)
    {
        if (ctx_.gestures == nullptr || ctx_.host == nullptr)
            return false;
        const int i = sliderAt({ e.x, e.y });
        if (i < 0)
            return false;
        const bool used = sliders_[static_cast<std::size_t>(i)]->wheel(e, *ctx_.gestures, ctx_.host->nowSeconds());
        if (used && writable(i))
            ctx_.touch(layout::kSlots[static_cast<std::size_t>(i)].pid);
        return used;
    }

    // ---- keyboard and accessibility ---------------------------------------------------------------------------------------

    bool SlotGrid::key(const funkgui::KeyEvent& e)
    {
        if (ctx_.gestures == nullptr)
            return false;
        if (entry_.open())                                       // ADR-89: the field takes every key
        {
            switch (entry_.key(e))
            {
                case ValueEntry::Result::commit: commitEntry(); break;
                case ValueEntry::Result::cancel: closeEntry(); break;
                case ValueEntry::Result::typing: break;
            }
            return true;
        }
        if (const int i = sliderOf(ctx_.focus); i >= 0 && ValueEntry::opens(e))
        {
            if (writable(i))
                openEntry(i, e);
            return true;                                         // a refused slot opens nothing (the footer says why)
        }
        if (const int i = sliderOf(ctx_.focus); i >= 0)
        {
            const bool used = sliders_[static_cast<std::size_t>(i)]->key(e, *ctx_.gestures);   // 02 §8.9
            if (used && writable(i))
                ctx_.touch(layout::kSlots[static_cast<std::size_t>(i)].pid);
            return used;
        }
        if (const int k = wordOf(ctx_.focus); k >= 0)
            return words_[static_cast<std::size_t>(k)]->key(e, *ctx_.gestures);
        return false;
    }

    // ---- typed values (ADR-89) ----------------------------------------------------------------------------------------

    bool SlotGrid::takesTypedKeys(uint32_t id) const { return sliderOf(id) >= 0; }

    void SlotGrid::openEntry(int i, const funkgui::KeyEvent& opener)
    {
        const funkgui::RuleSlider& s = *sliders_[static_cast<std::size_t>(i)];
        const funkgui::SlotGeom& g = s.geom();
        const funkgui::TextStyle& style = g.isPrimary() ? funkgui::type::kValueP : funkgui::type::kValueS;
        const funkgui::Rect box { g.x - 4.0f, g.valueTop() - 3.0f, g.w + 8.0f, style.px + 6.0f };
        std::string current = s.view().text.value;
        if (s.view().text.unit[0] != '\0')
            current += std::string(" ") + s.view().text.unit;
        entry_.begin(box, style, opener, current);
        entrySlider_ = i;
        ctx_.textEntry = static_cast<int>(ViewIndex::slotGrid);
        ctx_.textEntryBox = box;
    }

    bool SlotGrid::commitEntry()
    {
        const int i = entrySlider_;
        const FrameState& f = ctx_.frame;
        if (i < 0 || ctx_.gestures == nullptr || f.entry == nullptr)
        {
            closeEntry();
            return true;
        }
        const fcdsp::Pid pid = layout::kSlots[static_cast<std::size_t>(i)].pid;
        float plain = 0.0f;
        if (!fcdsp::parseHost(*f.entry, f.raw, pid, entry_.text(), plain) || !std::isfinite(plain))
        {
            entry_.refuse();
            return false;
        }
        ctx_.gestures->tap(ctx_.facade.port(pid), fcdsp::toNorm(pid, plain));
        ctx_.touch(pid);
        closeEntry();
        return true;
    }

    void SlotGrid::closeEntry() noexcept
    {
        if (entry_.open() && ctx_.textEntry == static_cast<int>(ViewIndex::slotGrid))
            ctx_.textEntry = -1;
        entry_.end();
        entrySlider_ = -1;
    }

    void SlotGrid::endTextEntry(bool commit)
    {
        if (commit && !commitEntry())
            closeEntry();                                        // a click elsewhere never leaves a typo behind
        else if (!commit)
            closeEntry();
    }

    int SlotGrid::focusOrder(std::span<uint32_t> out) const
    {
        // 02 §8.9 item 6: P0…P6, B0…B6, C0…C6, each followed by its attached word; locked, derived and n/a included.
        std::size_t n = 0;
        for (std::size_t i = 0; i < sliders_.size(); ++i)
        {
            if (n < out.size())
                out[n++] = sliders_[i]->a11yId();
            for (std::size_t k = 0; k < words_.size(); ++k)
                if (words_[k] && wordSlot_[k] == static_cast<int>(i) && words_[k]->visible() && n < out.size())
                    out[n++] = words_[k]->a11yId();
        }
        return static_cast<int>(n);
    }

    void SlotGrid::accessibility(std::vector<funkgui::A11yItem>& out) const
    {
        for (std::size_t i = 0; i < sliders_.size(); ++i)
        {
            const funkgui::RuleSlider& slider = *sliders_[i];
            funkgui::A11yItem it;
            slider.accessibility(it);
            const fcdsp::Pid pid = layout::kSlots[i].pid;
            const SlotModel& m = ctx_.slot(pid);
            const fcdsp::ParamSpec* s = m.spec();
            const funkgui::ValueState st = slider.view().state;
            if (s != nullptr && it.role == funkgui::A11yRole::slider && st != funkgui::ValueState::stepped)
            {
                // 02 §8.9: the Mode track in display units (RuleSlider speaks track space {0, 1}).
                const PlainRange span = trackSpan(pid, *s);
                const double a = toDisplayUnits(pid, *s, span.a), b = toDisplayUnits(pid, *s, span.b);
                it.lo = std::min(a, b);
                it.hi = std::max(a, b);
                it.step = (it.hi - it.lo) * 0.01;
                it.v = std::clamp(toDisplayUnits(pid, *s, m.resolved().plain), it.lo, it.hi);
            }
            out.push_back(std::move(it));
            for (std::size_t k = 0; k < words_.size(); ++k)
            {
                if (!words_[k] || wordSlot_[k] != static_cast<int>(i) || !words_[k]->visible())
                    continue;
                const std::size_t first = out.size();
                words_[k]->accessibility(out);
                if (wordModels_[k]->noBus() && first < out.size())
                    out[first].help = kNoBus;                    // drawn disabled; still operable
            }
        }
    }

    void SlotGrid::a11yAction(uint32_t id, funkgui::A11yAction a, double value)
    {
        if (a == funkgui::A11yAction::focus || ctx_.gestures == nullptr)
            return;                                              // the Panel moved the focus already
        if (const int i = sliderOf(id); i >= 0)
        {
            funkgui::RuleSlider& slider = *sliders_[static_cast<std::size_t>(i)];
            const fcdsp::Pid pid = layout::kSlots[static_cast<std::size_t>(i)].pid;
            const SlotModel& m = ctx_.slot(pid);
            double v = value;
            const funkgui::ValueState st = slider.view().state;
            if (a == funkgui::A11yAction::setValue && m.spec() != nullptr && !std::isnan(value)
                && st == funkgui::ValueState::continuous)
            {
                // Display units back to the track (accessibility() above rewrote the value interface).
                const PlainRange span = trackSpan(pid, *m.spec());
                const float plain = fromDisplayUnits(pid, *m.spec(), value);
                const float lo = std::min(span.a, span.b), hi = std::max(span.a, span.b);
                v = static_cast<double>(std::clamp(m.trackPosition(std::clamp(plain, lo, hi)), 0.0f, 1.0f));
            }
            slider.a11yAction(a, v, *ctx_.gestures);
            const bool write = a == funkgui::A11yAction::setValue || a == funkgui::A11yAction::increment
                            || a == funkgui::A11yAction::decrement;
            if (write && writable(i))
                ctx_.touch(pid);
            return;
        }
        if (const int k = wordOf(id); k >= 0)
            words_[static_cast<std::size_t>(k)]->a11yAction(id, a, *ctx_.gestures);
    }

    uint32_t SlotGrid::a11yRevision() const { return revision_; }
}
