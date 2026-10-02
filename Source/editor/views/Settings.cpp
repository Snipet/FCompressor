// Source/editor/views/Settings.cpp — the settings overlay (see Settings.h; v1.2, ADR-85).
#include "editor/views/Settings.h"

#include "editor/Layout.h"
#include "editor/Panel.h"
#include "editor/Tags.h"

#include "fcdsp/engine/Oversampler.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Setup.h"
#include "fcdsp/telemetry/UiFrame.h"

#include <funkgui/canvas/Canvas.h>
#include <funkgui/core/Col.h>
#include <funkgui/core/Ease.h>
#include <funkgui/core/Theme.h>
#include <funkgui/core/TypeScale.h>
#include <funkgui/panel/HostServices.h>
#include <funkgui/params/GestureController.h>
#include <funkgui/text/TextFit.h>
#include <funkgui/widgets/FocusRing.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <utility>

namespace fcmp::ui
{
    namespace
    {
        namespace S = layout::settings;
        namespace T = funkgui::type;
        using funkgui::Rect;

        constexpr funkgui::Point kNowhere { -1.0e6f, -1.0e6f };
        constexpr const char* kTimes = "\xC3\x97";              // ×
        constexpr const char* kDash  = "\xE2\x80\x93";          // –
        constexpr const char* kSep   = " \xC2\xB7 ";            // " · "

        constexpr std::array<const char*, 3> kQualitySpoken { "Eco", "Std", "HQ" };
        constexpr std::array<const char*, 3> kQualityFamily { "", "IIR", "FIR" };   // Oversampler.h: STD, HQ
        constexpr std::array<const char*, 3> kBudgetSpoken  { "Off", "5 ms", "20 ms" };
        constexpr std::array<const char*, 2> kKeyLabel      { "INTERNAL", "EXTERNAL" };
        constexpr std::array<const char*, 2> kKeySpoken     { "Internal", "External" };

        // The footer lines (fitted to layout::footer::kSpecLineW, 554 px of kLabel).
        constexpr const char* kQualitySpec    = "QUALITY   OVERSAMPLING OF THIS INSTANCE   A CHANGE SILENCES ONE BLOCK";
        constexpr const char* kBudgetSpec     = "LOOKAHEAD   ADDED AS LATENCY   THE LOOKAHEAD SLOT USES UP TO IT";
        constexpr const char* kKeySpec        = "SIDECHAIN   EXTERNAL: THE HOST'S KEY INPUT, WHEN ONE IS ROUTED";
        constexpr const char* kNewQualitySpec = "QUALITY FOR NEW INSTANCES   ON THIS COMPUTER, IN EVERY HOST";
        constexpr const char* kNewBudgetSpec  = "LOOKAHEAD FOR NEW INSTANCES   ON THIS COMPUTER, IN EVERY HOST";
        constexpr const char* kCopySpec       = "COPY REPORT   THESE DIAGNOSTICS AS TEXT, FOR A BUG REPORT";
        constexpr std::array<const char*, 5> kSpecs { kQualitySpec, kBudgetSpec, kKeySpec, kNewQualitySpec,
                                                      kNewBudgetSpec };

        constexpr const char* kCopyLabel   = "COPY REPORT";
        constexpr const char* kCopiedLabel = "COPIED";

        struct RowDef
        {
            const char* key;
            const char* spoken;
            bool        live;
        };
        enum RowIndex : int { rVersion, rLibraries, rFormat, rRate, rBlock, rChannels, rOs, rLookahead, rLatency, rLoad,
                              rOverruns, rAudio, rMode, rPresets, rDisplay, kRowCount };
        constexpr std::array<RowDef, kRowCount> kRowDefs { {
            { "VERSION", "Version", false },          { "LIBRARIES", "Libraries", false },
            { "FORMAT", "Format and host", false },   { "SAMPLE RATE", "Sample rate", false },
            { "BLOCK SIZE", "Block size", false },    { "CHANNELS", "Channels", false },
            { "OVERSAMPLING", "Oversampling", false }, { "LOOKAHEAD", "Lookahead", false },
            { "LATENCY", "Latency", false },          { "DSP LOAD", "DSP load", true },
            { "OVERRUNS", "Overruns", true },         { "AUDIO", "Audio", true },
            { "MODE", "Mode", false },                { "PRESETS", "Presets", false },
            { "DISPLAY", "Display", true },
        } };
        static_assert(kRowCount == Settings::kDiagRows, "one definition per DIAGNOSTICS row");

        // QUALITY's table, under its cells: a caption at x 40 and one text per column.
        enum TableRow : int { tOs, tFilter, tLatency, tRunsAt, kTableRows };
        constexpr std::array<const char*, kTableRows> kTableCaption { "OVERSAMPLING", "FILTER", "LATENCY, SAMPLES",
                                                                       "RUNS AT, KHZ" };

        // Appends UTF-8 text to a fixed buffer, never cutting inside a codepoint.
        struct Line
        {
            Line(char* o, std::size_t c) noexcept : out(o), cap(c)
            {
                if (cap > 0)
                    out[0] = '\0';
            }

            char*       out;
            std::size_t cap;
            std::size_t n = 0;

