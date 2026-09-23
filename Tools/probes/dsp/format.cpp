// FCMP_PROBE layer=dsp name=format scope=global timeout=60
//
// dsp.format (S1 F2; SPRINTS S1.2; 01 §4.6; K1 #15; K2 #25a-b): host and UI value text on every spec of Clean (01 §10.3)
// and of dsp.resolve's synthetic descriptor (every Kind, DisplayMaps, variants, the budget lock).
//   Spec rows: the text round trip formatHost -> parseHost -> formatHost is a fixed point for every spec at 65 host
//   points, every step and every range edge (a stepped value parses back to the same step); n/a prints U+2013 and is
//   never parsed; locked prints "(...)", derived "(= ...)", program "~..."; no ASCII '-' in any text (minus is U+2212);
//   the value never starts with the slot label (K2 #25a); units come from the fixed set; both '-' and U+2212 parse;
//   the design's own examples ("4:1", "250 µS", "(10 MS)", "(= 0.8 MS)", "~60 MS", "–"); formatValue truncates at a
//   UTF-8 boundary.
//   Golden rows (text): the synthetic descriptor's text at its defaults, each step and each range edge ("format.synth.*",
//   spaces as '_'), so a change to the universal formatting rule shows as drift. Arch-neutral: no libm in the text
//   path (llround only). Clean's rows are spec-only, so F9's Clean edits never rewrite this global golden (K3 #17).
#include "ProbeRegistry.h"

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/params/Setup.h"
#include "fcdsp/params/Text.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace fcdsp::modes
{
    extern const ModeDescriptor kClean;         // modes/clean/CleanDesc.cpp
}
namespace fcmp::probe::synth
{
    extern const fcdsp::ModeDescriptor kSynth;  // Tools/probes/dsp/resolve.cpp
}

namespace
{
    using namespace fcdsp;

    constexpr std::string_view kMinus = "−", kEnDash = "–";

    const char* pidName(Pid p) { return kHostParams[idx(p)].id; }

    ModeEntry entryOf(const ModeDescriptor& d) { return { &d, nullptr, 0, 0, nullptr, nullptr, nullptr }; }

    RawParams defaultsOf(const ModeDescriptor& d, LookaheadBudget b)
    {
        RawParams r;
        for (std::size_t i = 0; i < kNumModeParams; ++i)
            r.v[i] = kHostParams[i].def;
        r.budget = b;
        modeDefaults(d, r);
        return r;
    }

    std::string host(const ModeEntry& e, const RawParams& raw, Pid pid, float plain)
    {
        char buf[64];
        const int n = formatHost(e, raw, pid, plain, buf, sizeof buf);
        return std::string(buf, static_cast<std::size_t>(n > 0 ? n : 0));
    }

    std::string key(std::string_view a, std::string_view b = {}, std::string_view c = {}, std::string_view d = {})
    {
        std::string s(a);
        for (const std::string_view part : { b, c, d })
            if (!part.empty())
                s.append(".").append(part);
        return s;
    }

    int b2i(bool b) { return b ? 1 : 0; }

    // A spec row comparing two strings; the strings are printed when they differ.
    void textIs(funkgui::test::Probe& P, const std::string& k, const std::string& got, std::string_view want)
    {
        if (got != want)
            std::printf("NOTE     %s  got \"%s\"  want \"%.*s\"\n", k.c_str(), got.c_str(), static_cast<int>(want.size()),
                        want.data());
        P.eq(k, b2i(got == want), 1);
    }

    void formatIs(funkgui::test::Probe& P, const char* k, const ModeEntry& e, const RawParams& raw, Pid pid, float plain,
                  std::string_view want)
    {
        textIs(P, key("text", k), host(e, raw, pid, plain), want);
    }

