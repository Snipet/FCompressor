## Headline
- The repository documents describe three configurations and no browser demo. 03 has no CI text at all, and CLAUDE.md does not name the `web` preset, the seven ADR-93 lint rules or Emscripten.
- ADR-93 is 204 lines grown sprint by sprint, with 20 stale or contradictory sentences, among them a target that no longer exists and two gzip methods. A 12-group final structure is below.
- Prototyped in scratch: a CLAUDE.md of +21 lines; every proposed rule sentence shown to fail under `lint.deps`; the copied site passing its self-test from a plain static server; a document check that fails on today's tree.

## Facts
`S` = `/private/tmp/claude-501/-Users-seanfunk-audio-plugins-FCompressor/1e18d990-0133-428b-8995-1664ebc8733a/scratchpad/scout-l/docs`

1. **Tree.** `git diff --stat 7b2d165 58f13e9` is empty. `build-web/site/built-from.txt` reads `site 7b2d165… clean 2026-10-02T07:08:46Z`, so the stamp and the page footer name 7b2d165, not main's merge commit.
2. **Web tree tests** (`build-web/verify-tests.json`): 232 = 219 `ui.*` (24 probes; 14 Modes) + 12 `web.*` + `lint.deps`. Test time 201.66 s wall at `-j4` (`build-web/verify-ctest.log`). There is no `lint.headers` there (`cmake/FcmpProbes.cmake:260`).
3. **Native lead tree** (`build-lead/verify-tests.json`): 527 = 208 dsp + 94 proc + 219 ui + 2 lint + 4 web (`web.engine.print`, `.selfcheck`, `.tail`, `web.simd`).
4. **Site.** 13 files, 2,096,442 bytes. Gzip by `web.size`'s method (node zlib 9, `web/tests/size.mjs:14-28`) sums to 648,135. `gzip -9` per file from stdin gives 643,434, which is ADR-93:1203's figure. ADR-93:1182's 447,506 is the other method.
5. **Speed at the Sprint D gate, this Mac** (`build-web/verify-junit.xml`, `web.engine.speed`): clean 163.2× / 103.7× / 52.0×; worst HQ is mu-67 at 20.5×. This agrees with ADR-93's Sprint A row (168 / 102 / 55; 20.4×).
6. **Lint.** `cmake/LintDeps.cmake:5-54` lists 17 rules. 01 §2.2 (`01-core-contracts.md:148-170`) names none of the seven ADR-93 ones: `plugin.portable`, `editor.juce`, `web.juce`, `web.engine`, `web.emscripten`, `web.ui`, `web.facade`.
7. **`web.emscripten` scope.** It covers `Source/` only. `Tools/web/port/portcheck.cpp:41-42` includes Emscripten headers by design.
8. **Presets** (`CMakePresets.json:83-87,138,155,172`): `web` configures with `FCOMPRESSOR_WEB=ON`, Release; the build target is `fcmp_web`; `web-verify` exists. Build and test jobs are 6 (`:134-138,147`).
9. **Emscripten.** Pinned in `cmake/FcmpDeps.cmake:243` (6.0.3, FATAL otherwise) and `ci.yml` (`FCMP_EMSCRIPTEN: 6.0.3`). `em-config CACHE` prints `/opt/homebrew/Cellar/emscripten/6.0.3/libexec/cache`: one cache for every worktree.
10. **CI.** `ci.yml` already has five jobs: `dsp`, `plugin` (macos-26), `linux-dsp`, `linux-plugin`, `web` (ubuntu-24.04). `web` runs `verify.sh --strict build-web` and uploads only `fcmp-engine.wasm`. Its header (`ci.yml:10-13`) and job name "Web engine (wasm)" still describe Sprint A.
11. **03 has no CI and no web text.** `grep -n -i "web\|wasm\|emscripten\|\bCI\b"` on `03-build-verify-process.md` hits only `JUCE_WEB_BROWSER`. Linux got one comment (`:258-259`); that is the precedent (commit af3fcfb).
12. **`fcmp_web_editor_check` is gone.** ADR-93:1142 names it; no `cmake/*.cmake` mentions it and `build-web/build.ninja` has 0 references. The `.a` files in `build-web` are leftovers dated Oct 1 23:22.
13. **Page URLs.** `web/index.html:57` links `https://github.com/Snipet/FCompressor`, and `main.js:142` links `/tree/<sha>`. `size.mjs:58` allows exactly that repository; everything loaded is relative.
14. **Outcomes.** `docs/sprints/web-{a,b,c,d}.md` have no Outcome section (`s1.md:176` and `s13.md:157` do). Merges: #64 b46960d, #65 e92bd56, #66 eae740b, #67 58f13e9. FunkGui v0.12.0, v0.13.0, v0.14.0 (f950118).
15. **README checks out.** "Fourteen" equals 14 `FCMP_MODE` lines; zoom 100–175 equals `Layout.h:573`; the CI badge points at `ci.yml`. Nothing is untrue; the web is absent, and the CI sentence (`README.md:100-101`) omits the fifth job.
16. **FunkGui has no Emscripten pin and no `.github`.** Its README says "latest: `v0.10.0`" (`:17`, `:109`).

