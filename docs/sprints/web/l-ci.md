# Scout report: lead phase, CI

## Headline
- The Chrome gate cannot go green on a GPU-less Linux runner as the page stands: under SwiftShader `editor.pixels` fails (largest difference 7 of 255, about 1,600 of 2,457,600 samples over 2); every other row passes. The page needs FunkGui's software bound first; a scratch copy with it passes on Metal and on SwiftShader.
- Smallest gate: in the `web` job, upload `build-web/site`, download it again, and run FunkGui's `check-page.mjs` on the downloaded copy. Dry-run here step by step; it needs no new script.
- Firefox and Safari need a WebDriver runner. It is written (283 lines, no dependency) and passes 46 rows against a fake driver, each mutation tried turning a row red. It has never met a real chromedriver, geckodriver or safaridriver, so those legs only report.

All scratch files are in `S` = `/private/tmp/claude-501/-Users-seanfunk-audio-plugins-FCompressor/1e18d990-0133-428b-8995-1664ebc8733a/scratchpad/scout-l/ci`. The diff (189 lines) and the runner do not fit in 250 lines, so they are delivered as files: copy them out before the session directory goes.

## Facts
1. **The web job today** (`.github/workflows/ci.yml`):
   - Named "Web engine (wasm)" (:164); the pin is repeated by hand as `FCMP_EMSCRIPTEN: 6.0.3` (:168).
   - Runs `Scripts/verify.sh --strict build-web` (:199).
   - Prints `simdbench`, `speed`, `tail` to the log only, each with `|| true` (:200-206): nothing is recorded.
   - Uploads only `fcmp-engine.wasm` as `web-engine` (:207-212).
   - No other file names `web-engine` or the job name (grep over docs, README, CLAUDE, cmake, web, Scripts).
2. **The pin**: `cmake/FcmpDeps.cmake:243-247` is a FATAL_ERROR unless the toolchain is 6.0.3. This sed reads it and printed `6.0.3`: `sed -n 's/^ *set(FCMP_EMSCRIPTEN_VERSION \([0-9][0-9.]*\))$/\1/p' cmake/FcmpDeps.cmake`.
3. **`--strict`** (`Scripts/verify.sh:23-24, 281-285`): DRIFT and MISSING fail too. The summary line (:237) goes to stdout only; `build-web/verify-ctest.log` holds ctest's output and no summary. The web tree has 232 tests (ui 219, web 12, lint 1; `verify-tests.json`), 201.66 s here.
4. **built-from.txt**:
   - It says `clean` only when `git status --porcelain` is empty (`cmake/FcmpBuiltFrom.cmake:38-43`) and FunkGui is the pin (`cmake/FcmpWebSite.cmake:103-125`).
   - Scratch clone, detached HEAD: clean. With a file under `build-web/`: clean. With `page.png` or `site/` at the workspace root: **dirty**.
   - The local site says `site 7b2d165… clean` while HEAD is 58f13e9 (same tree `dc9a125e`; the merge commit was never built).
   - On `pull_request`, checkout's HEAD is the merge commit, so the footer would link `/tree/<merge sha>` (`web/main.js:142`).
5. **The page's protocol** (`web/main.js`):
   - Self-test by `?selftest=1` or the path `fcmp-ui.html` (:189-190); the log line is written before the title (:208-209).
   - Titles are RUNNING, PASS, `FAIL: <row>` (:159-161); watchdog 75 s (:747).
   - With no gesture the context stays suspended and everything except `page.audio` is judged (:615-616, :678-691). Confirmed by run: "the live audio is not judged".
6. **`check-page.mjs`** (`build-web/_deps/funkgui-src/tools/web/`):
   - Defaults are Metal on macOS, else `--use-angle=swiftshader --enable-unsafe-swiftshader` (:82-84); any `--chrome-flag` replaces them.
   - Serves with `python3 -m http.server` (:148); finds `google-chrome` on PATH (:78); passes no `--no-sandbox`.
   - Uses the global `WebSocket` (:164), which needs node 22 or later.
