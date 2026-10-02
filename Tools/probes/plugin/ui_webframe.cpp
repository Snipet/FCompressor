// FCMP_PROBE layer=ui name=webframe scope=global timeout=120
//
// ui.webframe (web lead phase, ADR-93; docs/sprints/web-lead.md, "The gate's contract"): the frame the browser gate
// expects of a capture page, held to the blessed goldens. The gate (Scripts/web-live.sh) compares what
// Module.fcmpFrame() returns in a browser with `ui.dump --facade web --host <name> --nolive 1 --fp`; this probe makes
// that node-side frame as ui.dump makes it (the real Panel{skipHint, syncPreview, ignoreLive} over a WebFacade whose
// link drops every record, which is the page before START; HeadlessHost at dpi 2, theme 0, setView(instant), settled
// at 1/60 s) and checks what the gate takes for granted. Spec rows only: the values compared with are ui.geometry's
// golden rows of Mode clean, read from the golden file at run time (--golden-root, with --arch's overlay where one
// exists), so no golden is added and a re-blessed ui.geometry needs nothing here. No JUCE and no Processor in this
// file: it runs natively and under node as it is.
//
//   facade.mode             the web facade starts in Mode clean, whose goldens these are
// For each of gui-live's five views (panel, chars.sidechain, chars.colour, modebrowser, presetbrowser):
//   <view>.golden_rows      the golden holds the view's rows `<view>.dpi2.*`: the two hashes, the six counts and the
//                           two extents at least (an empty or renamed golden is a failure, never a pass)
//   <view>.geometry, .text  the frame's two hashes are the golden's: the web facade's frame under `nolive` is the
//                           frame ui.geometry blessed over a FakeFacade (and gui-live holds the native editor to)
//   <view>.rows             the golden rows whose value is not the frame's line of that name, each judged by the
//                           golden's own tolerance as the harness judges it: 0. These are the counts (statics, live,
//                           texts, rrects, segments, areas), the extents and one row per tag; each one is listed
//   <view>.lines            the frame's lines, but view_w, view_h and glyphs_missing, that have no golden row: 0 (a
//                           tag the golden does not hold)
//   <view>.settled          the Panel came to rest within 600 frames
// The settings view, which gui-live leaves out (its DIAGNOSTICS show the processor) and the gate compares, since
// both of its sides are the web facade:
//   settings.geometry       the golden's settings.dpi2.geometry: the web facade changes the screen's text (the
//                           FORMAT row, the host, the diagnostics), not its layout
//   settings.host.text      under another --host name the text hash differs: the name is on the screen, so the
//                           module's `host` pin is what makes one expectation hold in every browser
//   settings.host.geometry  ... and the geometry hash does not
//   settings.host.repeat    the same name again gives the same two hashes
// For all six:
//   <view>.frametext        FrameText.h's lines for the frame, read back by a reader written here from the contract's
//                           list of names, give the frame's fingerprint and nothing else: both hashes, every count,
//                           the extents bit for bit, every tag with its count in ascending tag order, the view's
//                           size and the missing glyphs
//   <view>.glyphs_missing   0 (a host name the atlas cannot draw fails this: the module's `host` pin takes plain ASCII)
//
// The probe needs FCMP_PREFS_DIR (CTest sets a sandbox): the Panel and the web facade's presets read UiPreferences.
#include "ProbeRegistry.h"

#include "editor/Panel.h"
#include "editor/SubView.h"

#include "web/facade/EngineLink.h"
#include "web/facade/WebFacade.h"
#include "web/ui/FrameText.h"

#include "fcdsp/modes/Registry.h"

