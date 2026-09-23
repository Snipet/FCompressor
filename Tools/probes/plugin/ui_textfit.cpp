// FCMP_PROBE layer=ui name=textfit scope=mode timeout=300
//
// ui.textfit.<key> (03 §3.6, C's G6; U1s): every text a Mode can put into the 21 slots fits its 112 px slot, and the
// detent-label pair-fit rule of 02 §8.3 is what decides labels versus ticks (K1 #24: that rule is the gate; the
// 6-glyph step-label lint is only a printed NOTE). Spec rows only: nothing here is blessable.
//
// A. The descriptor, per slot and per spec (the base and every dependent-list variant, "<pid>.var<k>"):
//    textfit.<pid>.label_px        label (kLabel) + the word (kMicro) + 8 (02 §6.4 label budget), or + the label-row
//                                  tag + 4, <= 112
//    textfit.<pid>.brief_glyphs    ParamSpec::brief <= 18 glyphs (K1 #25), and .brief_px <= 112
//    textfit.<pid>.rule_8_3        a stepped list that is one of 02 §8.3's worked cases decides labels/ticks as the
//                                  table says (RuleSlider::detentLabelsFit, the function the slider draws with)
// B. Every value the slot can show, through the SlotModel views the slider draws (the steps; lo, hi, the default and
//    33 track points of a continuous range; 17 raw points over the host range, so CLAMPED FROM appears; the budget
//    OFF / 20 MS states of LOOKAHEAD), laid out as RuleSlider v0.6.0 lays them out:
//    textfit.<pid>.value_px        value + 6 + unit (kValueP/kLabel primary, kValueS/kMicro secondary) <= 112
//    textfit.<pid>.sub_px          the sub line, after a tag moved beside a word (+ 6), <= 112
//    textfit.<pid>.cut             texts cut with an ellipsis (no shorter label of their own) == 0
// C. The live readouts of the primary row (02 §6.4) over extreme telemetry: textfit.live.sub_px <= 112, .cut == 0.
// D. The rendered panel at dpi 1 and 2, at rest and live (a scripted UiFrame, the pointer resting on RATIO, AUTO on
//    where the Mode has it, MIX at 50 %): every text run the slot grid draws
//    (label, value + unit, sub, detent labels, word) stays inside its slot [x − 2, x + w + 2] (glyph ink boxes: the
//    quad minus the atlas padding) — textfit.render.dpi<d>.<state>.overflow == 0 — and a stepped slot draws detent
//    labels exactly when the pair-fit rule passes — .detent_rule == 0 mismatches.
// With FCMP_UI_PNG_DIR set, D also writes <dir>/panel-live-<key>.png (the live state at dpi 2) for inspection.
#include "ProbeRegistry.h"

#include "FakeFacade.h"

#include "editor/Layout.h"
#include "editor/Panel.h"
#include "editor/SlotModel.h"
#include "editor/SubView.h"

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/telemetry/UiFrame.h"

