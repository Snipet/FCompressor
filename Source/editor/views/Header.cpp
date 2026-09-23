// Source/editor/views/Header.cpp — U1a stub of the header (see Header.h): the region frame, the Mode latch's cells,
// the MODE caption and, so a frame names its Mode, the Mode name and topology line. U1b replaces it.
#include "editor/views/Header.h"

#include "editor/Layout.h"
#include "editor/Tags.h"

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"

#include <funkgui/canvas/Canvas.h>
#include <funkgui/core/Theme.h>
#include <funkgui/core/TypeScale.h>

#include <algorithm>
#include <string>
#include <string_view>
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

        // A string_view as a NUL-terminated string in a caller's buffer (no allocation in draw).
        template <std::size_t N>
        const char* cstr(std::string_view s, char (&buf)[N]) noexcept
        {
            const std::size_t n = std::min(s.size(), N - 1);
            std::copy_n(s.data(), n, buf);
            buf[n] = '\0';
            return buf;
        }

        const fcdsp::ModeDescriptor* descOf(const PanelContext& ctx) noexcept
        {
            return ctx.frame.entry != nullptr ? ctx.frame.entry->desc : nullptr;
        }
    }

    Header::Header(PanelContext& ctx) : ctx_(ctx) {}

    void Header::tick(float) {}

    void Header::draw(funkgui::Canvas& c, const funkgui::Theme& th) const
    {
        namespace H = layout::header;
        namespace T = funkgui::type;
        {
            const funkgui::Canvas::Scope scope(c, tag::plotFrame, false);
            outline(c, layout::kHeader, th.ink16);
            outline(c, H::kModePrev, th.ink16);
            outline(c, H::kModeName, th.ink16);
            outline(c, H::kModeNext, th.ink16);
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::caption, false);
            c.text("HEADER", layout::kHeader.x + 2.0f, 2.0f, T::kMicro, th.ink16);
            c.text("MODE", H::kModeCaptionRight, H::kModeCaptionTop, T::kCaption, th.ink52, funkgui::Align::right);
        }
        if (const fcdsp::ModeDescriptor* d = descOf(ctx_))
        {
            char buf[64];
            {
                const funkgui::Canvas::Scope scope(c, tag::modeName, false);
                c.text(cstr(d->name, buf), H::kModeNameTextX, c.capCentreTop(H::kModeNameCentreY, T::kLatch), T::kLatch,
                       th.ink100);
            }
            const funkgui::Canvas::Scope scope(c, tag::topology, false);
            c.text(d->topologyLine, H::kTopology.x, H::kTopology.y, T::kCaption, th.ink32);
        }
    }

    bool Header::hit(funkgui::Point p) const { return layout::kHeader.contains(p); }

    void Header::accessibility(std::vector<funkgui::A11yItem>& out) const
    {
        funkgui::A11yItem it;
        it.id = a11yId(ViewIndex::header, 1);
        it.role = funkgui::A11yRole::staticText;
        it.bounds = layout::header::kModeLatch;
        it.title = "Mode";
        if (const fcdsp::ModeDescriptor* d = descOf(ctx_))
            it.value = std::string(d->name);
        it.readOnly = true;
        out.push_back(std::move(it));
    }

    int Header::focusOrder(std::span<uint32_t>) const { return 0; }
}
