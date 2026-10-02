// Source/web/ui/FrameText.h: a recorded frame's fingerprint as text (web lead phase, ADR-93; docs/sprints/web-lead.md,
// "The gate's contract"). It is what Module.fcmpFrame() returns after its `hooks` line (WebMain.cpp) and the whole of
// what `ui.dump --fp` writes (Tools/probes/plugin/ui_dump.cpp): the browser gate compares the two line by line, so both
// are written here, once.
//
//   geometry <16 hex>          funkgui::fingerprint()'s two hashes, with its defaults: the live primitives are left
//   text <16 hex>              out, and a theme does not change either
//   statics <n>                the primitives hashed
//   live <n>                   the live primitives (counted, not hashed: the gate leaves this line out)
//   texts <n>   rrects <n>   segments <n>   areas <n>          one line each, in this order
//   max_x <g>   max_y <g>      the hashed primitives' extent, "%.9g"
//   tag.<name> <n>             one line per tag that occurs, in ascending tag order: its dump name in lower case
//                              (SLOT_LABEL: tag.slot_label), or its number when it has no name
//   view_w <n>   view_h <n>    the frame's logical size
//   glyphs_missing <n>         codepoints the frame asked for that the atlas does not hold
//
// One `name value` per line, each line ended by a line feed, numbers in the C locale. The lines down to the tags are
// ui.geometry's golden rows (funkgui::addMetrics: the same names, "%016llx" and "%.9g"), and all of them are
// `funkgui_framerender --fingerprint`'s, which knows no product tag name and so cannot stand in for this.
//
// Portable C++ (no Emscripten header): the probes include it natively and under node, the editor module in a browser.
#pragma once

#include <funkgui/canvas/Fingerprint.h>
#include <funkgui/canvas/PrimList.h>
#include <funkgui/canvas/Tags.h>
#include <funkgui/core/CLocale.h>

#include <string>

namespace fcmp::web
{
    // Appends the lines above for `frame` to `out`.
    inline void appendFrameText(std::string& out, const funkgui::PrimList& frame)
    {
        const funkgui::Fingerprint fp = funkgui::fingerprint(frame);
        char line[256];
        funkgui::snprintfC(line, sizeof line,
                           "geometry %016llx\ntext %016llx\nstatics %d\nlive %d\ntexts %d\nrrects %d\nsegments %d\n"
                           "areas %d\nmax_x %.9g\nmax_y %.9g\n",
                           static_cast<unsigned long long>(fp.geometry), static_cast<unsigned long long>(fp.text),
                           fp.statics, fp.live, fp.texts, fp.rrects, fp.segments, fp.areas,
                           static_cast<double>(fp.maxX), static_cast<double>(fp.maxY));
        out += line;
        for (const auto& [tag, count] : fp.tagCounts)
        {
            out += "tag.";
            if (const char* name = funkgui::tagName(tag))
            {
                for (; *name != '\0'; ++name)
                    out += *name >= 'A' && *name <= 'Z' ? static_cast<char>(*name - 'A' + 'a') : *name;
                funkgui::snprintfC(line, sizeof line, " %d\n", count);
            }
            else
                funkgui::snprintfC(line, sizeof line, "%u %d\n", static_cast<unsigned>(tag), count);
            out += line;
        }
        funkgui::snprintfC(line, sizeof line, "view_w %d\nview_h %d\nglyphs_missing %u\n", frame.info.logicalW,
                           frame.info.logicalH, frame.missingGlyphs);
        out += line;
    }
} // namespace fcmp::web
