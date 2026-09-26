// FCMP_PROBE layer=ui name=contrast scope=global timeout=300
//
// ui.contrast (ADR-73, S13 H1a): FCompressor's own high-contrast PAPER (Source/editor/ProductTheme.h). Spec-only: no
// golden rows (the palette is a source constant; its targets are ADR-73's, checked here).
//
//   contrast.<token>.ratio        each PAPER token's WCAG 2.x contrast ratio against the ground is at least ADR-73's
//                                 target: ink100 >= 15, ink70 >= 9, ink52 >= 6, ink32 >= 3.5, ink16 >= 1.8,
//                                 accent >= 4.5, accentDim >= 2.2, signal >= 5.5
//   contrast.<ink>.vs_graphite    every ink level (ink100 … ink16) is at least GRAPHITE's own ratio at that level
//   contrast.ladder               strictly ordered: ink100 > ink70 > ink52 > ink32 > ink16 > 1
//   contrast.ground               the ground is EDEBE6, FunkGui's PAPER ground (EditorHost's clear and fallback screen)
//   contrast.text_gamma           re-tuned below FunkGui PAPER's 1.4 (thin dark text no longer thinned), in (0.5, 1]
//   contrast.graphite.unchanged   productTheme(0) is funkgui::Theme::graphite(), bit for bit; productTheme(1) is the
//                                 product PAPER; an index that names no theme draws GRAPHITE, as Theme::byIndex
//   contrast.mode.<key>.<theme>   ADR-75 (v1.1): every registered Mode has its own colour (ProductTheme.h
//                                 kModeColours), at least 7 against GRAPHITE's ground and 5.5 against PAPER's; no two
//                                 Modes share one
//   contrast.mode.<key>.drawn.<theme>  the panel of that Mode, settled, draws its header rule (MODE_RULE) in exactly
//                                 that colour (the Panel's signal ink, eased to the Mode's)
//   contrast.draw.<theme>.*       every view of fcmp::ui::views() for every registered Mode, headless (FakeFacade,
//                                 dpi 1): the frame's clear colour is the theme's ground; its textGamma and every text
//                                 primitive's are the theme's (PAPER: the product's, so the Panel re-began the frame;
//                                 GRAPHITE: FunkGui's); the theme's ink100, ink52 and ink32 are drawn; and no primitive
//                                 is drawn in a token of the other palette where the two differ (PAPER: nothing is
//                                 left in FunkGui's old PAPER inks; GRAPHITE: nothing in the product's)
#include "ProbeRegistry.h"

#include "FakeFacade.h"

#include "editor/Panel.h"
#include "editor/ProductTheme.h"
#include "editor/Tags.h"

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"

#include <funkgui/canvas/Prim.h>
#include <funkgui/canvas/PrimList.h>
#include <funkgui/core/Col.h>
#include <funkgui/core/Ease.h>
#include <funkgui/core/Theme.h>
#include <funkgui/panel/HeadlessHost.h>
#include <funkgui/text/FontService.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>

namespace
{
    namespace ui = fcmp::ui;
    using funkgui::test::Probe;

    constexpr int   kMaxSettle = 600;
    constexpr float kDt = 1.0f / 60.0f;

    // ---- WCAG 2.x relative luminance and contrast ratio -------------------------------------------------------------

    double linear(uint8_t c)
    {
        const double v = static_cast<double>(c) / 255.0;
        return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    }

    double luminance(funkgui::Col c)
    {
        return 0.2126 * linear(c.r) + 0.7152 * linear(c.g) + 0.0722 * linear(c.b);
    }

    double ratio(funkgui::Col a, funkgui::Col b)
    {
        double la = luminance(a), lb = luminance(b);
        if (la < lb)
            std::swap(la, lb);
        return (la + 0.05) / (lb + 0.05);
    }

    bool same(funkgui::Col a, funkgui::Col b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }

    bool same(const funkgui::Theme& a, const funkgui::Theme& b)
    {
        return same(a.ground, b.ground) && same(a.ink100, b.ink100) && same(a.ink70, b.ink70) && same(a.ink52, b.ink52)
            && same(a.ink32, b.ink32) && same(a.ink16, b.ink16) && same(a.accent, b.accent)
            && same(a.accentDim, b.accentDim) && same(a.signal, b.signal)
            && funkgui::ease::sameBits(a.textGamma, b.textGamma);
    }

    std::string hex(funkgui::Col c)
    {
        char s[16];
        std::snprintf(s, sizeof s, "%02X%02X%02X", c.r, c.g, c.b);
        return s;
    }