            void add(std::string_view s) noexcept
            {
                for (const char ch : s)
                {
                    if (n + 1 >= cap)
                    {
                        while (n > 0 && (static_cast<unsigned char>(out[n]) & 0xC0u) == 0x80u)
                            --n;
                        break;
                    }
                    out[n++] = ch;
                }
                out[n] = '\0';
            }
            void add(const char* s) noexcept { add(std::string_view(s != nullptr ? s : "")); }
            // An integer, its thousands apart ("48 000") from 10 000 up; below that plain ("4096").
            void addInt(long long v) noexcept
            {
                char digits[32];
                const int len = std::snprintf(digits, sizeof digits, "%lld", v < 0 ? -v : v);
                if (len <= 0)
                    return;
                if (v < 0)
                    add("-");
                const bool group = len > 4;
                for (int i = 0; i < len; ++i)
                {
                    if (group && i > 0 && (len - i) % 3 == 0)
                        add(" ");
                    add(std::string_view(digits + i, 1));
                }
            }
            void addFixed(double v, int decimals) noexcept
            {
                char buf[32];
                std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
                add(buf);
            }
            // A rate in kHz: whole ("96"), else one decimal ("88.2").
            void addKhz(double hz) noexcept
            {
                const double k = hz / 1000.0;
                if (std::fabs(k - std::round(k)) < 0.05)
                    addInt(std::llround(k));
                else
                    addFixed(k, 1);
            }
            void addUpper(std::string_view s) noexcept
            {
                for (const char ch : s)
                {
                    const char u = ch >= 'a' && ch <= 'z' ? static_cast<char>(ch - 'a' + 'A') : ch;
                    add(std::string_view(&u, 1));
                }
            }
        };

        int clampIndex(int i) noexcept { return std::clamp(i, 0, 2); }

        // The rate the latency and the tables are worked out at: the configured one, else 48 kHz (said beside them).
        double rateOf(const Diagnostics& d) noexcept { return d.prepared && d.sampleRate > 0.0 ? d.sampleRate : 48000.0; }

        std::array<std::string, 3> qualityHelps()
        {
            std::array<std::string, 3> out;
            for (std::size_t i = 0; i < out.size(); ++i)
            {
                const fcdsp::OsDesign& d = fcdsp::kOs[i];
                std::string s = std::string(kQualitySpoken[i]) + ": ";
                s += d.factor <= 1 ? std::string("no oversampling")
                                   : std::to_string(d.factor) + " times " + kQualityFamily[i] + " oversampling";
                s += ", " + std::to_string(d.latency) + (d.latency == 1 ? " sample" : " samples") + " latency";
                out[i] = std::move(s);
            }
            return out;
        }

        std::array<std::string, 3> budgetHelps()
        {
            std::array<std::string, 3> out;
            for (std::size_t i = 0; i < out.size(); ++i)
            {
                const int ms = static_cast<int>(std::lround(fcdsp::budgetMs(static_cast<fcdsp::LookaheadBudget>(i))));
                out[i] = ms > 0 ? std::string(kBudgetSpoken[i]) + ": up to " + std::to_string(ms)
                                      + " ms of lookahead, adds " + std::to_string(ms) + " ms of latency"
                                : std::string(kBudgetSpoken[i]) + ": no lookahead, no added latency";
            }
            return out;
        }

        std::array<std::string, 3> newHelps(const std::array<const char*, 3>& spoken, const char* what)
        {
            std::array<std::string, 3> out;
            for (std::size_t i = 0; i < out.size(); ++i)
                out[i] = std::string(spoken[i]) + ": the " + what + " a new FCompressor starts with on this computer";
            return out;
        }

        std::vector<funkgui::CellText> choiceTexts(fcdsp::Pid pid, const std::array<const char*, 3>& spoken,
                                                   const std::array<std::string, 3>& help)
        {
            std::vector<funkgui::CellText> t;
            const auto& choices = fcdsp::kHostParams[fcdsp::idx(pid)].choices;   // the display row's labels
            for (std::size_t i = 0; i < 3; ++i)
                t.push_back({ choices[i], spoken[i], help[i].c_str() });
            return t;
        }

        std::vector<funkgui::CellText> keyTexts(const std::array<std::string, 2>& help)
        {
            return { { kKeyLabel[0], kKeySpoken[0], help[0].c_str() }, { kKeyLabel[1], kKeySpoken[1], help[1].c_str() } };
        }

        // The display row's cell rule: w(label) + 14 rounded up to an even px, 4 px apart, from kCellsX.
        std::vector<Rect> cellsAt(const funkgui::FontAtlasSdf& atlas, std::span<const char* const> labels, float y)
        {
            std::vector<Rect> out;
            float x = S::kCellsX;
            for (const char* l : labels)
            {
                const float w = 2.0f * std::ceil(0.5f * (funkgui::text::width(atlas, l, T::kCaption) + S::kCellPad));
                out.push_back({ x, y, w, S::kCellH });
                x += w + S::kCellGap;
            }
            return out;
        }

        std::vector<Rect> choiceCells(const funkgui::FontAtlasSdf& atlas, fcdsp::Pid pid, float y)
        {
            const auto& choices = fcdsp::kHostParams[fcdsp::idx(pid)].choices;
            const std::array<const char*, 3> labels { choices[0], choices[1], choices[2] };
            return cellsAt(atlas, labels, y);
        }