#include <funkgui/canvas/Fingerprint.h>
#include <funkgui/canvas/PrimList.h>
#include <funkgui/canvas/Tags.h>
#include <funkgui/core/CLocale.h>
#include <funkgui/panel/HeadlessGuiScope.h>
#include <funkgui/panel/HeadlessHost.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    using funkgui::test::Probe;
    namespace ui = fcmp::ui;

    constexpr int   kMaxSettle = 600;
    constexpr float kDt = 1.0f / 60.0f;
    constexpr ui::PanelOptions kGateOptions { true, true, true };     // skipHint, syncPreview, ignoreLive
    constexpr const char* kHost = "WEB-LIVE";                         // the gate's --host and `host` pin
    constexpr const char* kOtherHost = "Another Browser 1";
    constexpr std::array<std::string_view, 5> kViews { "panel", "chars.sidechain", "chars.colour", "modebrowser",
                                                       "presetbrowser" };   // gui-live's five

    int b(bool v) { return v ? 1 : 0; }

    // The page before START has no port: every record is dropped and no reply ever comes (ui.dump's --facade web).
    struct NullLink final : fcmp::web::EngineLink
    {
        void post(std::span<const std::uint8_t>) override {}
        void setSink(fcmp::web::ReplySink*) override {}
    };

    struct Frame
    {
        funkgui::PrimList list;                                       // the settled frame
        std::string       text;                                       // FrameText.h's lines for it
        int               settle = 0;
        bool              cleanMode = false;                          // the facade's Mode is clean
    };

    Frame render(const ui::ViewSpec& view, std::string_view hostName)
    {
        Frame f;
        NullLink link;
        fcmp::web::WebFacade facade(link);
        facade.setEnvironment("WEB", hostName);
        f.cleanMode = fcdsp::bySlot(static_cast<int>(facade.currentRaw().modeSlot)) == fcdsp::byKey("clean");
        ui::Panel panel(facade, kGateOptions);
        funkgui::HeadlessHost host(panel, 0, 2.0f);
        panel.setView(view, true);
        f.settle = host.settle(kMaxSettle, kDt);
        f.list = host.draw();
        fcmp::web::appendFrameText(f.text, f.list);
        return f;
    }

    // ---- the golden rows --------------------------------------------------------------------------------------------
    struct Golden
    {
        std::string value, tolerance;
    };
    using GoldenRows = std::map<std::string, Golden, std::less<>>;

    // A harness flag's value (before "--"); empty when the flag is absent.
    std::string harnessFlag(std::string_view flag)
    {
        const int argc = fcmp::probe::argc();
        char** argv = fcmp::probe::argv();
        for (int i = 1; i + 1 < argc; ++i)
        {
            const std::string_view a = argv[i] != nullptr ? argv[i] : "";
            if (a == "--")
                break;
            if (a == flag && argv[i + 1] != nullptr)
                return argv[i + 1];
        }
        return {};
    }

    // Golden format v2 (03 §3.2.2): "<key> TAB <value> TAB <tolerance>" lines, '#' comments. False when the file
    // cannot be opened; a line of another shape is skipped (the harness judges the file's form in ui.geometry).
    bool readGolden(const std::filesystem::path& file, GoldenRows& rows)
    {
        std::ifstream in(file);
        if (!in)
            return false;
        for (std::string line; std::getline(in, line);)
        {
            const std::size_t t1 = line.find('\t');
            const std::size_t t2 = t1 == std::string::npos ? t1 : line.find('\t', t1 + 1);
            if (line.empty() || line[0] == '#' || t2 == std::string::npos)
                continue;
            rows[line.substr(0, t1)] = { line.substr(t1 + 1, t2 - t1 - 1), line.substr(t2 + 1) };
        }
        return true;
    }

    // "exact": the same text; "abs:<x>": within x. Any other tolerance is not one of ui.geometry's and never passes.
    bool withinTolerance(const std::string& got, const Golden& want)
    {
        if (want.tolerance == "exact")
            return got == want.value;
        if (want.tolerance.starts_with("abs:"))
        {
            const double x = std::strtod(want.tolerance.c_str() + 4, nullptr);
            const double g = std::strtod(got.c_str(), nullptr), w = std::strtod(want.value.c_str(), nullptr);
            return !got.empty() && std::isfinite(g) && std::isfinite(w) && std::abs(g - w) <= x;
        }
        return false;
    }

    // The frame's lines as name -> value (a line is "<name> <value>").
    std::map<std::string, std::string, std::less<>> linesOf(std::string_view text)
    {
        std::map<std::string, std::string, std::less<>> lines;
        while (!text.empty())
        {
            const std::size_t end = text.find('\n');
            const std::string_view line = text.substr(0, end);
            if (const std::size_t space = line.find(' '); space != std::string_view::npos)
                lines[std::string(line.substr(0, space))] = std::string(line.substr(space + 1));
            text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        }
        return lines;
    }

    // ---- the reader of FrameText.h's lines, from the contract's list ------------------------------------------------
    struct ReadBack
    {
        funkgui::Fingerprint fp;
        int      viewW = 0, viewH = 0;
        uint32_t missing = 0;
    };

    // Takes "<name> " off the front of `text` and gives the rest of that line; false when the next line is not that
    // name's, or is not ended by a line feed.
    bool takeLine(std::string_view& text, std::string_view name, std::string_view& value)
    {
        const std::size_t end = text.find('\n');
        if (end == std::string_view::npos || !text.starts_with(name) || text.size() <= name.size()
            || text[name.size()] != ' ')
            return false;
        value = text.substr(name.size() + 1, end - name.size() - 1);
        text.remove_prefix(end + 1);
        return true;
    }

    bool readHash(std::string_view v, uint64_t& out)
    {
        if (v.size() != 16)
            return false;
        out = 0;
        for (const char c : v)
        {
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
                return false;
            out = out * 16u + static_cast<uint64_t>(c <= '9' ? c - '0' : c - 'a' + 10);
        }
        return true;
    }

    bool readInt(std::string_view v, long& out)
    {
        const std::string s(v);
        char* end = nullptr;
        out = funkgui::strtolC(s.c_str(), &end, 10);
        return !s.empty() && s[0] != ' ' && s[0] != '+' && end == s.c_str() + s.size();
    }

    bool readFloat(std::string_view v, float& out)
    {
        const std::string s(v);
        char* end = nullptr;
        out = funkgui::strtofC(s.c_str(), &end);
        return !s.empty() && s[0] != ' ' && end == s.c_str() + s.size();
    }

    // "<lower-case dump name>" or the number of a tag without a name; 0: neither.
    funkgui::Tag readTag(std::string_view name)
    {
        std::string upper(name);
        bool digits = !upper.empty();
        for (char& c : upper)
        {
            digits = digits && c >= '0' && c <= '9';
            if (c >= 'a' && c <= 'z')
                c = static_cast<char>(c - 'a' + 'A');
        }
        if (const funkgui::Tag t = funkgui::tagFromName(upper.c_str()); t != funkgui::tags::none)
            return t;
        long n = 0;
        return digits && readInt(name, n) && n > 0 && n <= 0xffff ? static_cast<funkgui::Tag>(n) : funkgui::tags::none;
    }

    bool readFrameText(std::string_view text, ReadBack& r)
    {
        std::string_view v;
        if (!takeLine(text, "geometry", v) || !readHash(v, r.fp.geometry) || !takeLine(text, "text", v)
            || !readHash(v, r.fp.text))
            return false;
        const std::array<std::pair<std::string_view, int*>, 6> counts { {
            { "statics", &r.fp.statics }, { "live", &r.fp.live }, { "texts", &r.fp.texts },
            { "rrects", &r.fp.rrects }, { "segments", &r.fp.segments }, { "areas", &r.fp.areas } } };
        long n = 0;
        for (const auto& [name, to] : counts)
        {
            if (!takeLine(text, name, v) || !readInt(v, n))
                return false;
            *to = static_cast<int>(n);
        }
        if (!takeLine(text, "max_x", v) || !readFloat(v, r.fp.maxX) || !takeLine(text, "max_y", v)
            || !readFloat(v, r.fp.maxY))
            return false;
        while (text.starts_with("tag."))
        {
            const std::size_t space = text.find(' ');
            const std::size_t end = text.find('\n');
            if (space == std::string_view::npos || end == std::string_view::npos || space > end)
                return false;
            const funkgui::Tag tag = readTag(text.substr(4, space - 4));
            if (tag == funkgui::tags::none || !readInt(text.substr(space + 1, end - space - 1), n)
                || (!r.fp.tagCounts.empty() && r.fp.tagCounts.back().first >= tag))
                return false;                                         // an unknown name, or not ascending
            r.fp.tagCounts.emplace_back(tag, static_cast<int>(n));
            text.remove_prefix(end + 1);
        }
        long w = 0, h = 0, missing = 0;
        if (!takeLine(text, "view_w", v) || !readInt(v, w) || !takeLine(text, "view_h", v) || !readInt(v, h)
            || !takeLine(text, "glyphs_missing", v) || !readInt(v, missing) || missing < 0)
            return false;
        r.viewW = static_cast<int>(w);
        r.viewH = static_cast<int>(h);
        r.missing = static_cast<uint32_t>(missing);
        return text.empty();                                          // and nothing after the last line
    }

    bool sameBits(float a, float c) { return std::bit_cast<uint32_t>(a) == std::bit_cast<uint32_t>(c); }

    // The rows every frame gets: the text reads back to the fingerprint, and no glyph is missing.
    void frameRows(Probe& P, const std::string& view, const Frame& f)
    {
        const funkgui::Fingerprint fp = funkgui::fingerprint(f.list);
        ReadBack r;
        const bool read = readFrameText(f.text, r);
        P.eq(view + ".frametext",
             b(read && r.fp.geometry == fp.geometry && r.fp.text == fp.text && r.fp.statics == fp.statics
               && r.fp.live == fp.live && r.fp.texts == fp.texts && r.fp.rrects == fp.rrects
               && r.fp.segments == fp.segments && r.fp.areas == fp.areas && sameBits(r.fp.maxX, fp.maxX)
               && sameBits(r.fp.maxY, fp.maxY) && r.fp.tagCounts == fp.tagCounts && !fp.tagCounts.empty()
               && r.viewW == f.list.info.logicalW && r.viewH == f.list.info.logicalH
               && r.missing == f.list.missingGlyphs), 1);
        P.eq(view + ".glyphs_missing", f.list.missingGlyphs, 0);
        if (f.settle > kMaxSettle)
            P.harnessError(view + ": the panel did not settle within 600 frames (an unsettled ease)");
    }

    void goldenRows(Probe& P, const std::string& view, const Frame& f, const GoldenRows& golden)
    {
        const std::string prefix = view + ".dpi2.";
        const std::map<std::string, std::string, std::less<>> lines = linesOf(f.text);
        const auto line = [&lines](std::string_view name) {
            const auto it = lines.find(name);
            return it != lines.end() ? it->second : std::string();
        };
        int rows = 0, differing = 0;
        bool geometry = false, text = false;
        for (auto it = golden.lower_bound(prefix); it != golden.end() && it->first.starts_with(prefix); ++it)
        {
            ++rows;
            const std::string name = it->first.substr(prefix.size());
            const std::string got = line(name);
            const bool same = withinTolerance(got, it->second);
            if (name == "geometry")
                geometry = same;
            else if (name == "text")
                text = same;
            else if (!same)
                ++differing;
            if (!same)
                std::printf("NOTE     %s: the frame has '%s', the golden %s (%s)\n", it->first.c_str(), got.c_str(),
                            it->second.value.c_str(), it->second.tolerance.c_str());
        }
        int unknown = 0;
        for (const auto& [name, value] : lines)
            if (name != "view_w" && name != "view_h" && name != "glyphs_missing" && !golden.contains(prefix + name))
            {
                ++unknown;
                std::printf("NOTE     %s%s: the frame's line (%s) has no golden row\n", prefix.c_str(), name.c_str(),
                            value.c_str());
            }
        P.ge(view + ".golden_rows", rows, 10.0);
        P.eq(view + ".geometry", b(geometry), 1);
        P.eq(view + ".text", b(text), 1);
        P.eq(view + ".rows", differing, 0);
        P.eq(view + ".lines", unknown, 0);
        P.eq(view + ".settled", b(f.settle <= kMaxSettle), 1);
    }
} // namespace