7. **Runs on a copy of the site** (Chrome 154 headless, always muted):

   | flags | verdict | pixels |
   |---|---|---|
   | `--use-angle=metal` | PASS in 2.5 s | largest 1, 0 over 2 |
   | SwiftShader | **FAIL: editor.pixels** | largest 7, 1588 to 1612 over 2 (0.65 per mille) |
   | SwiftShader + autoplay | same FAIL; `page.audio` PASS (114 quanta in 0.3 s) | same |
   | SwiftShader + autoplay + `--disable-audio-output` | same FAIL; `page.audio` PASS | same |
   | `--disable-gpu`, or `--use-angle=gl` | FAIL: browser (no WebGL2) | n/a |

   Under SwiftShader the editor still draws at 25 to 28 fps and all other rows pass.
8. **FunkGui already has the software bound**: `FunkGui/test/web/page.cpp:25-45, 160-162, 379` passes a frame when no sample differs by more than 16 and at most 10 per mille by more than 2, and documents SwiftShader at 8 and 5.1 per mille. Its page run here gave 1 on Metal, and 8 with 4.95 per mille on SwiftShader, VERDICT PASS. FCompressor's row is `pixels.over2 === 0` (`web/main.js:653`). No node test pins that rule.
9. **Registration and ownership**:
   - FunkGui registers `fg.web.page` with an explicit `add_test`, labels `fg;live` (`FunkGui/test/web/CMakeLists.txt:32-35`).
   - FCompressor's registrar hard-codes `verify;web;global` (`cmake/FcmpWeb.cmake:242`) and scans `web/tests/*.mjs` (:56). The site takes only `web/*.html|js|css|svg` (`FcmpWebSite.cmake:46-47`).
   - `gui-live.sh` is a script plus a custom target, not a ctest (`cmake/FcmpProbes.cmake:295-304`).
   - `Scripts/**` is lead-only in CLAUDE.md; `.github/**` and `web/**` are not listed yet (`plan.md:420` adds them). `LintDeps.cmake` rules cover `Source/**` only.
10. **This machine**: node 24.15.0, Chrome 154.0.8037.93, no chromedriver, no geckodriver, no Firefox. `safaridriver` exists and was never run. FunkGui has no `.github`.
11. **Runner images, from memory, not verified** (no network):
    - `ubuntu-24.04`: Chrome with chromedriver in `$CHROMEWEBDRIVER`, Firefox with geckodriver in `$GECKOWEBDRIVER`, python3, node 20/22/24 cached. About 90 % sure.
    - macOS images: Safari with `/usr/bin/safaridriver`, remote automation enabled at image build. About 80 %; `macos-26` assumed alike.
12. **What an artifact exposes** (about 90 % sure): a zip on the run page and the API. In a public repository any signed-in GitHub user can download it; it is not anonymous and never served as a page. Default retention is 90 days; `retention-days` takes 1 to 90.