    void parsesTo(funkgui::test::Probe& P, const char* k, const ModeEntry& e, const RawParams& raw, Pid pid,
                  std::string_view text, float want)
    {
        float got = -12345.f;
        const bool ok = parseHost(e, raw, pid, text, got);
        if (!ok || got != want)
            std::printf("NOTE     parse.%s  \"%.*s\" -> ok %d, %.9g (want %.9g)\n", k, static_cast<int>(text.size()),
                        text.data(), ok ? 1 : 0, static_cast<double>(got), static_cast<double>(want));
        P.eq(key("parse", k), b2i(ok && got == want), 1);
    }

    void refused(funkgui::test::Probe& P, const char* k, const ModeEntry& e, const RawParams& raw, Pid pid,
                 std::string_view text)
    {
        float got = 0.f;
        P.eq(key("parse.refused", k), b2i(!parseHost(e, raw, pid, text, got)), 1);
    }

    bool allowedUnit(std::string_view u)
    {
        return u.empty() || u == "DB" || u == "DBU" || u == "DB/OCT" || u == "MS" || u == "S" || u == "µS" || u == "HZ"
            || u == "%";
    }

    // ---- the round trip over one descriptor ----------------------------------------------------------------------------
    struct Counts
    {
        long texts = 0, parseFail = 0, fixedPoint = 0, stepMismatch = 0, asciiMinus = 0, labelInValue = 0, badUnit = 0,
             naText = 0, naParsed = 0, prefix = 0, emptySpoken = 0, lengthMismatch = 0;
        int notes = 0;
    };

    void note(Counts& c, const char* what, const ModeDescriptor& d, Pid pid, float x, const std::string& text)
    {
        if (c.notes++ < 20)
            std::printf("NOTE     %s: %s %s plain %.9g text \"%s\"\n", what, std::string(d.key).c_str(), pidName(pid),
                        static_cast<double>(x), text.c_str());
    }

    void roundTrip(const ModeDescriptor& d, const RawParams& raw, Pid pid, float x, Counts& c)
    {
        const ModeEntry e = entryOf(d);
        const std::size_t i = idx(pid);
        RawParams at = raw;
        at.v[i] = x;
        ParamView v;
        resolveView(d, at, v);
        const ResolvedParam& r = v.p[i];
        const ParamSpec& spec = *v.spec[i];

        char buf[64];
        const int n = formatHost(e, raw, pid, x, buf, sizeof buf);
        const std::string text(buf);
        ++c.texts;
        if (n != static_cast<int>(text.size()) || text.empty())
        {
            ++c.lengthMismatch, note(c, "length", d, pid, x, text);
            return;
        }
        if (text.find('-') != std::string::npos)
            ++c.asciiMinus, note(c, "ascii minus", d, pid, x, text);

        FormattedValue f;
        formatParts(v, pid, f);
        if (!allowedUnit(f.unit))
            ++c.badUnit, note(c, "unit", d, pid, x, text);
        if (f.spoken[0] == '\0')
            ++c.emptySpoken, note(c, "spoken", d, pid, x, text);
        if (spec.label != nullptr)
        {
            const std::size_t len = std::strlen(spec.label);
            if (std::strncmp(f.value, spec.label, len) == 0 && (f.value[len] == ' ' || f.value[len] == '\0'))
                ++c.labelInValue, note(c, "label in value", d, pid, x, text);
        }

        if (r.state == SlotState::na)
        {
            if (text != kEnDash)
                ++c.naText, note(c, "n/a text", d, pid, x, text);
            float p = 0.f;
            if (parseHost(e, raw, pid, text, p))
                ++c.naParsed, note(c, "n/a parsed", d, pid, x, text);
            return;
        }

        const bool program = (spec.flags & kFlagProgram) != 0;
        const bool prefixOk = r.state == SlotState::locked  ? (program ? text.front() == '~' : text.front() == '(' && text.back() == ')')
                            : r.state == SlotState::derived ? (program ? text.front() == '~' : text.rfind("(= ", 0) == 0 && text.back() == ')')
                                                            : text.front() != '(' && text.front() != '~';
        if (!prefixOk)
            ++c.prefix, note(c, "prefix", d, pid, x, text);

        float p = 0.f;
        if (!parseHost(e, raw, pid, text, p))
        {
            ++c.parseFail, note(c, "parse", d, pid, x, text);
            return;
        }
        const std::string again = host(e, raw, pid, p);
        if (again != text)
            ++c.fixedPoint, note(c, ("fixed point -> \"" + again + "\"").c_str(), d, pid, x, text);
        if (r.step >= 0)
        {
            RawParams back = raw;
            back.v[i] = p;
            ParamView bv;
            resolveView(d, back, bv);
            if (bv.p[i].step != r.step)
                ++c.stepMismatch, note(c, "step", d, pid, x, text);
        }
    }