    // The theme's tokens by name, ground excluded.
    struct Token
    {
        const char* name;
        funkgui::Col funkgui::Theme::*member;
        double target;                                           // ADR-73's PAPER target
    };
    constexpr std::array<Token, 8> kTokens { {
        { "ink100",    &funkgui::Theme::ink100,    15.0 },
        { "ink70",     &funkgui::Theme::ink70,      9.0 },
        { "ink52",     &funkgui::Theme::ink52,      6.0 },
        { "ink32",     &funkgui::Theme::ink32,      3.5 },
        { "ink16",     &funkgui::Theme::ink16,      1.8 },
        { "accent",    &funkgui::Theme::accent,     4.5 },
        { "accentdim", &funkgui::Theme::accentDim,  2.2 },
        { "signal",    &funkgui::Theme::signal,     5.5 },
    } };
    constexpr std::size_t kInkLevels = 5;                        // kTokens[0..4]: the ink ladder

    // ---- the palette ------------------------------------------------------------------------------------------------

    void palette(Probe& P)
    {
        const funkgui::Theme paper = ui::paperHighContrast();
        const funkgui::Theme graphite = funkgui::Theme::graphite();
        std::array<double, kTokens.size()> r{};
        for (std::size_t i = 0; i < kTokens.size(); ++i)
        {
            const Token& t = kTokens[i];
            r[i] = ratio(paper.*t.member, paper.ground);
            std::printf("NOTE     paper %-9s %s %6.2f (target %.1f; FunkGui PAPER %.2f, GRAPHITE %.2f)\n", t.name,
                        hex(paper.*t.member).c_str(), r[i], t.target,
                        ratio(funkgui::Theme::paper().*t.member, funkgui::Theme::paper().ground),
                        ratio(graphite.*t.member, graphite.ground));
            P.ge(std::string("contrast.") + t.name + ".ratio", r[i], t.target);
            if (i < kInkLevels)
                P.ge(std::string("contrast.") + t.name + ".vs_graphite", r[i],
                     ratio(graphite.*t.member, graphite.ground));
        }
        bool ordered = r[kInkLevels - 1] > 1.0;
        for (std::size_t i = 1; i < kInkLevels; ++i)
            ordered = ordered && r[i - 1] > r[i];
        P.eq("contrast.ladder", ordered ? 1 : 0, 1);
        P.eq("contrast.ground", same(paper.ground, funkgui::Col{ 0xED, 0xEB, 0xE6 })
                                    && same(paper.ground, funkgui::Theme::paper().ground) ? 1 : 0, 1);
        P.in("contrast.text_gamma", static_cast<double>(paper.textGamma), 0.5, 1.0);

        // GRAPHITE is FunkGui's, bit for bit; PAPER is the product's; anything else draws GRAPHITE (Theme::byIndex).
        const bool graphiteSame = same(ui::productTheme(ui::kThemeGraphite), graphite)
                               && same(ui::productTheme(ui::kThemeGraphite), funkgui::Theme::byIndex(0))
                               && same(ui::productTheme(7), graphite) && same(ui::productTheme(-1), graphite);
        P.eq("contrast.graphite.unchanged", graphiteSame ? 1 : 0, 1);
        P.eq("contrast.paper.product", same(ui::productTheme(ui::kThemePaper), paper) ? 1 : 0, 1);
    }

    // ---- what the Panel draws ---------------------------------------------------------------------------------------

    struct Seen
    {
        int  frames = 0;
        bool clearOk = true, gammaOk = true, textGammaOk = true;
        bool foreign = false;                                    // a primitive in the other palette's token
        std::string foreignWhat;
        bool ink100 = false, ink52 = false, ink32 = false;
    };

