// FCMP_PROBE layer=ui name=font scope=global timeout=120 platform=apple
//
// ui.font (03 §3.6, F §0.9; U1s): every string any registered Mode can put on screen has every glyph in the atlas, so
// a Mode that brings a label with a new character (µ, ∞, ≈ …) fails here in plain text instead of drawing a gap.
//
// Strings, per registered Mode: name, topology and spec lines; for every Mode-filtered parameter and every spec
// (base and variants): label, tag, reason, brief, the DisplayMap unit, every step's label and text, and formatParts'
// value and unit of every step and of the range ends and default; the internals' names and units; the SlotModel view
// of every slot at the Mode's defaults (value, unit, sub). Global: the universal slot labels and words, the host
// parameter names and choices, the slot grid's own words (CLAMPED FROM, FOLLOWS, EFF, DET, AUTO, DRY, STORED …,
// NO SIDECHAIN BUS CONNECTED, the EXTENSION note), RuleSlider's spec-line hints, and Layout.h's fixed legends.
//
// Spec rows: font.ok (the atlas is baked from the bundled face); font.strings (the collection is not empty);
// font.missing (codepoints without a glyph, all strings) == 0 and font.missing.<key> == 0 per Mode; space is not a
// glyph. Golden row: font.atlas.hash (exact) — FunkGui's FNV-1a of the atlas pixels; a CoreGraphics update can move it,
// which is a known drift reason (03 §3.6).
//
// macOS only (ADR-92): FunkGui rasterises the glyphs with juce::Graphics into a native Image, CoreGraphics here and
// JUCE's software renderer on Linux, so the atlas hash is a property of the platform. Linux runs this same body as
// ui.font_linux (ui_font_linux.cpp), whose golden holds Linux's atlas; every spec row is the same on both.
#include "ProbeRegistry.h"

#include "FakeFacade.h"

#include "editor/Layout.h"
#include "editor/SlotModel.h"
#include "editor/SubView.h"

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/params/Text.h"

#include <funkgui/text/FontAtlasSdf.h>
#include <funkgui/text/FontService.h>
#include <funkgui/text/TextFit.h>
#include <funkgui/widgets/ValueModel.h>