        int hostDefault(fcdsp::Pid pid) noexcept
        {
            return clampIndex(static_cast<int>(std::lround(fcdsp::kHostParams[fcdsp::idx(pid)].def)));
        }

        funkgui::Point captionAt(float cellY) noexcept { return { S::kLabelX, cellY + 4.0f }; }   // the display row's
    }

    Settings::Settings(PanelContext& ctx)
        : ctx_(ctx),
          qualityHelp_(qualityHelps()),
          budgetHelp_(budgetHelps()),
          keyHelp_{ { "Internal: the compressor listens to its own input",
                      "External: it listens to the host's key input; with none routed, to its own input" } },
          newQualityHelp_(newHelps(kQualitySpoken, "quality")),
          newBudgetHelp_(newHelps(kBudgetSpoken, "lookahead budget")),
          qualityModel_(ctx.facade.port(fcdsp::Pid::quality), choiceTexts(fcdsp::Pid::quality, kQualitySpoken, qualityHelp_)),
          budgetModel_(ctx.facade.port(fcdsp::Pid::labudget), choiceTexts(fcdsp::Pid::labudget, kBudgetSpoken, budgetHelp_)),
          keyModel_(ctx.facade.port(fcdsp::Pid::extkey), keyTexts(keyHelp_)),
          newQualityModel_(kPrefNewQuality, { 0, 1, 2 }, choiceTexts(fcdsp::Pid::quality, kQualitySpoken, newQualityHelp_),
                           hostDefault(fcdsp::Pid::quality)),
          newBudgetModel_(kPrefNewLookahead, { 0, 1, 2 }, choiceTexts(fcdsp::Pid::labudget, kBudgetSpoken, newBudgetHelp_),
                          hostDefault(fcdsp::Pid::labudget)),
          quality_(qualityModel_, choiceCells(ctx.atlas, fcdsp::Pid::quality, S::kQualityY), funkgui::CellStyle::text,
                   "QUALITY", captionAt(S::kQualityY), a11yId(ViewIndex::settings, kQualityLocal)),
          budget_(budgetModel_, choiceCells(ctx.atlas, fcdsp::Pid::labudget, S::kBudgetY), funkgui::CellStyle::text,
                  "LOOKAHEAD", captionAt(S::kBudgetY), a11yId(ViewIndex::settings, kBudgetLocal)),
          key_(keyModel_, cellsAt(ctx.atlas, kKeyLabel, S::kKeyY), funkgui::CellStyle::text, "SIDECHAIN",
               captionAt(S::kKeyY), a11yId(ViewIndex::settings, kKeyLocal)),
          newQuality_(newQualityModel_, choiceCells(ctx.atlas, fcdsp::Pid::quality, S::kNewQualityY),
                      funkgui::CellStyle::text, "QUALITY", captionAt(S::kNewQualityY),
                      a11yId(ViewIndex::settings, kNewQualityLocal)),
          newBudget_(newBudgetModel_, choiceCells(ctx.atlas, fcdsp::Pid::labudget, S::kNewBudgetY),
                     funkgui::CellStyle::text, "LOOKAHEAD", captionAt(S::kNewBudgetY),
                     a11yId(ViewIndex::settings, kNewBudgetLocal)),
          animation_(animationModel_, S::kAnimation, a11yId(ViewIndex::settings, kAnimationLocal))
    {
        groups_ = { &quality_, &budget_, &key_, &newQuality_, &newBudget_ };
        quality_.setSpokenTitle("Quality");
        budget_.setSpokenTitle("Lookahead budget");
        key_.setSpokenTitle("Sidechain");
        newQuality_.setSpokenTitle("Quality for new instances");
        newBudget_.setSpokenTitle("Lookahead for new instances");
        const float w = 2.0f * std::ceil(0.5f * (funkgui::text::width(ctx.atlas, kCopyLabel, T::kCaption) + S::kCellPad));
        copyRect_ = { layout::kContentRight - w, S::kCopyY, w, S::kCellH };
        const std::vector<Rect> qc = choiceCells(ctx.atlas, fcdsp::Pid::quality, S::kQualityY);
        const std::vector<Rect> bc = choiceCells(ctx.atlas, fcdsp::Pid::labudget, S::kBudgetY);
        for (std::size_t i = 0; i < 3; ++i)
        {
            qualityX_[i] = qc[i].centreX();
            budgetX_[i] = bc[i].centreX();
        }
        budgetRight_ = bc[2].right();
        for (std::size_t i = 0; i < rows_.size(); ++i)
        {
            rows_[i].key = kRowDefs[i].key;
            rows_[i].spoken = kRowDefs[i].spoken;
            rows_[i].live = kRowDefs[i].live;
        }
        refresh();
    }

    bool Settings::isOpen() const noexcept { return ctx_.overlay == Overlay::settings; }

    // ---- the diagnostics ------------------------------------------------------------------------------------------------