## Design and change list
**A. The page (prerequisite): `web/main.js:653`.** Judge `editor.pixels` by renderer class. A GPU keeps "none over 2". A renderer whose name matches `/swiftshader|llvmpipe|softpipe|software|basic render/i` gets FunkGui's bound (largest at most 16, at most 10 per mille over 2). The name is read from a throwaway canvas and logged as a NOTE. Prototype: `S/site-class/main.js`.
- Metal: PASS, "a GPU: none over 2", largest 1.
- SwiftShader: PASS, "software", 7 and 0.65 per mille.
- `main.js` grows by 1,093 bytes (2.8 %, inside `web.size`'s 20 %).
- Simpler variant, also run and passing on both: FunkGui's bound for every renderer. It loosens the Mac's row.

**B. `ci.yml`, the `web` job.** Full file `S/ci.proposed.yml` (parses as YAML), diff `S/ci.diff`. The new steps, in order after Build:
```yaml
    name: Web demo (wasm)
    defaults: { run: { shell: bash } }          # -eo pipefail: a step that tees still fails
    # Toolchain step, added: the pin is read from cmake/FcmpDeps.cmake (the job-level env is removed)
    #   V=$(sed -n 's/^ *set(FCMP_EMSCRIPTEN_VERSION \([0-9][0-9.]*\))$/\1/p' cmake/FcmpDeps.cmake)
    #   [ -n "$V" ] || exit 1;  echo "FCMP_EMSCRIPTEN=$V" >> "$GITHUB_ENV"
      - name: Verify
        run: Scripts/verify.sh --strict build-web | tee build-web/verify-out.txt
      - name: Numbers (the x86 runner's)       # same three checks, now: ... 2>&1 | tee build-web/numbers.txt
      - name: The site is this commit's
        run: grep -q "^site $(git rev-parse HEAD) clean " build-web/site/built-from.txt || { cat build-web/site/built-from.txt; exit 1; }
      - name: Upload the site                   # replaces "Upload the engine" (web-engine)
        uses: actions/upload-artifact@v4
        with: { name: web-site, path: build-web/site, if-no-files-found: error, retention-days: 14 }
      - name: Download the site again
        uses: actions/download-artifact@v4
        with: { name: web-site, path: "${{ runner.temp }}/web-site" }
      - name: Page (the downloaded site, a plain static server, headless Chrome)
        run: |
          diff -r build-web/site "$RUNNER_TEMP/web-site"
          google-chrome --version
          "$EMSDK_NODE" build-web/_deps/funkgui-src/tools/web/check-page.mjs "$RUNNER_TEMP/web-site" --page fcmp-ui \
            --chrome-flag --use-angle=swiftshader --chrome-flag --enable-unsafe-swiftshader --chrome-flag --mute-audio \
            | tee build-web/page-chrome.txt
      - name: Summary                           # if: always(); appended to $GITHUB_STEP_SUMMARY
```
- The summary holds: the built-from line, `GITHUB_SHA` and the pull request's head sha, Emscripten and node versions, verify.sh's summary line, Chrome's verdict and log, the size table (from `web/tests/size.mjs`'s "measured now" lines, with a totals row), and the speed table.
- `page-chrome.txt` joins the failure artifact `web-results`.
- The gate runs with the context suspended on purpose: it is deterministic, and it is what a visitor's first load is.

**C. `ci.yml`, new job `web-browsers`** (reported, never the verdict):
```yaml
  web-browsers:
    name: Web page (${{ matrix.browser }}, reported)
    needs: web
    runs-on: ${{ matrix.os }}
    timeout-minutes: 10
    continue-on-error: true
    strategy:
      fail-fast: false
      matrix:
        include:
          - { browser: chrome,  os: ubuntu-24.04 }   # the runner script's control: `web` already passed this browser
          - { browser: firefox, os: ubuntu-24.04 }
          - { browser: safari,  os: macos-26 }
    steps:
      - uses: actions/checkout@v5
        with: { sparse-checkout: Scripts/web }
      - uses: actions/setup-node@v4
        with: { node-version: 24 }
      - uses: actions/download-artifact@v4
        with: { name: web-site, path: site }
      - name: Browser and driver                # versions; the safari leg also: sudo /usr/bin/safaridriver --enable
      - run: node Scripts/web/page-check.mjs site --browser ${{ matrix.browser }} --screenshot "$RUNNER_TEMP/page.png" --log "$RUNNER_TEMP/page.txt"
      - if: always() && matrix.browser != 'safari'
        run: node Scripts/web/page-check.mjs site --browser ${{ matrix.browser }} --autoplay --log "$RUNNER_TEMP/page-audio.txt"
      # then, if: always(): the logs into the summary; upload web-page-<browser> (page.png, page*.txt; 14 days)
```

**D. `Scripts/web/page-check.mjs`** (new, lead-owned; prototype `S/page-check.mjs`).
- Flow: its own static server on 127.0.0.1 (GET and HEAD, a type by extension, nothing above the directory); start the driver on a free port; wait for `/status`; New Session; Navigate; poll one Execute Script returning `[title, log]` every 250 ms; record the WebGL renderer; Take Screenshot; Delete Session; SIGTERM then SIGKILL to the driver's process group. Exit 0, 1 or 2; every request is bounded by the deadline.
- Chrome: `--headless=new --mute-audio --window-size=1200,900`, Metal on macOS and SwiftShader elsewhere; `--autoplay` adds the autoplay policy.
- Firefox: `-headless`, prefs `media.volume_scale: "0.0"` (a string pref), `webgl.force-enabled: true`; `--autoplay` adds `media.autoplay.default: 0` and `blocking_policy: 0`.
- Safari: `{browserName: 'safari'}` only (no headless, no mute, no options). It is refused unless `GITHUB_ACTIONS=true` or `--safari-here` is given.
- `--serve [--port N]` serves the site for the plan's hand tests. Real Chrome passed the real site from this server (`check-page: PASS`).

