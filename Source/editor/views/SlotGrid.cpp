// Source/editor/views/SlotGrid.cpp — U1a stub of the slot grid (see SlotGrid.h). For each of the 21 slots at its
// Layout.h geometry it draws the hit frame and, from the slot's SlotModel view, its title (the Mode's label, so a
// renamed slot reads INPUT or PEAK RED.), its value, one line naming the state (the detent labels of a stepped slot,
// the brief of a locked one, FOLLOWS … for a derived one, the universal name under a renamed one) and its track rule,
// plus the AUTO / EXT / LISTEN words. No input: U1s replaces it with RuleSliders over the same SlotModels.
#include "editor/views/SlotGrid.h"

#include "editor/Layout.h"
#include "editor/SlotModel.h"
#include "editor/Tags.h"

#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"

#include <funkgui/canvas/Canvas.h>
#include <funkgui/canvas/Tags.h>
#include <funkgui/core/Theme.h>
#include <funkgui/core/TypeScale.h>
#include <funkgui/text/TextFit.h>
#include <funkgui/widgets/AttachedWord.h>
#include <funkgui/widgets/ValueModel.h>

#include <cstddef>
#include <cstring>
#include <string>
#include <utility>

namespace fcmp::ui
{
    namespace
    {
        void outline(funkgui::Canvas& c, const funkgui::Rect& r, funkgui::Col col)
        {
            c.hairlineH(r.x, r.y, r.w, col);
            c.hairlineH(r.x, r.bottom() - 1.0f, r.w, col);
            c.hairlineV(r.x, r.y, r.h, col);
            c.hairlineV(r.right() - 1.0f, r.y, r.h, col);
        }

        // Appends s to buf (NUL-terminated, never overflowing).
        template <std::size_t N>
        void append(char (&buf)[N], std::size_t& n, const char* s) noexcept
        {
            for (; s != nullptr && *s != '\0' && n + 1 < N; ++s)
                buf[n++] = *s;
            buf[n] = '\0';
        }

        // The state line of the stub: what makes each of the five states and the markers visible in a frame.
        template <std::size_t N>
        void stateLine(const funkgui::ValueView& v, char (&buf)[N]) noexcept
        {
            std::size_t n = 0;
            buf[0] = '\0';
            switch (v.state)
            {
                case funkgui::ValueState::stepped:
                    for (int i = 0; i < v.nDetents; ++i)
                    {
                        append(buf, n, i == 0 ? nullptr : " \xC2\xB7 ");       // U+00B7
                        append(buf, n, v.detents[i].label);
                    }
                    break;
                case funkgui::ValueState::locked:
                case funkgui::ValueState::derived:
                    append(buf, n, v.text.sub);
                    break;
                case funkgui::ValueState::na:
                    append(buf, n, "N/A");
                    break;
                case funkgui::ValueState::continuous:
                    append(buf, n, v.aka);                                      // renamed: the universal name
                    break;
            }
        }

        funkgui::Col labelInk(const funkgui::Theme& th, funkgui::ValueState s) noexcept
        {
            return s == funkgui::ValueState::na ? th.ink16 : s == funkgui::ValueState::locked ? th.ink32 : th.ink52;
        }

        funkgui::Col valueInk(const funkgui::Theme& th, const funkgui::ValueView& v) noexcept
        {
            switch (v.state)
            {
                case funkgui::ValueState::na:      return th.ink16;
                case funkgui::ValueState::locked:  return th.ink32;
                case funkgui::ValueState::derived: return th.ink52;
                case funkgui::ValueState::continuous:
                case funkgui::ValueState::stepped: return v.atDefault ? th.ink32 : th.ink100;
            }
            return th.ink100;
        }

        bool wordVisible(const PanelContext& ctx, fcdsp::Pid word) noexcept
        {
            // AUTO is hidden while automu is n/a in the Mode; EXT and LISTEN are globals, always shown (02 §6.4).
            return word != fcdsp::Pid::automu || ctx.slot(word).resolved().state != fcdsp::SlotState::na;
        }
    }

    SlotGrid::SlotGrid(PanelContext& ctx) : ctx_(ctx) {}

    void SlotGrid::tick(float) {}