    void roundTrips(funkgui::test::Probe& P, const ModeDescriptor& d)
    {
        Counts c;
        const std::string dk(d.key);
        for (const LookaheadBudget b : { LookaheadBudget::off, LookaheadBudget::ms5, LookaheadBudget::ms20 })
            for (const Pid pid : kResolveOrder)
            {
                const ParamEntry& e = d.params[pid];
                std::vector<RawParams> contexts{ defaultsOf(d, b) };
                if (e.driver != kNoPid)                                    // every step of a dependent list's driver
                    for (int s = 0; s < stepCount(d.params[e.driver].spec); ++s)
                    {
                        RawParams r = defaultsOf(d, b);
                        r[e.driver] = stepPlain(d.params[e.driver].spec, s);
                        contexts.push_back(r);
                    }
                for (const RawParams& raw : contexts)
                {
                    std::vector<float> xs;
                    for (int n = 0; n <= 64; ++n)
                        xs.push_back(toPlain(pid, static_cast<float>(n) / 64.f));
                    std::vector<const ParamSpec*> specs{ &e.spec };
                    for (const Variant& va : e.variants)
                        specs.push_back(&va.spec);
                    for (const ParamSpec* s : specs)
                    {
                        for (const Step& st : s->steps)
                            xs.push_back(st.plain);
                        xs.insert(xs.end(), { s->lo, s->hi, s->defaultPlain, s->value });
                    }
                    for (const float x : xs)
                        roundTrip(d, raw, pid, legal(pid, x), c);
                }
            }
        P.ge(key("format", dk, "texts"), static_cast<double>(c.texts), 22.0 * 65.0 * 3.0);
        P.eq(key("format", dk, "parse_failures"), c.parseFail, 0);
        P.eq(key("format", dk, "fixed_point_failures"), c.fixedPoint, 0);
        P.eq(key("format", dk, "step_mismatches"), c.stepMismatch, 0);
        P.eq(key("format", dk, "ascii_minus"), c.asciiMinus, 0);
        P.eq(key("format", dk, "label_in_value"), c.labelInValue, 0);
        P.eq(key("format", dk, "bad_units"), c.badUnit, 0);
        P.eq(key("format", dk, "na_text_mismatches"), c.naText, 0);
        P.eq(key("format", dk, "na_parsed"), c.naParsed, 0);
        P.eq(key("format", dk, "prefix_mismatches"), c.prefix, 0);
        P.eq(key("format", dk, "empty_spoken"), c.emptySpoken, 0);
        P.eq(key("format", dk, "length_mismatches"), c.lengthMismatch, 0);
    }

    // Golden text rows: spaces become '_' (golden values carry no whitespace).
    void goldenText(funkgui::test::Probe& P, const std::string& k, std::string text)
    {
        for (char& ch : text)
            if (ch == ' ')
                ch = '_';
        P.text(k, text);
    }
} // namespace