    void Settings::refresh()
    {
        refreshedAt_ = ctx_.seconds;
        diag_ = ctx_.facade.diagnostics();
        render_ = ctx_.renderInfo ? ctx_.renderInfo() : RenderInfo{};
        if (PresetAccess& pa = ctx_.facade.presets(); !presetsRead_ || pa.revision() != presetsRev_)
        {
            presetsRead_ = true;
            presetsRev_ = pa.revision();
            factoryPresets_ = 0;
            userPresets_ = 0;
            for (int i = 0, n = pa.count(); i < n; ++i)
                (pa.row(i).factory ? factoryPresets_ : userPresets_) += 1;
        }

        const Diagnostics& d = diag_;
        const double fs = rateOf(d);
        const int q = clampIndex(d.quality);
        const auto budget = static_cast<fcdsp::LookaheadBudget>(clampIndex(d.budget));
        std::array<Row, kDiagRows> was = rows_;
        const auto row = [this](RowIndex r) { return Line{ rows_[static_cast<std::size_t>(r)].value,
                                                           sizeof rows_[0].value }; };
        {
            Line l = row(rVersion);
            l.add("FCOMPRESSOR ");
            l.add(d.version[0] != '\0' ? d.version : kDash);
        }
        {
            Line l = row(rLibraries);
            l.add("FUNKGUI ");
            l.add(d.funkgui[0] != '\0' ? d.funkgui : kDash);
            l.add(kSep);
            l.add("JUCE ");
            l.add(d.juce[0] != '\0' ? d.juce : kDash);
        }
        {
            Line l = row(rFormat);
            l.add(d.format[0] != '\0' ? d.format : "UNKNOWN FORMAT");
            l.add(kSep);
            if (d.host[0] != '\0')
                l.addUpper(d.host);
            else
                l.add("UNKNOWN HOST");
        }
        {
            Line l = row(rRate);
            if (d.prepared)
            {
                l.addInt(std::llround(d.sampleRate));
                l.add(" HZ");
            }
            else
                l.add("THE HOST HAS NOT STARTED THE AUDIO");
        }
        {
            Line l = row(rBlock);
            if (d.prepared)
            {
                l.add("UP TO ");
                l.addInt(d.maxBlock);
                l.add(d.maxBlock == 1 ? " SAMPLE" : " SAMPLES");
            }
            else
                l.add(kDash);
        }
        {
            Line l = row(rChannels);
            if (d.prepared)
            {
                l.addInt(d.mainIns);
                l.add(" IN");
                l.add(kSep);
                l.addInt(d.mainOuts);
                l.add(" OUT");
                l.add(kSep);
                if (d.keyChans > 0)
                {
                    l.add("KEY ");
                    l.addInt(d.keyChans);
                }
                else
                    l.add("NO KEY INPUT");
            }
            else
                l.add(kDash);
        }
        {
            Line l = row(rOs);
            const fcdsp::OsDesign& os = fcdsp::kOs[static_cast<std::size_t>(q)];
            if (os.factor <= 1)
                l.add("NONE (ECO)");
            else
            {
                l.addInt(os.factor);
                l.add(kTimes);
                l.add(" ");
                l.add(kQualityFamily[static_cast<std::size_t>(q)]);
                l.add(", RUNS AT ");
                l.addInt(std::llround(fs * os.factor));
                l.add(" HZ");
            }
        }
        {
            Line l = row(rLookahead);
            const int la = fcdsp::lookaheadSamples(budget, fs);
            if (la <= 0)
                l.add("OFF");
            else
            {
                l.addInt(std::lround(fcdsp::budgetMs(budget)));
                l.add(" MS BUDGET, ");
                l.addInt(la);
                l.add(" SAMPLES");
            }
        }
        {
            Line l = row(rLatency);
            l.addInt(d.latencySamples);
            l.add(d.latencySamples == 1 ? " SAMPLE" : " SAMPLES");
            l.add(kSep);
            l.addFixed(1000.0 * d.latencySamples / fs, 2);
            l.add(" MS");
        }
        {
            Line l = row(rLoad);
            if (d.blocks == 0)
                l.add(kDash);
            else
            {
                l.addFixed(100.0 * static_cast<double>(d.loadAvg), 1);
                l.add(" %");
                l.add(kSep);
                l.add("PEAK ");
                l.addFixed(100.0 * static_cast<double>(d.loadPeak), 1);
                l.add(" %");
            }
        }
        {
            Line l = row(rOverruns);
            l.addInt(d.overruns);
            l.add(" OF ");
            l.addInt(d.blocks);
            l.add(d.blocks == 1 ? " BLOCK" : " BLOCKS");
        }
        {
            Line l = row(rAudio);
            const FrameState& f = ctx_.frame;
            l.add(!f.hasFrame ? "NO AUDIO SINCE THIS WINDOW OPENED" : f.fresh ? "RUNNING" : "STOPPED");
        }
        {
            Line l = row(rMode);
            int modes = 0;
            for (const fcdsp::ModeSlot& m : fcdsp::modeSlots())
                modes += m.entry != nullptr ? 1 : 0;
            if (const fcdsp::ModeDescriptor* desc = ctx_.frame.entry != nullptr ? ctx_.frame.entry->desc : nullptr)
            {
                l.add(desc->name);
                l.add(kSep);
                l.add("SLOT ");
                l.addInt(ctx_.frame.res.view.slot);
                l.add(kSep);
                l.add("REVISION ");
                l.addInt(desc->revision);
                l.add(kSep);
            }
            l.addInt(modes);
            l.add(" MODES");
        }
        {
            Line l = row(rPresets);
            l.addInt(factoryPresets_);
            l.add(" FACTORY");
            l.add(kSep);
            l.addInt(userPresets_);
            l.add(" USER");
        }
        {
            Line l = row(rDisplay);
            const RenderInfo& r = render_;
            if (!r.gpu)
                l.add("HEADLESS");
            else
            {
                l.add(r.renderer);                               // "METAL", "VULKAN": the host names its API
                l.add(kSep);
                l.addInt(r.zoomPercent);
                l.add(" % ZOOM");
                l.add(kSep);
                l.addFixed(r.scale, r.scale == std::floor(r.scale) ? 0 : 1);
                l.add(kTimes);
                l.add(" BACKING");
                l.add(kSep);
                l.addInt(std::lround(r.fps));
                l.add(" FPS");
                if (r.overflows > 0)
                {
                    l.add(kSep);
                    l.addInt(r.overflows);
                    l.add(" DROPPED");
                }
            }
        }

        // The tables' columns at this rate.
        for (std::size_t i = 0; i < 3; ++i)
        {
            Line k{ runsAt_[i].data(), runsAt_[i].size() };
            k.addKhz(fs * fcdsp::kOs[i].factor);
            Line b{ budgetSamples_[i].data(), budgetSamples_[i].size() };
            b.addInt(fcdsp::lookaheadSamples(static_cast<fcdsp::LookaheadBudget>(i), fs));
        }
        {
            Line l{ rateNote_, sizeof rateNote_ };
            l.add("AT ");
            l.addKhz(fs);
            l.add(" KHZ");
        }
        {
            Line l{ keyNote_, sizeof keyNote_ };
            if (!d.prepared)
                l.add("THE AUDIO HAS NOT STARTED YET");
            else if (d.keyChans <= 0)
                l.add("THE HOST ROUTES NO KEY INPUT");       // EXTERNAL then listens to the main input (its help)
            else
                l.add(d.keyChans == 1 ? "KEY INPUT: MONO FROM THE HOST" : "KEY INPUT: STEREO FROM THE HOST");
        }
        {
            Line l{ latency_, sizeof latency_ };
            l.addInt(d.latencySamples);
            l.add(d.latencySamples == 1 ? " SAMPLE" : " SAMPLES");
            l.add(kSep);
            l.addFixed(1000.0 * d.latencySamples / fs, 2);
            l.add(" MS");
        }

        for (std::size_t i = 0; i < rows_.size(); ++i)
            if (std::strcmp(was[i].value, rows_[i].value) != 0)
            {
                ++revision_;
                break;
            }
    }

