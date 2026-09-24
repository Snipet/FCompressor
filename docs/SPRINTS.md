# FCompressor sprint plan

Status: **executable sprint plan, written 2026-09-22 (design phase; no code exists).** It turns 03 §4.9 into sprints
S0 (bootstrap) … S13 (v1): 42 task cards, at most 3 per sprint, plus the lead's serial steps. It supersedes the
sprint table of 03 §4.9.3 and refines the work items of 03 §4.9.2; every difference, with its reason, is listed in §7
so the lead can patch 03 at the next freeze. Where this plan and 03 §4.9 disagree about *scheduling or ownership*, this
plan wins; for everything else (contracts, probes, process) the appendices win.

Sources: `docs/ARCHITECTURE.md`, `docs/DECISIONS.md` (ADR-nn, Qn), `docs/design/01-core-contracts.md` (01),
`02-funkgui-and-ui.md` (02), `03-build-verify-process.md` (03), critiques `K2-risk.md` (K2 #n) and `K3-parallel.md`
(K3 #n), research `docs/research/{A..F}-*.md`. HR = `/Users/seanfunk/audio/plugins/HardwareReverb` (read-only).

---

## 0. How to read and run this plan

### 0.1 Rules

1. **≤ 3 agent cards per sprint across both repositories.** The lead is not one of the 3 (Q1; if the lead counts, run
   the same cards in the same order two at a time: 21 sprints). Steps marked **LEAD** are serial and run at the sprint
   base (before agents spawn) or at the sprint end. **USER** marks an action only the user may take (system install,
   keychain credentials).
2. **Merged before use.** A card depends only on work merged at an earlier sprint boundary. FunkGui work is consumed
   only as a **tag** (the pipelining rule, 03 §4.5): a FunkGui feature an FCompressor card needs in sprint N is tagged at
   the end of sprint N−1 or earlier.
3. **Disjoint ownership.** Cards in one sprint own disjoint paths. Every sprint section ends with an explicit
   ownership check. Ownership may move between sprints (e.g. F4 creates `dsp/null.cpp`, F7 extends it later).
4. **Sizes** (the user's scale): **S** ≤ ~500 new lines, under half a session; **M** ~500–1,200 lines, most of one
   session; **L** ~1,200–2,000 lines, one full focused session. Anything larger is split. Every L card names a
   **split seam** the lead uses if it runs long.
5. **Card → manifest.** At the sprint base the lead copies each card into `docs/sprints/s<N>.md` in 03 §4.2's manifest
   format: `TASK` = card id; `OWNS` = the card's OWNS line verbatim; `FROZEN` = the sprint's frozen set (§0.3);
   `FUNKGUI` = the pin; `DONE` = the card's Acceptance. `Scripts/sprint/ownership.py docs/sprints/s<N>.md#<card id>
   <worktree>` reads the OWNS line (globs with `*`, `**`, `{a,b}`).
6. **Names.** FCompressor worktrees are harness-made under `.claude/worktrees/`, branch renamed `s<N>/<code>` (e.g.
   `s2/f3`). FunkGui worktrees are `/Users/seanfunk/audio/libraries/FunkGui.wt/s<N>-<code>`, branch `s<N>/<code>`.
   Read-only FCompressor worktrees for FunkGui cards: `.claude/worktrees/ro-s<N>-<code>` (K3 #22).
7. **Definition of done** for every card is 03 §4.7 (ownership clean; no warnings; `verify.sh` 0 blocking; a reason per
   candidate group; PNGs for UI; ≤ 40-line handoff) **plus** the card's Acceptance.

### 0.2 Standard commands (cards cite them by tag)

`$WT` is the FCompressor worktree, `$FWT` the FunkGui worktree, both written literally into every command (03 §4.3).

| Tag | Command |
|---|---|
| **[DSP]** | `cd "$WT" && cmake --workflow --preset dsp-verify` |
| **[AGENT]** | `cd "$WT" && cmake --workflow --preset agent-verify && Scripts/verify.sh "$WT/build-agent"` |
| **[GPU]** | `cd "$WT" && cmake --workflow --preset agent-gui-verify` |
| **[OWN]** | `cd "$WT" && python3 Scripts/sprint/ownership.py docs/sprints/s<N>.md#<id> "$WT"` (FunkGui cards: the lead runs it from the FCompressor checkout against `$FWT`) |
| **[FG]** | `cd "$FWT" && cmake --workflow --preset agent-verify && cmake --workflow --preset agent-gui-verify && tools/verify.sh "$FWT/build-agent-gui"` |
| **[XV]** | FunkGui cards from S1: `RO=/Users/seanfunk/audio/plugins/FCompressor/.claude/worktrees/ro-s<N>-<code>; cd "$RO" && cmake --preset agent -DFETCHCONTENT_SOURCE_DIR_FUNKGUI="$FWT" && cmake --build --preset agent && Scripts/verify.sh "$RO/build-agent"` |
| **[PNG]** | `<build>/fcmp_probe_plugin_artefacts/<Config>/fcmp_probe_plugin ui.dump --mode <key> --golden-root tests/golden --arch arm64 -- --view <id> --out <build>/png/<id>-<key>.dump --png <build>/png/<id>-<key>.png` (S5 revision: probe-own flags after `--`) |

CTest labels (03 §2.9): `verify`; layer `dsp`/`proc`/`ui`/`lint`; `global` or `mode:<key>`; `probe:<layer>.<name>`.
FunkGui tests: `verify`, `fg`, plus `gpu` (needs `FUNKGUI_WITH_BGFX`) or `live` (needs a window server; never in
`verify`). A selector such as `ctest --preset agent -L 'probe:dsp\.(simd|units)'` runs one card's probes.

### 0.3 Frozen sets (the `FROZEN` line of every FCompressor manifest)

Frozen means: changed only by a lead-approved revision between sprints (01 §0). A card may **add bodies** to a frozen
header it OWNS (F1 core, F3/F9 `ModeEngine.h`, F7 `Crossfade.h`), but never rename, remove or change a frozen
declaration.

| From sprint | Added to the frozen set | Freeze point |
|---|---|---|
| S1 | `Source/fcdsp/**/*.h` as written by F0 (01 §3–§8); `Modes.def` grammar; `cmake/**`, `CMakeLists.txt`, `CMakePresets.json`, `Scripts/**` (lead-owned from here); `Tools/probes/common/{ProbeRegistry.h,ProbeMain.cpp}` (probe macro and CLI); Harness v2 API; FunkGui CMake target and function names | FZ0 (end S0) |
| S2 | FunkGui `include/funkgui/{core,text,canvas,panel,params,widgets,a11y}/**` (v0.2.0); `Source/plugin/ProcessorFacade.h`; `resolve`/`snap` semantics | FZ1 (end S1) |
| S3 | `ModeEngine<T>`/`EngineRig` behaviour; `kStdLatency`, `kStdUpDelay`, `kHqLatency`, `kHqUpDelay` (**v1-forever**) | FZ2 (end S2) |
| S5 | `ParamSpec`/`ModeDescriptor` schema, proven on 8 descriptors | FZ3 (end S4) |
| S6 | `Source/editor/{SubView,Panel,Layout,Tags,HistoryStore,PreviewWorker,SlotModel}.h`, every class declaration in `Source/editor/views/*.h`, `Tools/probes/plugin/FakeFacade.h` | FZ4 (end S5) |
| v1 | `ui.geometry`/`ui.a11y`/`ui.input` golden rows (FZ5, S13); the v1-forever set of 01 §0 | FZ5, v1 tag |

---

## 1. Overview

| Sprint | Goal | Card 1 | Card 2 | Card 3 | FunkGui pin → tag | Freeze / gates |
|---|---|---|---|---|---|---|
| **S0** | Both repos configure; verification plumbing; GPU build chain | S0.1 **G1**\* FunkGui build, tools, GPU spike (FG, L) | S0.2 **F0** fcdsp contracts (L) | S0.3 **B0** build skeleton (L) | v0.0.1 → **v0.1.0** | FZ0; validate (stub) |
| **S1** | Math core, resolver, FunkGui API | S1.1 **F1**\* math core + determinism (L) | S1.2 **F2** resolver, text, Clean descriptor (M) | S1.3 **G2** FunkGui API + utilities (FG, L) | v0.1.0 → **v0.2.0** | FZ1 (+ LEAD facade) |
| **S2** | First engine, oversampler, recorder | S2.1 **F3** walking skeleton (L) | S2.2 **F6**\* oversampler (M) | S2.3 **G3**\* recorder + gallery infra (FG, L) | v0.2.0 → **v0.3.0** | FZ2 |
| **S3** | Clean complete + FB; host at ECO; shapes | S3.1 **F9**\* Clean + feedback solvers (L) | S3.2 **F4** EngineHost ECO (L) | S3.3 **G4** shapes, AREA, glyphs (FG, M) | v0.3.0 → **v0.4.0** | first Clean goldens |
| **S4** | Schema proven on 8 Modes | S4.1 **DW**\* descriptor wave (L) | S4.2 **F5** SC filter, router, link (M) | S4.3 **G5** RuleSlider, words (FG, M) | v0.4.0 → **v0.5.0** | FZ3; milestone gates |
| **S5** | Analysis; UI skeleton; cell widgets | S5.1 **F8** analysis (M) | S5.2 **U1a** UI skeleton (L) | S5.3 **G6** cell widgets (FG, L) | v0.5.0 → **v0.6.0** | FZ4 |
| **S6** | Host complete; slots; chrome | S6.1 **F7** EngineHost complete (L) | S6.2 **U1s** slots (L) | S6.3 **U1b** chrome (M) | v0.6.0 | – |
| **S7** | Real processor; band; Bus G | S7.1 **P1** processor (L) | S7.2 **U2** band (L) | S7.3 **M1** Bus G (M) | v0.6.0 | first real validate |
| **S8** | State; Characteristics B; GPU host | S8.1 **P2** state (M) | S8.2 **U4** Characteristics B (M) | S8.3 **G7**\* EditorHost + live parity (FG, L) | v0.6.0 → **v0.7.0** | milestone gates; gui-live (gallery) |
| **S9** | FET 76, Opto 2A; Characteristics A | S9.1 **M2** FET 76 (M) | S9.2 **M3** Opto 2A (M) | S9.3 **U3** Characteristics A (M) | v0.7.0 | – |
| **S10** | Mu 67, Diode 609; GPU editor | S10.1 **M4** Mu 67 (L) | S10.2 **M5** Diode 609 (M) | S10.3 **U7** GPU editor (M) | v0.7.0 | first FCompressor gui-live |
| **S11** | Bus 25, Brickwall; preset library | S11.1 **M6** Bus 25 (M) | S11.2 **M7** Brickwall (M) | S11.3 **G8** FunkPresets (FG, L) | v0.7.0 → **v0.8.0** | all 8 Modes final |
| **S12** | Presets; browsers | S12.1 **P3** presets + factory bank (M) | S12.2 **U5** Mode browser (M) | S12.3 **U6** preset strip/browser (M) | v0.8.0 | milestone gates |
| **S13** | Harden; release; **v1** | S13.1 **H1a** UI audit + freeze prep (M) | S13.2 **H1b** RT/sanitiser fixes (M, conditional) | S13.3 **R1** release script (S) | v0.8.0 | **FZ5; v1.0.0** |

\* = spike (settles an early risk, 03 §4.9.5). FG = FunkGui repository; everything else is FCompressor.

**Why 14 sprints, not 03's 13.** 03 §4.9 had 39 work items. This plan has 42 cards: U1a is split (it was larger than
one session), H1 is split, and the release script gets a card. 42 / 3 = 14. S13.2 is conditional, so it doubles as
the plan's only buffer slot.

**The shippable v1 (end of S13):** signed, notarised **universal** AU/VST3/Standalone (needs Q6 = universal, see §8);
8 non-provisional Modes; the always-visible band and the full-panel Characteristics screen; factory and user presets;
`verify` (≈ 207 tests), `validate.sh`, sanitiser and `gui-live` gates green.

---

## 2. Dependency graph and critical path

### 2.1 Graph, by sprint

`X ◄ A, B` means X needs A and B **merged at an earlier boundary** (for FunkGui: tagged). Every arrow below points to
an earlier sprint; that is the check that the schedule is legal.

```
pre   G0 (LEAD: HR snapshot + Harness v2 + bootstrap CMake → FunkGui v0.0.1)      L-base (LEAD: deps cache, base commit)
S0    G1 ◄ G0                   F0                              B0 ◄ G0
S1    G2 ◄ G1                   F1 ◄ F0, B0                     F2 ◄ F0, B0
      └────────────► LEAD-FZ1: Source/plugin/ProcessorFacade.h ◄ G2 (v0.2.0)
S2    G3 ◄ G2                   F3 ◄ F1, F2                     F6 ◄ F1
S3    G4 ◄ G3                   F9 ◄ F3                         F4 ◄ F3
S4    G5 ◄ G3, G4               DW ◄ F2, F3, F9                 F5 ◄ F1, F3
S5    G6 ◄ G3                   F8 ◄ F3, F5, F9                 U1a ◄ G3, G4, DW, LEAD-FZ1
S6    U1b ◄ U1a, G6             F7 ◄ F4, F5, F6                 U1s ◄ U1a, G4, G5, DW, F2
S7    P1 ◄ F7, F2, G2, FZ1      U2 ◄ U1s, F4, F7, F8, G4, G6    M1 ◄ DW, F7
S8    P2 ◄ P1                   U4 ◄ U1a, U1s, F8               G7 ◄ G3
S9    M2 ◄ DW, F7               M3 ◄ DW, F7                     U3 ◄ U2, U1s
S10   M4 ◄ DW, F5, F7           M5 ◄ DW, F7, M1 (DualRelease)   U7 ◄ G7, P1, U1a
S11   M6 ◄ DW, F5, F7           M7 ◄ DW, F7                     G8 ◄ G1, LEAD preset decision (S6), HR presets
S12   P3 ◄ P2, G8               U5 ◄ U1b, U1s                   U6 ◄ U1a, G6
S13   H1a ◄ U1b…U7              H1b ◄ S12 milestone findings    R1 ◄ B0, U7
v1    LEAD: FZ5 bless · all gates · lead-x86 verify · universal build · sign + notarise · tag v1.0.0
```

### 2.2 Critical path

```
            S0     S1     S2     S3     S4             S5        S6      S7      S9           S12            S13
DSP chain   B0 ─►  F1 ─►  F3 ─►  F9 ─►  DW ───────────────────┐
            F0 ─►  F2 ─┘                                      │
FunkGui     G1 ─►  G2 ─►  G3 ─►  G4 ─►  G5 ───────────────────┼─► U1s ─► U2 ─► U3 ─ ─ ─►  U5, U6, P3 ─► H1a, R1 ─► v1
                          G3 ──────────────────────► U1a ─────┘                   (resource-bound tail)
Processor                        F4 ──────────┐
chain                            F6 ──────────┴────► F7 (S6) ─► P1 (S7) ─► P2 (S8) ─ ─ ─ ─► P3 (S12) ◄─ G8 (S11)
```

- **Longest dependency chain: 10 cards** (G0 → B0 → F1 → F3 → F9 → DW → U1s → U2 → U3 → H1a), and the FunkGui chain
  (G0 → G1 → … → G5 → U1s → …) is exactly as long. Both converge on **U1s (S6)**, the plan's pinch point: a slip in F9,
  DW, G5 or U1a moves U1s and everything UI behind it.
- **Zero float in this schedule:** F3 → F9 → DW; G4 → G5; U1a → U1s → U2; F7 → P1 → P2; G8 → P3; the S12 cards → H1a.
  **Float:** U3 (S9) could run in S8 (1 sprint); U4 (S8) could run in S7; G7 (S8) could run in S3–S7 but no slot is
  free; the Mode queue M1…M7 has none spare (S7, S9, S10, S11 carry 1–2 Mode cards each).
- **The plan is throughput-bound** (42 cards over 3 slots). With unlimited slots the chain would finish in 9 sprints
  (B0 in S0 … H1a in S8); the other 5 sprints are capacity. Any card that slips a sprint pushes v1 a sprint unless it can take S13.2's slot (conditional) — the lead's
  only buffer.
- **External risk on the path:** G8 depends on HR's preset code settling (C, B §4.1). The lead decides "snapshot or
  re-implement" at the end of S6 (§4 S6 checklist), which keeps G8 in S11 either way.
- **Fallback if F9 runs long** (the heaviest early card): carry F9's feedback half into S4 in place of F5, move F5 to
  S5 in place of F8, and F8 to S6 in place of U1b; U1b moves to S7 in place of M1 and the Mode queue shifts one sprint
  (+1 sprint to v1). DW's FB Modes then run on FF generic traits (still `provisional`).

---

## 3. Standard lead procedures

### 3.1 Sprint base (before agents spawn)

1. `main` holds the committed base: the previous merge, any lead revisions to frozen headers approved from handoffs,
   the pin in `cmake/FcmpDeps.cmake`, and `docs/sprints/s<N>.md` with one manifest per card (§0.1 rule 5).
2. FunkGui cards: `git -C /Users/seanfunk/audio/libraries/FunkGui worktree add -b s<N>/<code>
   ../FunkGui.wt/s<N>-<code> v0.<last>.0`, and (from S1) the read-only FCompressor worktree
   `git -C /Users/seanfunk/audio/plugins/FCompressor worktree add --detach .claude/worktrees/ro-s<N>-<code> <base sha>`.
3. Spawn ≤ 3 agents: FCompressor cards with `isolation: 'worktree'`; FunkGui cards with `$FWT` and the base SHA in the
   prompt. The lead edits nothing agents own until the sprint end.

### 3.2 Sprint-end checklist (every sprint; 03 §4.8)

1. **Review** each handoff: `git -C <wt> diff`; [OWN]; candidate `.diff` files; PNGs; accept or reject each
   interface-change request (accepted ones become a lead revision in the next base).
2. **FunkGui first** (only in sprints with a G card): commit the worktree on its branch; `git merge --no-ff`; on
   FunkGui `main` run the `agent-verify` **and** `agent-gui-verify` workflows (the tag gate, 02 §1.10), then
   `tools/verify.sh <build>` (it wipes stale candidates/results) and `FUNKGUI_ALLOW_BLESS=1 tools/golden.py adopt <build> …
   --reason …` (FZ0 errata: adopt only right after a wiping verify, R-G1 #4); bump
   `project(FunkGui VERSION 0.x.0)` and `CHANGELOG.md` (with its golden impact); commit; `git tag -a v0.x.0`.
3. **Bump the pin** (`FCMP_FUNKGUI_TAG/_SHA/_VERSION`); `rm -rf build-lead/_deps/funkgui-* build-lead/CMakeFiles/fc-{tmp,stamp}/funkgui*`
   (FZ0 errata: the fetch state must go too, or the update step runs in a missing checkout).
4. **Merge FCompressor cards**: commit each worktree on its branch; `git merge --no-ff s<N>/<code>` in card order.
5. **Integration verify**: `cmake --workflow --preset lead-verify`, then `Scripts/verify.sh --integration build-lead`
   (fails on any FunkGui override; K2 #26c). No blocking result is accepted.
6. **Bless** once per reason group: `FCMP_ALLOW_BLESS=1 Scripts/golden.py adopt build-lead --only '<globs>' --reason
   '<why>'`. Rerun `verify.sh`: fully green except the **expected candidates** named in the sprint's exit criteria.
7. **Host validation**: `cmake --build --preset owner` (installs), then `Scripts/validate.sh build-lead` (auval
   `-strict`, pluginval strictness 10 on `.vst3` and `.component`; K2 #19).
8. **x86** (only if Rosetta is installed, Q6): `cmake --workflow --preset lead-x86-verify`; `Scripts/verify.sh
   build-lead-x86` writes `build-lead-x86/verify-passed-<sha>`.
9. **Milestone gates** where the sprint says so (§3.4).
10. **Commit** in order: merges → `deps: FunkGui v0.x.0` → `goldens: s<N> (<reasons>)`; `git tag -a fcmp-s<N>`.
11. **Clean up**: `git worktree remove` every card and `ro-` worktree (keep branches one sprint); write the outcome into
    `docs/sprints/s<N>.md` (slow tests, reasons, carried work).
12. **Carry-over**: an unfinished card keeps its worktree and OWNS in S<N+1>; the lead displaces the lowest-priority
    card of S<N+1> (never a card on the §2.2 chain) and updates §1.

### 3.2a GitHub PRs (from Sprint 7; user instruction 2026-09-24)

Both repositories have a private GitHub `origin` (`Snipet/FCompressor`, `Snipet/FunkGui`). At each sprint end the lead
pushes every card branch `s<N>/<code>` and opens one PR per card into `main` (body = the handoff summary + the lead's
rulings), then does the §3.2 integration on a branch `s<N>/lead` (card merges `--no-ff`, lead fixes, pin bump, goldens,
outcome), pushes it and opens the **Sprint N integration** PR. After the integration verify is green the lead merges
that PR with a merge commit (`gh pr merge --merge`, never squash/rebase — pinned SHAs and history must survive); the card
PRs then show as merged. The lead pulls `main`, tags (`fcmp-s<N>`; FunkGui `v0.x.0` on the merged commit) and pushes
tags. FunkGui goes first so FCompressor can pin the merged, tagged commit. Agents never push.

### 3.3 FunkGui fixes between G cards

A FunkGui bug found by an FCompressor card goes into its handoff. If the fix is small, the lead makes it on FunkGui
`main` at the boundary, runs the FunkGui tag gate and tags a PATCH (`v0.x.y+1`); the next base pins it. Larger fixes
become a card. FunkGui is never edited in a live FCompressor worktree.

### 3.4 Milestone gates (S4, S8, S12, and the v1 release)

`cmake --workflow --preset asan-verify`, `tsan-verify`, `tsan-agent-verify`, `rtsan-verify`; `ctest --preset lead -L
bench` run alone; `Scripts/gui-live.sh build-lead` (from S10; S8 runs FunkGui's gallery parity instead); a
`universal` compile (`cmake --preset universal && cmake --build --preset universal`). Findings go into the next
sprint's manifests (S12's become card S13.2).

---

## 4. Sprints

Each sprint lists: goal; LEAD steps at the base; the cards; the ownership check; exit criteria; the sprint-specific
lead checklist (in addition to §3.2).

---

### Sprint 0 — Bootstrap

**Goal.** Both repositories exist and configure; FunkGui has real targets and a proven GPU build chain; FCompressor has
its whole build and verification skeleton and its frozen DSP contracts. Nothing makes sound yet.

**LEAD steps before any agent (pre-work):**

- **S0.L1 Decisions.** Record the answers (or the defaults) to Q3 (harness in FunkGui), Q8 (prebuilt tools), Q5 (mix
  0–200 %), Q10 (calibration) and Q9 (FET `GR` switch): F0 bakes Q5/Q9/Q10 into `HostParams`. Q6 (universal) is needed
  by the end of S1 (§8).
- **S0.L2 Machine cache.** Write `Scripts/deps.sh` (03 §2.4) and run it: clone JUCE `8.0.4` and bgfx.cmake
  `v1.153.9385-561` with submodules into `~/audio/.deps`, verify the five SHAs (03 §1.3), build `shaderc` (stamped with
  the bgfx.cmake SHA) and `pluginval` (pinned tag) into `~/audio/.deps/tools`, write `DEPS.lock`, `chmod -R a-w` the
  source trees. A one-time `--seed-from` copy out of HR's `build/_deps` is allowed only if the SHAs verify; nothing
  permanent depends on HR's `build/`.
- **S0.L3 FunkGui G0** (02 §2.1, amended): `git init /Users/seanfunk/audio/libraries/FunkGui`.
  - Commit 1 "Snapshot of HardwareReverb GUI": the 02 §2.2 file list byte for byte at FunkGui paths with **HR file
    stems**; the bgfx-coupled `SdfCanvas.{h,cpp}` go under `include/funkgui/gpu/` and `src/gpu/` so the per-directory
    Core/Gpu glob holds from day one; `SEED.tsv` with sha256s re-verified against HR **on the day of the copy**.
  - Commit 2 "Mechanical renames": the 02 §2.3 sed script only (kept in `README.md` → Provenance).
  - Commit 3 "Bootstrap": `CMakeLists.txt` (~60 lines: `project(FunkGui VERSION 0.0.1)`; options
    `FUNKGUI_WITH_BGFX/_WITH_PRESETS/_HARNESS_ONLY/_BUILD_TOOLS` accepted; INTERFACE `FunkGui::harness`; **empty
    placeholder** `FunkGui::core/gpu/presets`; `FUNKGUI_VERSION` as `CACHE INTERNAL`; **placeholder functions**
    `funkgui_configure_product/_compile_shaders/_add_font` that only print, so B0's final link lines configure against
    v0.0.1) **and Harness v2**: `include/funkgui/test/Harness.h` written to 03 §3.2.1–§3.2.4 exactly (Tol grammar,
    spec/golden rows, `lines` sidecars, base+overlay loading, duplicate-key error, `--bless-to` candidates with atomic
    rename, `RESULT` JSON, exit codes 0–4). It is the one piece of new product logic the lead writes (everything else
    the lead writes is scripts or transcription of the appendices): every S0 card compiles against it, and G1 owns its
    self-test. `CLAUDE.md`, `.gitignore`.
  - `git tag -a v0.0.1`; `git worktree add --detach ../FunkGui.wt/pin-<sha7> v0.0.1` (for B0's override test).
- **S0.L4 FCompressor base commit** on `main`: the docs (`ARCHITECTURE.md`, `DECISIONS.md`, `design/`, this file),
  `.gitignore` += `/CMakeUserPresets.json`, `/.claude/worktrees/`; `CLAUDE.md` (03 §4.6–§4.7 rules); `README.md`;
  `LICENSE` (GPL-3.0); `Scripts/deps.sh`; `Source/fcdsp/modes/Modes.def` with its header comment and the **8 slot lines
  as commented reservations** (`// FCMP_MODE(0, "clean", Clean)` …, 01 §8.1); `tests/fixtures/{modes-ever,modeparam}.tsv`
  (header lines only); `docs/sprints/s0.md` (three manifests; B0's carries the v0.0.1 SHA).
- **S0.L5 Spawn** G1 (`FunkGui.wt/s0-g1` from v0.0.1), F0 and B0.

#### S0.1 · G1\* · FunkGui build system, tools on Harness v2, GPU build-chain spike — FunkGui · L

- **Goal.** FunkGui's real CMake (never edited again after S0, like FCompressor's), its tools and self-tests on Harness
  v2, and proof that the snapshot's GPU sources and shaders build with the prebuilt shaderc.
- **OWNS** (FunkGui): `CMakeLists.txt CMakePresets.json cmake/** include/funkgui/test/Harness.h
  include/funkgui/core/{Config,Env}.h src/core/Env.cpp include/funkgui/prefs/UiPreferences.h
  src/prefs/UiPreferences.cpp include/funkgui/gpu/** src/gpu/** tools/{FontProbe,AtlasDump,PrefsCheck,FrameRender}.cpp
  tools/{golden.py,verify.sh,check-headers.sh} test/CMakeLists.txt test/unit/harness_self.cpp test/smoke/**`
- **Reads.** 02 §1 (all), §2.2–2.3, §3.11; 03 §1.2, §2.3–2.5, §3.2–3.3; A §6.3–6.4; HR `CMakeLists.txt:229-277`
  (shader rules) and `Tools/*` (read-only).
- **Inputs.** v0.0.1 (Harness v2); `~/audio/.deps` (JUCE, bgfx.cmake, `tools/shaderc`).
- **Deliverables.**
  - `cmake/FunkGuiDeps.cmake`: consumed mode = JUCE 8.0.4 check, `if(NOT TARGET bgfx)` guarded fetch with **normal
    variables**, shaderc check `if(NOT FUNKGUI_SHADERC AND NOT TARGET shaderc)` plus `EXISTS`; top-level mode = the same
    `.deps` defaults and SHA asserts as `FcmpDeps.cmake`.
  - `cmake/FunkGuiTargets.cmake`: per-directory globs (`src/{core,text,canvas,panel,params,widgets,a11y,live,prefs,juce}/**`
    → Core; `src/gpu/**` → Gpu; `src/presets/**` → FunkPresets **when non-empty**, else the placeholder, SQLite3 linked
    only then); `FunkGuiFonts`; `FunkGuiShaders` → embedded Metal headers in `${FUNKGUI_GENERATED_DIR}`;
    `funkgui_configure_product/_compile_shaders/_add_font` (licences from `${bgfx_SOURCE_DIR}`); tool targets **by glob**
    (`tools/*.cpp` → `FunkGui<Name>`, `FrameRender.cpp` → `funkgui_framerender`, `GalleryProbe` also compiles
    `test/gallery/*.cpp` when present, `tools/*/CMakeLists.txt` added as subdirectories); `-ffp-contract=off` on
    FunkGui's own tests; options per 02 §1.7.
  - `test/CMakeLists.txt`: self-registering tests from a first line `// FUNKGUI_TEST name=fg.<x> timeout=<s> gpu=<0|1>
    [exe=<tool> args="…"] [labels=live]` in any `test/**/*.{cpp,mm}` (mirrors `FCMP_PROBE`), so later G cards add tests
    without CMake edits; `fg.headers` runs `tools/check-headers.sh` (every public header standalone; `gpu/` only with bgfx).
  - The four HR tools ported to Harness v2 and `funkgui::env()` (a tool that cannot be ported without G3/G4 moves to
    `tools/legacy/` and is restored by its owner: FrameRender → G4).
  - Snapshot GPU sources compile under `FUNKGUI_OBJC_PREFIX` (static class names kept; runtime names are G7's).
  - `tools/golden.py` (report / diff / adopt with every refusal of 03 §3.2.5, `--allow-env`, `--x86` merge rules,
    `xarch.` abort); `tools/verify.sh`; presets `agent`, `agent-gui`, `lead` + workflows `agent-verify`,
    `agent-gui-verify`, `lead-verify`.
- **Acceptance.** [FG] green with `fg.harness.self`, `fg.headers`, `fg.font.probe`, `fg.prefs.check`, `fg.smoke.core`,
  and (gpu) `fg.shader.hash`, `fg.smoke.gpu` (a console consumer that compiles and links every snapshot `.mm`/`.cpp` of
  `FunkGui::gpu`). Negative checks recorded in the handoff: `-DFUNKGUI_SHADERC=/nonexistent` → FATAL; with the stamped
  prebuilt shaderc, `ninja -C build-agent-gui -t targets all | grep -c 'tint\|spirv'` = 0. No [XV] in S0 (the base has
  no FCompressor CMake).
- **Split seam.** Tool ports (FontProbe, AtlasDump, PrefsCheck, FrameRender) → G2 in S1.
- **K2.** #17 (shaderc guard; normal variables, never `CACHE … FORCE`); #26d (`FUNKGUI_BUILD_TOOLS`); #26g (FunkGui
  tests with `-ffp-contract=off`); #16 (do **not** half-fix ObjC names here).

#### S0.2 · F0 · fcdsp contracts as compiling headers — FCompressor · L

- **Goal.** Every frozen `fcdsp` header of 01 §3–§8 as compiling C++20, with the few implementations 03 §4.9.2 lists,
  so S1–S4 cards build against fixed contracts.
- **OWNS:** `Source/fcdsp/core/*.h Source/fcdsp/params/*.h Source/fcdsp/params/HostParams.cpp Source/fcdsp/engine/*.h
  Source/fcdsp/engine/host/Crossfade.h Source/fcdsp/modes/*.h Source/fcdsp/modes/Registry.cpp Source/fcdsp/telemetry/*.h
  Source/fcdsp/analysis/Analysis.h`
- **Reads.** 01 §1–§8, §9.1 (`<UI>` names), §10.1–10.2; K2 #1–#3, #8, #9, #20; E §3.2, §3.5–3.6.
- **Inputs.** None (no CMake in its worktree; B0 writes it in parallel).
- **Deliverables.**
  - `core/*.h`: types, constants and op **declarations** (`f32x4`, the op set, `FastMath` names, `ScopedFtz`,
    `Smoother4`, `LinearRamp`, `ControlTicker`, `Sanitize`); `Units.h` implemented (`TimeLaw`, conversions).
  - `params/`: `Pid.h` (+ `kResolveOrder`, `kSnapDomain`, `kApvtsOrder` with a permutation `static_assert`), `Setup.h`,
    `HostParams.{h,cpp}` (29 parameters, `toPlain/toNorm/legal`, defaults; Q5 range 0–2 for `mix`), `ParamSpec.h`,
    `EngineParams.h` (116 B `static_assert`; `kRangeOff = 60`, `kS2Off = 24`), `Resolve.h`, `Text.h`.
  - `engine/`: `Stage.h` (concepts with `FbAffine`, `B::solveFb/commitFb`), `IEngine.h` (`Carry` with `msDomain`,
    `ControlIo::bits`, `EngineTelemetry`), `ModeEngine.h` (the class template with member **declarations**; F3 adds
    bodies), `TestTap.h` (spans), `EngineHost.h` (01 §5.4 API incl. `setTap`), `Oversampler.h` (`OsDesign`, `kOs`,
    latency constants as placeholders F6 replaces), `host/Crossfade.h` (`KernelKey` with `keyExt`, `PathState`,
    `kFadeMs`, `kMinFadeGapMs`).
  - `modes/`: `ModeDescriptor.h` (`revision`, `provisional`), `ModeKit.h` (all helpers inline constexpr, `allNa`,
    time-spec helpers), `DefineMode.h` (`FCDSP_DEFINE_MODE`, `makeModeEntry<T>`), `Registry.{h,cpp}` — `Registry.cpp`
    expands `Modes.def` three times and is **empty-registry safe** (`modeSlots()` empty; `resolveSlot` returns a null
    `ModeSlot{0, "", nullptr}` until slot 0 is active), so probe executables link from S1 on.
  - `telemetry/`: `Seqlock.h`, `UiFrame.h` (288 B; `overlaySmoothed` inline), `HistoryRing.h` (32 B columns, claim-word
    protocol), all implemented header-only.
  - `analysis/Analysis.h`: 01 §7 declarations (`inputThresholdDb` inline).
- **Acceptance** (in the worktree, no CMake):
  `cd "$WT" && for h in $(find Source/fcdsp -name '*.h'); do clang++ -std=c++20 -fsyntax-only -Wall -Wextra -Wshadow
  -Wpedantic -Werror -ffp-contract=off -I Source -x c++-header "$h" || exit 1; done`;
  `cd "$WT" && clang++ -std=c++20 -O3 -Wall -Wextra -Wshadow -Wpedantic -Werror -ffp-contract=off -I Source -c
  Source/fcdsp/params/HostParams.cpp -o /dev/null` and the same for `Source/fcdsp/modes/Registry.cpp`;
  `grep -rn 'FCMP_TEST_TAP\|#include <juce\|funkgui/' Source/fcdsp` → no hits. At integration: `lint.headers`,
  `lint.deps` (B0).
- **Split seam.** If long, the lead transcribes the remaining headers from 01 as the FZ0 freeze step; `HostParams.cpp`
  and `Registry.cpp` stay with F0.
- **K2.** #1 (`FbAffine`, no `alphaFor`); #2 (tap = runtime spans, unconditional `setTap`); #3d (`Carry.msDomain`),
  #3a/b (`PathState`); #8 (claim word + fences); #9 (`kApvtsOrder` separate from `Pid`); #10 (`revision`); #20 (finite
  sentinels); #22 (`keyExt` in `KernelKey`); #25a/b (universal host names; unit in text).

#### S0.3 · B0 · FCompressor build skeleton and verification plumbing — FCompressor · L

- **Goal.** 03 §2 end to end: configure in three configurations, self-registering probes over `Modes.def`, the gates
  (`verify.sh`, `validate.sh`, lints), and a passthrough stub plugin — written once and never edited by agents again.
- **OWNS:** `CMakeLists.txt CMakePresets.json cmake/** Scripts/{verify.sh,validate.sh,golden.py,check-headers.sh}
  Scripts/sprint/ownership.py Tools/probes/common/{ProbeMain.cpp,ProbeRegistry.h,Signals.h,Tolerances.h,AllocCounter.cpp,RtInterposer.cpp}
  Tools/probes/dsp/selftest.cpp Source/plugin/{Processor.h,Processor.cpp,CreateEditorGeneric.cpp}
  Resources/FCompressor.entitlements`
- **Reads.** 03 §1–§2 (all), §3.1–3.3, §4.6–4.8; 02 §1.9; B §4, §7.2, §7.6; HR `CMakeLists.txt` (read-only).
- **Inputs.** FunkGui v0.0.1 (Harness v2, placeholder targets and functions); `~/audio/.deps`.
- **Deliverables.**
  - `cmake/FcmpArch.cmake`, `FcmpDeps.cmake` (pins; `.deps` defaults as normal variables; JUCE/bgfx/FunkGui asserts;
    override ancestry check + loud WARNING; `fcmp-deps.txt`; bgfx hidden visibility; PRIVATE-link assert),
    `FcmpSources.cmake` (the 03 §2.1 glob map; a generated `BuildInfo.cpp` keeps `fcdsp` non-empty and carries the flags
    line for `release.sh`'s manifest; `CreateEditorGpu.cpp` is chosen in the GPU configuration **if present**, else
    `CreateEditorGeneric.cpp`, and configure prints which — `FCOMPRESSOR_GPU_EDITOR=1` only for the former;
    `FactoryIncludes.h` + `FCMP_FACTORY_BANK_REVISION` from `Source/plugin/factory/*.inc`), `FcmpPlugin.cmake`
    (03 §2.8), `FcmpProbes.cmake` (03 §2.9; `fcmp_bench` only when `Tools/bench/*.cpp` exist; `verify` and
    `verify-gui-live` targets), `FcmpProduct.h.in`, `LintDeps.cmake`.
  - `CMakePresets.json`: every configure preset of 03 §2.10 + workflows `agent-verify`, `agent-gui-verify`,
    `dsp-verify`, `lead-verify`, `lead-x86-verify`, `asan-verify`, `tsan-verify`, `tsan-agent-verify`, `rtsan-verify`.
    `FCOMPRESSOR_RTSAN` probes compiler support; if unsupported, `RtInterposer.cpp` (malloc/free/pthread_mutex_lock/
    os_unfair_lock_lock/write/mach_msg counters on the audio thread) is the fallback.
  - Lints: `lint.deps` (01 §2.2 include rules; the libm regex under `fcdsp/{core,engine,modes}`; `FCMP_TEST_TAP` = 0
    hits; indented-`static`-variable grep under `fcdsp`), `lint.headers` (`Scripts/check-headers.sh`: every `.h` under
    `Source/fcdsp`, plus `Source/plugin/ProcessorFacade.h` and `Source/editor/{SubView,Panel,Layout,Tags}.h` when present,
    with the FunkGui include dir from `fcmp-deps.txt`).
  - `Scripts/verify.sh` (wipes candidates/results; `ctest -L verify -j4`; **classifies `probe-results/*.json` itself**
    into BLOCKING / DRIFT / MISSING / IMPROVED; refuses `--quick`; `--integration` fails on an override; writes
    `verify-passed-<sha>` only when fully green; prints the 10 slowest tests), `validate.sh` (03 §4.8 step 7;
    `--vst3-only` runs pluginval on an uninstalled `.vst3` for agents), `golden.py` (wrapper that finds FunkGui's
    `tools/golden.py` through `fcmp-deps.txt`), `sprint/ownership.py` (03 §4.6; ignores `.claude/`).
  - Probe common: `ProbeMain.cpp` (dispatch `<layer>.<name>`, `--mode <key>` passed to the probe as `Ctx::key`,
    `ScopedFtz`, harness CLI pass-through), `ProbeRegistry.h` (`FCMP_PROBE`), `Signals.h` (PCG32, closed-form sines,
    log sweep), `Tolerances.h` (C §5.13, indexed by rigor 0/1/2), `AllocCounter.cpp`; `dsp.selftest`.
  - Stub `Processor` (passthrough; buses 1→1, 1→2, 2→2, optional sidechain; `getNumPrograms() == 1`; no parameters,
    no listeners); `CreateEditorGeneric.cpp`; the entitlements file.
- **Acceptance.** [DSP] and [AGENT] green with `lint.deps`, `lint.headers`, `dsp.selftest` (labels `lint`,
  `probe:dsp.selftest`); `cd "$WT" && cmake --preset agent-gui && cmake --build --preset agent-gui` (bgfx from `.deps`,
  prebuilt shaderc selected, all asserts pass, prints "GPU editor: Generic"); `Scripts/verify.sh` exits ≠ 0 on a
  scratch failing probe and 0 without it; `cmake --preset agent -DFETCHCONTENT_SOURCE_DIR_FUNKGUI=…/FunkGui.wt/pin-<sha7>`
  warns, and a non-descendant directory FATALs; `Scripts/validate.sh --vst3-only "$WT/build-agent"` passes on the stub;
  the handoff records configure and cold-build times (03 §2.11 asks for them).
- **Split seam.** `validate.sh` and `ownership.py` → LEAD at the S0 end.
- **K2.** #26a–f (asserts, ancestry, sticky override, tools ON, PRIVATE link, hidden bgfx); #14 (libm lint); #2
  (`FCMP_TEST_TAP` lint); #18 (`rtsan` preset or interposer; `tsan-agent`); #19 (`validate.sh`); #25d, #6 (the stub
  already has one program and no listeners).

**Ownership check S0.** G1 is in the FunkGui repository; F0 and B0 in FCompressor. F0 owns only paths under
`Source/fcdsp/` and nothing else; B0 owns nothing under `Source/fcdsp/` (its `fcdsp` TU is generated into the build
directory). `Scripts/check-headers.sh` belongs to B0 (03 had it in F0 while B0 owned all of `Scripts/`: resolved).
`Modes.def`, `Scripts/deps.sh` and `tests/fixtures/**` are lead files in the base. **F0 ∩ B0 = ∅; G1 ∩ {F0, B0} = ∅.**

**Exit criteria.**
- Demo: `cmake --workflow --preset dsp-verify` on `main` (configure ≈ 2 s); `cmake --preset lead && cmake --build
  --preset lead` builds FCompressor AU/VST3/Standalone (stub) with FunkGui v0.1.0's `gpu` sources compiled inside the
  consumer; `auval -strict -v aufx Fcmp Funk` and pluginval 10 pass on the installed stub.
- Labels green on `build-lead`: `lint`, `probe:dsp.selftest`. FunkGui `build-lead` (agent and agent-gui): `fg` (7
  tests). Expected candidates: none in FCompressor.

**Lead checklist S0 (in addition to §3.2).** Run [OWN] for F0 and G1 from B0's worktree (the script does not exist on
`main` yet). Merge order: FunkGui G1 → tag **v0.1.0**; FCompressor B0 → pin v0.1.0 → F0 → `lint.headers` over F0's
headers. Bless `fg.font.probe`, `fg.prefs.check`, `fg.shader.hash`. First `validate.sh` (stub). Declare **FZ0**
(record the frozen list in `docs/sprints/s0.md`). Write B0's measured build times into `docs/sprints/s0.md`.

---

### Sprint 1 — Math core, resolver, FunkGui API

**Goal.** SIMD and fma-only math proven bit-identical across build configurations; the one resolver and host text
proven on every `ParamSpec` kind; FunkGui's public UI API frozen with its core utilities implemented.

**LEAD base.** Pin v0.1.0. Frozen set: FZ0 (§0.3).

#### S1.1 · F1\* · Math core and the determinism spike — FCompressor · L

- **OWNS:** `Source/fcdsp/core/** Tools/probes/dsp/{simd,units}.cpp`
- **Reads.** 01 §5.1; E §0.9, §3.2, §9.2 rows 1–2; 03 §2.6, §3.4 (`dsp.simd`, `dsp.units`).
- **Inputs.** F0 core declarations (op names frozen), B0 build; pin v0.1.0.
- **Deliverables.** NEON and SSE4.1/AVX2 backends for every `f32x4` op (scalar == lane 0); fma-only `log2`/`exp2`
  (≤ 2.3e-5 dB / ≤ 7.4e-4 dB), `tanh`, `logCosh`, `tanPi`, `sinPi`, `cosPi`; `ScopedFtz` (FPCR / MXCSR); `Sanitize`
  (NaN/inf bit test → 0, clamp ±1e6); `Smoother4` (20 ms one-pole, exact landing), `LinearRamp`, `ControlTicker`
  (absolute-index ticks); probes `dsp.simd` (xarch hashes, NaN policy of min/max pinned), `dsp.units`.
- **Acceptance.** [DSP]; `ctest --preset dsp -L 'probe:dsp\.(simd|units)'`; **spike**:
  `cd "$WT" && cmake --preset lead && cmake --build --preset lead --target fcmp_probe_dsp && ctest --test-dir
  "$WT/build-lead" -L 'probe:dsp\.simd'` then `diff <(cut -f1,2 build-agent/golden-candidates/arm64/global/dsp.simd.txt)
  <(cut -f1,2 build-lead/golden-candidates/arm64/global/dsp.simd.txt)` is empty (RelWithDebInfo ≡ Release+LTO);
  `cmake --preset lead-x86 && cmake --build --preset lead-x86 --target fcmp_probe_dsp` compiles the SSE backend;
  [AGENT].
- **Split seam.** `tanPi/sinPi/cosPi` (first consumer F5, S4) → F5.
- **K2.** #14 (no libm; scalar == lane 0); #15 (SSE backend must at least compile every sprint); #13 (`Sanitize`);
  #20 (smoother landing). Fallback if the spike fails: agents build Release with `FCOMPRESSOR_LTO=OFF` (ADR-54).

#### S1.2 · F2 · Resolver, host text, Clean descriptor — FCompressor · M

- **OWNS:** `Source/fcdsp/params/{Resolve,Text}.cpp Source/fcdsp/modes/clean/CleanDesc.cpp
  Tools/probes/dsp/{resolve,format,hostparams}.cpp`
- **Reads.** 01 §3–§4 (all), §10.1–10.3; E §4.2; K1 #8, #15.
- **Inputs.** F0 headers; B0 build; pin v0.1.0.
- **Deliverables.** `snap` (per kind; snap domains; ties to the lower step; no hysteresis), `stepCount/stepPlain/
  stepIndexOf/activeSpec`, `resolveView` (resolve order, variants, budget lock/clamp with its reason text, derived pass,
  display maps, tags), `resolve` (+ `physicalDefault`, `desc.physical`), `modeDefaults`, `formatParts/formatValue/
  formatHost/parseHost` (U+2212 minus, U+2013 for n/a, `(10 MS)`, `(= 0.8 MS)`, `~`). Clean's descriptor per 01 §10.3
  with **`provisional = true`** (F9 clears it) and **external linkage** (`extern constexpr ModeDescriptor kClean{…}`
  in `fcdsp::modes`, so traits headers and probes declare `extern const ModeDescriptor kClean;`). Global probes:
  `dsp.resolve` (a probe-local synthetic descriptor with every kind, a variant, a derived spec, the budget lock, and
  Bus-G-style 2/4/10 steps with boundaries at S = 0.625/0.825; plus a 1/4096 sweep of Clean's table), `dsp.format`
  (text round trip for every spec of both descriptors; `-` and U+2212 parse), `dsp.hostparams` (toPlain/toNorm round
  trip ≤ 1e-6 relative at 4,097 points; `legal()`; defaults in range; `kApvtsOrder` is a permutation of 29 IDs).
- **Acceptance.** [DSP]; `ctest --preset dsp -L 'probe:dsp\.(resolve|format|hostparams)'`; [AGENT].
- **K2.** #4 (the resolver never writes; the FET `GR` boundary sits at 3.5 between steps 0 and 7); #25a/b (the value
  never contains the label; units in the text); #14 (`params/` may use libm; rows fed by host maps use `absrel`).

#### S1.3 · G2 · FunkGui API contracts and implemented utilities — FunkGui · L

- **OWNS** (FunkGui): `include/funkgui/core/{Col,Theme,TypeScale,TagPalette,Geometry,Ease,Format}.h
  include/funkgui/text/{TextStyle,FontAtlasSdf,FontService,TextFit}.h
  include/funkgui/canvas/{Prim,PrimList,Canvas,Tags,Axis,Fingerprint,SoftRaster,Expand}.h
  include/funkgui/panel/{Input,Panel,HostServices,CaptureConfig,HeadlessHost}.h
  include/funkgui/params/{ParamPort,GestureController,JuceParamPort}.h
  include/funkgui/widgets/{ValueModel,RuleSlider,SegmentedSelector,LatchToggle,AttachedWord,ThemeCells,DwellSelector,HintLine,FocusRing}.h
  include/funkgui/a11y/A11yItem.h include/funkgui/gpu/SdfCanvas.h src/gpu/SdfCanvas.cpp src/text/FontAtlasSdf.cpp
  src/params/{GestureController,JuceParamPort}.cpp src/core/Format.cpp src/text/TextFit.cpp src/a11y/A11yItem.cpp
  test/unit/{gesture,paramport,textfit,format,ease,a11yline}.cpp`
- **Reads.** 02 §0, §2.2–2.3, §3.1–3.9, §5.2–5.8; A §2, §6.4.
- **Inputs.** v0.1.0 (base of `FunkGui.wt/s1-g2`).
- **Deliverables.** The public headers of 02 §3.2–§3.6 and §5 (`Prim` 84 B `static_assert`, `PrimList`, `Canvas` with
  `protected: Prim& emit(PrimKind)`, `Expand.h` = the bgfx-free vertex expansion declaration (`Vtx` 64 B),
  `Panel`, `Input`, `HostServices`, `HeadlessHost`, `ParamPort`, `ValueModel`/`ValueView`/`Detent`,
  `CellModel`/`ToggleModel`, widget declarations, `A11yItem`, FunkGui `Tags` 1–255); `Theme` `ice`→`signal` with values
  bit-identical; `Col::fade/premix`; `TypeScale` via `text/TextStyle.h` (no bgfx in Core; the snapshot `SdfCanvas`
  includes it); **implemented**: `GestureController` (begin/set/end, drag, wheel, `tapMany` bracketed through
  `HostServices`), `JuceParamPort` (wraps `juce::RangedAudioParameter`), `ease` (fixed-dt, no wall clock), `fmt`
  (U+2212), `text::width/fits/fitEllipsis`, `a11yDumpLine`, `FontAtlasSdf::baked()`.
- **Acceptance.** [FG] with new `fg.gesture`, `fg.paramport`, `fg.textfit`, `fg.format`, `fg.ease`, `fg.a11yline`;
  `fg.headers` covers every new header; `fg.smoke.gpu` still links; [XV] green.
- **Split seam.** `JuceParamPort` (first consumer P1, S7) → G3.
- **K2.** #7 (gestures only, never an undo manager); #27 (ports are references owned elsewhere); #23 (`tapMany` must
  bracket a batch); 02 §3.7 determinism rules.

**Ownership check S1.** F1 owns `Source/fcdsp/core/**` and two probe files; F2 owns two `params/*.cpp`, one Clean file
and three other probe files; G2 is in FunkGui. `Tools/probes/dsp/` names are distinct (`simd, units` vs `resolve,
format, hostparams`). **F1 ∩ F2 = ∅; G2 ∩ {F1, F2} = ∅.**

**Exit criteria.**
- Demo: `fcmp_probe_dsp dsp.resolve --verbose` prints the 2/4/10 snap boundaries; the `dsp.simd` xarch hashes are
  identical in `build-agent` and `build-lead` (and in `build-lead-x86` if Rosetta is installed).
- Labels green: `probe:dsp.(simd|units|resolve|format|hostparams)`, `lint` (now including `ProcessorFacade.h`);
  FunkGui `fg`. Expected candidates: none after blessing.

**Lead checklist S1.** Tag FunkGui **v0.2.0**; pin it. **LEAD-FZ1: write `Source/plugin/ProcessorFacade.h`** verbatim
from 02 §9.5 against v0.2.0's `ParamPort.h`; `lint.headers` must pass with it. Confirm the determinism spike (compare
the lead preset's `dsp.simd` hashes with F1's handoff); if it failed, switch the `agent` preset to Release with
`FCOMPRESSOR_LTO=OFF`. **Q6 must be answered now** (§8): if universal, the user installs Rosetta and step 8 of §3.2
runs from here on. Bless `dsp.simd` xarch rows. Declare **FZ1**.

---

### Sprint 2 — First engine, oversampler, recorder

**Goal.** A Mode (Clean, walking-skeleton traits) runs through the real `ModeEngine` and the registry; the oversampler's
latencies freeze for v1; FunkGui records and fingerprints frames headlessly.

**LEAD base.** Pin v0.2.0. Frozen: FZ0 + FZ1.

#### S2.1 · F3 · Walking-skeleton engine; Clean registered — FCompressor · L

- **OWNS:** `Source/fcdsp/engine/ModeEngine.h Source/fcdsp/engine/stages/detector/PeakLog.h
  Source/fcdsp/engine/stages/gain/QuadKnee.h Source/fcdsp/engine/stages/link/LinkMax.h
  Source/fcdsp/engine/stages/ballistics/SmoothBranching.h Source/fcdsp/engine/stages/stage2/NoStage2.h
  Source/fcdsp/engine/stages/colour/ColourNone.h Source/fcdsp/engine/stages/scshape/Flat.h
  Source/fcdsp/modes/clean/{Clean.h,Clean.cpp} Source/fcdsp/modes/Modes.def
  Tools/probes/common/{EngineRig.h,EngineRig.cpp,Measure.h,Measure.cpp,Fidelity.h}
  Tools/probes/dsp/{registry,static,time,quant}.cpp`
- **Reads.** 01 §5.1–5.3, §8, §10.1–10.3; E §2.1–2.5, §3.5–3.6; C §5.2–5.4; K3 #10.
- **Inputs.** F0, F1, F2 merged; pin v0.2.0.
- **Deliverables.** `ModeEngine<T>` bodies: FF `control()` (tick → design → detector → target → link → ballistics →
  range → GR), `colour()`, carry/seed, `telemetry()`, internals, the analysis statics (`staticGr`, `scShapeDb`,
  `colourCurve`), `offAmt_`/`s2On_` 20 ms ramps, the `{thrDb, slope, min(range,60)}` smoother layout; the seven
  policies above; Clean walking-skeleton traits and `FCDSP_DEFINE_MODE(Clean)` (arena asserts live); **uncomment slot
  0 only** in `Modes.def`; `EngineRig` (03 §3.4 "Rig"); `Measure` (single-bin DFT, t63/t10–90, `hf_ratio`);
  `Fidelity.h` (**provisional Modes print fidelity rows as NOTE lines instead of failing them**, §7 D12); probes
  `dsp.registry` (every 01 §8.3 row except `fb.monotone`, which F9 adds; `thr.slope` uses the inline
  `inputThresholdDb`), `dsp.static` (D1), `dsp.time` (D2, Rig rows), `dsp.quant` (D3, including D1 between detents).
- **Acceptance.** [DSP]; `ctest --preset dsp -L 'probe:dsp\.registry|mode:clean'` → `dsp.registry`,
  `dsp.static.clean`, `dsp.time.clean`, `dsp.quant.clean` pass (Clean is provisional, so fidelity rows are NOTEs and
  goldens are refused); [AGENT].
- **Split seam.** `dsp.quant` → S3.2 (F4), which then owns `quant.cpp`.
- **K2.** #4 (`offAmt` ramp; `crossmode.no_off` row); #9 (permutation row); #20 (smoother layout); #1 (leave the FB
  seam for F9: FB = solve → link → commit); #24 (Rig runs under `ScopedFtz`).