**E. `web/tests/pagecheck.mjs` and its fake driver** (new; prototypes `S/page-check.test.mjs`, `S/fake-driver.mjs`). Give it `// FCMP_WEB_TEST name=web.pagecheck timeout=180 args={source}`. It takes 21 s and starts no browser; the web tree becomes 233 tests. The fake driver file carries no FCMP_WEB_TEST line, so it registers nothing.

**F. One tool or two (question 4): two tools, one protocol.**
- `web-live.sh` stays a wrapper over the pinned FunkGui's `check-page.mjs`: DevTools needs no driver, and this Mac has none.
- `page-check.mjs` is WebDriver, the only protocol Firefox and Safari share.
- No ctest for the browser run; `gui-live.sh` is the precedent.

**G. Which run proves "the downloaded artifact passes" (question 3).** The in-job upload, download, `diff -r`, `check-page.mjs` sequence is the least machinery: three steps, a proven tool, a python server nobody here wrote. The separate job repeats it on a second machine and is the only way to reach Safari.

## Traps
1. **SwiftShader fails today's pixel row** (fact 7). Without change A the gate is red on any GPU-less runner.
2. **A file in the workspace before the build makes the site `dirty`** (fact 4). Keep every by-product under `build-web/` or `$RUNNER_TEMP`; the step "The site is this commit's" fails otherwise (it failed here on the 7b2d165 site against HEAD 58f13e9).
3. **On a pull request the site names the merge commit.** A site meant for hosting must come from a push to `main`.
4. **`run:` on Linux is `bash -e` without pipefail**: `verify.sh | tee` would pass a failed gate. Hence `shell: bash`.
5. **Any `--chrome-flag` replaces the runner's GPU default.** A flag group passed as one word gives "FAIL: browser" (my own zsh mistake showed it).
6. **`$EMSDK_NODE` must be node 22 or later** for `check-page.mjs`. ci.yml:193 already prints its version in every past web log.
7. **Chrome's sandbox on ubuntu-24.04**: if Chrome exits before its DevTools line, add `--chrome-flag --no-sandbox` (or `--arg --no-sandbox`).
8. **The job is renamed** and three checks are added. Whether `main` requires checks by name must be read by the lead: `gh api repos/Snipet/FCompressor/branches/main/protection`.
9. **How a `continue-on-error` leg shows in the pull request's check list** I do not know for certain (red X or green); the summary carries the log either way.
10. **Autoplay with no audio device**: a context that says "running" while no quanta flow fails `page.audio`. Keep `--autoplay` out of the gate.
11. **The pixel count moves run to run** (1588 to 1612): never pin a count.
12. **Firefox, unverified**:
    - A snap Firefox breaks geckodriver's profile directory.
    - Headless WebGL2 without a GPU may be refused despite `webgl.force-enabled` (fallback: `xvfb-run` with `--headed`).
    - The page asks the offline worklet for its self-check before `startRendering()`; Firefox may not service that port until rendering starts.
13. **Safari, unverified**: a visible window, no mute. Unknown: WebGL2 in the VM; whether the worklet answers on a suspended context; whether the looped offline render is sample-exact.
14. **A driver that dies raced the failed fetch** in my first prototype (two exit messages). Fixed, and `crash.exit` holds it.
15. **`upload-artifact` strips the directory name and skips hidden files.** `diff -r` after the download catches a lost file (run here: exit 1, "Only in build-web/site: loop.js").