    std::string Settings::report() const
    {
        std::string s = "FCompressor diagnostics\n";
        for (const Row& r : rows_)
        {
            s += r.key;
            s += ": ";
            s += r.value;
            s += '\n';
        }
        return s;
    }

    bool Settings::copyReport()
    {
        if (ctx_.host == nullptr)
            return false;                                        // not attached: no host to ask
        refresh();
        if (!ctx_.host->copyText(report()))
            return false;                                        // a host without a clipboard: nothing to announce
        copiedUntil_ = ctx_.seconds + S::kCopiedS;
        ++revision_;
        return true;
    }

    // ---- tick -----------------------------------------------------------------------------------------------------------

    funkgui::Point Settings::pointerForWidgets() const noexcept
    {
        return pointerOver_ && ctx_.pointerIn ? ctx_.pointer : kNowhere;
    }

    void Settings::tick(float dt)
    {
        // The Panel ticks the overlay only while it is shown: a missed frame is a fresh opening (the browsers' rule).
        const bool gap = lastTick_ < 0.0 || (dt > 0.0f && ctx_.seconds - lastTick_ > 1.5 * static_cast<double>(dt));
        lastTick_ = ctx_.seconds;
        if (!isOpen())
        {
            copyArmed_ = false;                                  // fading out
            return;
        }
        if (gap)
        {
            refresh();
            if (ctx_.focusVisible && groupOf(ctx_.focus) < 0 && ctx_.focus != a11yId(ViewIndex::settings, kCopyLocal))
                ctx_.focus = a11yId(ViewIndex::settings, kQualityLocal);   // opened from the keyboard: QUALITY
        }
        else if (ctx_.seconds - refreshedAt_ >= static_cast<double>(S::kRefreshS))
            refresh();
        if (copiedUntil_ >= 0.0 && ctx_.seconds >= copiedUntil_)
        {
            copiedUntil_ = -1.0;
            ++revision_;
        }

        const funkgui::Point pointer = pointerForWidgets();
        for (funkgui::SegmentedSelector* s : groups_)
            s->tick(dt, pointer);
        const bool overCopy = copyRect_.contains(pointer);
        copyHover_ = funkgui::ease::hover(copyHover_, overCopy, dt);
        const uint32_t animId = animation_.a11yId();
        const bool animFocus = ctx_.focusVisible && ctx_.focus == animId;
        const bool overAnim = animation_.contains(pointer);
        animation_.tick(dt, overAnim || animationCaptured_, animFocus, ctx_.alwaysChrome);
        char animSpec[sizeof ctx_.handNext.spec];
        animation_.specLine(animSpec, sizeof animSpec);
        if (animationCaptured_ && ctx_.pointerPressed)
            ctx_.offerHand(fcdsp::kNoPid, HandKind::drag, animId, animSpec);
        if (overAnim)
            ctx_.offerHand(fcdsp::kNoPid, HandKind::hover, animId, animSpec);
        if (animFocus)
            ctx_.offerHand(fcdsp::kNoPid, HandKind::focus, animId, animSpec);

        for (int g = 0; g < kGroups; ++g)
        {
            const uint32_t id = group(g).a11yId();
            if (group(g).contains(pointer))
                ctx_.offerHand(fcdsp::kNoPid, HandKind::hover, id, kSpecs[static_cast<std::size_t>(g)]);
            if (ctx_.focusVisible && groupOf(ctx_.focus) == g)
                ctx_.offerHand(fcdsp::kNoPid, HandKind::focus, id, kSpecs[static_cast<std::size_t>(g)]);
        }
        const uint32_t copyId = a11yId(ViewIndex::settings, kCopyLocal);
        if (overCopy)
            ctx_.offerHand(fcdsp::kNoPid, HandKind::hover, copyId, kCopySpec);
        if (ctx_.focusVisible && ctx_.focus == copyId)
            ctx_.offerHand(fcdsp::kNoPid, HandKind::focus, copyId, kCopySpec);
    }

