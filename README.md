# FCompressor

[![CI](https://github.com/Snipet/FCompressor/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/Snipet/FCompressor/actions/workflows/ci.yml)
![macOS 14+ on Apple Silicon](https://img.shields.io/badge/macOS-14%2B%20·%20Apple%20Silicon-555)
![Linux x86-64](https://img.shields.io/badge/Linux-x86--64%20·%20X11%20and%20Wayland-555)
![AU · VST3 · Standalone](https://img.shields.io/badge/formats-AU%20·%20VST3%20·%20Standalone-555)
[![Licence: GPL-3.0](https://img.shields.io/badge/licence-GPL--3.0-555)](LICENSE)

An all-in-one compressor plugin for macOS and Linux. Fourteen compressor types, called **Modes**, share one interface:
the same controls in the same places for every Mode, with a Mode free to lock a control or restrict it to its hardware's
steps. Everything the display draws (the transfer curve, the operating point, the gain-reduction history, the meters) is
computed by the same code the audio runs, and the test suite checks that it matches.

The same DSP and the same editor also run in a browser, as a demo:
[snipet.github.io/FCompressor](https://snipet.github.io/FCompressor/) (see [Web demo](#web-demo)).

![FCompressor running the Opto 2A Mode](docs/images/fcompressor.png)

## Modes

| Mode | What it is |
| --- | --- |
| Clean | A transparent digital compressor, with optional tube, diode or bright colour |
| Bus G | A VCA bus compressor with stepped ratios |
| FET 76 | A feedback FET limiter |
| Opto 2A | A tube optical leveller |
| Mu 67 | A variable-mu tube limiter whose ratio rises with level |
| Diode 609 | A diode-bridge compressor and limiter |
| Bus 25 | A VCA bus compressor that runs feed-forward or feedback |
| Brickwall | A lookahead true-peak limiter |
| Octo | A hybrid VCA compressor with distortion, up to NUKE |
| Console E | A console channel's VCA compressor |
| Opto 3A | A solid-state optical compressor |
| Diode 54 | A diode-bridge compressor with a fast-attack option |
| Mu Mastering | A variable-mu mastering compressor and limiter |
| Opto Tube 1B | A tube optical compressor with manual or fixed timing |

Each Mode has its own factory presets, and `docs/modes/` has a sheet per Mode: what it runs, every modelling constant
with its source, and its measurements.

## Features

- **One interface for every Mode.** Switching Mode keeps your automation meaningful and crossfades the sound.
- **A display you can trust:** transfer curve with the live operating point, gain-reduction history, a VU or hardware
  meter face per Mode, and a Characteristics screen with the detector, the side chain and the colour stage.
- **Side chain:** high-pass, tilt emphasis, external key and listen; stereo link and mid/side modes.
- **Mix to 200 %**, delta (hear what the compressor removes), auto makeup, lookahead up to 20 ms, and oversampling at
  three qualities (ECO, STD, HQ).
- **Presets:** a factory bank per Mode, user presets with categories, import and export.
- **Undo, redo and A/B:** the editor's own history (host automation is never touched) and two sounds to compare,
  each with its preset, kept in the session.
- **An output trim** after the mix, and **typed values**: click or Tab to any control and type a number.
- **Settings and diagnostics:** audio settings with their costs in latency, defaults for new instances, an animation
  speed (down to none), and a DSP load and host report you can copy.
- **Zoom** from 100 to 175 %, graphite and paper themes, full keyboard control and screen-reader support.
- **Real-time safe:** no allocation, lock or system call on the audio thread, and the same output at any block size.

## Building

Requirements: macOS 14 or later on Apple Silicon, Xcode (Apple clang with C++20), CMake 3.30 or later, Ninja, git and
Python 3. JUCE, bgfx and FunkGui are fetched at pinned versions during configure.

```sh
git clone https://github.com/Snipet/FCompressor.git
cd FCompressor
cmake --preset owner && cmake --build --preset owner   # Release; installs into ~/Library/Audio/Plug-Ins
```

`Scripts/deps.sh` optionally caches JUCE, bgfx and a prebuilt shader compiler in `~/audio/.deps`, so new build
directories skip the downloads and the shader-compiler build.

### Linux

Linux builds the VST3 and the Standalone (there is no AU) on x86-64 with Clang, from the same presets. JUCE's windows
are X11 windows, so on a Wayland desktop the editor runs through XWayland, as every JUCE plugin and the Linux hosts that
embed them do. The editor draws with Vulkan; without a Vulkan driver it shows a "GPU renderer unavailable" screen while
the audio keeps working.

```sh
# Debian / Ubuntu (24.04 or later); other distributions have the same packages under their own names.
# CMake 3.30 or later is needed: Ubuntu 24.04's own is 3.28, so there take it from Kitware's apt repository,
# `pipx install cmake` or `snap install cmake --classic` (Ubuntu 24.10 and Debian 13 can `apt install cmake`).
sudo apt install clang llvm ninja-build pkg-config git python3 \
  libasound2-dev libfreetype-dev libfontconfig1-dev libglib2.0-dev libsqlite3-dev \
  libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxcursor-dev libxrender-dev libxcomposite-dev \
  libgl-dev libegl-dev
cmake --preset owner && cmake --build --preset owner   # Release; installs the VST3 into ~/.vst3
```

The Standalone is `build/FCompressor_artefacts/Release/Standalone/FCompressor`. Clang is the default compiler on Linux
(set `CC`/`CXX` to choose another Clang); preferences and presets live in `~/.config/FCompressor/`.

## Web demo

The DSP and the editor also build for a browser: two WebAssembly modules and a static page, without JUCE. The engine
runs in an AudioWorklet with the plugin's arithmetic (the test suite holds it to the plugin's own results); the editor
is the plugin's panel, drawn with WebGL2. It is a demonstration, not a plugin format. CI publishes it from `main` to
[snipet.github.io/FCompressor](https://snipet.github.io/FCompressor/), once the jobs that gate it have passed.

Press START. The page plays a loop it synthesises, or an audio file you open or drop on it; the file never leaves
your browser. It needs a desktop browser with WebAssembly, AudioWorklet and WebGL2, and an HTTPS or localhost address.
Its tests run in headless Chrome, on macOS (with the GPU, and with a software renderer) and on x86-64 Linux in CI; CI
also runs its self-test and checks in Firefox and Safari (both pass) and reports them without failing on them. It has
not been tried by hand in Firefox or Safari yet.

What differs from the plugin (the page lists the same):
- No preset import or export; your own presets last until the page is closed.
- No side-chain key input.
- The DSP load in the settings shows a dash.
- With no input the engine idles and the meters stop.
- A QUALITY or LOOKAHEAD change rebuilds the engine on the audio thread (the plugin uses another thread) and may click.
- Where the system reverses the scroll direction, the wheel turns a value the other way; a notched wheel moves a
  stepped control about two steps.
- Typed values take plain keys only: no input method, no dead keys.
- No host parameter menu on a right-click.
- Desktop browsers with WebGL2 only, and nothing for a screen reader in the editor.
- On the CHARACTERISTICS screen the attack and release curves follow a control once it rests, not while it moves.

To build and serve it yourself you need Emscripten 6.0.3 with `em-config` on the `PATH` (from emsdk:
`emsdk install 6.0.3 && emsdk activate 6.0.3`; the configure refuses any other version), node 22 or later, CMake 3.30
or later, Ninja and git. JUCE and bgfx are not needed; FunkGui is fetched at its pinned tag.

```sh
cmake --preset web && cmake --build --preset web    # the site is build-web/site
Scripts/web-live.sh --serve build-web                # serves it on 127.0.0.1 and prints the address; Ctrl-C stops it
```

Any static server works as well, on `127.0.0.1` or `localhost`, for example
`python3 -m http.server 8000 --bind 127.0.0.1 --directory build-web/site`. An `http` address of another machine on
your network is not a secure context: there the browser gives the page no AudioWorklet, and the page says so.
`wrangler.jsonc` lets Cloudflare build and serve the same site from an import of this repository; the build command
there is `bash Scripts/cloudflare-build.sh`. `docs/DECISIONS.md` ADR-93 has the design, the measurements and the reasons.

## Testing

Every probe is a CTest test, and `Scripts/verify.sh` classifies the results against the blessed goldens:

```sh
cmake --workflow --preset dsp-verify                  # the DSP library alone, no JUCE
cmake --workflow --preset agent-verify                # the headless plugin and every probe
Scripts/verify.sh build-agent                         # the pass/fail gate
cmake --workflow --preset web-verify                  # the web demo: its tests, and every UI probe under node
Scripts/verify.sh --strict build-web                  # its gate
Scripts/web-live.sh build-web                         # the browser gate: the built site in headless Chrome
```

CI runs the DSP library and the shipping configuration (GPU editor, Release, LTO) through the same gate on every push
to `main` and every pull request, on Apple Silicon and on x86-64 Linux, against the same goldens. Another job builds
the web demo on x86-64 Linux and runs its gate and the browser gate, on the build and again on the site downloaded
from the run's artifact; Firefox and Safari are run and reported, not gated. After a push to `main` on which every
gating job has passed, CI publishes that tested site to GitHub Pages and checks the published page.

## Repository map

```
Source/fcdsp/      the JUCE-free DSP library: core, parameters, engine, telemetry, analysis, one directory per Mode
Source/plugin/     the JUCE processor, state and presets (portable/: the model code, without JUCE)
Source/editor/     the panel and its views, without JUCE (+ gpu/ for the bgfx editor)
Source/web/        the web demo: engine/ (the DSP behind a C ABI), facade/ (the editor's processor), ui/ (its main)
web/               the web demo's page, its AudioWorklet script, its test-only pages and its node tests
Tools/probes/      the test probes, one self-registering file each; Tools/web/ the web demo's checks
tests/golden/      the blessed golden results; tests/fixtures/ write-once fixtures
Scripts/           deps.sh, verify.sh, validate.sh, golden.py, release.sh, gui-live.sh, web-live.sh (+ web/)
docs/              ARCHITECTURE.md, DECISIONS.md, the design appendices, the sprint records, the Mode sheets
```

The GUI library, [FunkGui](https://github.com/Snipet/FunkGui), is a separate repository consumed at a pinned tag.

## Documentation

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md): modules, data flow, Modes, UI, threading, latency, state and testing.
- [`docs/DECISIONS.md`](docs/DECISIONS.md): every design decision (ADR-nn) with its reasons.
- [`docs/modes/`](docs/modes/): one sheet per Mode.
- [`docs/design/`](docs/design/): the binding contracts for the core, the UI and the build.

## Licence

FCompressor is GPL-3.0 ([`LICENSE`](LICENSE)). It builds on [JUCE 8](https://juce.com) (fetched under its open-source
AGPLv3 licence), [bgfx](https://github.com/bkaradzic/bgfx) (BSD-2-Clause) and FunkGui (GPL-3.0). The web demo has no
JUCE and no bgfx; its modules contain parts of Emscripten's runtime, musl, libc++, libc++abi and compiler-rt, and the
published site carries their licences beside the GPL and the typeface's (JetBrains Mono, SIL Open Font License 1.1).

The Mode names describe circuit families. Product names mentioned in the documentation are trademarks of their owners;
FCompressor is not affiliated with or endorsed by them.