    void SlotGrid::draw(funkgui::Canvas& c, const funkgui::Theme& th) const
    {
        namespace T = funkgui::type;
        for (const layout::SlotPlace& place : layout::kSlots)
        {
            const funkgui::SlotGeom g = layout::slotGeom(place);
            funkgui::ValueView v;
            ctx_.slot(place.pid).view(v);
            {
                const funkgui::Canvas::Scope scope(c, tag::plotFrame, false);
                outline(c, g.hit(), th.ink16);
            }
            {
                const funkgui::Canvas::Scope scope(c, funkgui::tags::slotLabel, false);
                c.text(v.label, g.x, g.top, T::kLabel, labelInk(th, v.state));
            }
            const bool hasWord = place.word != fcdsp::kNoPid && wordVisible(ctx_, place.word);
            if (hasWord)
            {
                const funkgui::Canvas::Scope scope(c, funkgui::tags::word, false);
                c.text(place.wordLabel, g.x + g.w, g.top, T::kMicro, th.ink32, funkgui::Align::right);
            }
            else if (v.tag != nullptr)
            {
                const funkgui::Canvas::Scope scope(c, funkgui::tags::slotLabel, false);
                c.text(v.tag, g.x + g.w, g.top, T::kMicro, th.ink32, funkgui::Align::right);
            }
            {
                const funkgui::Canvas::Scope scope(c, funkgui::tags::slotValue, false);
                char value[sizeof v.text.value + sizeof v.text.unit + 1];
                std::size_t n = 0;
                append(value, n, v.text.value);
                if (v.text.unit[0] != '\0')
                {
                    append(value, n, " ");
                    append(value, n, v.text.unit);
                }
                c.text(value, g.x, g.valueTop(), g.isPrimary() ? T::kValueP : T::kValueS, valueInk(th, v));
            }
            {
                const funkgui::Canvas::Scope scope(c, funkgui::tags::slotSub, false);
                char line[96];
                char fitted[96];
                stateLine(v, line);
                const bool tagIsLine = v.tag != nullptr && std::strcmp(v.tag, line) == 0;   // a brief-less locked slot
                if (hasWord && v.tag != nullptr && !tagIsLine)                  // a word and a tag: the tag moves here
                {
                    std::size_t n = 0;
                    char both[96];
                    both[0] = '\0';
                    append(both, n, v.tag);
                    append(both, n, " ");
                    append(both, n, line);
                    funkgui::text::fitEllipsis(ctx_.atlas, both, T::kMicro, g.w, fitted, sizeof fitted);
                }
                else
                {
                    funkgui::text::fitEllipsis(ctx_.atlas, line, T::kMicro, g.w, fitted, sizeof fitted);
                }
                c.text(fitted, g.x, g.subTop(), T::kMicro, th.ink32);
            }
            if (v.state != funkgui::ValueState::na)
            {
                const funkgui::Canvas::Scope scope(c, funkgui::tags::slotTrack, false);
                if (v.state == funkgui::ValueState::locked)
                    c.dotted(g.x, g.trackY(), g.w, 3.0f, th.ink16);
                else
                    c.hairlineH(g.x, g.trackY(), g.w, th.ink16);
            }
        }
    }

    bool SlotGrid::hit(funkgui::Point p) const { return layout::kSlotGrid.contains(p); }

    void SlotGrid::accessibility(std::vector<funkgui::A11yItem>& out) const
    {
        uint32_t local = 1;
        for (const layout::SlotPlace& place : layout::kSlots)
        {
            funkgui::ValueView v;
            ctx_.slot(place.pid).view(v);
            funkgui::A11yItem it;
            it.id = a11yId(ViewIndex::slotGrid, local++);
            it.role = funkgui::A11yRole::staticText;
            it.bounds = layout::slotGeom(place).hit();
            it.title = v.label;
            if (v.aka != nullptr)
                it.description = v.aka;
            it.value = v.text.spoken;
            if (v.reason != nullptr)
                it.help = v.reason;
            it.enabled = v.state != funkgui::ValueState::na;
            it.readOnly = true;
            out.push_back(std::move(it));
        }
    }

    int SlotGrid::focusOrder(std::span<uint32_t>) const { return 0; }
}