    bool Settings::wantsFullRate() const
    {
        if (!isOpen())
            return false;
        for (const funkgui::SegmentedSelector* s : groups_)
            if (!s->settled())
                return true;
        if (!animation_.settled())
            return true;
        const bool overCopy = copyRect_.contains(pointerForWidgets());
        return copiedUntil_ >= 0.0 || !funkgui::ease::sameBits(copyHover_, overCopy ? 1.0f : 0.0f);
    }

    // ---- drawing --------------------------------------------------------------------------------------------------------

    void Settings::draw(funkgui::Canvas& c, const funkgui::Theme& th) const
    {
        const Rect& a = S::kArea;
        const float barRuleY = S::kCopyY - 12.0f;
        {
            const funkgui::Canvas::Scope scope(c, tag::settingsBg, false);
            c.rrect(S::kGround.x, S::kGround.y, S::kGround.w, S::kGround.h, 0.0f, th.ground);
            c.hairlineH(a.x, a.y, a.w, th.ink16);
            c.hairlineH(a.x, a.bottom() - 1.0f, a.w, th.ink16);
            c.hairlineV(S::kDividerX, S::kHeadingY + 16.0f, barRuleY - S::kHeadingY - 28.0f, th.ink16);
            c.hairlineH(a.x, S::kRuleY, S::kColumnRight - a.x, th.ink16);
            c.hairlineH(a.x, S::kInterfaceRuleY, S::kColumnRight - a.x, th.ink16);
            c.hairlineH(a.x, barRuleY, a.w, th.ink16);
        }

        char fitted[160];
        const auto fitText = [&](const char* text, float x, float y, float maxW, const funkgui::TextStyle& st,
                                 funkgui::Col ink, funkgui::Align align = funkgui::Align::left) {
            funkgui::text::fitEllipsis(ctx_.atlas, text, st, maxW, fitted, sizeof fitted);
            c.text(fitted, x, y, st, ink, align);
        };
        const auto heading = [&](const char* first, const char* second, float x, float y) {
            c.text(first, x, y, T::kCaption, th.ink52);
            if (second != nullptr)
                c.text(second, x + c.textWidth(first, T::kCaption) + 12.0f, y, T::kCaption, th.ink32);
        };

        {
            const funkgui::Canvas::Scope scope(c, tag::settingsText, false);
            heading("AUDIO", "THIS INSTANCE", S::kLabelX, S::kHeadingY);
            heading("DIAGNOSTICS", nullptr, S::kDiagKeyX, S::kHeadingY);
            heading("NEW INSTANCES", "THIS COMPUTER", S::kLabelX, S::kNewHeadingY);
            heading("INTERFACE", "THIS COMPUTER", S::kLabelX, S::kInterfaceHeadingY);
            fitText("HOW FAST THE PANEL MOVES", S::kAnimationNote.x, S::kAnimationNote.y,
                    S::kColumnRight - S::kAnimationNote.x, T::kMicro, th.ink32);

            // QUALITY's table and LOOKAHEAD's, each value centred on its cell; the selected column ink70.
            const int q = qualityModel_.active();
            for (int r = 0; r < kTableRows; ++r)
            {
                const float y = S::kTableY0 + S::kTablePitch * static_cast<float>(r);
                c.text(kTableCaption[static_cast<std::size_t>(r)], S::kLabelX, y, T::kMicro, th.ink32);
                for (std::size_t i = 0; i < 3; ++i)
                {
                    char cell[24];
                    Line l{ cell, sizeof cell };
                    switch (static_cast<TableRow>(r))
                    {
                        case tOs:      l.addInt(fcdsp::kOs[i].factor); l.add(kTimes); break;
                        case tFilter:  l.add(i == 0 ? kDash : kQualityFamily[i]); break;
                        case tLatency: l.addInt(fcdsp::kOs[i].latency); break;
                        case tRunsAt:  l.add(runsAt_[i].data()); break;
                        case kTableRows: break;
                    }
                    c.text(cell, qualityX_[i], y, T::kMicro, static_cast<int>(i) == q ? th.ink70 : th.ink32,
                           funkgui::Align::centre);
                }
            }
            const int b = budgetModel_.active();
            c.text("LATENCY, SAMPLES", S::kLabelX, S::kBudgetTableY, T::kMicro, th.ink32);
            for (std::size_t i = 0; i < 3; ++i)
                c.text(budgetSamples_[i].data(), budgetX_[i], S::kBudgetTableY, T::kMicro,
                       static_cast<int>(i) == b ? th.ink70 : th.ink32, funkgui::Align::centre);
            c.text(rateNote_, budgetRight_ + 12.0f, S::kBudgetTableY, T::kMicro, th.ink32);

            fitText(keyNote_, S::kCellsX, S::kKeyNoteY, S::kColumnRight - S::kCellsX, T::kMicro, th.ink32);

            c.text("LATENCY", S::kLabelX, S::kLatencyY + 1.0f, T::kCaption, th.ink52);
            c.text(latency_, S::kCellsX, c.sharedBaselineTop(S::kLatencyY + 1.0f, T::kCaption, T::kLabel), T::kLabel,
                   th.ink100);
            fitText("WHAT THE HOST COMPENSATES", S::kCellsX, S::kLatencyNoteY, S::kColumnRight - S::kCellsX, T::kMicro,
                    th.ink32);
            fitText("SAVED SESSIONS KEEP THEIR OWN", S::kCellsX, S::kNewNoteY, S::kColumnRight - S::kCellsX,
                    T::kMicro, th.ink32);

            c.text("ESC CLOSES", S::kLabelX, S::kCopyY + 4.0f, T::kMicro, th.ink32);
        }

        for (int g = 0; g < kGroups; ++g)
            group(g).draw(c, th, ctx_.focusVisible && groupOf(ctx_.focus) == g);
        animation_.draw(c, th, ctx_.focusVisible && ctx_.focus == animation_.a11yId());

        // DIAGNOSTICS.
        for (std::size_t i = 0; i < rows_.size(); ++i)
        {
            const Row& r = rows_[i];
            const float y = S::kDiagY0 + S::kDiagPitch * static_cast<float>(i);
            {
                const funkgui::Canvas::Scope scope(c, tag::settingsText, false);
                c.text(r.key, S::kDiagKeyX, y, T::kMicro, th.ink32);
            }
            const funkgui::Canvas::Scope scope(c, tag::settingsValue, r.live);
            fitText(r.value, S::kDiagValueX, y, layout::kContentRight - S::kDiagValueX, T::kMicro, th.ink70);
        }

        // COPY REPORT.
        {
            const funkgui::Canvas::Scope scope(c, tag::settingsGear, false);
            const bool copied = copiedUntil_ >= 0.0;
            const funkgui::Col ink = copyArmed_ ? th.accent
                                   : copied     ? th.ink70
                                                : funkgui::mix(th.ink52, th.ink100, copyHover_);
            c.text(copied ? kCopiedLabel : kCopyLabel, copyRect_.centreX(),
                   c.capCentreTop(copyRect_.centreY(), T::kCaption), T::kCaption, ink, funkgui::Align::centre);
            if (ctx_.focusVisible && ctx_.focus == a11yId(ViewIndex::settings, kCopyLocal))
                funkgui::drawFocusRing(c, copyRect_, th.accent);
        }
    }