#### S2.2 · F6\* · Oversampler spike — FCompressor · M

- **OWNS:** `Source/fcdsp/engine/{Oversampler.h,Oversampler.cpp} Source/fcdsp/engine/stages/colour/Adaa.h
  Tools/probes/dsp/os.cpp Tools/probes/plugin/osref.cpp`
- **Reads.** 01 §5.6; E §2.9, §5.2–5.3; K2 #11, #12, #28; ADR-16, ADR-17, ADR-60.
- **Inputs.** F0, F1 merged; pin v0.2.0.
- **Deliverables.** 2× polyphase IIR halfband + a constexpr first-order Thiran section (STD), 4× linear-phase FIR (HQ),
  constexpr coefficients; the **values** of `kStdLatency` (≤ 4), `kStdUpDelay`, `kHqLatency` (≤ 64), `kHqUpDelay`;
  ADAA-1 shapers via `FastMath::logCosh`; `dsp.os`, `proc.osref` (JUCE as passband/rejection reference, no latency
  comparison).
- **Acceptance.** `ctest --preset agent -L 'probe:(dsp\.os|proc\.osref)'`; the handoff states the four constants and
  the STD group delay error (≤ 0.01 samples to 1 kHz; τg at 10 kHz); [AGENT].
- **K2.** #11a/b (fractional-delay-exact STD; `D_up` constants for the SC delay); #12 (a control `Oversampler` is the
  mix-0 reference); #14; #28.