#include <funkgui/panel/HeadlessGuiScope.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    namespace ui = fcmp::ui;
    namespace layout = fcmp::ui::layout;
    using funkgui::test::Probe;

    // The strings one scope can show, deduplicated.
    struct Strings
    {
        std::set<std::string> all;
        void add(const char* s)
        {
            if (s != nullptr && s[0] != '\0')
                all.insert(s);
        }
        void add(std::string_view s)
        {
            if (!s.empty())
                all.insert(std::string(s));
        }
    };

    // Codepoints of `s` that the atlas has no glyph for (space excepted: it is an advance, not a glyph).
    int missing(const funkgui::FontAtlasSdf& atlas, const std::string& s, std::set<uint32_t>& which)
    {
        int n = 0;
        for (const char* p = s.c_str(); *p != '\0';)
        {
            const uint32_t cp = funkgui::text::decodeUtf8(p);
            if (cp == ' ')
                continue;
            if (cp == 0 || atlas.glyph(cp) == nullptr)
            {
                ++n;
                which.insert(cp);
            }
        }
        return n;
    }

    void addFormatted(Strings& s, const fcdsp::ParamView& view, fcdsp::Pid pid)
    {
        fcdsp::FormattedValue f;
        fcdsp::formatParts(view, pid, f);
        s.add(f.value);
        s.add(f.unit);
    }

    void addSpec(Strings& s, const fcdsp::ParamSpec& spec)
    {
        s.add(spec.label);
        s.add(spec.tag);
        s.add(spec.reason);
        s.add(spec.brief);
        s.add(spec.display.unit);
        for (const fcdsp::Step& st : spec.steps)
        {
            s.add(st.label);
            s.add(st.text);
        }
    }

    void modeStrings(Strings& s, const fcdsp::ModeEntry& entry, fcmp::probe::FakeFacade& facade)
    {
        const fcdsp::ModeDescriptor& d = *entry.desc;
        s.add(d.name);
        s.add(d.topologyLine);
        s.add(d.specLine);
        for (const fcdsp::InternalSpec& in : d.internals)
        {
            s.add(in.name);
            s.add(in.unit);
        }
        fcdsp::RawParams base;
        for (std::size_t i = 0; i < fcdsp::kNumModeParams; ++i)
            base.v[i] = fcdsp::kHostParams[i].def;
        base.modeSlot = static_cast<uint8_t>(fcdsp::slotOf(entry));
        for (std::size_t i = 0; i < fcdsp::kNumModeParams; ++i)
        {
            const auto pid = static_cast<fcdsp::Pid>(i);
            const fcdsp::ParamEntry& pe = d.params[pid];
            addSpec(s, pe.spec);
            for (const fcdsp::Variant& var : pe.variants)
                addSpec(s, var.spec);
            // formatParts of every step and of the range ends and default, under the base spec's resolution
            std::vector<float> values { pe.spec.lo, pe.spec.hi, pe.spec.defaultPlain, pe.spec.value };
            for (const fcdsp::Step& st : pe.spec.steps)
                values.push_back(st.plain);
            for (const float v : values)
            {
                fcdsp::RawParams raw = base;
                raw.v[i] = fcdsp::legal(pid, v);
                fcdsp::ParamView view;
                fcdsp::resolveView(d, raw, view);
                addFormatted(s, view, pid);
            }
        }
        // What the slots draw at the Mode's defaults (value, unit, sub), and the words.
        ui::FrameState f;
        f.raw = base;
        f.entry = &entry;
        fcdsp::resolve(entry, f.raw, f.res);
        f.eng = f.res.eng;
        for (const layout::SlotPlace& place : layout::kSlots)
        {
            ui::SlotModel m(facade, f, place.pid);
            funkgui::ValueView v;
            m.view(v);
            s.add(v.label);
            s.add(v.tag);
            s.add(v.text.value);
            s.add(v.text.unit);
            s.add(v.text.sub);
            for (int k = 0; k < v.nDetents; ++k)
                s.add(v.detents[k].label);
        }
    }

    void globalStrings(Strings& s)
    {
        for (const layout::SlotPlace& place : layout::kSlots)
        {
            s.add(place.label);
            s.add(place.wordLabel);
        }
        for (const fcdsp::HostParam& h : fcdsp::kHostParams)
        {
            s.add(h.name);
            s.add(h.unit);
            for (int k = 0; h.choices != nullptr && k < h.numSteps; ++k)
                s.add(h.choices[k]);
        }
        // The slot grid's own texts (SlotModel, SlotGrid) and RuleSlider's (FunkGui v0.6.0) spec-line hints.
        for (const char* t : { "CLAMPED FROM", "CLAMPED", "FOLLOWS", "EFF", "DET", "AUTO", "DRY", "\xE2\x88\x92INF", " DB",
                               "AT", "\xE2\x80\xA6", "\xE2\x89\x88", "STORED", "(USED BY OTHER MODES)",
                               "NO SIDECHAIN BUS CONNECTED", "CLICK / RETURN TOGGLES", "ON", "OFF",
                               "EXTENSION \xE2\x80\x94 NOT ON THE ORIGINAL UNIT \xE2\x80\x94 NEUTRAL AT DEFAULT",
                               "STEPS", "\xC2\xB7", "DRAG / WHEEL / ARROWS STEP", "CLICK A STEP", "DBL-CLICK RESET",
                               "END STEPS", "DRAG   SHIFT FINE   DBL-CLICK RESET", "\xE2\x80\x93", "~", "+", "=" })
            s.add(t);
        // Layout.h's fixed legends.
        for (const char* t : layout::readouts::kNames)
            s.add(t);
        for (const layout::controlPath::EventRow& e : layout::controlPath::kEvents)
            s.add(e.label);
        for (const layout::AxisLabel& a : layout::step::kAttackLabels)
            s.add(a.text);
        for (const layout::AxisLabel& a : layout::step::kReleaseLabels)
            s.add(a.text);
        for (const layout::AxisLabel& a : layout::sidechain::kLabels)
            s.add(a.text);
    }

    int report(const funkgui::FontAtlasSdf& atlas, const Strings& s, const std::string& scope)
    {
        int total = 0;
        for (const std::string& str : s.all)
        {
            std::set<uint32_t> which;
            const int n = missing(atlas, str, which);
            if (n == 0)
                continue;
            total += n;
            std::printf("NOTE     %s: '%s' has %d codepoint(s) without a glyph:", scope.c_str(), str.c_str(), n);
            for (const uint32_t cp : which)
                std::printf(" U+%04X", static_cast<unsigned>(cp));
            std::printf("\n");
        }
        return total;
    }
}

namespace fcmp::probe
{
    int uiFont(funkgui::test::Probe& P);         // ui.font's body; ui.font_linux runs it too (ui_font_linux.cpp)
}

FCMP_PROBE(ui, font)
{
    return fcmp::probe::uiFont(P);
}

int fcmp::probe::uiFont(funkgui::test::Probe& P)
{
    const funkgui::HeadlessGuiScope gui;                          // FontService bakes the atlas through JUCE's fonts
    funkgui::FontService& fonts = funkgui::FontService::get();
    const funkgui::FontAtlasSdf& atlas = fonts.atlas();
    P.eq("font.ok", atlas.baked() && fonts.ok() ? 1 : 0, 1);

    std::size_t strings = 0;
    int total = 0;
    {
        Strings g;
        globalStrings(g);
        strings += g.all.size();
        total += report(atlas, g, "global");
    }
    for (const fcdsp::ModeSlot& slot : fcdsp::modeSlots())
    {
        if (slot.entry == nullptr || slot.entry->desc == nullptr)
            continue;
        fcmp::probe::FakeFacade facade(slot.key);                 // ports for the SlotModels (never written)
        Strings m;
        modeStrings(m, *slot.entry, facade);
        strings += m.all.size();
        const int n = report(atlas, m, std::string(slot.key));
        total += n;
        P.eq("font.missing." + std::string(slot.key), n, 0);
    }
    P.ge("font.strings", static_cast<double>(strings), 100.0);
    P.eq("font.missing", total, 0);
    P.hash("font.atlas.hash", fonts.atlasHash());
    return P.finish();
}