FCMP_PROBE(dsp, format)
{
    using fcmp::probe::synth::kSynth;
    const ModeDescriptor& clean = fcdsp::modes::kClean;
    const ModeEntry ce = entryOf(clean), se = entryOf(kSynth);
    const RawParams cOff = defaultsOf(clean, LookaheadBudget::off), c5 = defaultsOf(clean, LookaheadBudget::ms5);
    const RawParams sOff = defaultsOf(kSynth, LookaheadBudget::off), s5 = defaultsOf(kSynth, LookaheadBudget::ms5);

    // ---- 1. the round trip on every spec of both descriptors ----------------------------------------------------------
    roundTrips(P, clean);
    roundTrips(P, kSynth);

    // ---- 2. the design's texts (01 §3.1, §4.6; K1 #15) ------------------------------------------------------------------
    formatIs(P, "clean.thr.default", ce, cOff, Pid::thr, -18.f, "−18.0 DB");
    formatIs(P, "clean.thr.zero", ce, cOff, Pid::thr, -0.01f, "0.0 DB");              // no "−0.0"
    formatIs(P, "clean.ratio.4", ce, cOff, Pid::ratio, 0.75f, "4.0:1");
    formatIs(P, "clean.ratio.1", ce, cOff, Pid::ratio, 0.f, "1.0:1");
    formatIs(P, "clean.ratio.20", ce, cOff, Pid::ratio, 0.95f, "20:1");
    formatIs(P, "clean.ratio.inf", ce, cOff, Pid::ratio, 1.f, "∞:1");
    formatIs(P, "clean.knee", ce, cOff, Pid::knee, 6.f, "6.0 DB");
    formatIs(P, "clean.range.off", ce, cOff, Pid::range, 60.f, "OFF");
    formatIs(P, "clean.range.30", ce, cOff, Pid::range, 30.f, "30.0 DB");
    formatIs(P, "clean.atk.10ms", ce, cOff, Pid::atk, 10.f, "10 MS");
    formatIs(P, "clean.atk.250us", ce, cOff, Pid::atk, 0.25f, "250 µS");
    formatIs(P, "clean.atk.5us", ce, cOff, Pid::atk, 0.005f, "5 µS");
    formatIs(P, "clean.atk.0p8ms", ce, cOff, Pid::atk, 0.8f, "800 µS");
    formatIs(P, "clean.atk.1p5ms", ce, cOff, Pid::atk, 1.5f, "1.5 MS");
    formatIs(P, "clean.rel.200ms", ce, cOff, Pid::rel, 200.f, "200 MS");
    formatIs(P, "clean.rel.1p2s", ce, cOff, Pid::rel, 1200.f, "1.2 S");
    formatIs(P, "clean.rel.999p9ms_rounds_to_1s", ce, cOff, Pid::rel, 999.9f, "1 S");
    formatIs(P, "clean.tmode.man", ce, cOff, Pid::tmode, 0.f, "MANUAL");
    formatIs(P, "clean.tmode.auto", ce, cOff, Pid::tmode, 1.f, "AUTO RELEASE");
    formatIs(P, "clean.hold.zero", ce, cOff, Pid::hold, 0.f, "0 MS");
    formatIs(P, "clean.look.budget_off", ce, cOff, Pid::look, 10.f, "(0 MS)");
    formatIs(P, "clean.look.budget_5", ce, c5, Pid::look, 10.f, "5 MS");
    formatIs(P, "clean.det.pk_rms", ce, cOff, Pid::det, 2.f, "PEAK + RMS");
    formatIs(P, "clean.det.index_above_list", ce, cOff, Pid::det, 7.f, "PEAK + RMS");   // indices >= n: the last entry
    formatIs(P, "clean.schpf.off", ce, cOff, Pid::schpf, 0.f, "OFF");
    formatIs(P, "clean.schpf.80", ce, cOff, Pid::schpf, 80.f, "80 HZ");
    formatIs(P, "clean.sce", ce, cOff, Pid::sce, -1.5f, "−1.5 DB/OCT");
    formatIs(P, "clean.link", ce, cOff, Pid::link, 1.f, "100 %");
    formatIs(P, "clean.stmode", ce, cOff, Pid::stmode, 1.f, "MID/SIDE");
    formatIs(P, "clean.voice", ce, cOff, Pid::voice, 0.f, "OFF");
    formatIs(P, "clean.drive.voice_off_na", ce, cOff, Pid::drive, 3.f, kEnDash);
    formatIs(P, "clean.makeup", ce, cOff, Pid::makeup, -3.f, "−3.0 DB");
    formatIs(P, "clean.automu.on", ce, cOff, Pid::automu, 1.f, "ON");
    formatIs(P, "clean.mix", ce, cOff, Pid::mix, 1.5f, "150 %");
    formatIs(P, "clean.s2thr.na", ce, cOff, Pid::s2thr, 0.f, kEnDash);

    formatIs(P, "synth.thr.dial", se, s5, Pid::thr, -18.f, "30");                       // INPUT 30; never "INPUT 30"
    formatIs(P, "synth.ratio.step_text", se, s5, Pid::ratio, 0.75f, "4:1");
    formatIs(P, "synth.ratio.between_steps", se, s5, Pid::ratio, 0.6f, "2:1");
    formatIs(P, "synth.knee.derived", se, s5, Pid::knee, 40.f, "(= 6.0 DB)");
    formatIs(P, "synth.range.na", se, s5, Pid::range, 30.f, kEnDash);
    formatIs(P, "synth.atk.live", se, s5, Pid::atk, 1.f, "1 MS");
    formatIs(P, "synth.atk.step_text", se, s5, Pid::atk, 20.f, "SLOW 30 MS");
    formatIs(P, "synth.atk.micro_step", se, s5, Pid::atk, 0.012f, "10 µS");
    formatIs(P, "synth.atk.clamped_edge", se, s5, Pid::atk, 0.05f, "100 µS");
    formatIs(P, "synth.rel.step", se, s5, Pid::rel, 300.f, "0.3 S");
    formatIs(P, "synth.rel.auto", se, s5, Pid::rel, 5000.f, "AUTO");
    formatIs(P, "synth.tmode.gr_on", se, s5, Pid::tmode, 1.f, "GAIN REDUCTION ON");
    formatIs(P, "synth.tmode.long_text_prints_label", se, s5, Pid::tmode, 7.f, "OFF");
    formatIs(P, "synth.s2atk.locked", se, s5, Pid::s2atk, 3.f, "(10 MS)");
    formatIs(P, "synth.look.budget_off", se, sOff, Pid::look, 10.f, "(0 MS)");
    formatIs(P, "synth.det.locked_span", se, s5, Pid::det, 0.f, "(PEAK)");
    formatIs(P, "synth.schpf.numeric_label", se, s5, Pid::schpf, 100.f, "100 HZ");
    formatIs(P, "synth.sce.dial", se, s5, Pid::sce, 3.f, "5.0");
    formatIs(P, "synth.link.step_text", se, s5, Pid::link, 1.f, "LINKED");
    formatIs(P, "synth.stmode", se, s5, Pid::stmode, 2.f, "MID ONLY");
    formatIs(P, "synth.makeup.dial", se, s5, Pid::makeup, 6.f, "40");
    formatIs(P, "synth.s2thr.dbu_step", se, s5, Pid::s2thr, -18.f, "4 DBU");
    formatIs(P, "synth.s2thr.off_step", se, s5, Pid::s2thr, 24.f, "LIMITER OUT");
    formatIs(P, "synth.hold.derived_time", se, s5, Pid::hold, 0.f, "(= 0.8 MS)");      // hold and look print MS
    formatIs(P, "synth.hold.derived_budget_off", se, sOff, Pid::hold, 0.f, "(= 0 MS)");
    formatIs(P, "synth.s2rel.program", se, s5, Pid::s2rel, 0.f, "~60 MS");
    {
        RawParams clean1 = s5;
        clean1[Pid::voice] = 1.f;
        formatIs(P, "synth.drive.variant_na", se, clean1, Pid::drive, 5.f, kEnDash);
    }

    // ---- 3. the parts: value never holds the label, unit apart, spoken words, prefix ------------------------------------
    {
        const auto parts = [](const ModeDescriptor& d, RawParams raw, Pid pid, float x)
        {
            raw.v[idx(pid)] = x;
            ParamView v;
            resolveView(d, raw, v);
            FormattedValue f;
            formatParts(v, pid, f);
            return f;
        };
        const FormattedValue thr = parts(clean, cOff, Pid::thr, -18.f);
        textIs(P, "parts.clean.thr.value", thr.value, "−18.0");
        textIs(P, "parts.clean.thr.unit", thr.unit, "DB");
        textIs(P, "parts.clean.thr.spoken", thr.spoken, "minus 18.0 decibels");
        P.eq("parts.clean.thr.prefix", thr.prefix, 0);
        textIs(P, "parts.clean.ratio.spoken", parts(clean, cOff, Pid::ratio, 0.75f).spoken, "4.0 to 1");
        textIs(P, "parts.clean.ratio.inf.spoken", parts(clean, cOff, Pid::ratio, 1.f).spoken, "infinity to 1");
        textIs(P, "parts.clean.atk.spoken", parts(clean, cOff, Pid::atk, 0.25f).spoken, "250 microseconds");
        textIs(P, "parts.clean.det.spoken_falls_back_to_text", parts(clean, cOff, Pid::det, 2.f).spoken, "PEAK + RMS");
        textIs(P, "parts.clean.na.spoken", parts(clean, cOff, Pid::s2thr, 0.f).spoken, "not applicable");
        const FormattedValue inp = parts(kSynth, s5, Pid::thr, -18.f);
        textIs(P, "parts.synth.thr.value_without_label", inp.value, "30");
        textIs(P, "parts.synth.thr.dial_unit", inp.unit, "");
        const FormattedValue slow = parts(kSynth, s5, Pid::atk, 30.f);
        textIs(P, "parts.synth.atk.step_value", slow.value, "SLOW 30");
        textIs(P, "parts.synth.atk.step_unit", slow.unit, "MS");
        textIs(P, "parts.synth.tmode.step_spoken", parts(kSynth, s5, Pid::tmode, 7.f).spoken, "gain reduction off");
        const FormattedValue s2rel = parts(kSynth, s5, Pid::s2rel, 0.f);
        P.eq("parts.synth.s2rel.prefix_program", s2rel.prefix, '~');
        textIs(P, "parts.synth.s2rel.spoken", s2rel.spoken, "about 60 milliseconds");
        P.eq("parts.synth.s2atk.prefix_locked", parts(kSynth, s5, Pid::s2atk, 0.f).prefix, '(');
        P.eq("parts.synth.knee.prefix_derived", parts(kSynth, s5, Pid::knee, 0.f).prefix, '=');
        textIs(P, "parts.synth.s2thr.numeric_step", parts(kSynth, s5, Pid::s2thr, -14.f).value, "8");
        textIs(P, "parts.synth.s2thr.numeric_step_unit", parts(kSynth, s5, Pid::s2thr, -14.f).unit, "DBU");
        textIs(P, "parts.synth.s2thr.numeric_step_spoken", parts(kSynth, s5, Pid::s2thr, -14.f).spoken, "8 d B u");
    }

    // ---- 4. parseHost: '-' and U+2212, units, labels, texts, words, dials; refusals ------------------------------------
    parsesTo(P, "clean.thr.ascii_minus", ce, cOff, Pid::thr, "-18 DB", -18.f);
    parsesTo(P, "clean.thr.unicode_minus", ce, cOff, Pid::thr, "−18.0 DB", -18.f);
    parsesTo(P, "clean.thr.bare", ce, cOff, Pid::thr, "  -6.5 ", -6.5f);
    parsesTo(P, "clean.thr.lower_case_unit", ce, cOff, Pid::thr, "-12 db", -12.f);
    parsesTo(P, "clean.thr.plus", ce, cOff, Pid::thr, "+3", 3.f);
    parsesTo(P, "clean.thr.clamps_to_host", ce, cOff, Pid::thr, "-100", -60.f);
    parsesTo(P, "clean.ratio.r_to_1", ce, cOff, Pid::ratio, "4:1", 0.75f);
    parsesTo(P, "clean.ratio.bare_r", ce, cOff, Pid::ratio, "4", 0.75f);
    parsesTo(P, "clean.ratio.negative_unicode", ce, cOff, Pid::ratio, "−2.0:1", 1.5f);
    parsesTo(P, "clean.ratio.negative_ascii", ce, cOff, Pid::ratio, "-2:1", 1.5f);
    parsesTo(P, "clean.ratio.infinity", ce, cOff, Pid::ratio, "∞:1", 1.f);
    parsesTo(P, "clean.ratio.inf_word", ce, cOff, Pid::ratio, "inf", 1.f);
    parsesTo(P, "clean.atk.micro", ce, cOff, Pid::atk, "250 µs", 0.25f);
    parsesTo(P, "clean.atk.us", ce, cOff, Pid::atk, "250 us", 0.25f);
    parsesTo(P, "clean.atk.greek_mu", ce, cOff, Pid::atk, "250μS", 0.25f);
    parsesTo(P, "clean.atk.bare_ms", ce, cOff, Pid::atk, "0.25", 0.25f);
    parsesTo(P, "clean.rel.seconds", ce, cOff, Pid::rel, "1.2 s", 1200.f);
    parsesTo(P, "clean.range.off", ce, cOff, Pid::range, "OFF", 60.f);
    parsesTo(P, "clean.schpf.off", ce, cOff, Pid::schpf, "off", 0.f);
    parsesTo(P, "clean.schpf.hz", ce, cOff, Pid::schpf, "80 Hz", 80.f);
    parsesTo(P, "clean.schpf.khz", ce, cOff, Pid::schpf, "0.1 kHz", 100.f);
    parsesTo(P, "clean.link.percent", ce, cOff, Pid::link, "50 %", 0.5f);
    parsesTo(P, "clean.link.bare_percent", ce, cOff, Pid::link, "50", 0.5f);
    parsesTo(P, "clean.det.text", ce, cOff, Pid::det, "PEAK + RMS", 2.f);
    parsesTo(P, "clean.det.label_any_case", ce, cOff, Pid::det, "pk+rms", 2.f);
    parsesTo(P, "clean.det.index", ce, cOff, Pid::det, "1", 1.f);
    parsesTo(P, "clean.tmode.label", ce, cOff, Pid::tmode, "AUTO", 1.f);
    parsesTo(P, "clean.tmode.text", ce, cOff, Pid::tmode, "auto release", 1.f);
    parsesTo(P, "clean.automu.on", ce, cOff, Pid::automu, "ON", 1.f);
    parsesTo(P, "clean.look.locked_readout", ce, cOff, Pid::look, "(0 MS)", 0.f);
    parsesTo(P, "clean.drive.variant_steps_absent_number_ok", ce, cOff, Pid::drive, "6 DB", 6.f);
    parsesTo(P, "synth.thr.dial", se, s5, Pid::thr, "30", -18.f);
    parsesTo(P, "synth.thr.universal_db", se, s5, Pid::thr, "−12 DB", -12.f);
    parsesTo(P, "synth.ratio.snaps_to_detent", se, s5, Pid::ratio, "5:1", 0.75f);
    parsesTo(P, "synth.ratio.label", se, s5, Pid::ratio, "10", 0.9f);
    parsesTo(P, "synth.s2thr.dbu", se, s5, Pid::s2thr, "4 DBU", -18.f);
    parsesTo(P, "synth.s2thr.bare_dial", se, s5, Pid::s2thr, "8", -14.f);
    parsesTo(P, "synth.s2thr.text", se, s5, Pid::s2thr, "limiter out", 24.f);
    parsesTo(P, "synth.s2thr.dbfs_snaps", se, s5, Pid::s2thr, "-17 DB", -18.f);
    parsesTo(P, "synth.tmode.off", se, s5, Pid::tmode, "OFF", 7.f);
    parsesTo(P, "synth.sce.dial", se, s5, Pid::sce, "5.0", 3.f);
    parsesTo(P, "synth.makeup.dial", se, s5, Pid::makeup, "40", 6.f);
    parsesTo(P, "synth.link.text", se, s5, Pid::link, "linked", 1.f);
    parsesTo(P, "synth.atk.hybrid_step_text", se, s5, Pid::atk, "SLOW 30 MS", 30.f);
    parsesTo(P, "synth.atk.hybrid_outside_snaps", se, s5, Pid::atk, "80 ms", 100.f);
    parsesTo(P, "synth.hold.derived_readout", se, s5, Pid::hold, "(= 0.8 MS)", 0.8f);
    parsesTo(P, "synth.s2atk.locked_readout", se, s5, Pid::s2atk, "(10 MS)", 10.f);
    parsesTo(P, "synth.s2rel.program_readout", se, s5, Pid::s2rel, "~60 MS", 60.f);
    refused(P, "empty", ce, cOff, Pid::thr, "");
    refused(P, "blank", ce, cOff, Pid::thr, "   ");
    refused(P, "garbage", ce, cOff, Pid::thr, "abc");
    refused(P, "na_dash", ce, cOff, Pid::s2thr, kEnDash);
    refused(P, "wrong_unit", ce, cOff, Pid::thr, "12 HZ");
    refused(P, "ratio_for_time", ce, cOff, Pid::atk, "4:1");
    refused(P, "trailing_junk", ce, cOff, Pid::thr, "-3 DB x");
    refused(P, "zero_ratio", ce, cOff, Pid::ratio, "0:1");

    // ---- 5. formatValue: bounded, NUL-terminated, never splits a UTF-8 sequence ----------------------------------------
    {
        ParamView v;
        RawParams r = cOff;
        r[Pid::det] = 2.f;
        resolveView(clean, r, v);
        char buf[16];
        std::memset(buf, 'x', sizeof buf);
        const int n5 = formatValue(v, Pid::det, buf, 5);
        P.eq("truncate.ascii.length", n5, 4);
        textIs(P, "truncate.ascii.text", std::string(buf), "PEAK");
        const int n4 = formatValue(v, Pid::thr, buf, 4);                  // "−18.0 DB": U+2212 is 3 bytes
        P.eq("truncate.utf8.fits_whole_minus", b2i(n4 == 3 && std::string_view(buf) == kMinus), 1);
        const int n3 = formatValue(v, Pid::thr, buf, 3);
        P.eq("truncate.utf8.never_splits", b2i(n3 == 0 && buf[0] == '\0'), 1);
        P.eq("truncate.zero_cap", formatValue(v, Pid::thr, buf, 0), 0);
        const int full = formatValue(v, Pid::thr, buf, sizeof buf);
        P.eq("truncate.full_length", full, static_cast<int64_t>(std::strlen("−18.0 DB")));
    }

    // ---- 6. golden: the synthetic descriptor's texts ------------------------------------------------------------------
    for (const Pid pid : kResolveOrder)
    {
        const ParamSpec& s = kSynth.params[pid].spec;
        const std::string base = key("format.synth", pidName(pid));
        goldenText(P, base + ".default", host(se, s5, pid, s5[pid]));
        if (s.kind == Kind::continuous || s.kind == Kind::hybrid)
        {
            goldenText(P, base + ".lo", host(se, s5, pid, s.lo));
            goldenText(P, base + ".hi", host(se, s5, pid, s.hi));
        }
        for (std::size_t i = 0; i < s.steps.size(); ++i)
            goldenText(P, base + ".step" + std::to_string(i), host(se, s5, pid, s.steps[i].plain));
    }

    return P.finish();
}