## Tests (each seen failing)
- **`web.pagecheck`, 46 rows**: three verdicts and their exit codes; cleanup (session deleted, driver gone, also on timeout and on SIGTERM to the runner); a hung page; no session; a driver that crashes; a lost session; a driver never ready; a non-JSON reply; a script error; a failed screenshot; a missing driver; a driver that ignores SIGTERM; capabilities per browser; the Safari refusal; the server (wasm type, HEAD, 404 reported, nothing above the site in three spellings, POST 405); usage.
- **Mutations of the runner, each turned rows red**: no Delete Session (6 rows); no `--mute-audio` (3); no path check (`server.above`: 200); FAIL exits 0 (`fail.exit`); no kill (10); volume as a number (2); Safari anywhere (`caps.safari.refused`); no exit watch (`crash.exit`); no log at the end (`crash.log`).
- **CI step "Page"**: exit 1 on today's page under SwiftShader, exit 0 with change A, exit 1 when the artifact lost a file.
- **CI step "The site is this commit's"**: exit 1 on a site built from another commit.
- **Page row `editor.pixels` by class**: a GPU over 2 fails as today; software over 16 or over 10 per mille fails.

## Open questions, with recommendations
1. **Pixel rule: by renderer class, or FunkGui's bound everywhere?** By class: the Mac and any hand run on a GPU keep 2 of 255.
2. **Gate in the `web` job or in a separate job?** In the job. A separate Chrome gate is rerunnable alone but needs either the untested WebDriver path or a second checkout of FunkGui at the pin.
3. **Keep the Chrome leg in `web-browsers`?** Yes, for the first rounds: it tells a runner bug from a browser difference. Drop it once Firefox and Safari have reported.
4. **Rename the job?** Yes, "Web demo (wasm)", after reading the branch protection.
5. **Keep `web-engine`?** No: the site holds the engine, and nothing names the old artifact.
6. **Retention?** 14 days. The hand tests happen within days, and hosting will build its own.
7. **Register `web.pagecheck` in the gate?** Yes: a CI-only script with no test rots. It costs 21 s, and ADR-93's "232 tests" becomes 233.
8. **Read the Emscripten pin from CMake?** Yes: one pin; a mismatch today costs a failed round.
9. **Promote Firefox or Safari to a gate?** Not before each has passed several runs; say so in ADR-93.

## What the lead must verify by running CI, in order
0. **Before pushing**: read the node version in the last web job's log (trap 6) and the branch protection (trap 8).
1. **Round 1**: change A, the `web` job changes, the three reported legs, `page-check.mjs` and its test. It answers:
   - Chrome on the runner: present, sandbox, SwiftShader WebGL2, the software pixel numbers on x86, `engine.silence` on a shared runner.
   - The artifact round trip and the built-from step on a merge commit.
   - How the summary renders.
   - Through the chrome leg, whether the runner works with a real chromedriver.
   - First data from geckodriver and safaridriver, and how a failed reported leg is displayed.
2. **Round 2**: fix the runner from the chrome leg's log, if needed. Read the `--autoplay` runs: does a context run with no audio device (`page.audio`)?
3. **Round 3**: Firefox (traps in 12) and Safari (13). Each FAIL names its row; a page change belongs to the lead.
4. **After the merge, on `main`**: the site names main's commit and the footer link resolves.

## What I ran
- Read whole: `ci.yml`, `FcmpDeps.cmake` (the pin), `FcmpWeb.cmake`, `FcmpWebSite.cmake`, `FcmpBuiltFrom.cmake`, `verify.sh`, `web/main.js`, `check-page.mjs`, ADR-93, the plan's lead phase, `web-d.md`, FunkGui's `test/web/CMakeLists.txt`.
- Thirteen headless Chrome runs through scratch copies of `check-page.mjs`, every one with `--mute-audio`:
  - Nine on scratch copies of the site (`S/site`, `S/site-class`, one FunkGui-rule copy).
  - Two on a scratch copy of FunkGui's own sink page.
  - One with the runner's own server.
  - Two with the flags mis-split.
- `page-check.test.mjs`: 7 green runs and 9 mutation runs.
- The proposed shell steps under `bash -eo pipefail` in a scratch clone (`git clone --depth 1 file://…`). The clone carries stand-in files under `build-web/`; it is not a build.
- `ruby -ryaml` on the proposed workflow.
- `fcmp_web_check.js speed` from a scratch copy, three times, for the summary.
- Nothing was built, and nothing was written under FCompressor or FunkGui (`git status` clean in both). No network, no install, Safari never started.
- No process of mine is left (`pgrep -f scout-l/ci`: 0). One python server and one Chrome profile under `scout-l/hand` belong to another scout and were left alone.