#### S2.3 · G3\* · Recorder, dump v2, fingerprint, HeadlessHost, gallery infrastructure — FunkGui · L

- **OWNS** (FunkGui): `src/canvas/{Canvas,PrimList,Fingerprint,Expand}.cpp src/text/FontService.cpp src/panel/**
  test/gallery/GalleryPanel.{h,cpp} tools/GalleryProbe.cpp test/unit/{canvas_parity,canvas_expansion,headless_settle}.cpp`
- **Reads.** 02 §3 (all), §3.11; A §2.1–2.5; HR `SdfCanvas.cpp` (via the snapshot).
- **Inputs.** v0.2.0.
- **Deliverables.** `Canvas` recorder with HR primitive maths byte-identical; tag/live scopes; dump v2 write/parse
  (accepts HR v1); `Fingerprint` (skips `live` prims and text gamma); `FontService` (CPU bake; `atlasHash()` equals
  FontProbe's); `HeadlessHost` (fixed dt, `settle()` ≤ 600 frames, key/drag/wheel replay); the `Panel` base; the
  **bgfx-free** `expandToVertices(const PrimList&, std::vector<Vtx>&)` (HR's a,b,c,a,c,d order; `BgfxSink::submit`
  itself is G7's); `GalleryPanel` with self-registering sections and `GalleryProbe` (`--section`, scripted states,
  dpi 1/2, theme invariance, a11y lines) so each widget card adds `fg.gallery.<section>` without touching another's
  goldens.
- **Acceptance.** [FG] with `fg.canvas.parity` (record → write → parse → fingerprint equal), `fg.canvas.expansion`
  (**spike**: one prim of each kind bit-identical to hand-computed HR vertices), `fg.headless.settle`; [XV].
- **Split seam.** `GalleryPanel`/`GalleryProbe` → G4 (S3).
- **K2.** #26g. Spike fallback (03 §4.9.5): keep HR's `SdfCanvas` path under the recorder until fixed.

**Ownership check S2.** F3's stage headers (`detector/PeakLog, gain/QuadKnee, link/LinkMax, ballistics/SmoothBranching,
stage2/NoStage2, colour/ColourNone, scshape/Flat`) and F6's (`colour/Adaa.h`) are different files; `ModeEngine.h`
(F3) ≠ `Oversampler.*` (F6); probe files `registry, static, time, quant` vs `os`, `plugin/osref`. `Modes.def` is F3's
alone this sprint. G3 is in FunkGui. **F3 ∩ F6 = ∅; G3 ∩ {F3, F6} = ∅.**

**Exit criteria.**
- Demo: `fcmp_probe_dsp dsp.static --mode clean --verbose` prints Clean's curve at 1 kHz within 0.05 dB of the
  textbook formula; the oversampler constants are fixed; a FunkGui `GalleryProbe` dump fingerprints identically after
  a write/parse round trip.
- Labels green: `probe:dsp.(registry|os)`, `mode:clean` (dsp), `probe:proc.osref`; FunkGui `fg`. Expected candidates:
  `modes/clean/*` `golden_missing` (Clean provisional; `adopt` refuses).

**Lead checklist S2.** Tag **v0.3.0**; pin it. Bless `dsp.os`. Declare **FZ2**: write the four oversampler constants
into 01 §5.6 as a lead revision; they are v1-forever from here.

---

### Sprint 3 — Clean complete and feedback; host at ECO; shapes

**Goal.** Clean becomes the first final Mode (every detector, voice and the feedback solvers) and gets the first
goldens; audio flows end to end through `EngineHost` at ECO; FunkGui can draw areas, polylines and the new glyphs.

**LEAD base.** Pin v0.3.0. Frozen: FZ0–FZ2.

#### S3.1 · F9\* · Clean complete and the feedback solvers — FCompressor · L

- **OWNS:** `Source/fcdsp/engine/ModeEngine.h Source/fcdsp/engine/stages/detector/{RmsLog,DualDet}.h
  Source/fcdsp/engine/stages/combinators/{DetSelect,Hold,CrestAuto,ColourSelect,FeedbackZdf,FeedbackDelayed}.h
  Source/fcdsp/engine/stages/colour/{TubeSym,DiodeAsym,Bright}.h Source/fcdsp/engine/stages/gain/QuadKnee.h
  Source/fcdsp/engine/stages/ballistics/SmoothBranching.h Source/fcdsp/modes/clean/**
  Tools/probes/dsp/{registry,static,srsweep,fbsolve}.cpp docs/modes/clean.md`
- **Reads.** 01 §5.2–5.3, §10.3; E §2.5–2.7, §6.5; K2 #1, #5; C §5.2, §5.8 (D11).
- **Inputs.** F3 merged; pin v0.3.0.
- **Deliverables.** Clean's full traits (01 §10.3) and `provisional = false`; `QuadKnee::solveFb` (closed form over
  `FbAffine`), `SmoothBranching::solveFb/commitFb`, `FeedbackZdf` (Newton, bracketed), `FeedbackDelayed` with the
  `prepare()`-time guard `k ≤ α/(1−α)` at the actual fs and a ZDF fallback; the FB path in `ModeEngine::control`
  (per-lane solve → link → commit); `fb.monotone` rows in `dsp.registry`; FB bisection rows in `dsp.static`;
  `dsp.srsweep` (D11, Rig; a 22.05 kHz stability row for any Mode on `FeedbackDelayed`); `dsp.fbsolve` (global, on a
  probe-local FB traits struct: every branch agrees with 200-step bisection to 1e-5 dB; stable at a 20 µs attack up to
  ∞:1; the guard triggers the fallback); `docs/modes/clean.md`.
- **Acceptance.** [DSP]; `ctest --preset dsp -L 'mode:clean|probe:dsp\.(registry|fbsolve)'` with **fidelity rows
  blocking** (Clean is no longer provisional); [AGENT].
- **Split seam.** The feedback half (solvers, `dsp.fbsolve`, `fb.monotone`) → carried per §2.2's fallback.
- **K2.** #1 (affine form, max of roots); #5a (monotone), #5b (link between solve and commit), #5c (guard at runtime,
  not `static_assert`); #14 (colour shapers on `FastMath`); #24.

#### S3.2 · F4 · EngineHost ECO skeleton — FCompressor · L

- **OWNS:** `Source/fcdsp/engine/EngineHost.cpp Source/fcdsp/engine/host/{Ramps,TelemetryAccum}.h
  Tools/probes/dsp/{null,hostile,rt,zipper,telemetry}.cpp Tools/bench/Bench.cpp`
- **Reads.** 01 §5.4 (steps 0–3 at ECO), §5.5 (types), §5.7–5.8, §6; C §5.5–5.8; E §7.
- **Inputs.** F3 merged (F6's `Oversampler` exists but STD/HQ are F7's); pin v0.3.0.
- **Deliverables.** `configure` (the only allocation), `latencyFor`, `process` at ECO (sanitise → route: L/R, mono
  duplication, key select → 64-sample chunks → per-path `PathState` gain smoothers → `control()` → `g = linFromDb(preGain
  − GR)` → `colour()` → makeup → mix at base rate against the **un-pre-gained** dry → bypass ramp → poison fallback →
  snap flag), a Mode change as a snapped swap (crossfades are F7's), telemetry accumulation and publish (`UiFrame`
  seqlock, 1 ms `HistoryColumn`s, attach **count**), `setTap`, `[[clang::nonblocking]]` on `process`; probes with
  **ECO rows**: `dsp.null` (ECO bit-exact against `delay(x, L)`), `dsp.hostile`, `dsp.rt`, `dsp.zipper` (incl.
  detent-edge rows), `dsp.telemetry` (10⁷ reads, torn = 0; columns bit-identical over bs {1, 17, 64, 128, 512, 4096});
  `fcmp_bench` (ns/sample per Mode vs `ctBudgetNsPerSample`, label `bench`).
- **Acceptance.** [DSP]; `ctest --preset dsp -L 'probe:dsp\.(null|hostile|rt|zipper|telemetry)'`;
  `cd "$WT" && cmake --preset tsan && cmake --build --preset tsan && ctest --preset tsan -L 'probe:dsp\.telemetry'`;
  [AGENT].
- **Split seam.** `Bench.cpp` → F7.
- **K2.** #13 (sanitise before any delay line); #2 (tap spans); #3c (dry never pre-gained); #8 (claim-word stress);
  #12 (ECO null); #4iv (detent-edge rows); #18 (`nonblocking`); #20.

#### S3.3 · G4 · Shapes, AREA shader, SoftRaster, FrameRender CLI, glyphs — FunkGui · M

- **OWNS** (FunkGui): `src/canvas/{CanvasShapes,SoftRaster}.cpp shaders/** fonts/** include/funkgui/text/Glyphs.def
  src/text/FontAtlasSdf.cpp tools/{FrameRender.cpp,subset-font.sh} test/gallery/{AreaGallery,GlyphGallery}.cpp
  test/unit/softraster_area.cpp`
- **Reads.** 02 §4.1–4.4, §4.6; A §2.6, §4.
- **Inputs.** v0.3.0.
- **Deliverables.** `area`, `areaStrip`, `polyline`, `disc`, `dotted`, `axis`; the `fs_ui.sc` branch chain with
  `KIND_AREA` (kinds 0–2 maths unchanged) **and** its `SoftRaster` mirror in the same commit; `funkgui_framerender`
  (`<dump> <png> [ss]`, `--fingerprint`, `--check`, `--legacy-hr`); `Glyphs.def` (+10 glyphs, append-only) →
  `kExtraChars`; the regenerated subset (throwaway venv under `$TMPDIR`) and FontProbe proof subset ≡ upstream.
  The 32 MiB transient buffer moves to G7 (it lives in `BgfxContext`).
- **Acceptance.** [FG] with `fg.gallery.area`, `fg.gallery.glyphs`, `fg.softraster.area` (shader decode ≡ mirror for
  flags 0…7), `fg.font.probe` and `fg.shader.hash` producing candidates; PNGs of both sections via
  `funkgui_framerender`; [XV]. CHANGELOG golden impact: `atlas`.
- **K2.** A §2.6 #4 (shader and mirror together); #26g.

**Ownership check S3.** F9 and F4 share no file: F9 owns `ModeEngine.h`, `QuadKnee.h`, `SmoothBranching.h`, new
detector/combinator/colour headers, `modes/clean/**` and probe files `registry, static, srsweep, fbsolve`; F4 owns
`EngineHost.cpp`, `host/{Ramps,TelemetryAccum}.h`, probe files `null, hostile, rt, zipper, telemetry`, `Tools/bench/`.
G4 is in FunkGui. **F9 ∩ F4 = ∅; G4 ∩ {F9, F4} = ∅.**

**Exit criteria.**
- Demo: Clean with every detector and voice passes its textbook spec; the FB solver agrees with bisection;
  `fcmp_probe_dsp dsp.null --mode clean` is bit-exact at ECO; `funkgui_framerender` renders AREA strips.
- Labels green: every `mode:clean` dsp test; `probe:dsp.(fbsolve|telemetry|registry)`; FunkGui `fg`. Expected
  candidates: none after blessing.

**Lead checklist S3.** Tag **v0.4.0** (re-bless `fg.font.probe`, `fg.shader.hash`: atlas impact). Bless the first
**Clean goldens** (`modes/clean/*`: static, time, quant, srsweep, null, hostile, zipper) and `dsp.fbsolve`. Read HR's
`Source/presets/` status (mtimes, sha256 drift since research) for the S6 decision.

---

### Sprint 4 — Schema proven on eight Modes

**Goal.** All eight descriptors exist (seven provisional, on generic traits), so the `ParamSpec`/`ModeDescriptor` schema
freezes on evidence from every first-wave Mode before any UI code depends on it; side-chain filter, router and link
policies land; FunkGui gets its main control widget.

**LEAD base.** Pin v0.4.0. Frozen: FZ0–FZ2. Q9 (FET `GR` switch) answered.

#### S4.1 · DW\* · Descriptor wave — FCompressor · L

- **OWNS:** `Source/fcdsp/modes/{bus-g,fet-76,opto-2a,mu-67,diode-609,bus-25,brickwall}/** Source/fcdsp/modes/Modes.def`
- **Reads.** 01 §4, §8.4, §10.2, §10.4–10.7; D §2, §5; K1 #24–#28; ADR-25, ADR-30, ADR-56.
- **Inputs.** F2, F3, F9 merged; pin v0.4.0.
- **Deliverables.** For each of the 7 keys: `<Traits>Desc.cpp` (step tables, `ParamTable`, `physical()`, time specs,
  `InternalSpec[]`, `provisional = true`, `revision = 1`, external linkage), `<Traits>.h` (generic traits from existing
  policies; FB Modes use `QuadKnee::solveFb` + `SmoothBranching`), `<Traits>.cpp` (`FCDSP_DEFINE_MODE`); uncomment
  slots 1–7 in `Modes.def`. Mode-local helpers stay in the Mode's directory (`ModeKit.h` is frozen).
- **Acceptance.** `ctest --preset dsp -L 'probe:dsp\.(registry|quant)'` → `crossmode.no_off` over all 56 ordered
  pairs, `fb.monotone` for every FB Mode and step, `dsp.quant.<key>` × 8; [AGENT] with 0 spec failures (fidelity rows
  NOTE for the 7 provisional Modes); `fcmp_probe_dsp dsp.quant --mode bus-g --verbose` shows the 2/4/10 boundaries.
- **Split seam.** Mu 67, Diode 609, Bus 25, Brickwall (01 §10.7, the sketched four) → the lead writes the last of them
  as the FZ3 freeze step.
- **K2.** #4 (FET: `atk` continuous 0.02–0.8 ms `hwrev`; `GR` on `tmode` at steps 0/7); #5a (FET ALL monotone); #20
  (Diode `s2thr` OFF = 24); #21 (Brickwall: `look` budget lock, TP needs a budget, `automu` n/a); #10.

#### S4.2 · F5 · Side-chain filter, router, link, delay — FCompressor · M

- **OWNS:** `Source/fcdsp/engine/host/{ScFilter,Router,Delay}.h
  Source/fcdsp/engine/stages/link/{LinkIndependent,LinkMean,LinkCvSum}.h Tools/probes/dsp/{sc,link,router}.cpp`
- **Reads.** 01 §5.4 (steps 2a–c), §5.2 (link); E §8; K2 #5b.
- **Inputs.** F1, F3 merged (F9 for the FB link row, merged); pin v0.4.0.
- **Deliverables.** TPT SVF HPF (exact bypass at OFF) + 6-section tilt with `FastMath::tanPi` prewarp per tick;
  `Router` (6 `stmode`s, key select, encode/decode, lane domain for `Carry.msDomain`); `Delay` (buffers from
  `configure` only); link policies including the FB application after the per-lane solve; probes `dsp.sc` (global),
  `dsp.router` (global: encode/decode bit-exact round trips, key routing), `dsp.link.<key>` (Rig; FB steady-state row).
- **Acceptance.** [DSP]; `ctest --preset dsp -L 'probe:dsp\.(sc|router|link)'` (link × 8; fidelity NOTE for provisional
  Modes); [AGENT].
- **K2.** #14 (no libm in SVF design); #5b; #3d (domain reporting); #22 (key select is part of the kernel key; F7 uses it).

#### S4.3 · G5 · RuleSlider, AttachedWord, FocusRing — FunkGui · M

- **OWNS** (FunkGui): `include/funkgui/widgets/{RuleSlider,AttachedWord,FocusRing}.h
  src/widgets/{RuleSlider,AttachedWord,FocusRing}.cpp test/gallery/{RuleSliderGallery,WordGallery,FocusRingGallery}.cpp
  test/unit/ruleslider_input.cpp`
- **Reads.** 02 §5.3–5.5, §8.1–8.3; F §3.
- **Inputs.** v0.4.0 (`dotted` draws the locked track).
- **Deliverables.** `RuleSlider` in all five states + hybrid end cells + markers (rename, `+`, `~`, clamped), detent
  ticks with the adjacent-pair label fit rule, index-space a11y (0…n−1, step 1), arrows move one detent, wheel
  accumulation (`kWheelNotch` 0.10), drags land on detents, locked refuses writes; `AttachedWord` (hidden when n/a,
  disabled with the reason when locked); `FocusRing`. Header changes are additive only and flagged in the handoff.
- **Acceptance.** [FG] with `fg.gallery.ruleslider`, `fg.gallery.word`, `fg.gallery.focusring`, `fg.ruleslider.input`;
  PNGs; [XV].
- **K2.** #7; 02 §3.7.

**Ownership check S4.** DW owns only the 7 Mode directories and `Modes.def`; F5 owns `engine/host/{ScFilter,Router,
Delay}.h`, `stages/link/*` (new files) and probe files `sc, link, router`; G5 is in FunkGui. **DW ∩ F5 = ∅; G5 ∩
{DW, F5} = ∅.**

**Exit criteria.**
- Demo: all 8 Modes appear in `ctest -N` (≈ 8 × the per-Mode dsp probes); `fcmp_probe_dsp dsp.registry --verbose`
  shows 56 clean cross-Mode pairs.
- Labels green: `probe:dsp.(registry|quant|sc|router|link)` × 8 Modes; everything from S3; FunkGui `fg`. Expected
  candidates: `modes/{bus-g,fet-76,opto-2a,mu-67,diode-609,bus-25,brickwall}/*` `golden_missing` (provisional; refused).

**Lead checklist S4.** Tag **v0.5.0**; pin. Declare **FZ3**: any schema revision DW requested lands in the S5 base as a
lead revision of `ParamSpec.h`/`ModeDescriptor.h` **before** U1a starts. **Milestone gates** (§3.4, no gui-live yet).
Bless `dsp.sc`, `dsp.router`, `dsp.link.clean`. Q2 (FunkPresets + SQLite) and Q4, Q7 should be answered before S5.

---

### Sprint 5 — Analysis, UI skeleton, cell widgets

**Goal.** The analysis functions the display relies on are proven bit-equal to the audio kernel; the whole editor
exists as a frozen composition of stubs that renders every view headlessly for every Mode; FunkGui completes its
widget set.

**LEAD base.** Pin v0.5.0. Frozen: FZ0–FZ3. Q4 (Characteristics keeps the chrome) answered — it shapes `Layout.h`.

#### S5.1 · F8 · Analysis — FCompressor · M

- **OWNS:** `Source/fcdsp/analysis/Analysis.cpp Tools/probes/dsp/analysis.cpp`
- **Reads.** 01 §7, §6.2; E §6; K1 #22; K2 #24; ADR-42.
- **Inputs.** F3, F5, F9 merged (DW for the 8-Mode rows); pin v0.5.0.
- **Deliverables.** `staticGain`, `staticGr`, `localRatio`, `netGainDb`, `stepResponse` (private engine in a local
  arena), `measure`, `scResponse` (host `ScFilter` + the Mode's `ScShape`), `colourCurve`, `harmonicsDb`; every entry
  opens `ScopedFtz`; `dsp.analysis.<key>` (`staticGr` bit-equal to the kernel over 1,024 points; FB settled GR within
  0.01 dB; `stepResponse` bit-identical to an engine render).
- **Acceptance.** [DSP]; `ctest --preset dsp -L 'probe:dsp\.analysis'` × 8; [AGENT].
- **K2.** #24 (FTZ in analysis; `resolve` + `overlaySmoothed`, never `EngineParams` from `UiFrame` alone).

#### S5.2 · U1a · UI skeleton: Panel composition, layout, tags, stubs, FakeFacade — FCompressor · L

- **OWNS:** `Source/editor/{Panel.h,Panel.cpp,SubView.h,Layout.h,Tags.h,HistoryStore.h,PreviewWorker.h,PreviewWorker.cpp,SlotModel.h,SlotModel.cpp}
  Source/editor/views/** Tools/probes/plugin/{FakeFacade.h,FakeFacade.cpp,ui_geometry.cpp,ui_dump.cpp}`
- **Reads.** 02 Part 2 intro, §6–§9 (all); 01 §6; F §2–§4; K3 #13, #15.
- **Inputs.** `ProcessorFacade.h` (FZ1); FunkGui v0.5.0 (API, recorder, `HeadlessHost`, shapes, `RuleSlider`); DW
  merged (8 Modes).
- **Deliverables.** `fcmp::ui::Panel` as the fixed composition of 02 Part 2 (dispatch order, ids `(subView << 16) |
  local`, `views()` with the 5 view ids, `setView`, `PanelOptions`, `shutdown()` = stop `PreviewWorker` then
  `closeGestures()`); `SubView.h`; `Layout.h` and `Tags.h` **complete** (02 §6.1–6.6, §7.1–7.3, §8); **stub files for
  all 9 sub-views and all 8 plots** with their final class declarations (each stub draws its frame and title), with
  `Band` and `CharScreen` already composing their plots at the `Layout.h` rects so later plot cards show up without
  touching the composer; declarations of `HistoryStore`, `PreviewWorker`, `SlotModel` (02 §9.3–9.4); `FakeFacade` (29
  in-memory ports, scripted `UiFrame`s, a real `HistoryRing`, a counting batch, a fake `PresetAccess`);
  `ui.geometry.<key>` (spec: every view × dpi {1, 2} renders with no harness error, theme invariance; golden rows are
  candidates only until FZ5); the `ui.dump` subcommand (`FCMP_PROBE(ui, dump)` without a `// FCMP_PROBE` line, so no
  CTest test).
- **Acceptance.** [AGENT] with `probe:ui.geometry` × 8; `Scripts/check-headers.sh "$WT/build-agent"` covers the editor
  headers; [PNG] for all 5 views × `clean`.
- **Split seam.** The `HistoryStore`, `PreviewWorker` and `SlotModel` declarations (transcriptions of 02 §9.3–9.4) →
  LEAD as part of the FZ4 freeze.
- **K2.** #27 (teardown order encoded in `shutdown()`; ports belong to the facade); #23 (FakeFacade counts batches so
  probes can assert them); 02 §3.7 (fixed dt, `syncPreview`).

#### S5.3 · G6 · Cell widgets and UI utilities — FunkGui · L

- **OWNS** (FunkGui): `include/funkgui/widgets/{SegmentedSelector,LatchToggle,ThemeCells,HintLine,DwellSelector}.h
  include/funkgui/live/LiveFeed.h include/funkgui/prefs/UiPreferences.h include/funkgui/juce/MenuLook.h
  include/funkgui/text/LineEdit.h src/widgets/{SegmentedSelector,LatchToggle,ThemeCells,HintLine,DwellSelector}.cpp
  src/live/LiveFeed.cpp src/prefs/UiPreferences.cpp src/juce/MenuLook.cpp src/text/LineEdit.cpp tools/PrefsCheck.cpp
  test/gallery/{SegmentedGallery,LatchGallery,ThemeGallery,HintGallery,DwellGallery,LineEditGallery}.cpp
  test/unit/{livefeed,lineedit,dwell}.cpp`
- **Reads.** 02 §5.5, §5.7–5.10; A §1.2 (BgfxEditor regions), HR `PresetPanel.cpp` (read-only).
- **Inputs.** v0.5.0.
- **Deliverables.** `SegmentedSelector`, `LatchToggle`, `ThemeCells`, `HintLine`, `DwellSelector` + `ScreenFader`
  (fixed dt), `CellModel`/`ToggleModel` implementations, `LiveFeed` (staleness), `UiPreferences` generic int keys (per
  product folder, Q7), `MenuLook`, `LineEdit`.
- **Acceptance.** [FG] with `fg.gallery.{segmented,latch,theme,hint,dwell,lineedit}`, `fg.livefeed`, `fg.lineedit`,
  `fg.dwell`, `fg.prefs.check`; [XV].
- **Split seam.** `LineEdit` + `MenuLook` (first consumer U6, S12) → G8 (S11).
- **K2.** 02 §3.7 (no wall-clock easing); #26g.

**Ownership check S5.** F8 owns `analysis/Analysis.cpp` and one probe; U1a owns `Source/editor/**` (listed files +
`views/**`) and four files under `Tools/probes/plugin/`; G6 is in FunkGui. **F8 ∩ U1a = ∅; G6 ∩ {F8, U1a} = ∅.**

**Exit criteria.**
- Demo: headless PNGs of `panel`, `chars.sidechain`, `chars.colour`, `modebrowser`, `presetbrowser` for every Mode
  (stubs, correct geometry); `fcmp_probe_dsp dsp.analysis --mode clean` bit-equal rows.
- Labels green: `probe:dsp.analysis` × 8, `probe:ui.geometry` × 8, `lint`; FunkGui `fg`. Expected candidates: the S4
  provisional set; `ui.geometry.*` `golden_missing` (pre-FZ5; refused).

**Lead checklist S5.** Tag **v0.6.0**; pin. Declare **FZ4** (the UI composition, §0.3). Bless `dsp.analysis.clean`.

---

### Sprint 6 — EngineHost complete, slots, chrome

**Goal.** `EngineHost` is feature-complete (oversampling, mix in the OS domain, lookahead, crossfades, delta, listen);
the 21 slots show every Mode's real states; the header, display row and footer work.

**LEAD base.** Pin v0.6.0. Frozen: FZ0–FZ4.

#### S6.1 · F7 · EngineHost complete — FCompressor · L

- **OWNS:** `Source/fcdsp/engine/EngineHost.cpp Source/fcdsp/engine/host/Crossfade.h
  Tools/probes/dsp/{switch,latency,print,null,hostile,rt,zipper,time}.cpp`
- **Reads.** 01 §5.4–5.6; E §5.1–5.4; K2 #3, #11, #21a, #22; ADR-14, ADR-16, ADR-17.
- **Inputs.** F4, F5, F6 merged; pin v0.6.0.
- **Deliverables.** The OS domain (one `up()`, one `down()` per chunk), mix in the OS domain against the un-pre-gained
  dry, OS-rate gain interpolation (`gr[n−1] + (k/F)(gr[n] − gr[n−1])`), SC delay `L_la − look + D_up −
  scDelaySamples()`, lookahead and budget, `Crossfade` (frozen outgoing `EngineParams`, per-path gain staging, carry
  seeded with the `msDomain` max-merge, 20 ms equal-gain, starts ≥ 50 ms apart counted in samples, `keyExt` in the
  key), delta, listen, 20 ms ramps; probes `dsp.switch` (lower-slot pairs, both directions; A→B→A raw bit-exact; the
  extra pairs owned by `clean` and `fet-76`), `dsp.latency` (Mode-independent), `dsp.print`; STD/HQ rows added to
  `null`, `hostile`, `rt`, `zipper`; the Quality row of `dsp.time` (τ across ECO/STD/HQ ≤ 1.5 base samples).
- **Acceptance.** [DSP] (every `dsp.*` × 8 Modes, 0 spec failures); [AGENT].
- **Split seam.** `dsp.print` → S7.3 (M1).
- **K2.** #3a–e; #11b/c; #12 (STD/HQ null vs a control `Oversampler` round trip); #21a; #22; #13.

#### S6.2 · U1s · Slot model and slot grid — FCompressor · L

- **OWNS:** `Source/editor/{SlotModel.h,SlotModel.cpp} Source/editor/views/SlotGrid.{h,cpp}
  Tools/probes/plugin/{ui_textfit,ui_a11y,ui_input,ui_font}.cpp`
- **Reads.** 02 §6.4, §8 (all), §9.4; 01 §4.4–4.6.
- **Inputs.** U1a (FZ4), DW, F2 merged; FunkGui v0.6.0.
- **Deliverables.** `SlotModel` (resolveView → `ValueView`; `formatParts`; snap-on-write to canonical detents inside
  begin/end gestures; locked refuses; `tapMany` inside a batch); `SlotGrid` (21 slots in 3 × 7 at `Layout.h`
  coordinates; AUTO on MAKEUP, EXT on DETECT, LISTEN on SC HPF; TIME MODE slot; landing carets, 02 §8.7); probes
  `ui.textfit.<key>`, `ui.a11y.<key>` (+ `lines` candidates), `ui.input.<key>` (+ `taborder` lines), `ui.font`.
- **Acceptance.** [AGENT] with `probe:ui.(textfit|a11y|input|font)` (8 × 3 + 1); [PNG] `panel` × {`clean`, `bus-g`,
  `fet-76`, `mu-67`}.
- **Split seam.** `ui_font.cpp` → U1b.
- **K2.** #4 (a Mode change never writes another parameter; landing writes nothing); #7; #23; K1 #24 (the pair-fit
  rule is the gate).

#### S6.3 · U1b · Chrome: header, display row, footer — FCompressor · M

- **OWNS:** `Source/editor/views/{Header,DisplayRow,Footer}.{h,cpp} Tools/probes/plugin/ui_chrome.cpp`
- **Reads.** 02 §6.2–6.3, §6.6, §8.5, §8.8, §8.10, §9.5.
- **Inputs.** U1a (FZ4); FunkGui v0.6.0 (`SegmentedSelector`, `LatchToggle`, `ThemeCells`, `HintLine`, `DwellSelector`,
  `LiveFeed`).
- **Deliverables.** Header (title, topology caption, Mode latch ‹ NAME › with dwell, group line); DisplayRow (GAIN
  REDUCTION readout with staleness, QUALITY and LOOKAHEAD cells over the global ports, DELTA/BYPASS/CHARACTERISTICS
  latches; CHARACTERISTICS toggles `UiState.charExpanded` through `Panel::setView`); Footer (spec line of the hovered
  item, reasons, `StateNotice` texts, the `wantsLookahead && budget off` hint, THEME cells); probe `ui.chrome.<key>`
  (the Mode latch writes only `mode`, one gesture; cells write their ports; footer shows the hovered slot's reason;
  THEME swap is geometry-invariant).
- **Acceptance.** [AGENT] with `probe:ui.(chrome|geometry)`; [PNG] `panel` × {`clean`, `opto-2a`}.
- **K2.** #6 (the UI only writes `quality`/`labudget` ports; latency is `SetupWatcher`'s); #10 (revision notice text);
  #25c (monitoring latches).

**Ownership check S6.** F7 owns `EngineHost.cpp`, `Crossfade.h` and 8 dsp probe files; U1s owns `SlotModel.*`,
`views/SlotGrid.*` and 4 `ui_*` probes; U1b owns `views/{Header,DisplayRow,Footer}.*` and `ui_chrome.cpp`. No path
appears twice. **F7 ∩ U1s = F7 ∩ U1b = U1s ∩ U1b = ∅.**

**Exit criteria.**
- Demo: `fcmp_probe_dsp dsp.switch --mode fet-76 --verbose` shows click-free switches against Clean and Bus G;
  headless panel PNGs show each Mode's real slot states (stepped Bus G ratio, locked Opto attack, FET `INPUT`).
- Labels green: `dsp` (all), `probe:ui.(textfit|a11y|input|font|chrome|geometry)`. Expected candidates: provisional
  Modes; `ui.geometry.*`, `ui.a11y.*`, `ui.input.*` golden rows (deferred to FZ5).

**Lead checklist S6.** No FunkGui tag. Bless `dsp.{switch,print,null,hostile,rt,zipper}.clean` and `ui.font`'s atlas
hash. **FunkPresets decision** (with Q2): snapshot HR's `Source/presets/` in S11 if it has been stable since S3,
otherwise G8 re-implements from the headers (02 §11 Q7) — record it in `docs/sprints/s6.md`.

---

### Sprint 7 — Real processor, band, Bus G

**Goal.** The plugin is a real, host-validated compressor (generic editor); the always-visible band draws truthful
live curves and history; Bus G is the first hardware Mode made final.

**LEAD base.** Pin v0.6.0.

#### S7.1 · P1 · Processor — FCompressor · L

- **OWNS:** `Source/plugin/{Processor.h,Processor.cpp,SetupWatcher.h,ParamLayout.cpp,HostText.cpp,State.h,State.cpp,Presets.cpp}
  Tools/probes/plugin/{layout,text,chunk,latency,bypass,null}.cpp`
- **Reads.** 01 §2.3, §3.1, §4.6, §5.6, §9.1; 02 §9.5; B §1, §7.4; K2 #6, #7, #23, #25.
- **Inputs.** F7, F2 merged; `ProcessorFacade.h`; FunkGui v0.6.0 (`JuceParamPort`).
- **Deliverables.** APVTS (`nullptr` undo manager, `kApvtsOrder`, labels `""`, universal names, version hints, one
  program); 29 processor-owned `JuceParamPort`s behind `port(Pid)`; `currentRaw()` with the configured budget;
  `SetupWatcher` (20 Hz; `suspendProcessing` reconfigure; `setLatencySamples`; `updateHostDisplay` on a Mode change);
  `prepareToPlay` configure + latency; `processBlock` (raw snapshot → `resolveSlot` → `resolve` → `BlockParams` →
  `EngineHost::process`; the previous `BlockParams` while a batch is open; `[[clang::nonblocking]]`);
  `processBlockBypassed`; telemetry attach count; the facade; `HostText` lambdas (`formatHost`/`parseHost`,
  thread-safe); the batch counter. `State.{h,cpp}` with the frozen save/load entry points and an APVTS-only body (P2
  replaces it); `Presets.cpp` with an empty `PresetAccess` (P3 replaces it) — so P2 and P3 never touch
  `Processor.cpp`. Probes `proc.layout`, `proc.chunk`, `proc.{text,latency,bypass,null}.<key>`.
- **Acceptance.** [AGENT] with `probe:proc.(layout|chunk|text|latency|bypass|null)`; `Scripts/validate.sh --vst3-only
  "$WT/build-agent"`; `cd "$WT" && cmake --preset rtsan && cmake --build --preset rtsan && ctest --preset rtsan -L
  'probe:proc\.latency'`.
- **Split seam.** `HostText.cpp` + `proc.text` → P2 (S8).
- **K2.** #6 (no listeners/`AsyncUpdater`; latency set in `prepareToPlay`); #7; #9; #18; #19; #23; #25a/b/d; #27
  (ports outlive editors); #14 (`absrel` rows).

#### S7.2 · U2 · Band: history, transfer, meters — FCompressor · L

- **OWNS:** `Source/editor/HistoryStore.h Source/editor/views/{Band,HistoryPlot,TransferPlot,MeterColumn}.{h,cpp}
  Tools/probes/plugin/{EngineFacade.h,EngineFacade.cpp,ui_curve.cpp,ui_truth.cpp}`
- **Reads.** 02 §6.5, §8.7–8.8, §9.1–9.3, §9.6; 01 §6–§7; K1 #9, #22; ADR-42.
- **Inputs.** U1s, F8, F7 merged; FunkGui v0.6.0 (shapes, `LiveFeed`).
- **Deliverables.** `HistoryStore` (20,480 × 1 ms columns; laps and `b5` gap markers); `HistoryPlot` (IN area, OUT,
  hanging GR, DET; min/max; seam-free `areaStrip`); `TransferPlot` (static pre-makeup curve, net curve, ghosts of other
  detents, operating dot + GR needle, threshold/knee/ratio/range handles proxying `SlotModel`s with relative drags
  unless `kFlagPlotIsPlain` + identity map, curve landing); `MeterColumn`; one dB→y map at 4 px/dB; all plots
  parameterised by rect and px/dB so `CharScreen` reuses them; **`EngineFacade`**: a `ProcessorFacade` over a real
  `fcdsp::EngineHost` (48 kHz, bs 128, deterministic program, real telemetry), so `ui.truth` needs no JUCE processor;
  probes `ui.curve.<key>`, `ui.truth.<key>` (band rows).
- **Acceptance.** [AGENT] with `probe:ui.(curve|truth|geometry)` × 8; [PNG] `panel` × {`clean`, `bus-g`, `fet-76`}
  with scripted live frames.
- **Split seam.** `MeterColumn` → U4.
- **K2.** #24 (curves from `resolve` + `overlaySmoothed`); #8 (claim-word reads, gap markers).

#### S7.3 · M1 · Bus G — FCompressor · M

- **OWNS:** `Source/fcdsp/modes/bus-g/** Source/fcdsp/engine/stages/ballistics/DualRelease.h
  Source/fcdsp/engine/stages/combinators/AutoSwitch.h Source/fcdsp/engine/stages/colour/VcaBus.h
  Tools/probes/dsp/dualrelease.cpp docs/modes/bus-g.md`
- **Reads.** 01 §10.4; D §2.4 (SSL G); E §2.5; K2 #1.
- **Inputs.** DW, F7 merged; pin v0.6.0.
- **Deliverables.** Real traits; [H] constants fitted (dial offset) and listed in `docs/modes/bus-g.md`;
  `provisional = false`; `DualRelease` with its FF step **and** its FB affine forms (fast `{α_f·r_f1, 1−α_f}`, slow
  `{α_s·r_s1 + (1−α_s)·α_f·r_f1, (1−α_s)(1−α_f)}`, max of roots) for Diode 609 later; `dsp.dualrelease`.
- **Acceptance.** `ctest --preset agent -L 'mode:bus-g|probe:dsp\.dualrelease'` all pass with fidelity rows blocking;
  [AGENT].
- **K2.** #1; #4 (AUTO detent-edge rows); #10; #20.

**Ownership check S7.** P1 owns only `Source/plugin/**` files and 6 proc probe files; U2 owns `Source/editor/HistoryStore.h`,
4 view pairs and 4 plugin-probe files; M1 owns `modes/bus-g/**`, 3 new stage headers, `dsp/dualrelease.cpp` and its Mode
sheet. File names under `Tools/probes/plugin/` differ (`layout…null` vs `EngineFacade, ui_curve, ui_truth`).
**P1 ∩ U2 = P1 ∩ M1 = U2 ∩ M1 = ∅.**

**Exit criteria.**
- Demo: the installed plugin (generic editor) compresses in a DAW; Quality and Lookahead changes re-report latency;
  headless band PNGs show live history and the operating dot.
- Labels green: `proc` (all), `probe:ui.(curve|truth)`, `mode:bus-g` (all 24 tests). Expected candidates: provisional
  Modes except Bus G; UI golden rows (FZ5).

**Lead checklist S7.** First **real** `validate.sh` (Mode fuzzing, setup fuzzing, state restore into non-fresh
instances). Bless `proc.layout` (`layout.params.hash`), `proc.text` defaults for `clean` and `bus-g`, and `modes/bus-g/*`.

---

### Sprint 8 — State, Characteristics B, GPU host

**Goal.** Sessions save and restore exactly (the v1-forever state format gets its evidence early); the step-response,
side-chain and colour panes exist; FunkGui's GPU host proves live == headless on its own gallery app.

**LEAD base.** Pin v0.6.0.

#### S8.1 · P2 · State and migration — FCompressor · M

- **OWNS:** `Source/plugin/{State.h,State.cpp,StateMigration.cpp} Tools/probes/plugin/{state,modeparam,fixtures}.cpp`
- **Reads.** 01 §9.1; C §5.7; K2 #10, #23, #25c; K3 #14.
- **Inputs.** P1 merged; pin v0.6.0.
- **Deliverables.** Save/load per 01 §9.1 (`stateVersion`, `modeId`, `modeRev`, migrations table, `StateNotice`,
  `listen`/`delta` reset, one batch with the snap after the last write, `<UI charExpanded scTab>`, optional
  `writePreset/readPreset` hooks left null); probes `proc.state.<key>` (non-fresh restore, bitwise), `proc.modeparam`,
  `proc.fixtures` (synthesised in-probe blobs until v1; candidate blobs written to `build-*/fixture-candidates/`).
- **Acceptance.** [AGENT] with `probe:proc.(state|modeparam|fixtures)`; `Scripts/validate.sh --vst3-only
  "$WT/build-agent"`.
- **K2.** #10; #23; #25c; K3 #14 (state never links FunkPresets).

#### S8.2 · U4 · Characteristics B: step response, side chain, colour — FCompressor · M

- **OWNS:** `Source/editor/{PreviewWorker.h,PreviewWorker.cpp} Source/editor/views/{StepPlot,SidechainPlot,ColourPlot}.{h,cpp}
  Tools/probes/plugin/ui_chars.cpp`
- **Reads.** 02 §7.2–7.4, §9.3; 01 §7; ADR-41.
- **Inputs.** U1a (stubs), U1s, F8 merged; FunkGui v0.6.0.
- **Deliverables.** `PreviewWorker` (`stepResponse` off the message thread; computed inside `tick()` with
  `syncPreview`; `wantsFullRate()` while pending; stoppable before `closeGestures`); `StepPlot` (attack and release,
  declared spec, measured crossing); `SidechainPlot` (detector-path response with an SC HPF handle proxying the `schpf`
  slot); `ColourPlot` (static transfer + harmonic bars); probe `ui.chars.<key>` (crossing marker = `analysis::measure`
  ± 0.5 px; SC curve on `scResponse` ≤ 0.5 px; colour curve on `colourCurve`; bars = `harmonicsDb`).
- **Acceptance.** [AGENT] with `probe:ui.(chars|geometry)`; [PNG] `chars.sidechain`, `chars.colour` × {`clean`,
  `fet-76`, `opto-2a`}.
- **K2.** #24; #27 (worker stops first).

#### S8.3 · G7\* · EditorHost, GPU sink, runtime ObjC names, live parity — FunkGui · L

- **OWNS** (FunkGui): `include/funkgui/gpu/** src/gpu/** src/panel/CaptureConfig.cpp tools/capture-frame.sh
  tools/GalleryApp/** test/unit/objc_names.mm`
- **Reads.** 02 §4.5, §5.1, §5.6; A §1, §6.1, §6.6; K2 #16, #27.
- **Inputs.** v0.6.0.
- **Deliverables.** `EditorHost` (surface lifecycle, fallback, retry, backing scale, `FramePump` client, diagnostics
  incl. overflow counter; `CaptureConfig` read once from `<PREFIX>` env; fixed-dt capture; teardown order),
  `A11yBridge`, `FramePump`/`DisplayLink`/`NativeSurface` as `juce::ObjCClass` with randomised names under
  `FUNKGUI_OBJC_PREFIX "RenderView_"`/`"DisplayLinkTarget_"`, `BgfxSink::submit` (on `expandToVertices`),
  `BgfxContext` (atlas from `FontService`, `maxTransientVbSize` 32 MiB, `configure()`); the snapshot `SdfCanvas`
  deleted; `capture-frame.sh <app> <out.dump> <ENV_PREFIX>`; `GalleryApp` (Standalone, no-op processor, hosts
  `GalleryPanel`); tests `fg.objc.names` (two registrations → distinct class names) and `fg.gallery.live` (label
  `live`: capture fingerprint == headless fingerprint for 3 sections).
- **Acceptance.** [FG]; `lockf -t 900 /tmp/fcmp-gui.lock ctest --test-dir "$FWT/build-agent-gui" -L live` green; [XV]
  **and** the same with `cmake --preset agent-gui` in the `ro-` worktree (FCompressor's GPU configuration compiles the
  new `gpu` sources; the editor is still Generic).
- **Split seam.** `A11yBridge` → a lead FunkGui PATCH (v0.7.1) at the S9 base; its first consumer is U7 (S10).
- **K2.** #16; #27; #26f; 02 §4.5 (overflow made visible). Spike fallback: parity on geometry only, text excluded.

**Ownership check S8.** P2 owns `Source/plugin/{State.*,StateMigration.cpp}` (P1's minimal `State.*` is merged, P1 is
done) and 3 proc probes; U4 owns `PreviewWorker.*`, 3 view pairs and `ui_chars.cpp`; G7 is in FunkGui.
**P2 ∩ U4 = ∅; G7 ∩ {P2, U4} = ∅.**

**Exit criteria.**
- Demo: a session saved in Clean with odd values reloads bit-exactly into a running instance; the Characteristics
  step and side-chain panes render (headless); FunkGui's gallery Standalone captures bit-equal to headless.
- Labels green: `probe:proc.(state|modeparam|fixtures)`, `probe:ui.chars`; FunkGui `fg` + `live`.

**Lead checklist S8.** Tag **v0.7.0**; the pin moves at the S9 base. **Milestone gates** (§3.4, gui-live = FunkGui
gallery). Review P2's fixture candidates (not adopted before v1: fixtures are write-once from a shipped build).

---

### Sprint 9 — FET 76, Opto 2A, Characteristics A

**Goal.** Two more hardware Modes final; the full-panel Characteristics screen complete (control path, readouts,
tabs, handles).

**LEAD base.** Pin v0.7.0.

#### S9.1 · M2 · FET 76 — FCompressor · M

- **OWNS:** `Source/fcdsp/modes/fet-76/** Source/fcdsp/engine/stages/colour/FetColour.h
  Source/fcdsp/engine/stages/law/FetVcr.h Tools/probes/dsp/fetcolour.cpp docs/modes/fet-76.md`
- **Reads.** 01 §10.5; D §2.1; E §2.6–2.7.
- **Inputs.** DW, F7 merged; pin v0.7.0.
- **Deliverables.** Real traits (FB), `FetColour` with revision constants on `voice`, `law::FetVcr`, ALL as a
  monotone curve (larger k, threshold shift, attack and colour changes), the `GR` switch ramp, linked minimum attack;
  `kT0` and the dial law fitted **before** the Mode enters `modes-ever.tsv`; `provisional = false`; `dsp.fetcolour`.
- **Acceptance.** `ctest --preset agent -L 'mode:fet-76|probe:dsp\.fetcolour'` with fidelity rows blocking; [AGENT].
- **K2.** #4; #5a; #1; #14; #10.

#### S9.2 · M3 · Opto 2A — FCompressor · M

- **OWNS:** `Source/fcdsp/modes/opto-2a/** Source/fcdsp/engine/stages/detector/OptoSense.h
  Source/fcdsp/engine/stages/ballistics/OptoCell.h Source/fcdsp/engine/stages/gain/OptoCellCurve.h
  Source/fcdsp/engine/stages/scshape/R37Shelf.h Source/fcdsp/engine/stages/colour/TubeTransformer.h
  Tools/probes/dsp/optocell.cpp docs/modes/opto-2a.md`
- **Reads.** 01 §10.6; D §2.2; E §2.7.
- **Inputs.** DW, F7 merged; pin v0.7.0.
- **Deliverables.** The T4 cell (two-stage release with memory), R37 emphasis shelf, tube/transformer colour,
  `FeedbackDelayed` with its runtime guard; `provisional = false`; `dsp.optocell`.
- **Acceptance.** `ctest --preset agent -L 'mode:opto-2a|probe:dsp\.optocell'` incl. the 22.05 kHz `dsp.srsweep` row;
  [AGENT].
- **K2.** #5c; #1; #14.

#### S9.3 · U3 · Characteristics A: screen, control path, readouts — FCompressor · M

- **OWNS:** `Source/editor/views/{CharScreen,ControlPathPlot,Readouts}.{h,cpp}
  Tools/probes/plugin/{ui_truth,ui_charscreen}.cpp`
- **Reads.** 02 §7 (all), §9.1–9.2; F §4–§5.
- **Inputs.** U2, U4, U1s merged; FunkGui v0.7.0.
- **Deliverables.** `CharScreen` complete (enter/leave by latch, Return and band double-click; 0.12 s `ScreenFader`;
  chrome in identical pixels; larger HISTORY/TRANSFER + the stage-1 curve for two-stage Modes; IN/SC/GR/OUT meters;
  SC|COLOUR tabs on `UiState.scTab`; handles proxy slot models; keyboard and a11y); `ControlPathPlot` (target vs
  applied GR with the min/max band, phase lane, the Mode's history internal, event stripes); `Readouts`; `ui.truth`
  rows for CONTROL PATH and READOUTS 11–18; probe `ui.charscreen.<key>` (chrome prims identical on both screens;
  every toggle path; handle drags write the same ports as the slots; Tab order within the screen).
- **Acceptance.** [AGENT] with `probe:ui.(truth|charscreen|geometry)`; [PNG] `chars.sidechain` × {`clean`,
  `diode-609`, `opto-2a`}.
- **K2.** #24; #27. Q4 fixes the chrome reading.

**Ownership check S9.** M2 and M3 own different Mode directories and different stage headers (`colour/FetColour, law/FetVcr`
vs `detector/OptoSense, ballistics/OptoCell, gain/OptoCellCurve, scshape/R37Shelf, colour/TubeTransformer`); U3 owns 3
view pairs and 2 plugin probes (`ui_truth.cpp` passes from U2 to U3). **M2 ∩ M3 = M2 ∩ U3 = M3 ∩ U3 = ∅.**

**Exit criteria.** Demo: FET 76 and Opto 2A behave per their sheets; the full Characteristics screen renders every
pane headlessly. Labels green: `mode:fet-76`, `mode:opto-2a`, `probe:ui.charscreen`. **Lead checklist S9.** Bless
`modes/{fet-76,opto-2a}/*`.

---

### Sprint 10 — Mu 67, Diode 609, GPU editor

**Goal.** The two hardest remap/feedback Modes final; the real GPU editor in the plugin, with live parity proven.

**LEAD base.** Pin v0.7.0.

#### S10.1 · M4 · Mu 67 — FCompressor · L

- **OWNS:** `Source/fcdsp/modes/mu-67/** Source/fcdsp/engine/stages/gain/ProgressiveKnee.h
  Source/fcdsp/engine/stages/ballistics/{TcSelector,MultiStage3}.h Source/fcdsp/engine/stages/colour/TubePushPull.h
  Tools/probes/dsp/{progressiveknee,tcselector}.cpp docs/modes/mu-67.md`
- **Reads.** 01 §10.7 (Mu 67); D §2.3; E §2.6–2.7; K1 #26.
- **Inputs.** DW, F5, F7 merged; pin v0.7.0.
- **Deliverables.** `ProgressiveKnee` in `FeedbackZdf`, `TcSelector` (TC1–4 as `SmoothBranching`, TC5/6 as
  `MultiStage3`, max of roots), `TubePushPull`, Lat/Vert via `stmode`, the live EFF ratio; `provisional = false`.
- **Acceptance.** `ctest --preset agent -L 'mode:mu-67|probe:dsp\.(progressiveknee|tcselector)'`; `fb.monotone` for
  every DC THRESH; [AGENT].
- **Split seam.** `TubePushPull` → a `ColourSelect` of existing shapers; the real stage follows after v1.
- **K2.** #1; #5a.

#### S10.2 · M5 · Diode 609 — FCompressor · M

- **OWNS:** `Source/fcdsp/modes/diode-609/** Source/fcdsp/engine/stages/stage2/SharedElementMax.h
  Source/fcdsp/engine/stages/colour/DiodeBridge.h Source/fcdsp/engine/stages/scshape/SlowHp.h
  Tools/probes/dsp/sharedelement.cpp docs/modes/diode-609.md`
- **Reads.** 01 §10.7 (Diode 609); D §2.5; E §3.3.
- **Inputs.** DW, F7, M1 (`DualRelease` FB forms) merged; pin v0.7.0.
- **Deliverables.** `SharedElementMax<PeakLog, SmoothBranching>` stage 2 on aux lanes, `DiodeBridge` colour, SLOW →
  SC HP, A1/A2 through `DualRelease` inside the FB solve, `s2On` ramp; `provisional = false`.
- **Acceptance.** `ctest --preset agent -L 'mode:diode-609|probe:dsp\.sharedelement'`; [AGENT].
- **K2.** #1; #20; #4 (`s2thr` OFF detent edge).

#### S10.3 · U7 · GPU editor and live parity — FCompressor · M

- **OWNS:** `Source/editor/gpu/** Source/plugin/CreateEditorGpu.cpp Scripts/gui-live.sh`
- **Reads.** 02 §5.1, §6.1; 03 §3.6 (live parity); K2 #16, #27.
- **Inputs.** G7 (v0.7.0), P1, U1a merged.
- **Deliverables.** `fcmp::ui::Editor : funkgui::EditorHost` (960×640, `setResizable(false, false)`, one `setSize`, owns
  the `Panel`, destructor stops the worker then closes gestures); `CreateEditorGpu.cpp` (configure now prints "GPU
  editor: Gpu"); `Scripts/gui-live.sh` (03 §3.6: `lockf`, views `panel`, `chars.sidechain`, `modebrowser` × `clean`,
  the `FCMP_UI_*` variables, fingerprint equality with headless).
- **Acceptance.** [GPU]; `Scripts/gui-live.sh "$WT/build-agent-gui"` → 3/3 equal (the only GUI agent this sprint);
  `Scripts/validate.sh --vst3-only "$WT/build-agent-gui"` (editor open/close × N).
- **K2.** #16 (AU + VST3 in one process); #27; #26e; #19.

**Ownership check S10.** M4 and M5 own different Mode directories and different new stage headers; U7 owns
`Source/editor/gpu/**`, one `plugin/` file (`CreateEditorGpu.cpp`, no other card touches `Source/plugin/`) and one
lead-delegated script. **M4 ∩ M5 = M4 ∩ U7 = M5 ∩ U7 = ∅.**

**Exit criteria.** Demo: the plugin opens its real GPU editor in a DAW; Mu 67 and Diode 609 behave per their sheets.
Labels green: `mode:mu-67`, `mode:diode-609`; `ui.live` (label `live`) 3/3. **Lead checklist S10.** First FCompressor
`gui-live` from `build-lead`. Bless `modes/{mu-67,diode-609}/*`.

---

### Sprint 11 — Bus 25, Brickwall, preset library

**Goal.** The last two Modes final, so no Mode is provisional; FunkGui ships the preset data layer.

**LEAD base.** Pin v0.7.0. If S6 chose "snapshot": **S11.L1** copy HR `Source/presets/*` byte for byte into FunkGui
`include/funkgui/presets/` and `src/presets/` as its own commit with `SEED.tsv` rows (sha256 verified that day), before
G8's worktree is created.

#### S11.1 · M6 · Bus 25 — FCompressor · M

- **OWNS:** `Source/fcdsp/modes/bus-25/** Tools/probes/dsp/bus25topo.cpp docs/modes/bus-25.md`
- **Reads.** 01 §10.7 (Bus 25); D §2.4 (API 2500); E §2.7.
- **Inputs.** DW, F5, F7 merged; pin v0.7.0.
- **Deliverables.** FF/FB per chunk on `voice` (NEW/OLD), `LinkCvSum` in FB after the per-lane solve, the `tmode`
  variant (stepped ∪ VAR release), 3 knees, THRUST on the host `sce` tilt; any new policy is Mode-local in
  `modes/bus-25/`; `provisional = false`; `dsp.bus25topo` (a NEW↔OLD flip is a clean kernel crossfade).
- **Acceptance.** `ctest --preset agent -L 'mode:bus-25|probe:dsp\.bus25topo'`; [AGENT].
- **K2.** #5b; #22; #1.

#### S11.2 · M7 · Brickwall — FCompressor · M

- **OWNS:** `Source/fcdsp/modes/brickwall/** Source/fcdsp/engine/stages/detector/TruePeak4x.h
  Source/fcdsp/engine/stages/ballistics/SlidingMaxBox.h Source/fcdsp/engine/stages/colour/LoudClip.h
  Tools/probes/dsp/slidingmax.cpp docs/modes/brickwall.md`
- **Reads.** 01 §10.7 (Brickwall); D §2.8; E §5.4; K2 #21.
- **Inputs.** DW, F7 merged; pin v0.7.0.
- **Deliverables.** `SlidingMaxBox` over `PrepareInfo::scratch` (exact re-sum every 4,096 samples; `look` slews ≤ 1
  sample per tick), true-peak SC 4× with `scDelaySamples()`, `LoudClip`; HQ ceiling ≤ +0.1 dB, STD ≤ +1.0 dB TP;
  `provisional = false`; `dsp.slidingmax`.
- **Acceptance.** `ctest --preset agent -L 'mode:brickwall|probe:dsp\.slidingmax'` incl. the 10-minute soak row of
  `dsp.null.brickwall`; [AGENT].
- **K2.** #21a–d; #12; #13.

#### S11.3 · G8 · FunkPresets — FunkGui · L

- **OWNS** (FunkGui): `include/funkgui/presets/** src/presets/** test/unit/{presets_store,presets_file,presets_hooks}.cpp`
- **Reads.** 01 §9.2; 02 §1.2, §2.2 (presets row); HR `Source/presets/*.h` (read-only, or the S11.L1 snapshot).
- **Inputs.** v0.7.0 (+ the snapshot commit if any); Q2 = yes.
- **Deliverables.** `funkgui::presets`: `PresetTypes` + `Attribute`, `ProductConfig` (name, extension, XML root, DB env
  variable), `PresetHooks` (beginApply, applyBefore, captureExtra, onApplied), `PresetManager`, `PresetStore` (SQLite
  from the SDK, WAL, in-memory fallback), `PresetFile` (tags and timestamps never exported). FunkGui's CMake picks up
  `src/presets/**` automatically (G1).
- **Acceptance.** [FG] with `fg.presets.store` (round trip, WAL, fallback, env override), `fg.presets.file`,
  `fg.presets.hooks` (apply order); [XV].
- **Split seam.** Re-implementation case: `PresetFile` import/export → P3 (S12) as an FCompressor-side adapter.
- **K2.** #23 (apply inside a batch via hooks); #19 (sandboxed fallback); ADR-51.

**Ownership check S11.** M6 owns only `modes/bus-25/**`, one probe and its sheet; M7 owns `modes/brickwall/**`, three
new stage headers, one probe and its sheet; G8 is in FunkGui. **M6 ∩ M7 = ∅; G8 ∩ {M6, M7} = ∅.**

**Exit criteria.** Demo: all 8 Modes are final; `cmake --preset release && cmake --build --preset release --target
fcmp_probe_dsp && ctest --preset release -L 'probe:dsp\.registry'` passes with `FCOMPRESSOR_RELEASE=ON` (no provisional
Mode). Labels green: `mode:bus-25`, `mode:brickwall`; FunkGui `fg`. Expected candidates: UI golden rows only.
**Lead checklist S11.** Tag **v0.8.0** (pin at the S12 base). Bless `modes/{bus-25,brickwall}/*`.

---

### Sprint 12 — Presets and browsers

**Goal.** Factory and user presets work end to end; the Mode and preset browsers complete the UI; the last milestone
gates produce the hardening list.

**LEAD base.** Pin v0.8.0.

#### S12.1 · P3 · Preset integration and factory bank — FCompressor · M

- **OWNS:** `Source/plugin/{Presets.cpp,factory/**} Tools/probes/plugin/{presets,prefs}.cpp`
- **Reads.** 01 §9.2; 02 §9.5 (`PresetAccess`); C §5.7.7.
- **Inputs.** P2, G8 (v0.8.0) merged.
- **Deliverables.** `PresetAccess` over `funkgui::presets` (`ProductConfig` FCompressor / `.fcmppreset` /
  `FCompressorPreset` / `FCMP_PRESETS_DB`); hooks (batch open, `mode` from `modeId`, capture `modeId`/`modeRev`,
  batch close raises the snap); `isPresetParameter` = the 22 Mode-filtered parameters; `isModified` at 1e-4; the
  `<PRESET>` state hooks installed through `State.h`; `factory/FactoryBank.cpp` + `factory/<key>.inc` for 8 Modes
  (Init at index 0, fixed UUIDs, `FCMP_FACTORY_BANK_REVISION`); probes `proc.presets`, `proc.prefs`.
- **Acceptance.** [AGENT] with `probe:proc.(presets|prefs|state)`; `Scripts/validate.sh --vst3-only "$WT/build-agent"`.
- **K2.** #23; #10; #19; #25d.

#### S12.2 · U5 · Mode browser — FCompressor · M

- **OWNS:** `Source/editor/views/ModeBrowser.{h,cpp} Tools/probes/plugin/{ui_browsers,ui_modebrowser}.cpp`
- **Reads.** 02 §8.5–8.6, §8.9.
- **Inputs.** U1b, U1s merged; FunkGui v0.8.0.
- **Deliverables.** The overlay at `{40,64,880,286}` (8 group columns × 12 rows, paging, current row), arrows / Return /
  Esc, Alt-click = switch + `modeDefaults` inside one batch; probes `ui.browsers` (global, spec-only) and
  `ui.modebrowser.<key>`.
- **Acceptance.** [AGENT] with `probe:ui.(browsers|modebrowser|geometry)`; [PNG] `modebrowser` × `clean`.
- **K2.** #23; #4 (a plain switch writes only `mode`).

#### S12.3 · U6 · Preset strip and browser — FCompressor · M

- **OWNS:** `Source/editor/views/{PresetStrip,PresetBrowser}.{h,cpp} Tools/probes/plugin/ui_presets.cpp`
- **Reads.** 02 §6.2 (header strip), §8.9; F §6.
- **Inputs.** U1a merged; FunkGui v0.8.0 (`LineEdit`, `MenuLook`); `PresetAccess` (FZ1) through `FakeFacade`.
- **Deliverables.** Header strip ‹ name › with a modified marker; browser overlay (factory/user, categories, save-as via
  `LineEdit`, menus via `MenuLook`); probe `ui.presets` (global: ‹ › calls `step(±1)`; apply is one call; save-as flow;
  redraw on `revision()`; a11y).
- **Acceptance.** [AGENT] with `probe:ui.(presets|geometry)`; [PNG] `presetbrowser` × `clean`.

**Ownership check S12.** P3 owns `Source/plugin/{Presets.cpp,factory/**}` and 2 proc probes; U5 and U6 own different
view pairs and different `ui_*` probes (`ui_browsers, ui_modebrowser` vs `ui_presets`). **P3 ∩ U5 = P3 ∩ U6 = U5 ∩ U6 = ∅.**

**Exit criteria.** Demo: factory presets load across Modes and user presets save/rename/import in the installed
plugin; the Mode browser picks any Mode. Labels green: `probe:proc.(presets|prefs)`, `probe:ui.(browsers|modebrowser|presets)`.
**Lead checklist S12.** **Milestone gates** incl. `gui-live` for all 5 views, `lead-x86-verify` and a `universal`
compile; the findings list becomes card S13.2's OWNS (or S13.2 is not spawned). Bless `proc.presets`.

---

### Sprint 13 — Hardening, UI freeze, release: **v1**

**Goal.** Fix what the gates found, freeze the UI goldens, and ship the signed, notarised universal v1.

**LEAD base.** Pin v0.8.0 (or a PATCH). Q6 = universal and Rosetta installed (§8); the user has stored the notary
profile (`xcrun notarytool store-credentials`) and the Developer ID identity is in the login keychain (USER).

#### S13.1 · H1a · UI audit and freeze preparation — FCompressor · M

- **OWNS:** `Source/editor/** Tools/probes/plugin/ui_*.cpp Tools/probes/plugin/{FakeFacade,EngineFacade}.{h,cpp}`
- **Deliverables.** Tab order and accessibility audit across every sub-view (every slot a Tab stop incl. locked,
  derived and n/a; handled keys return `true`); fixes; the `ui.a11y`/`ui.input` lines and `ui.geometry` rows stable for
  adoption; a VoiceOver check list for the lead.
- **Acceptance.** [AGENT] with `-L ui` all green; [GPU]; `Scripts/gui-live.sh "$WT/build-agent-gui"` for all 5 views.
- **K2.** #27.

#### S13.2 · H1b · Real-time, sanitiser and bench findings (conditional) — FCompressor · M

- **OWNS:** exactly the files named in the S12 findings list, drawn only from `Source/fcdsp/** Source/plugin/**
  Tools/probes/dsp/*.cpp Tools/bench/** docs/modes/**` and the non-`ui_` files of `Tools/probes/plugin/`. **Not spawned
  if the list is empty**; the slot is then the plan's buffer.
- **Acceptance.** `cmake --workflow --preset asan-verify`, `tsan-verify`, `tsan-agent-verify`, `rtsan-verify` green in
  the worktree; [AGENT]; bench report vs `ctBudgetNsPerSample`.
- **K2.** #18; #13; #8.

#### S13.3 · R1 · Release script — FCompressor · S

- **OWNS:** `Scripts/release.sh`
- **Reads.** 03 §5; HR `Scripts/release.sh` (read-only); B §5, §6.2.
- **Deliverables.** 03 §5 in full, plus refusals for a Generic editor (`FCOMPRESSOR_GPU_EDITOR` unset) and a
  `FCOMPRESSOR_RELEASE=OFF` tree; `--self-test` exercises every refusal (dirty tree, override, missing stamp, universal
  without `build-lead-x86/verify-passed-<sha>`, extra exported symbols, Generic editor, provisional Mode) in a scratch
  clone.
- **Acceptance.** `Scripts/release.sh --self-test`; ad-hoc dry run `FCMP_SIGNING_IDENTITY= Scripts/release.sh
  build-release "$WT/out"` → zips + `MANIFEST.txt`, `codesign --verify --strict` on each bundle, `nm -gU` export check.
- **K2.** #15; #26f; #19.

**Ownership check S13.** H1a owns `Source/editor/**` and the `ui_*`/facade probe files; H1b is confined to
non-editor sources and non-UI probes; R1 owns one script. **H1a ∩ H1b = H1a ∩ R1 = H1b ∩ R1 = ∅.**

**Exit criteria (v1).**
- `Scripts/verify.sh --integration build-lead` fully green with **0 candidates**; `build-lead-x86/verify-passed-<sha>`;
  `validate.sh` green; `gui-live` 5/5; asan, tsan, tsan-agent, rtsan green.
- Signed, notarised, stapled **universal** zips of AU, VST3 and Standalone with `MANIFEST.txt`; post-flight on a clean
  user account (auval, one AU host, one VST3 host, GarageBand for the sandbox).

**Lead checklist S13 (release order).**
1. Merge H1a, H1b, R1; integration verify.
2. **FZ5**: `FCMP_ALLOW_BLESS=1 Scripts/golden.py adopt build-lead --allow-geometry --only 'modes/*/ui.*' --reason
   'UI freeze FZ5'` (+ the `lines` sidecars); rerun verify: 0 candidates.
3. Append the 8 Modes to `tests/fixtures/modes-ever.tsv` and `modeparam.tsv`; `project(FCompressor VERSION 1.0.0)`;
   commit; `git tag -a v1.0.0`.
4. `cmake --workflow --preset lead-x86-verify`; `cmake --preset universal && cmake --build --preset universal`;
   all milestone gates on the tagged tree.
5. `Scripts/release.sh build-universal dist/` (sign, notarise, staple, manifest); post-flight checklist.
6. After shipping: adopt state fixtures from the shipped build (`tests/fixtures/state/*.bin`, write-once) as
   `fixtures: v1.0.0`; `git tag -a fcmp-s13`. FunkGui stays v0.8.x (v1.0.0 is HR's migration tag, §5).

---

## 5. After v1

**Mode waves** (03 §4.9.3 pattern; D §6.3 order). Each wave: one descriptor-wave card (≤ 6 descriptors, owns the wave's
new `Modes.def` lines from slot 8 up), then ≤ 3 Mode DSP cards per sprint; from now on each Mode card also owns
`Source/plugin/factory/<key>.inc`, and any change that moves a shipped Mode's `dsp.print` default hash needs
`revision++` and a Mode-sheet entry (ADR-29).

| Wave | Modes (D §6.1 ids) | Engine/UI work first |
|---|---|---|
| W2 | Octo (M18), Opto Tube 1B (M04), Console E (M10), Mu Mastering (M06), Opto 3A (M03), Diode 54 (M17) | none: new policies only |
| W3 | Twin Stage (M24), RMS 60 (M14), Module Tilt (M13), Module FB (M12), Zener Desk (M19), Pump (M31) | an engine card for serial stage 2 (`Stage2Kind::serialPre`) before M24; tempo-synced release (host BPM into `BlockParams`, a sprint-frozen contract revision) before M31 |
| later | M20–M23, M25–M27, M07, M08, M29, M32–M34 | M26 negative ratio and M35 Upward need a FunkGui MINOR for GR < 0 in HISTORY/meters (02 §11 Q5) |

**HardwareReverb → FunkGui migration** (ADR-04; only after FCompressor v1):
1. FunkGui **v1.0.0**: freeze the public API (pre-1.0 → semver promise), CHANGELOG golden-impact audit, `SEED.tsv` diff
   against HR's current `Source/gui`.
2. HR under version control (HR has no git today, 02 §2.1 — a user decision) and a baseline capture of HR's goldens.
3. HR consumes FunkGui v1.0.0 through FetchContent; its `Source/gui` is deleted; `BgfxEditor` is ported onto
   `EditorHost` + a `funkgui::Panel` composition; HR's tools move to Harness v2 (bare-number tolerances parse as `abs:`).
4. Gate: HR's goldens identical — `funkgui_framerender --legacy-hr` hashes, FontProbe, PrefsCheck, preset probes. Any
   difference is a FunkGui bug fixed by a FunkGui PATCH, never by editing HR's expectations.
5. HR's presets onto `FunkGui::presets`.

**Backlog (v2 appends and deferrals):** `lshape`, `out`, reference level, per-Mode memory (01 §12.4); a `s2GrMaxDb`
history field (02 §11 Q8); indexed quads (02 §4.5); hardware knob direction (02 §11 Q6); Intel, if v1 shipped
arm64-only.

---

## 6. Card → work-item map

| 03 §4.9.2 item | Card(s) here | Change |
|---|---|---|
| G0 | S0.L3 (LEAD) | + Harness v2 and placeholder CMake functions |
| G1–G3, G5–G8 | S0.1, S1.3, S2.3, S4.3, S5.3, S8.3, S11.3 | G3 + gallery infra; G7 + `BgfxSink::submit`/`BgfxContext` |
| G4 | S3.3 | moved before G5 |
| F0, B0 | S0.2, S0.3 | `Registry.cpp` → F0; `check-headers.sh` → B0; `ProcessorFacade.h` → LEAD-FZ1 |
| F1, F2 | S1.1, S1.2 | F2's evidence = global probes; `dsp.quant` → F3 |
| F3, F6, F9, F4 | S2.1, S2.2, S3.1, S3.2 | + `NoStage2`, `Fidelity.h`, `dsp.srsweep`, `dsp.fbsolve`, `Bench.cpp` |
| DW, F5, F8, F7 | S4.1, S4.2, S5.1, S6.1 | + `dsp.router` |
| P1–P3 | S7.1, S8.1, S12.1 | P1 creates minimal `State.*`/`Presets.cpp`; P2 pulled to S8 |
| M1–M7 | S7.3, S9.1, S9.2, S10.1, S10.2, S11.1, S11.2 | all after F7 |
| U1a | S5.2 (U1a skeleton) + S6.2 (U1s slots) | split |
| U1b, U2–U7 | S6.3, S7.2, S9.3, S8.2, S12.2, S12.3, S10.3 | + `ui.chrome`, `ui.chars`, `ui.charscreen`, `ui.modebrowser`, `ui.presets`; `ui.browsers` → U5 |
| H1 | S13.1 (H1a) + S13.2 (H1b) | split |
| – | S13.3 (R1) | new |

---

## 7. Deviations from 03 §4.9 and fixes to the appendices

The lead applies these to 03 (and where noted 01/02) as revisions at the next freeze point.

| # | Change | Why |
|---|---|---|
| D1 | Harness v2 is written by the **lead in G0** and ships in v0.0.1 (03 §3.2.2 said B0 uses only `ScopedFtz` until v0.1.0) | B0's `ProbeMain`/`FCMP_PROBE` signature and `dsp.selftest` need `funkgui::test::Probe` in S0; the alternative (B0 in S1) adds a sprint to the critical path |
| D2 | G4 (shapes) runs **before** G5 (RuleSlider): v0.4.0 = shapes, v0.5.0 = RuleSlider | `RuleSlider`'s locked track is `Canvas::dotted`, which G4 implements (02 §4.2) |
| D3 | U1a split into **U1a (skeleton)** and **U1s (slots)**; the skeleton stubs every plot as well as every sub-view, and `Band`/`CharScreen` compose their plots from S5 | U1a was more than one session; plot stubs make U2/U3/U4 order-independent and file-disjoint |
| D4 | `ProcessorFacade.h` is written by the **lead at FZ1** (end of S1), not by F0 | it cannot compile before v0.2.0 declares `ParamPort`; no uncompilable file ever sits in the tree |
| D5 | `Registry.cpp` moves from F3 to **F0** and is empty-registry safe | probe files that call `byKey`/`modeSlots()` must link in S1, before any Mode is registered |
| D6 | F2's S1 evidence is the global `dsp.resolve`/`dsp.format`/`dsp.hostparams`; `dsp.quant` moves to F3; FZ1's evidence changes accordingly (03 §4.9.4 named `dsp.quant.clean`) | Clean is registered only in S2 (it needs `ModeEngine`), and D3 includes a D1 row that needs `EngineRig` |
| D7 | `Scripts/check-headers.sh` → B0 (03 gave it to F0 while B0 owned `Scripts/`) | an ownership overlap inside S0 |
| D8 | `Scripts/verify.sh` classifies `probe-results/*.json` itself; `golden.py report` stays a lead tool (03 §3.2.5 had verify call it) | B0 cannot reach FunkGui's `golden.py` in S0; the DoD gate should not depend on another repo's tool version |
| D9 | The CPU vertex expansion becomes a bgfx-free Core function (`canvas/Expand.h`, G3); `BgfxSink::submit`, `BgfxContext` fonts and the 32 MiB budget move to **G7**; the snapshot `SdfCanvas` is retired by G7, not G3 | `fg.canvas.expansion` then runs headless; nothing uses the GPU path between S2 and S8 |
| D10 | Gallery infrastructure (`GalleryPanel`, `GalleryProbe`) is G3's; gallery tests are per section, `fg.gallery.<section>` (02 §3.11 had `fg.gallery.<state>.dpi{1,2}`) | unassigned in 03; per-section files mean a widget card never rewrites another card's goldens |
| D11 | `ui.truth` runs on `EngineFacade` (a real `fcdsp::EngineHost`, no JUCE processor) | U2 precedes P1; the truth property is engine ↔ UI, and `proc.chunk` covers processor ↔ engine |
| D12 | For a `provisional` Mode, fidelity spec rows (curve error, τ, link law, THD) print NOTE lines instead of failing; structural rows stay blocking (`Tools/probes/common/Fidelity.h`, F3) | with generic traits DW could never meet the DoD; 03 §3.8 assumed every spec row passes at once |
| D13 | Owners for probes 03 left unassigned: `dsp.srsweep` → F9, `ui.browsers` → U5, `fcmp_bench` → F4; new probes `dsp.{fbsolve,router,dualrelease,fetcolour,optocell,progressiveknee,tcselector,sharedelement,bus25topo,slidingmax}`, `ui.{chrome,chars,charscreen,modebrowser,presets}` | every card needs a gating probe of its own file |
| D14 | 42 cards, 14 sprints (S0–S13); v1 at the end of S13 | D3 and the H1 split; the new R1 card |
| D15 | Mode cards depend on F7 (Host-driven probes: switch, zipper, null, latency, print); P2 pulled into S8 (03's own suggestion) | a Mode is not done until all 13 dsp probes run on it |
| D16 | `stages/stage2/NoStage2.h` added to F3 | Clean's traits need it (01 §10.3) |
| D17 | F4 and F7 own the same probe files in different sprints (ECO rows, then STD/HQ rows); F9 extends `static.cpp` and `registry.cpp`; U3 extends `ui_truth.cpp` | ownership moves between sprints, never within one |
| D18 | FunkGui registers tests from `// FUNKGUI_TEST` lines and tools by glob, so its CMake is never edited after S0 | mirrors FCompressor's 03 §2.1/§2.9 rule; G3–G8 add files only |
| D19 | v0.0.1 carries placeholder `funkgui_*` CMake functions | B0's final link lines configure against the bootstrap pin (the function names are FZ0) |
| D20 | P1 creates `State.{h,cpp}` and `Presets.cpp` with frozen entry points and minimal bodies; P2 and P3 replace the bodies | `Processor.cpp` stays P1's alone (03 §4.9.6 hotspot) |
| D21 | G0 places the bgfx-coupled `SdfCanvas.{h,cpp}` under `gpu/` | the Core/Gpu per-directory glob holds from the first tag |
| D22 | `CreateEditorGpu.cpp` is chosen only if present (configure prints which); `release.sh` refuses a Generic editor | the GPU configuration builds from S0, three sprints before U7 |
| D23 | Probe `Ctx` carries only the Mode key; probes resolve it through the registry | `ProbeMain` (B0) cannot link the registry in S0 |
| D24 | Descriptors have external linkage (`extern constexpr ModeDescriptor k<Traits>{…}`) | traits (01 §5.3 `static constexpr const ModeDescriptor& desc`) and F2's S1 probe both reference them from other TUs |
| D25 | FZ4's evidence is `ui.geometry.*` × 8 + `lint.headers` over the editor headers (03 §4.9.4 named `ui.textfit`/`ui.a11y`, which now arrive in S6 with U1s) | follows from D3 |

---

## 8. User decisions, and when they are needed

| When | Question | Default the plan assumes | Consequence if different |
|---|---|---|---|
| before S0 | Q3 harness in FunkGui | yes | Harness v2 goes to `Tools/probes/common/Harness.h` (lead pre-work, owned by B0 afterwards) |
| before S0 | Q8 prebuilt shaderc/pluginval | yes | each GPU build directory builds shaderc (≈ 2,060 CPU-s) |
| before S0 | Q5 mix 0–200 %, Q10 calibration, Q9 FET `GR` switch | yes, yes, yes | F0/DW descriptor changes before FZ0/FZ3 |
| before S0 | Q1 lead outside the 3 | yes | the same cards two at a time (21 sprints) |
| **by end of S1** | **Q6 universal** — the requested v1 needs it | **universal: the user runs `softwareupdate --install-rosetta --agree-to-license`** | without Rosetta, ADR-47 ships arm64-only and the "signed universal build" exit criterion of S13 is not met |
| before S5 | Q4 Characteristics keeps the chrome; Q7 prefs per product | yes; per product | `Layout.h` (U1a) and `UiPreferences` (G6) change |
| by end of S6 | Q2 FunkPresets in FunkGui + SQLite; snapshot vs re-implement | yes; snapshot if HR's presets were stable since S3 | fallback: `Source/plugin/presets/` in FCompressor, owned by P3 (+1 card) |
| before S13 | notary profile and Developer ID in the keychain (USER) | present | release stops at an ad-hoc-signed build |
| after v1 | HR under git; FunkGui v1.0.0 migration | – | §5 |