#include <funkgui/canvas/Prim.h>
#include <funkgui/canvas/PrimList.h>
#include <funkgui/canvas/Tags.h>
#include <funkgui/core/TypeScale.h>
#include <funkgui/panel/HeadlessHost.h>
#include <funkgui/text/FontAtlasSdf.h>
#include <funkgui/text/FontService.h>
#include <funkgui/text/TextFit.h>
#include <funkgui/widgets/RuleSlider.h>
#include <funkgui/widgets/ValueModel.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    namespace ui = fcmp::ui;
    namespace layout = fcmp::ui::layout;
    using funkgui::test::Probe;

    constexpr int   kMaxSettle = 600;
    constexpr float kDt = 1.0f / 60.0f;
    constexpr float kW = layout::kSlotW;                     // 112
    constexpr float kSlack = 2.0f;                           // 02 §8.3: [x − 2, x + w + 2]
    constexpr float kTagGapLabelRow = 4.0f;                  // label, >= 4 px, a right-aligned label-row tag
    constexpr float kWordGap = 8.0f;                         // 02 §6.4 label budget: + width(word) + 8
    constexpr const char* kEllipsis = "\xE2\x80\xA6";

    const funkgui::FontAtlasSdf& atlas() { return funkgui::FontService::get().atlas(); }
    float width(const char* s, const funkgui::TextStyle& st) { return funkgui::text::width(atlas(), s, st); }

    std::string pidName(fcdsp::Pid p) { return fcdsp::kHostParams[fcdsp::idx(p)].id; }

    int glyphs(const char* s)
    {
        int n = 0;
        for (const char* p = s; p != nullptr && *p != '\0'; ++p)
            if ((static_cast<unsigned char>(*p) & 0xC0u) != 0x80u)
                ++n;
        return n;
    }

    bool endsWithEllipsis(const char* s)
    {
        const std::size_t n = std::strlen(s), e = std::strlen(kEllipsis);
        return n >= e && std::strcmp(s + n - e, kEllipsis) == 0;
    }

    fcdsp::RawParams defaults(const fcdsp::ModeEntry& entry)
    {
        fcdsp::RawParams raw;
        for (std::size_t i = 0; i < fcdsp::kNumModeParams; ++i)
            raw.v[i] = fcdsp::kHostParams[i].def;
        raw.modeSlot = static_cast<uint8_t>(fcdsp::slotOf(entry));
        return raw;
    }

    void refresh(ui::FrameState& f, const fcdsp::ModeEntry& entry)
    {
        f.entry = &entry;
        fcdsp::resolve(entry, f.raw, f.res);
        f.eng = f.res.eng;
        if (f.live)
            fcdsp::overlaySmoothed(f.ui, f.eng);
    }

    bool wordVisible(const ui::FrameState& f, const layout::SlotPlace& place)
    {
        if (place.word == fcdsp::kNoPid)
            return false;
        return fcdsp::idx(place.word) >= fcdsp::kNumModeParams
            || f.res.view.p[fcdsp::idx(place.word)].state != fcdsp::SlotState::na;
    }

    // What RuleSlider (v0.6.0) draws from a view, measured.
    struct Measure
    {
        float labelPx = 0, valuePx = 0, subPx = 0;
        bool  cut = false;
    };

    Measure measure(const funkgui::ValueView& v, const layout::SlotPlace& place, bool word)
    {
        Measure m;
        const bool primary = place.row == layout::SlotRow::p;
        const funkgui::TextStyle& vs = primary ? funkgui::type::kValueP : funkgui::type::kValueS;
        const funkgui::TextStyle& us = primary ? funkgui::type::kLabel : funkgui::type::kMicro;
        const bool na = v.state == funkgui::ValueState::na;
        const bool hasTag = v.tag != nullptr && v.tag[0] != '\0' && !na;
        m.labelPx = width(v.label, funkgui::type::kLabel);
        if (word)
            m.labelPx += width(place.wordLabel, funkgui::type::kMicro) + kWordGap;
        else if (hasTag)
            m.labelPx += width(v.tag, funkgui::type::kMicro) + kTagGapLabelRow;
        if (!na)
            m.valuePx = width(v.text.value, vs)
                      + (v.text.unit[0] != '\0' ? ui::SlotModel::kUnitGap + width(v.text.unit, us) : 0.0f);
        const bool labels = v.state == funkgui::ValueState::stepped
                         && funkgui::RuleSlider::detentLabelsFit(atlas(), v.detents, v.nDetents, kW);
        if (!na && !labels && v.text.sub[0] != '\0')
            m.subPx = (hasTag && word ? width(v.tag, funkgui::type::kMicro) + ui::SlotModel::kTagGap : 0.0f)
                    + width(v.text.sub, funkgui::type::kMicro);
        m.cut = endsWithEllipsis(v.text.value) || endsWithEllipsis(v.text.sub);
        return m;
    }

    // ---- A: 02 §8.3's worked cases --------------------------------------------------------------------------------------

    struct WorkedCase { std::string_view mode; fcdsp::Pid pid; std::vector<std::string> labels; bool labelsDrawn; };

    std::vector<std::string> minusRange(int from, int to)          // "−20" … "0" in 1 dB (Mu 67's INPUT)
    {
        std::vector<std::string> out;
        for (int d = from; d <= to; ++d)
            out.push_back(d < 0 ? "\xE2\x88\x92" + std::to_string(-d) : std::to_string(d));
        return out;
    }

    std::vector<WorkedCase> workedCases()
    {
        using fcdsp::Pid;
        return {
            { "bus-g", Pid::ratio, { "2", "4", "10" }, true },
            { "fet-76", Pid::ratio, { "4", "8", "12", "20", "ALL" }, true },
            { "bus-25", Pid::ratio, { "1.5", "2", "3", "4", "6", "10", "\xE2\x88\x9E" }, true },
            { "bus-g", Pid::atk, { ".1", ".3", "1", "3", "10", "30" }, true },
            { "bus-25", Pid::atk, { ".03", ".1", ".3", "1", "3", "10", "30" }, true },
            { "bus-g", Pid::rel, { ".1", ".3", ".6", "1.2", "AUTO" }, true },
            { "opto-2a", Pid::ratio, { "COMP", "LIMIT" }, true },
            { "clean", Pid::voice, { "OFF", "TUBE", "DIODE", "BRIGHT" }, false },
            { "clean", Pid::det, { "PEAK", "RMS", "PK+RMS" }, true },
            { "clean", Pid::stmode, { "ST", "M/S", "MID", "SIDE", "M>S", "S>M" }, false },
            { "mu-67", Pid::schpf, { "OFF", "50", "100", "200", "350" }, true },
            { "mu-67", Pid::drive, minusRange(-20, 0), false },
            { "diode-609", Pid::rel, { ".1", ".4", ".8", "1.5", "A1", "A2" }, true },
            { "bus-25", Pid::link, { "IND", "50", "60", "70", "80", "90", "100" }, true },
        };
    }

    void descriptorLint(Probe& P, const fcdsp::ModeEntry& entry)
    {
        const fcdsp::ModeDescriptor& d = *entry.desc;
        const std::vector<WorkedCase> cases = workedCases();
        int worked = 0;
        for (const layout::SlotPlace& place : layout::kSlots)
        {
            const fcdsp::ParamEntry& pe = d.params[place.pid];
            std::vector<const fcdsp::ParamSpec*> specs { &pe.spec };
            for (const fcdsp::Variant& var : pe.variants)
                specs.push_back(&var.spec);
            for (std::size_t k = 0; k < specs.size(); ++k)
            {
                const fcdsp::ParamSpec& s = *specs[k];
                const std::string key = "textfit." + pidName(place.pid) + (k == 0 ? "" : ".var" + std::to_string(k));
                if (s.brief != nullptr)
                {
                    P.le(key + ".brief_glyphs", glyphs(s.brief), 18);
                    P.le(key + ".brief_px", width(s.brief, funkgui::type::kMicro), kW);
                }
                if (s.kind == fcdsp::Kind::stepped)
                {
                    for (const fcdsp::Step& st : s.steps)
                        if (glyphs(st.label) > 6)
                            std::printf("NOTE     %s: step label '%s' has %d glyphs (> 6: the registry's style warning; "
                                        "the pair-fit rule is the gate, K1 #24)\n", key.c_str(), st.label,
                                        glyphs(st.label));
                    for (const WorkedCase& wc : cases)
                    {
                        if (wc.mode != d.key || wc.pid != place.pid || wc.labels.size() != s.steps.size())
                            continue;
                        bool same = true;
                        for (std::size_t i = 0; i < s.steps.size(); ++i)
                            same = same && wc.labels[i] == s.steps[i].label;
                        if (!same)
                            continue;
                        std::vector<funkgui::Detent> det;
                        for (const fcdsp::Step& st : s.steps)
                            det.push_back({ 0.0f, st.label, st.label });
                        const bool fit = funkgui::RuleSlider::detentLabelsFit(atlas(), det.data(),
                                                                              static_cast<int>(det.size()), kW);
                        P.eq(key + ".rule_8_3", fit ? 1 : 0, wc.labelsDrawn ? 1 : 0);
                        ++worked;
                    }
                }
            }
        }
        std::printf("NOTE     textfit: %d of 02 §8.3's worked cases match this Mode's step lists\n", worked);
    }

    // ---- B: every value through the SlotModel views -------------------------------------------------------------------

    struct Worst
    {
        float label = 0, value = 0, sub = 0;
        int   cuts = 0, views = 0;
        std::string valueText, subText;

        void add(const Measure& m, const funkgui::ValueView& v)
        {
            ++views;
            label = std::max(label, m.labelPx);
            if (m.valuePx > value)
            {
                value = m.valuePx;
                valueText = std::string(v.text.value) + (v.text.unit[0] != '\0' ? std::string(" ") + v.text.unit : "");
            }
            if (m.subPx > sub)
            {
                sub = m.subPx;
                subText = v.text.sub;
            }
            if (m.cut)
            {
                ++cuts;
                std::printf("NOTE     cut: '%s' / '%s'\n", v.text.value, v.text.sub);
            }
        }
    };

    std::vector<float> sweepValues(fcdsp::Pid pid, const fcdsp::ParamSpec& s)
    {
        std::vector<float> out;
        for (const fcdsp::Step& st : s.steps)
            out.push_back(st.plain);
        if (s.lo < s.hi)
        {
            out.push_back(s.lo);
            out.push_back(s.hi);
            out.push_back(s.defaultPlain);
            const float n0 = fcdsp::toNorm(pid, s.lo), n1 = fcdsp::toNorm(pid, s.hi);
            for (int i = 0; i <= 32; ++i)
                out.push_back(fcdsp::toPlain(pid, n0 + (n1 - n0) * static_cast<float>(i) / 32.0f));
        }
        for (int i = 0; i <= 16; ++i)                              // the host range: raw values outside the Mode's
            out.push_back(fcdsp::toPlain(pid, static_cast<float>(i) / 16.0f));
        out.push_back(s.value);
        return out;
    }

    void valueSweep(Probe& P, const fcdsp::ModeEntry& entry, fcmp::probe::FakeFacade& facade)
    {
        const fcdsp::ModeDescriptor& d = *entry.desc;
        for (const layout::SlotPlace& place : layout::kSlots)
        {
            const fcdsp::ParamEntry& pe = d.params[place.pid];
            struct Case { const fcdsp::ParamSpec* spec; int driverStep; };
            std::vector<Case> cases { { &pe.spec, -1 } };
            for (const fcdsp::Variant& var : pe.variants)
                cases.push_back({ &var.spec, var.driverStep });
            for (std::size_t k = 0; k < cases.size(); ++k)
            {
                const std::string key = "textfit." + pidName(place.pid) + (k == 0 ? "" : ".var" + std::to_string(k));
                Worst worst;
                for (const fcdsp::LookaheadBudget budget : { fcdsp::LookaheadBudget::off, fcdsp::LookaheadBudget::ms20 })
                {
                    if (budget != fcdsp::LookaheadBudget::off && place.pid != fcdsp::Pid::look)
                        continue;
                    ui::FrameState f;
                    f.raw = defaults(entry);
                    f.raw.budget = budget;
                    if (pe.driver != fcdsp::kNoPid)
                    {
                        // Select the case: a variant is the driver on its step `driverStep` (drivers resolve first,
                        // 01 §4.4); the base spec is the driver on a step no variant names.
                        const fcdsp::ParamSpec& ds = d.params[pe.driver].spec;
                        int step = cases[k].driverStep;
                        for (int i = 0; step < 0 && i < static_cast<int>(ds.steps.size()); ++i)
                        {
                            bool named = false;
                            for (const fcdsp::Variant& var : pe.variants)
                                named = named || var.driverStep == i;
                            if (!named)
                                step = i;
                        }
                        if (step >= 0 && static_cast<std::size_t>(step) < ds.steps.size())
                            f.raw[pe.driver] = ds.steps[static_cast<std::size_t>(step)].plain;
                    }
                    ui::SlotModel model(facade, f, place.pid);
                    for (const float value : sweepValues(place.pid, *cases[k].spec))
                    {
                        f.raw[place.pid] = fcdsp::legal(place.pid, value);
                        refresh(f, entry);
                        // Another case of this entry is active: skip. A spec that is none of the entry's own is the
                        // resolver's (LOOKAHEAD locked by the budget, 01 §4.4) and is measured with this case.
                        const fcdsp::ParamSpec* active = f.res.view.spec[fcdsp::idx(place.pid)];
                        bool own = active == &pe.spec;
                        for (const fcdsp::Variant& var : pe.variants)
                            own = own || active == &var.spec;
                        if (own && active != cases[k].spec)
                            continue;
                        funkgui::ValueView v;
                        model.view(v);
                        worst.add(measure(v, place, wordVisible(f, place)), v);
                    }
                }
                if (worst.views == 0)
                {
                    P.harnessError(key + ": no raw state selects this spec (variant driver not found)");
                    continue;
                }
                if (worst.value > kW || worst.sub > kW)
                    std::printf("NOTE     %s: widest value '%s' %.1f px, widest sub '%s' %.1f px\n", key.c_str(),
                                worst.valueText.c_str(), static_cast<double>(worst.value), worst.subText.c_str(),
                                static_cast<double>(worst.sub));
                P.le(key + ".label_px", worst.label, kW);
                P.le(key + ".value_px", worst.value, kW);
                P.le(key + ".sub_px", worst.sub, kW);
                P.eq(key + ".cut", worst.cuts, 0);
            }
        }
    }

    // ---- C: live readouts ------------------------------------------------------------------------------------------------

    fcdsp::UiFrame liveFrame(const ui::FrameState& f, float xDb, float grDb, float atkMs, float relMs, float autoDb)
    {
        fcdsp::UiFrame u = fcmp::probe::FakeFacade::quietFrame(f.res.view.slot, f.res.eng);
        u.flags |= fcdsp::kUiLive;
        for (int l = 0; l < 2; ++l)
        {
            u.inPeakDb[l] = u.inRmsDb[l] = xDb;
            u.outPeakDb[l] = u.outRmsDb[l] = xDb - grDb;
            u.curveXDb[l] = xDb;
            u.targetGrDb[l] = u.appliedGrDb[l] = u.blockMaxGrDb[l] = grDb;
            u.attackNowMs[l] = atkMs;
            u.releaseNowMs[l] = relMs;
        }
        u.makeupEffDb = f.res.eng.makeupDb + autoDb;
        return u;
    }

    void liveSweep(Probe& P, const fcdsp::ModeEntry& entry, fcmp::probe::FakeFacade& facade)
    {
        Worst worst;
        ui::FrameState f;
        f.raw = defaults(entry);
        if (entry.desc->params[fcdsp::Pid::automu].spec.kind != fcdsp::Kind::notApplicable)
            f.raw[fcdsp::Pid::automu] = 1.0f;                    // AUTO on where the Mode has it: the AUTO readout
        std::vector<std::unique_ptr<ui::SlotModel>> models;
        for (const layout::SlotPlace& place : layout::kSlots)
            models.push_back(std::make_unique<ui::SlotModel>(facade, f, place.pid));
        for (const float mix : { 0.0f, 0.5f, 1.0f, 2.0f })
            for (const float x : { -69.95f, -12.34f, 5.95f })
                for (const float gr : { 0.0f, 12.34f, 59.95f })
                    for (const float t : { 0.0123f, 99.96f, 29999.0f })
                        for (const float autoDb : { -23.95f, 35.95f })
                        {
                            f.raw[fcdsp::Pid::mix] = mix;
                            f.live = false;
                            refresh(f, entry);
                            f.ui = liveFrame(f, x, gr, t, t, autoDb);
                            f.live = f.fresh = f.hasFrame = f.overlaid = true;
                            refresh(f, entry);
                            for (std::size_t i = 0; i < layout::kSlots.size(); ++i)
                            {
                                if (layout::kSlots[i].row != layout::SlotRow::p)
                                    continue;
                                funkgui::ValueView v;
                                models[i]->view(v);
                                worst.add(measure(v, layout::kSlots[i], wordVisible(f, layout::kSlots[i])), v);
                            }
                        }
        std::printf("NOTE     textfit.live: widest sub '%s' %.1f px over %d views\n", worst.subText.c_str(),
                    static_cast<double>(worst.sub), worst.views);
        P.le("textfit.live.value_px", worst.value, kW);
        P.le("textfit.live.sub_px", worst.sub, kW);
        P.eq("textfit.live.cut", worst.cuts, 0);
    }

    // ---- D: the rendered frame -------------------------------------------------------------------------------------------

    bool isText(const funkgui::Prim& p) { return static_cast<int>(p.d2[2] + 0.5f) == static_cast<int>(funkgui::PrimKind::text); }

    bool slotTag(funkgui::Tag t)
    {
        namespace tg = funkgui::tags;
        return t == tg::slotLabel || t == tg::slotValue || t == tg::slotSub || t == tg::detentLabel || t == tg::word;
    }

    // A glyph's ink box: its quad minus the atlas padding (kSpread + 1 texels at the run's scale). The scale is read
    // back from the quad's AA term d1[1] = 1 / (dpi · 2 · kSpread · scale) (Canvas::text).
    struct Box { float x0, x1, cy; };
    Box inkBox(const funkgui::Prim& p, float dpi)
    {
        const float aa = p.d1[1];
        const float scale = aa > 0.0f ? 1.0f / (aa * dpi * 2.0f * static_cast<float>(funkgui::FontAtlasSdf::kSpread)) : 0.0f;
        const float pad = static_cast<float>(funkgui::FontAtlasSdf::kSpread + 1) * scale;
        return { p.x0 + pad, p.x1 - pad, 0.5f * (p.y0 + p.y1) };
    }

    struct Run { funkgui::Tag tag; float x0, x1, cy; };

    // Consecutive text glyphs of one tag, left to right on one line: one text() call, or a value and its unit, or the
    // detent labels of one slot (anything else in between ends the run).
    std::vector<Run> slotRuns(const funkgui::PrimList& pl, float dpi)
    {
        std::vector<Run> runs;
        bool open = false;
        for (const funkgui::Prim& p : pl.prims)
        {
            if (!isText(p) || !slotTag(p.tag))
            {
                open = false;
                continue;
            }
            const Box b = inkBox(p, dpi);
            if (!layout::kSlotGrid.contains({ 0.5f * (b.x0 + b.x1), b.cy }))
            {
                open = false;
                continue;
            }
            if (open && runs.back().tag == p.tag && b.x0 >= runs.back().x0 - 1.0f && std::fabs(b.cy - runs.back().cy) < 12.0f
                && b.x0 - runs.back().x1 < 40.0f)
            {
                runs.back().x1 = std::max(runs.back().x1, b.x1);
                continue;
            }
            runs.push_back({ p.tag, b.x0, b.x1, b.cy });
            open = true;
        }
        return runs;
    }

    // The slot a run belongs to: the column whose [x − 6, x + w + 6) holds the run's start, the row whose hit band holds
    // its centre line; -1 when none (a run starting in a gutter is attributed to the column it starts nearest).
    int ownerSlot(const Run& r)
    {
        int best = -1;
        float bestD = 1.0e9f;
        for (std::size_t i = 0; i < layout::kSlots.size(); ++i)
        {
            const funkgui::SlotGeom g = layout::slotGeom(layout::kSlots[i]);
            const funkgui::Rect hit = g.hit();
            if (r.cy < hit.y || r.cy >= hit.bottom())
                continue;
            const float d = r.x0 < g.x ? g.x - r.x0 : (r.x0 > g.x + g.w ? r.x0 - (g.x + g.w) : 0.0f);
            if (d < bestD)
            {
                bestD = d;
                best = static_cast<int>(i);
            }
        }
        return best;
    }

    void renderCheck(Probe& P, const fcdsp::ModeEntry& entry)
    {
        const char* pngDir = std::getenv("FCMP_UI_PNG_DIR");
        for (const int dpi : { 1, 2 })
            for (const bool live : { false, true })
            {
                const std::string key = "textfit.render.dpi" + std::to_string(dpi) + (live ? ".live" : ".rest");
                fcmp::probe::FakeFacade facade(entry.desc->key);
                if (live)
                {
                    if (entry.desc->params[fcdsp::Pid::automu].spec.kind != fcdsp::Kind::notApplicable)
                        facade.setPlain(fcdsp::Pid::automu, 1.0f);
                    facade.setPlain(fcdsp::Pid::mix, 0.5f);
                }
                ui::Panel panel(facade, { true, true, false });
                funkgui::HeadlessHost host(panel, 0, static_cast<float>(dpi));
                const int frames = host.settle(kMaxSettle, kDt);
                if (frames > kMaxSettle)
                {
                    P.harnessError(key + ": the panel did not settle within 600 frames");
                    continue;
                }
                if (live)
                {
                    // A live stream keeps the panel at full rate until it goes stale, so publish once the rest state
                    // has settled and draw the next frame: the sliders re-read their views in that tick.
                    // The pointer rests on RATIO's value, so the frame also carries hover chrome and the footer's
                    // spec line (the hover eases in over 90 ms; the stream stays fresh for 0.5 s).
                    const ui::FrameState& f = panel.context().frame;
                    facade.publish(liveFrame(f, -12.0f, 4.2f, f.res.eng.atkTauMs * 0.8f, f.res.eng.relTauMs * 1.7f, 4.1f));
                    const funkgui::SlotGeom ratio = layout::slotGeom(*layout::slotOf(fcdsp::Pid::ratio));
                    host.move(ratio.x + 20.0f, ratio.valueTop() + 8.0f);
                    host.tick(12, kDt);
                    P.eq(key + ".is_live", panel.context().frame.live ? 1 : 0, 1);
                }
                const funkgui::PrimList& pl = host.draw();
                const std::vector<Run> runs = slotRuns(pl, static_cast<float>(dpi));
                int overflow = 0;
                for (const Run& r : runs)
                {
                    const int i = ownerSlot(r);
                    if (i < 0)
                        continue;
                    const funkgui::SlotGeom g = layout::slotGeom(layout::kSlots[static_cast<std::size_t>(i)]);
                    if (r.x0 < g.x - kSlack || r.x1 > g.x + g.w + kSlack)
                    {
                        ++overflow;
                        std::printf("NOTE     %s: %s text run %.1f…%.1f outside slot %s [%.0f, %.0f]\n", key.c_str(),
                                    funkgui::tagName(r.tag) != nullptr ? funkgui::tagName(r.tag) : "?",
                                    static_cast<double>(r.x0), static_cast<double>(r.x1),
                                    pidName(layout::kSlots[static_cast<std::size_t>(i)].pid).c_str(),
                                    static_cast<double>(g.x), static_cast<double>(g.x + g.w));
                    }
                }
                int mismatch = 0;
                for (std::size_t i = 0; i < layout::kSlots.size(); ++i)
                {
                    funkgui::ValueView v;
                    panel.context().slot(layout::kSlots[i].pid).view(v);
                    if (v.state != funkgui::ValueState::stepped)
                        continue;
                    const bool fit = funkgui::RuleSlider::detentLabelsFit(atlas(), v.detents, v.nDetents, kW);
                    bool drawn = false;
                    for (const Run& r : runs)
                        if (r.tag == funkgui::tags::detentLabel && ownerSlot(r) == static_cast<int>(i))
                            drawn = true;
                    if (drawn != fit)
                    {
                        ++mismatch;
                        std::printf("NOTE     %s: %s draws detent labels %s but the pair-fit rule says %s\n", key.c_str(),
                                    pidName(layout::kSlots[i].pid).c_str(), drawn ? "yes" : "no", fit ? "yes" : "no");
                    }
                }
                P.ge(key + ".runs", static_cast<double>(runs.size()), 42.0);   // at least a label and a value per slot
                P.eq(key + ".overflow", overflow, 0);
                P.eq(key + ".detent_rule", mismatch, 0);
                P.eq(key + ".glyphs_missing", static_cast<int64_t>(pl.missingGlyphs), 0);
                if (live && dpi == 2 && pngDir != nullptr && pngDir[0] != '\0')
                {
                    const std::string path = std::string(pngDir) + "/panel-live-" + std::string(entry.desc->key) + ".png";
                    std::printf("NOTE     %s %s\n", host.writePng(path.c_str(), 2) ? "wrote" : "could not write",
                                path.c_str());
                }
            }
    }
}

FCMP_PROBE(ui, textfit)
{
    const juce::ScopedJuceInitialiser_GUI juceInit;               // FontService bakes the atlas through JUCE's fonts
    const fcdsp::ModeEntry* entry = fcdsp::byKey(C.key);
    if (entry == nullptr || entry->desc == nullptr)
    {
        P.harnessError("ui.textfit: unknown Mode '" + std::string(C.key) + "'");
        return P.finish();
    }
    P.eq("textfit.font.ok", atlas().baked() && funkgui::FontService::get().ok() ? 1 : 0, 1);
    fcmp::probe::FakeFacade facade(C.key);                        // ports for the SlotModels (never written here)
    descriptorLint(P, *entry);
    valueSweep(P, *entry, facade);
    liveSweep(P, *entry, facade);
    renderCheck(P, *entry);
    P.eq("textfit.facade.writes", static_cast<int64_t>(facade.writes().size()), 0);
    return P.finish();
}