FCMP_PROBE(ui, webframe)
{
    (void) C;
    const funkgui::HeadlessGuiScope gui;                              // FontService bakes the atlas
    const char* prefsDir = std::getenv("FCMP_PREFS_DIR");
    if (prefsDir == nullptr || *prefsDir == '\0')
    {
        P.harnessError("ui.webframe reads UiPreferences: set FCMP_PREFS_DIR to a sandbox (CTest does)");
        return P.finish();
    }

    // ui.geometry's rows of Mode clean: the base file, then the architecture's overlay where there is one.
    namespace fs = std::filesystem;
    const fs::path root = harnessFlag("--golden-root");
    const fs::path scope = fs::path("modes") / "clean" / "ui.geometry.txt";
    GoldenRows golden;
    if (root.empty() || !readGolden(root / "base" / scope, golden))
    {
        P.harnessError("ui.webframe: cannot read " + (root / "base" / scope).string() + " (--golden-root)");
        return P.finish();
    }
    if (const std::string arch = harnessFlag("--arch"); !arch.empty())
        readGolden(root / arch / scope, golden);                      // absent for most: the base rows stand

    bool cleanMode = true;
    for (const std::string_view id : kViews)
    {
        const ui::ViewSpec* view = ui::findView(id);
        if (view == nullptr)
        {
            P.harnessError("ui.webframe: no view '" + std::string(id) + "'");
            continue;
        }
        const Frame f = render(*view, kHost);
        cleanMode = cleanMode && f.cleanMode;
        goldenRows(P, std::string(id), f, golden);
        frameRows(P, std::string(id), f);
    }

    if (const ui::ViewSpec* settings = ui::findView("settings"))
    {
        const Frame f = render(*settings, kHost);
        const Frame other = render(*settings, kOtherHost);
        const Frame again = render(*settings, kHost);
        cleanMode = cleanMode && f.cleanMode;
        const funkgui::Fingerprint fp = funkgui::fingerprint(f.list);
        const funkgui::Fingerprint fpOther = funkgui::fingerprint(other.list);
        const funkgui::Fingerprint fpAgain = funkgui::fingerprint(again.list);
        const auto it = golden.find("settings.dpi2.geometry");
        const std::string geometry = linesOf(f.text)["geometry"];
        P.eq("settings.geometry", b(it != golden.end() && withinTolerance(geometry, it->second)), 1);
        P.eq("settings.settled", b(f.settle <= kMaxSettle), 1);
        P.eq("settings.host.text", b(fp.text != fpOther.text), 1);
        P.eq("settings.host.geometry", b(fp.geometry == fpOther.geometry), 1);
        P.eq("settings.host.repeat", b(fp.text == fpAgain.text && fp.geometry == fpAgain.geometry
                                       && f.text == again.text), 1);
        std::printf("NOTE     settings: geometry %s; text %016llx as %s, %016llx as %s\n", geometry.c_str(),
                    static_cast<unsigned long long>(fp.text), kHost, static_cast<unsigned long long>(fpOther.text),
                    kOtherHost);
        frameRows(P, "settings", f);
    }
    else
        P.harnessError("ui.webframe: no view 'settings'");
    P.eq("facade.mode", b(cleanMode), 1);
    return P.finish();
}