## Design and change list

### CLAUDE.md (106 → 127 lines, 8,135 → 10,067 bytes)
Full text is `S/claude/CLAUDE.after.md`; the diff is `S/claude/CLAUDE.diff`.

- **L3-7 (header):** after `ADR-92` insert `; a browser demo, the same DSP and editor as WebAssembly with no JUCE, ADR-93`, and rewrap (5 lines become 6).
- **L34-36 (lead-only), replace with:**
```
- Lead-only: `CMakeLists.txt`, `cmake/**`, `CMakePresets.json`, `Scripts/**`, `.github/**`, `.gitignore`, `CLAUDE.md`,
  `README.md`, `LICENSE`, `docs/**`, `Resources/**`, `web/**` (the demo's page and its node tests), `tests/golden/**`,
  `tests/fixtures/**` (write-once) — except where a card's OWNS names them. Exception: `docs/modes/<key>.md` belongs
  to that Mode's card.
```
- **After L52 (layer rules), add:**
```
- **What the browser demo compiles has no JUCE** (ADR-93): `Source/editor/**` outside `gpu/` includes and names nothing
  of JUCE's (`juce::`, `JUCE_*`, `jassert`); a menu, a file chooser and the clipboard are `HostServices` calls.
  `Source/plugin/portable/**`: no JUCE, of FunkGui only `funkgui/params/ParamPort.h`. `Source/web/**`: no JUCE;
  `engine/` is portable C++ over `fcdsp` alone; `facade/` reaches the engine only through an `EngineLink` (of
  `web/engine/` only `WebProtocol.h`; never `EngineHost`, never `editor/`); an Emscripten header appears only under
  `Source/web/ui`.
```
- **L53, replace with:** ``- Floating point: `-ffp-contract=off`, never `-ffast-math`, never `-mrelaxed-simd` (wasm). No warnings: our sources build with `-Werror`.``
- **After L67 (real-time rules), add:**
```
- The one deviation (ADR-93): the demo's AudioWorklet has no second thread, so a `quality`/`labudget` change
  reconfigures (and allocates) inside `fcmp_web_post`, between two render quanta. `fcmp_web_process` never allocates,
  locks or calls libm.
```
- **After L78 (presets), add:**
```
  - `web` — the browser demo: wasm32, Emscripten 6.0.3 (any other fails the configure), Release. `[WEB]` = `cmake
    --workflow --preset web-verify && Scripts/verify.sh --strict "$WT/build-web"`: the `web.*` tests and every `ui.*`
    probe as wasm32 under node, against the same goldens (no overlay, hence `--strict`). Run it when you touch
    `Source/web/**`, `web/**`, `Source/editor/**` outside `gpu/`, `Source/plugin/portable/**` or a `ui_*` probe.
    Emscripten's system-library cache is shared by every worktree: on a lock or a half-written library wait and retry,
    never clear it.
```
- **After L81, add:**
```
- A browser, when a card needs one: headless Chrome through FunkGui's `tools/web/check-page.mjs`, always with
  `--chrome-flag --mute-audio --chrome-flag --use-angle=metal` (it plays through the speakers; a flag replaces the
  runner's defaults); stop every server you start. `Scripts/web-live.sh` (the browser gate) is the lead's.
```
No counts are written (219 and 232 change with every Mode).

### README.md
- **New section `## Web demo`, after `### Linux` (before `## Testing`):**
  > The same DSP and the same editor also build for a browser, as a demonstration: two WebAssembly modules and a static page. The engine runs in an AudioWorklet with the plugin's arithmetic, bit for bit (the test suite holds it to the plugin's goldens); the editor is the plugin's panel, drawn with WebGL2. It contains no JUCE and is not a plugin format. Nothing is hosted yet: build it and serve it yourself.
  ```sh
  # Emscripten 6.0.3 exactly (configure refuses any other; `emsdk install 6.0.3`) with its node, CMake 3.30, Ninja,
  # git and Python 3. JUCE and bgfx are not needed; FunkGui is fetched at its pinned tag.
  cmake --preset web && cmake --build --preset web        # the site is build-web/site
  python3 -m http.server 8137 --bind 127.0.0.1 --directory build-web/site   # then open http://127.0.0.1:8137/
  ```
  > Press START. The page plays a loop it synthesises, or an audio file you open or drop, which never leaves the browser. It needs HTTPS or localhost (it does not run from a `file:` address), WebAssembly, AudioWorklet and WebGL2, on a desktop browser; it has been run in Chrome on macOS only. What differs from the plugin is listed on the page: no preset import or export, and your own presets last until the page is closed; no side-chain key input; the DSP load shows a dash; a QUALITY or LOOKAHEAD change rebuilds the engine on the audio thread and may click; nothing for a screen reader inside the editor. ADR-93 in `docs/DECISIONS.md` has the reasons and the measurements.
- **`:95-98` (Testing block), add two lines:** `cmake --workflow --preset web-verify` with the comment "the web demo: its tests, and every UI probe as WebAssembly under node", and `Scripts/verify.sh --strict build-web`.
- **`:100-101`, append:** "A fifth job builds the web demo on x86-64 Linux and runs its tests and every UI probe as WebAssembly under node, against the same goldens; the built site is kept as a workflow artifact and is not published." The artifact clause depends on the CI change.
- **`:106-112` (repository map):**
  - `Source/plugin/` becomes "the JUCE processor, state and presets (portable/: the model code, without JUCE)".
  - `Source/editor/` becomes "the panel and its views, without JUCE (+ gpu/ for the bgfx editor)".
  - Add `Source/web/        the web demo: engine/ (the DSP behind a C ABI), facade/ (the editor's processor), ui/ (the editor module)`.
  - Add `web/               the demo's page, its AudioWorklet script and its node tests`.
  - Add `Tools/web/         the web checks`.
  - Add `web-live.sh` to the Scripts list.
- **`:126-127` (Licence), append:** "The web demo's modules are built with Emscripten and contain parts of musl, libc++, libc++abi and compiler-rt; the site carries their licences beside the GPL and the typeface's OFL."

### ARCHITECTURE.md
- **`:21-22`**, after the closing parenthesis add: "From ADR-93 the same DSP and editor also build as a browser demo: two WebAssembly modules and a static page, not a plugin format (§3.1)."
- **`:62-65`** (topology box), add two lines: `Source/web     the browser demo: engine, facade, ui (ADR-93)` and `web/           the demo's page, worklet script, node tests`.
- **`:82-85`** (pins), append: "The web configuration pins Emscripten 6.0.3 the same way (asserted at configure, a row of `fcmp-deps.txt`)."
- **`:105`** is wrong since Sprint C. Replace with `editor  (namespace fcmp::ui; FunkGui::core; no JUCE outside editor/gpu)`.
- **`:111`**: add `portable/ (no JUCE): EditHistory, FactoryData` to the plugin box.
- **`:126-130`** (layer table), add rows:
  - `plugin/portable` | no JUCE; of FunkGui only `ParamPort.h` (`plugin.portable`) | the model code is the plugin's and the browser facade's
  - `editor` outside `gpu/` | names nothing of JUCE's (`editor.juce`) | the same sources compile as wasm32
  - `web/engine` | portable C++ over `fcdsp` alone, no Emscripten header (`web.engine`) | it builds natively for the checks, and the module needs no JavaScript glue
  - `web/facade` | no Emscripten header, no `EngineHost`; the engine only through an `EngineLink` (`web.facade`) | `proc.webnull` runs it beside a real `Processor`
  - `web/ui` | the only place an Emscripten header may appear (`web.emscripten`, `web.ui`) | everything else stays testable natively
- **`:133-135`**: "Three configurations" becomes "Four configurations: … and **web** (ADR-93: wasm32 through Emscripten; no JUCE, no bgfx, no threads; `fcmp-engine.wasm`, `fcmp-ui.js`/`.wasm`, the UI probes and the checks as node programs, `build-web/site`)."
- **New §3.1 "The browser demo (ADR-93)"** after `:140`:
  ```
   main thread: fcmp-ui.js/.wasm                         AudioWorklet thread: fcmp-worklet.js + fcmp-engine.wasm
   Panel (Source/editor, no gpu/) on funkgui::WebHost     fcmp_web_process: EngineHost::process, 128 frames a quantum
   WebFacade : ProcessorFacade (30 values, EditHistory,   fcmp_web_post, between quanta: Params · Attach · Reset · Pull
     WebPresets, a mirror HistoryRing)   ── Params, Pull ──▶   → Reply (UiFrame, new history columns, flags, latency)
   PortLink : EngineLink (WebProtocol bytes) ◀── Reply ──
   WebGlSink (WebGL2, one draw call)      one MessagePort; ArrayBuffers transferred; no SharedArrayBuffer
  ```
  "The facade posts one Params record per write or per outermost batch, and pulls telemetry once before each Panel tick. Nothing but bytes crosses the port, so the same facade runs natively over a loopback link, where `proc.webnull` proves it equal to the `Processor` bit for bit."
- **`:347-352`** (thread table), add two rows:
  - **AudioWorklet (browser demo)** | `fcmp_web_process`; `fcmp_web_post` between quanta | in `process`: allocate, lock, post a message, call libm
  - **Browser main thread** | Panel, `WebFacade`, the pull before each tick, the preview inline once a control has rested 0.15 s | touch engine state
- **`:355-357`** (rule 1), append: "The browser demo has no `Processor` and no second thread: there a `quality`/`labudget` change reconfigures, and allocates, inside `fcmp_web_post` on the worklet thread, between two render quanta, and may click. It is the one deviation from this section (ADR-93); `fcmp_web_process` keeps every rule."
- **`:364-367`** (rule 4), append: "wasm has neither flush-to-zero nor denormals-are-zero: the WASM SIMD128 backend flushes tiny results itself, by x86's rule, and the engine wrapper zeroes denormal input and idles behind a silence gate."
- **`:372-373`** (rule 7), add: "the web tree's gate and `Scripts/web-live.sh` (headless Chrome)".
- **`:420-424`**, append: "The browser demo keeps the factory bank and the session's user presets in memory (`WebPresets`); UI preferences go to localStorage."
- **§10, new item 8:** "**The web tree.** `verify.sh --strict build-web` runs every `ui.*` probe as wasm32 under node against the base goldens (no overlay), the `web.*` tests (self-registered from `// FCMP_WEB_TEST` lines, judged by exit code) and `lint.deps`. Natively `web.simd`, three `web.engine.*` tests, `proc.webnull`, `proc.webpresets` and `ui.web` run in every gate, so the wrapper and the facade cannot drift from the engine and the processor."
- **`:461-462`**, add: "Web (ADR-93): `-DFCOMPRESSOR_WEB=ON`, Emscripten's toolchain found through `em-config`, `cmake/FcmpWeb.cmake` and `cmake/FcmpWebSite.cmake`."
- **`:471`**, add `web` to the presets.
- **§13**, two rows: the `fcmp_web_*` C ABI and `WebProtocol` records | `Source/web/engine/{WebEngine.h,WebProtocol.h}` | ADR-93 | abi 1; and the module/page seam | `docs/sprints/web-d.md` | ADR-93.
- **Already stale before the web work** (the lead's call): `:3` "no code exists yet"; `:489` "≤ 3 at once" (6 since 2026-10-01); `:438` "13 dsp, 5 proc, 6 ui … ≈ 207 tests".

### 03-build-verify-process.md
Recommended shape: one new subsection plus short pointers, as ADR-92 did, not web rows threaded through nine tables.

- **`:46`**: "Three configurations" becomes "Four", with a bullet "**Web** (`FCOMPRESSOR_WEB=ON`, ADR-93): wasm32 through Emscripten; no JUCE, no bgfx, no threads (§2.12)."
- **`:55-58`**, add: "`fcmp_probe_web` and `fcmp_web_check` (§2.12)."
- **`:107-115`**, add `FcmpPlatform.cmake` (missing since ADR-92), `FcmpWeb.cmake` and `FcmpWebSite.cmake`.
- **`:116-142`**, add `Source/web/`, `Tools/web/`, `web/`, `Scripts/web-live.sh` and `.github/workflows/ci.yml`.
- **`:153`** is wrong. The `Source/editor` row loses "JUCE" and becomes "FunkGui core; no JUCE outside `gpu/`". Add rows for `plugin/portable`, `web/engine`, `web/facade` and `web/ui`, as in ARCHITECTURE.
- **`:258-259`**, add a comment line: `# (ADR-93: option FCOMPRESSOR_WEB before project() takes Emscripten's toolchain file from em-config; include(cmake/FcmpWeb.cmake) follows FcmpProbes.)`
- **`:292-304`** (options), add `FCOMPRESSOR_WEB` (OFF) and `FCOMPRESSOR_WEB_FMA` (`exact`).
- **`:580`, `:631`**: the FCMP_PROBE line takes an optional ` platform=<apple|linux|web>[,…]` (ADR-92, ADR-93: the test registers on those platforms only; `ui.font` is `apple,web`, `ui.font_linux` is `linux`).
- **`:694-718`** (presets):
  - Add row `web | build-web | web, Release | agents whose card touches what the browser compiles; the lead's gate; CI`.
  - Build presets: `web` builds `fcmp_web`.
  - Workflows: add `web-verify`.
  - "jobs 4, -l 12" is stale: jobs are 6 with no load limit.
- **`:848`**: `--arch arm64|x86_64|wasm32`.
- **`:1311`**: add `.github/**`, `web/**`, `README.md`, `LICENSE`.
- **New `### 2.12 The web configuration (ADR-93)`:**
  > The `web` preset builds for wasm32 with Emscripten, pinned to 6.0.3 in `cmake/FcmpDeps.cmake` (another version is a configure error; the version is a row of `fcmp-deps.txt`). It implies DSP-only for every JUCE branch; FunkGui is configured without JUCE (`FunkGui::core`, `FunkGui::web`). ISA flag `-msimd128`, never `-mrelaxed-simd`. `FCOMPRESSOR_WEB_FMA=unfused` exists for measurement only.
  >
  > Targets. In every configuration: `fcmp_web_engine_lib` (`Source/web/engine` over `fcdsp`) and `fcmp_web_check` (`Tools/web/*.cpp`, one subcommand per check). Web only: `fcmp_web_engine` (`fcmp-engine.wasm`: standalone, no JavaScript glue, no imports), `fcmp_web_ui` (`fcmp-ui.js`/`.wasm`), `fcmp_probe_web` (the editor outside `gpu/`, `plugin/portable`, the facade, the engine wrapper and every `layer=ui` probe, a node program), `fcmp_web_port_check`, `fcmp_web_site`, and `fcmp_web`, the build preset's target.
  >
  > Tests. The web tree registers the `ui.*` probes on `fcmp_probe_web` with `--arch wasm32` and three times the timeout; `dsp.*`, `proc.*` and `lint.headers` are native only. A web test is a line `// FCMP_WEB_TEST name=web.<x> timeout=<s> [on=native|web|all] [args=<a,b,…>]` in `Tools/web/*.cpp` (default `all`) or `web/tests/*.mjs` (always web), with the placeholders `{golden}`, `{source}`, `{build}`, `{engine}`; labels `verify;web;global`; judged by exit code; a name ending `.speed` or `.tail` runs alone. wasm32 has no golden overlay and `golden.py adopt` takes no wasm32 candidate, so the web tree's gate is `Scripts/verify.sh --strict build-web`.
  >
  > The site. `build-web/site` is what a static server serves: the page, `fcmp-ui.html` (the page again: the self-test's path), the two modules, `licences/` and `built-from.txt` = `site <sha|none> <clean|dirty> <UTC>`, `clean` only when FCompressor's tree is clean and the FunkGui compiled in is the pinned commit unchanged. It is made afresh by every build, and `fcmp_probes` depends on it, so the gate's stamp covers it. Nothing publishes it.
- **§3.6**, new paragraph after `:1075`: "**Browser parity, `Scripts/web-live.sh <build-web>`** (lead only, like `gui-live.sh`): serves `build-web/site` on 127.0.0.1 and runs the page's self-test (`?selftest=1`) in headless Chrome, muted: …" The rows come from the web-live scout.
- **`:1387-1390`** (step 5), add: "and the web tree: `cmake --workflow --preset web-verify`, then `Scripts/verify.sh --integration --strict build-web`".
- **`:1412-1417`** (step 8), add: "`Scripts/web-live.sh build-web` (Chrome, muted)".
- **End of §4.8, new paragraph:** "**CI (ADR-87, ADR-92, ADR-93).** `.github/workflows/ci.yml` runs five jobs on every push to main and every pull request: `dsp` and `plugin` on Apple Silicon, `linux-dsp` and `linux-plugin` on x86-64 Linux (the `dsp` preset; the `lead` preset through `verify.sh --integration --strict`), and `web` on x86-64 Linux (Emscripten 6.0.3 from emsdk; `verify.sh --strict build-web`; the site uploaded as an artifact). `FCMP_TIMING_SCALE=3`. Nothing is signed, installed or published."

### 01-core-contracts.md (recommended, so CLAUDE.md's "Layer rules (01 §2.2 …)" stays true)
After `:159` add rules 7–10: `plugin.portable`; `editor.juce`; `web.juce` with `web.engine`, `web.facade` and `web.ui`; `web.emscripten`. Use the sentences of `LintDeps.cmake:31-54`. `:113` "editor/ … JUCE + FunkGui::core" is stale.

### ADR-93: final structure (keep every number; current line ranges → group)
| # | Group | Takes | Edits |
|---|---|---|---|
| 1 | Decision and shape | 1034-1042 | Title: drop "in progress". One history line: "Built in four sprints and a lead phase, 2026-10-01 to 02 (PRs #64–#67, #NN; manifests `docs/sprints/web-*.md`)". Platform: desktop browsers with WebGL2 and AudioWorklet; Chrome measured. |
| 2 | Arithmetic and denormals, with the numbers | 1043-1058, 1076-1088 | The flush rule stays; the table stays. Add the Sprint D gate's row for this Mac (Fact 5). |
| 3 | The engine module and the real-time deviation | 1059-1075 | The wrapper's input zeroing and the silence gate move here from "Denormals". "545 KB" becomes 545,704 bytes. |
| 4 | The editor without JUCE | 1102-1109 (G-A, G-B), 1121-1127, 1133-1142, 1172-1179 | Present tense throughout. |
| 5 | The facade | 1143-1153 | Drop "For Sprint D's page …". |
| 6 | The browser host | 1110-1120, 1157-1164 | Add v0.14.0's `beforeTick` in one clause. |
| 7 | The editor module and the page | 1181-1201 | — |
| 8 | The site and licences | 1202-1207 | One gzip method; add the CI artifact. |
| 9 | Tests and gates | 1089-1100, 1167-1171, 1208-1219 | Add: counts with their breakdown (Facts 2, 3), `web-live.sh`, the CI job, the lead gate. |
| 10 | The degradations (one list) | 1227-1232, 1164-1166, 1114-1120, 1162, 1153-1154, 1236-1237 | Merge the wheel, the fill rule, "no chooser, IME, a11y mirror", the DSP-load dash and the zoom fit into the one list; delete "(above)". |
| 11 | What was reviewed | 1055-1057 and 1063-1064 (Sprint A's findings), 1128-1132, 1220-1226 | Sprint C has no entry (review-fix commits 482c852 and 3c03c8d exist). |
| 12 | Follow-ups and what waits for the user | 1179-1180, 1153-1154, 1233-1237 | Hosting; the preview Worker; persistence; import/export; the DSP probes under node; Safari and Firefox; refused-record acknowledgement and DSP load in the protocol; sound by ear; recorded loops with a CREDITS file. |

Stale or contradictory sentences:
- `:1034` "(v1.2 or later; in progress)".
- `:1037` "this entry grows with them".
- `:1040` "from Sprint D, the editor module".
- `:1108-1109` "The views switch to them in Sprint C; until then nothing in the plugin calls them".
- `:1117` "the preset list: Sprint C" (done: `PresetBrowser.cpp:1610-1615`).
- `:1142` `fcmp_web_editor_check` (gone; `fcmp_probe_web` and `fcmp_web_ui` compile those sources).
- `:1153-1156` "Not in the protocol yet … For Sprint D's page" (done at `:1185-1189`).
- `:1179` "Follow-up, not started" (moves to group 12).
- `:1193` "every URL relative" versus the footer's repository link. Say: "nothing is loaded from another origin; the one absolute URL is the link to the source repository and the built commit".
- `:1203` "643,434 gzip" versus `:1182` "447,506 gzip" (two methods; 648,135 by `web.size`'s).
- `:1214-1215` "by hand, not in CI".
- `:1229-1230` "(above)".
- `:1233-1237` "Not done: the plan's lead phase".
- `:1235` Safari and Firefox not run: stays true; moves to group 12.

### SPRINTS.md and plan.md (the minimum)
- **`SPRINTS.md:49`**, add a row: `| **[WEB]** | cd "$WT" && cmake --workflow --preset web-verify && Scripts/verify.sh --strict "$WT/build-web" (ADR-93; Emscripten 6.0.3) |`. At `:55` add `web` to the layer labels.
- **`SPRINTS.md` §5**, one line: "**Web demo (ADR-93), 2026-10-01 to 02:** four sprints outside this plan (`docs/sprints/web-{a,b,c,d}.md`, from `docs/sprints/web/plan.md`) and a lead phase; PRs #64–#67 and #NN; not published."
- **`plan.md:2`**, one status line: "Status: Sprints A to D are merged and the lead phase is done except publishing, which waits for the hosting decision. ADR-93 records what was built; where it differs from this plan, ADR-93 is right."
- No Outcome sections in `web-*.md`.

### FunkGui (a list for the lead)
README:
- `:8` "for JUCE audio plugins on macOS and Linux": there is now a JUCE-free core and a browser host.
- `:17`, `:109` `v0.10.0`: now v0.14.0.
- `:27-34` hosts diagram: no `WebHost + WebGlSink`.
- `:46-79`: no **web** layer (`WebHost`, `WebGlSink`, `WebPrefs`, `WebInput.h`, `WebClock.h`); `HostServices` menus, choosers and clipboard; the committed atlas; the prefs backend.
- `:83-93`: Emscripten, node and Chrome for the web tests.
- `:119-122`: `FunkGui::web`.
- `:134-145`: `FUNKGUI_WITH_JUCE`, `FUNKGUI_WEB`.
- `:185` "the font atlas is baked through JUCE": JUCE builds only.
- `:193-213`: presets `nojuce` and `web`; `tools/web/check-page.mjs`; the live pages.
- `:231-245`: `src/nojuce`, `src/web`, `test/web`, `tools/web`, `tools/GalleryWeb`.
- `:262`: FCompressor's browser demo.

CLAUDE.md:
- `:3` "after its v1, HardwareReverb": the migration is done.
- `:6` `s<N>.md` only.
- `:45-46`: no rule for JUCE-free code or for where an Emscripten header may be.
- `:54`: "Done" names `agent` and `agent-gui` only, not `nojuce` or `web`; no Chrome-muted rule and no cache rule.

Comments in lead-owned files, also stale: `cmake/FcmpWeb.cmake:3-4` ("from a later sprint … what Sprint A delivers"); `cmake/FcmpProbes.cmake:7,10` (`[platform=apple|linux]`); `ci.yml:10-13`.

## Traps
1. **CLAUDE.md is loaded by every session.** The proposal costs 21 lines. The largest saving is the "A browser, when a card needs one" bullet (3 lines), if no future card drives Chrome.
2. **Counts go stale with every Mode** (15 web-tree tests per Mode). Write them only in ADR-93, dated.
3. **A document commit makes the tree dirty.** `verify.sh` then writes no stamp and `built-from.txt` says dirty. Commit the documents, rebuild (`cmake --build --preset web`), then gate (03 §4.8 step 6). The site of record should be rebuilt on main; today's names 7b2d165.
4. **03 `:594-643` is copied CMake.** Adding `platform=` there must match `cmake/FcmpProbes.cmake:215-217`, or say "as of FZ0; the file is the truth".
5. **README is public.** "Nothing is hosted yet" and "Chrome on macOS only" must change the day they stop being true.
6. **Serve on 127.0.0.1 or localhost only.** An http LAN address is not a secure context, and the page refuses it in its own words.
7. **`[WEB]` as written runs the tests twice** (workflow test step, then `verify.sh`); one pass is 202 s wall here.
8. **`--integration` on the web tree** needs the FunkGui pin itself; an override also turns `built-from.txt` dirty.

## Tests (each can fail)
1. **`lint.docs`** (prototype `S/doccheck.py`, 50 lines, no dependency). Today four of five rows fail; with the proposed CLAUDE.md, `claude.presets` passes.
   - `claude.presets`: every visible configure preset is named in CLAUDE.md. Today `['web']` is missing.
   - `emscripten.pin`: `FcmpDeps.cmake` = `ci.yml` = every "Emscripten x.y.z" in CLAUDE.md and README, at least one each. Today there is none.
   - `rules.documented`: every `lint.deps` rule id is in 01 §2.2. Today seven are missing.
   - `ci.jobs`: every `ci.yml` job is named in 03 §4.8. Today all five are missing.
   - `readme.modes`: the README's number word and table rows equal `Modes.def`. Passes: 14.
2. **`lint.deps` already holds every proposed rule sentence.** 19 planted lines in a scratch copy gave 21 violations, each with the expected rule id (`web.engine`, `web.facade`, `web.ui`, `web.emscripten`, `web.juce`, `editor.juce`, `plugin.portable`); the baseline gave 0.
3. **A row for the README's serve command**, if web-live does not cover it: the site copied elsewhere passes `?selftest=1` from `python3 -m http.server` (my run: PASS in 2.2 s).

## Open questions, with recommendations
1. **`[WEB]` form?** Keep the workflow form for symmetry with `[AGENT]`, or use `cmake --preset web && cmake --build --preset web && Scripts/verify.sh --strict …` (one test pass; CI does this). I recommend the second; the diff above uses the first, as `web-d.md` did.
2. **`web/**` lead-only, with "a Sprint-0 card's OWNS" generalised to "a card's OWNS"?** Yes: U-1 and U-2 owned `web/` files, and the follow-ups will too.
3. **Add rules 7–10 to 01 §2.2?** Yes; otherwise CLAUDE.md's section header must cite ADR-93 and `LintDeps.cmake` instead.
4. **03: one §2.12 plus pointers, or rows in every table?** §2.12 plus pointers.
5. **ADR-93's title version.** "(after v1.1; built 2026-10-01 to 02; not published)". `CMakeLists.txt:54` still says 1.1.0 and v1.2.0 is untagged.
6. **One gzip method.** `web.size`'s (node zlib 9): 648,135 for the site.
7. **Add `lint.docs` to the gate?** Yes, the four structural rows plus `readme.modes`; it is how 03's regex, job counts and agent cap went stale unnoticed.
8. **Frozen-index rows for the C ABI, the protocol and the seam?** Yes, two rows.
9. **FunkGui documents.** One docs-only commit on its main, no tag (the pin is a tag; nothing a consumer compiles changes).
10. **`[WEB]` in the definition of done** for cards touching what the browser compiles? Yes (in the CLAUDE.md text above); CI's `web` job is otherwise the first to see it.

## Facts the lead must supply
- `Scripts/web-live.sh`: its arguments, lock, result directory, last line and rows; whether it runs in CI.
- The CI artifact's name, the `web` job's final name and timeout, and whether the downloaded artifact passed from a plain server.
- The lead-phase PR number, date and main SHA.
- Sprint C's review summary (count and kind).
- The by-hand browser results: Chrome version, any Intel or AMD machine, context loss, dropped file, PNGs.
- Whether anyone listened to the demo.
- The x86 runner's final speed and tail numbers (PR #67's "Numbers" step).
- The hosting and demo-audio decisions (both pending).

## What I ran
- **Read whole:** CLAUDE.md, README.md, ARCHITECTURE.md, ADR-92 and ADR-93, the plan's lead phase, `web-d.md`, `LintDeps.cmake`, `FcmpWeb.cmake`, `FcmpWebSite.cmake`, `CMakePresets.json`, `CMakeLists.txt`, `ci.yml`, FunkGui's README and CLAUDE.md, 01 §2.1-2.3, SPRINTS §0 and §5-8.
- **Read in part:** 03 (§0-§2.2, §2.7-§3.3, §3.6-§3.9, §4.1-§4.8), `FcmpProbes.cmake`, `verify.sh`. 02 was not read.
- **Read-only in the repository:** `git log/diff/show`, `build-web/verify-{tests.json,junit.xml,ctest.log}`, `build-lead/verify-tests.json`, `gzip -9 -c` of the site's files to stdout, `em-config CACHE`.
- **In `S/lint`:** a copy of `Source/`, `Tools/` and `LintDeps.cmake`; `cmake -DFCMP_SOURCE_DIR=S/lint -P cmake/LintDeps.cmake` gave 0 violations in 1.6 s, then 21 after 19 planted lines.
- **In `S/served/any/path/demo`:** a copy of `build-web/site`; `node …/check-page.mjs <copy> --page fcmp-ui --chrome-flag --use-angle=metal --chrome-flag --autoplay-policy=no-user-gesture-required --chrome-flag --mute-audio` printed `check-page: PASS` (HeadlessChrome 154; self-check hash 5a96ce217d29ca6f on main thread and worklet; 0 of 480,000 frames differ; 117× real time; pixels within 1 of 255).
- **In `S`:** `CLAUDE.after.md`, `CLAUDE.diff`, `doccheck.py` (output above).
- `pgrep -fl "http.server|headless"` is empty after the run. Nothing was edited, built or tested inside either repository; no network; no Safari.