    // ---- input ----------------------------------------------------------------------------------------------------------

    bool Settings::hit(funkgui::Point p) const { return S::kArea.contains(p); }

    int Settings::groupOf(uint32_t id) const noexcept
    {
        for (int g = 0; g < kGroups; ++g)
        {
            const uint32_t base = group(g).a11yId();
            if (id >= base && id <= base + 3u)
                return g;
        }
        return -1;
    }

    void Settings::pointerMove(const funkgui::PointerEvent&) { pointerOver_ = true; }

    void Settings::pointerExit() { pointerOver_ = false; }

    void Settings::pointerDown(const funkgui::PointerEvent& e)
    {
        pointerOver_ = true;
        copyArmed_ = false;
        if (!isOpen() || ctx_.gestures == nullptr)
            return;
        for (funkgui::SegmentedSelector* s : groups_)
            s->pointerDown(e, *ctx_.gestures);                   // each takes only a press on its own cells
        animationCaptured_ = false;
        if (animation_.contains({ e.x, e.y }) && ctx_.host != nullptr)
        {
            animationCaptured_ = true;
            ctx_.focus = animation_.a11yId();                    // the ring hidden, as a slot's (ADR-89)
            animation_.pointerDown(e, *ctx_.gestures, *ctx_.host);
        }
        if (!e.popup && copyRect_.contains({ e.x, e.y }))
            copyArmed_ = true;                                   // COPY REPORT fires on a release inside
    }

    void Settings::pointerDrag(const funkgui::PointerEvent& e)
    {
        if (animationCaptured_ && ctx_.gestures != nullptr)
            animation_.pointerDrag(e, *ctx_.gestures);
        if (copyArmed_ && !copyRect_.contains({ e.x, e.y }))
            copyArmed_ = false;                                  // dragging off cancels
    }