    void scan(const funkgui::PrimList& pl, const funkgui::Theme& want, const funkgui::Theme& other, Seen& s,
              const char* where)
    {
        ++s.frames;
        s.clearOk = s.clearOk && same(pl.info.clear, want.ground);
        s.gammaOk = s.gammaOk && funkgui::ease::sameBits(pl.info.textGamma, want.textGamma);
        const auto unpack = [](uint32_t v) {
            return funkgui::Col{ static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v >> 16),
                                 static_cast<uint8_t>(v >> 24) };
        };
        for (const funkgui::Prim& p : pl.prims)
        {
            const bool text = p.d2[2] > 0.5f && p.d2[2] < 1.5f;  // PrimKind::text
            if (text)
                s.textGammaOk = s.textGammaOk && funkgui::ease::sameBits(p.d1[2], want.textGamma);
            for (const uint32_t packed : { p.c0, p.c1 })
            {
                const funkgui::Col c = unpack(packed);
                if (c.a != 255)
                    continue;                                    // fades and transparent borders
                s.ink100 = s.ink100 || same(c, want.ink100);
                s.ink52 = s.ink52 || same(c, want.ink52);
                s.ink32 = s.ink32 || same(c, want.ink32);
                for (const Token& t : kTokens)
                    if (!same(other.*t.member, want.*t.member) && same(c, other.*t.member) && !s.foreign)
                    {
                        s.foreign = true;
                        s.foreignWhat = std::string(where) + ": " + t.name + " " + hex(c);
                    }
            }
        }
    }

    void drawn(Probe& P)
    {
        const funkgui::Theme product = ui::paperHighContrast();
        const funkgui::Theme oldPaper = funkgui::Theme::paper();
        const funkgui::Theme graphite = funkgui::Theme::graphite();
        for (const int theme : { ui::kThemePaper, ui::kThemeGraphite })
        {
            const funkgui::Theme& want = theme == ui::kThemePaper ? product : graphite;
            const funkgui::Theme& other = theme == ui::kThemePaper ? oldPaper : product;
            Seen s;
            bool settled = true;
            for (const fcdsp::ModeSlot& m : fcdsp::modeSlots())
            {
                if (m.entry == nullptr || m.entry->desc == nullptr)
                    continue;
                for (const ui::ViewSpec& v : ui::views())
                {
                    fcmp::probe::FakeFacade facade(m.entry->desc->key);
                    ui::Panel panel(facade, { true, true, false });
                    funkgui::HeadlessHost host(panel, theme, 1.0f);
                    panel.setView(v, true);
                    settled = settled && host.settle(kMaxSettle, kDt) <= kMaxSettle;
                    const std::string where = std::string(m.entry->desc->key) + "/" + v.id;
                    scan(host.draw(), want, other, s, where.c_str());
                }
            }
            if (!settled)
                P.harnessError("ui.contrast: a view did not settle within 600 frames");
            if (s.foreign)
                std::printf("NOTE     theme %d: %s\n", theme, s.foreignWhat.c_str());
            const std::string k = std::string("contrast.draw.") + (theme == ui::kThemePaper ? "paper" : "graphite");
            P.ge(k + ".frames", s.frames, 5);
            P.eq(k + ".clear", s.clearOk ? 1 : 0, 1);
            P.eq(k + ".frame_gamma", s.gammaOk ? 1 : 0, 1);
            P.eq(k + ".text_gamma", s.textGammaOk ? 1 : 0, 1);
            P.eq(k + ".inks", s.ink100 && s.ink52 && s.ink32 ? 1 : 0, 1);
            P.eq(k + ".no_foreign_token", s.foreign ? 0 : 1, 1);
        }
    }
}

namespace
{
    // ---- ADR-75: the Mode colours ------------------------------------------------------------------------------------

    void modeColours(Probe& P)
    {
        const funkgui::Theme themes[2] = { funkgui::Theme::graphite(), ui::paperHighContrast() };
        const char* names[2] = { "graphite", "paper" };
        const double targets[2] = { 7.0, 5.5 };
        int distinctFails = 0;
        for (const fcdsp::ModeSlot& m : fcdsp::modeSlots())
        {
            if (m.entry == nullptr || m.entry->desc == nullptr)
                continue;
            const std::string_view key = m.entry->desc->key;
            bool listed = false;
            for (const ui::ModeColour& c : ui::kModeColours)
                listed = listed || c.key == key;
            P.eq("contrast.mode." + std::string(key) + ".listed", listed ? 1 : 0, 1);
            for (int t = 0; t < 2; ++t)
            {
                const funkgui::Col col = ui::modeColour(key, themes[t]);
                const double r = ratio(col, themes[t].ground);
                std::printf("NOTE     mode %-10s %-8s %s %6.2f\n", std::string(key).c_str(), names[t], hex(col).c_str(),
                            r);
                P.ge("contrast.mode." + std::string(key) + "." + names[t], r, targets[t]);

                // What the settled panel draws: the header rule (MODE_RULE) in exactly the Mode's colour.
                fcmp::probe::FakeFacade facade(key);
                ui::Panel panel(facade, { true, true, false });
                funkgui::HeadlessHost host(panel, t == 0 ? ui::kThemeGraphite : ui::kThemePaper, 1.0f);
                panel.setView(ui::views()[0], true);
                host.settle(kMaxSettle, kDt);
                int rules = 0, right = 0;
                for (const funkgui::Prim& p : host.draw().prims)
                    if (p.tag == ui::tag::modeRule)
                    {
                        ++rules;
                        const funkgui::Col c { static_cast<uint8_t>(p.c0), static_cast<uint8_t>(p.c0 >> 8),
                                               static_cast<uint8_t>(p.c0 >> 16), static_cast<uint8_t>(p.c0 >> 24) };
                        right += same(c, col) ? 1 : 0;
                    }
                P.eq("contrast.mode." + std::string(key) + ".drawn." + names[t], rules > 0 && right == rules ? 1 : 0, 1);
            }
        }
        for (std::size_t i = 0; i < ui::kModeColours.size(); ++i)
            for (std::size_t j = i + 1; j < ui::kModeColours.size(); ++j)
                distinctFails += same(ui::kModeColours[i].graphite, ui::kModeColours[j].graphite)
                              || same(ui::kModeColours[i].paper, ui::kModeColours[j].paper) ? 1 : 0;
        P.eq("contrast.mode.distinct", distinctFails, 0);
    }
}

FCMP_PROBE(ui, contrast)
{
    const juce::ScopedJuceInitialiser_GUI juceInit;               // FontService bakes the atlas through JUCE's fonts
    P.eq("contrast.font.ok", funkgui::FontService::get().atlas().baked() ? 1 : 0, 1);
    palette(P);
    modeColours(P);
    drawn(P);
    return P.finish();
}