    void Settings::pointerUp(const funkgui::PointerEvent& e)
    {
        const bool fire = copyArmed_ && copyRect_.contains({ e.x, e.y }) && isOpen();
        copyArmed_ = false;
        if (animationCaptured_ && ctx_.gestures != nullptr)
            animation_.pointerUp(e, *ctx_.gestures);
        animationCaptured_ = false;
        if (fire)
            copyReport();
    }

    bool Settings::key(const funkgui::KeyEvent& e)
    {
        if (!isOpen() || e.key == funkgui::Key::escape)
            return false;                                        // Esc: the Panel closes the overlay
        if (const int g = groupOf(ctx_.focus); g >= 0 && ctx_.focusVisible && ctx_.gestures != nullptr)
            return group(g).key(e, *ctx_.gestures);
        if (ctx_.focusVisible && ctx_.focus == animation_.a11yId() && ctx_.gestures != nullptr)
            return animation_.key(e, *ctx_.gestures);
        if (ctx_.focusVisible && ctx_.focus == a11yId(ViewIndex::settings, kCopyLocal)
            && (e.key == funkgui::Key::enter || e.key == funkgui::Key::space))
        {
            copyReport();
            return true;
        }
        return false;
    }

    funkgui::Cursor Settings::cursor(funkgui::Point p) const
    {
        for (const funkgui::SegmentedSelector* s : groups_)
            if (s->contains(p))
                return s->cursorAt(p);
        if (animation_.contains(p))
            return funkgui::Cursor::leftRight;
        return copyRect_.contains(p) ? funkgui::Cursor::pointingHand : funkgui::Cursor::normal;
    }

    // ---- accessibility --------------------------------------------------------------------------------------------------

    void Settings::accessibility(std::vector<funkgui::A11yItem>& out) const
    {
        for (const funkgui::SegmentedSelector* s : groups_)
            s->accessibility(out);
        {
            funkgui::A11yItem anim;
            animation_.accessibility(anim);
            anim.title = "Animation";
            anim.help = "How fast the panel's fades and eases run, on every FCompressor on this computer";
            out.push_back(std::move(anim));
        }

        const auto staticText = [&out](uint32_t local, const char* title, const char* value, Rect bounds) {
            funkgui::A11yItem it;
            it.id = a11yId(ViewIndex::settings, local);
            it.role = funkgui::A11yRole::staticText;
            it.bounds = bounds;
            it.title = title;
            it.value = value;
            it.readOnly = true;
            out.push_back(std::move(it));
        };
        staticText(kKeyNoteLocal, "Key input", keyNote_,
                   { S::kCellsX, S::kKeyNoteY - 2.0f, S::kColumnRight - S::kCellsX, 14.0f });
        staticText(kLatencyLocal, "Latency", latency_,
                   { S::kLabelX, S::kLatencyY - 2.0f, S::kColumnRight - S::kLabelX, 18.0f });
        for (std::size_t i = 0; i < rows_.size(); ++i)
        {
            const float y = S::kDiagY0 + S::kDiagPitch * static_cast<float>(i);
            staticText(kDiagLocal0 + static_cast<uint32_t>(i), rows_[i].spoken, rows_[i].value,
                       { S::kDiagKeyX, y - 3.0f, layout::kContentRight - S::kDiagKeyX, S::kDiagPitch });
        }

        funkgui::A11yItem copy;
        copy.id = a11yId(ViewIndex::settings, kCopyLocal);
        copy.role = funkgui::A11yRole::button;
        copy.bounds = copyRect_;
        copy.title = copiedUntil_ >= 0.0 ? "Copied" : "Copy report";
        copy.help = "Copies the diagnostics as text, for a bug report";
        out.push_back(std::move(copy));
    }

    int Settings::focusOrder(std::span<uint32_t> out) const
    {
        std::size_t n = 0;
        for (int g = 0; g < kGroups && n < out.size(); ++g)
            out[n++] = group(g).a11yId();
        if (n < out.size())
            out[n++] = animation_.a11yId();                      // ADR-90
        if (n < out.size())
            out[n++] = a11yId(ViewIndex::settings, kCopyLocal);
        return static_cast<int>(n);
    }

    void Settings::a11yAction(uint32_t id, funkgui::A11yAction a, double value)
    {
        if (!isOpen())
            return;
        if (id == a11yId(ViewIndex::settings, kCopyLocal))
        {
            if (a == funkgui::A11yAction::press || a == funkgui::A11yAction::toggle)
                copyReport();
            return;
        }
        if (const int g = groupOf(id); g >= 0 && ctx_.gestures != nullptr)
            group(g).a11yAction(id, a, value, *ctx_.gestures);
        if (id == animation_.a11yId() && ctx_.gestures != nullptr)
            animation_.a11yAction(a, value, *ctx_.gestures);
    }

    void Settings::doubleClick(const funkgui::PointerEvent& e)
    {
        if (isOpen() && ctx_.gestures != nullptr && animation_.contains({ e.x, e.y }))
            animation_.doubleClick(*ctx_.gestures);             // NORMAL
    }

    bool Settings::wheel(const funkgui::WheelEvent& e)
    {
        if (!isOpen() || ctx_.gestures == nullptr || ctx_.host == nullptr || !animation_.contains({ e.x, e.y }))
            return false;
        return animation_.wheel(e, *ctx_.gestures, ctx_.host->nowSeconds());
    }

    uint32_t Settings::a11yRevision() const { return revision_; }
}
