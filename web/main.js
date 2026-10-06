// web/main.js: the page of the browser demo (ADR-93, web Sprint D). An ES module, and the page's only script file
// besides the editor module's glue (fcmp-ui.js), which this file adds once its half of the seam is in place
// (docs/sprints/web-d.md, "The seam between the module and the page").
//
// What the page does:
//   before START   says what the browser lacks, if anything (no WebAssembly, a file: address, an insecure context, no
//                  AudioWorklet, no WebGL2), and otherwise loads the editor, which draws behind the overlay.
//   START          creates the AudioContext inside the click (48 kHz asked for; the rate it gets is the rate used),
//                  loads fcmp-worklet.js, the engine's bytes and the sample loop, hands the worklet's port to the
//                  editor module and plays the sample loop through a gain node into the engine.
//   the loops      the SAMPLE LOOP is a file made for the demo (audio/loop.wav). sample.js reads it and fits it to the
//                  context's rate as one exact period, so it plays at unity with no fade: the page never changes the
//                  file's level. The SYNTH LOOP is the one the page makes (loop.js). Each has its button. A sample
//                  loop that does not load never fails START: the synth loop plays, the page says so, and a press on
//                  SAMPLE LOOP tries again, from the server.
//   a file         chosen or dropped: decoded by the browser at the context's rate, faded at both ends and looped in
//                  place of the source. A file that is too large, too short, too long or not audio leaves the source
//                  as it was and says so. A file dropped while START is loading is kept, and plays once the demo runs.
//   a new source   takes the place of the old one: the old one fades out in 30 ms, then the new one fades in. A
//                  second choice inside those 30 ms replaces the first, which never plays. One case differs: the
//                  first use of the synth loop makes it inside the press (some 50 ms), so a source chosen just
//                  before it plays for that time, faded in and out.
//   RESUME         whenever the context is not running (no gesture yet, or the browser paused it).
//   a failure      of the engine or of the editor, whenever it comes, ends the demo and says why: THE DEMO COULD NOT
//                  START before it runs, THE DEMO STOPPED afterwards. The editor fails in three ways and only abort()
//                  tells Module.onAbort: an exception or a trap in its main() or in a frame is an uncaught error. A
//                  demo that has ended gives up a load of the sample loop that still runs, and its notice is empty:
//                  nothing it said of the source is true any more.
//   the status     what the overlay says (#fcmp-status) is a live region, so it is never display: none and never
//                  holds an old word: while the demo plays the overlay is away and the line says PLAYING.
//   the footer     the commit the site was built from (built-from.txt), linked only when the build was that commit.
// Every URL here is relative, so the site works from any path, and nothing is loaded from another origin
// (web/tests/size.mjs fails otherwise). What the page says is upper case, as the editor speaks.
//
// ?selftest=1, and the path fcmp-ui.html (which FunkGui's page runner opens): the checks of runSelftest() below.
// document.title is RUNNING, then PASS or "FAIL: <the first failing row>", and the rows are in #funkgui-log. An
// uncaught error or an unhandled rejection is a FAIL at once, and no PASS replaces it. When this file never runs
// (it, loop.js or sample.js is missing or does not parse), index.html says so itself, until boot() sets fcmpBooted.
//
// Of the seam the page uses Module.fcmpPort.connect and disconnect, Module.fcmpStatus and Module.fcmpSelftest; and,
// in the self-test only, Module.fcmpA11y's fullRate where the module has it (the web lead phase's contract). It
// never pulls (the module does, once a frame), and it calls neither Module.fcmpResetEngine (a new source runs into
// the engine as the next song would into the plugin) nor Module.fcmpShutdown (the module ends itself when the page
// goes).
//
// The pieces with no browser in them are exported, and web/tests/page.mjs runs them under node; the page itself
// starts only where there is a document. For a driver (a DevTools session), globalThis.fcmpPage answers state(),
// context(), node(), source() (what plays: null, or { kind, name, frames, sampleRate } of the buffer, the kind
// 'sample', 'synth' or 'file') and stats(), the worklet's counters.
import { synthLoop } from './loop.js';
import { SAMPLE_URL, fitLoop, readWav } from './sample.js';

export const SELF_CHECK_HASH = '5a96ce217d29ca6f';    // Tools/web/enginecheck.cpp kSelfCheckHash
export const ATLAS_HASH = 'b744c79b9bb755d0';         // tests/golden/base/global/ui.font.txt font.atlas.hash
export const QUANTUM = 128;                           // fcmp-worklet.js QUANTUM: the engine's block
export const MAX_FILE_BYTES = 32 * 1024 * 1024;
export const MIN_FILE_SECONDS = 0.1;
export const MAX_FILE_SECONDS = 600;
// The sample loop's file, web/audio/loop.wav, as docs/sprints/web-loop.md gives it: the self-test holds what the page
// fetched to these, and web/tests/page.mjs holds these to the file.
export const SAMPLE_SHA256 = '0327dec3cbc7de82cf3ed6d9f0533d7035681c20297b0c9cb7aae2bc9a8b7e52';
export const SAMPLE_RATE = 44100;
export const SAMPLE_FRAMES = 341420;
// The sample loop has this long from the moment its fetch began. Then the fetch is given up and the synth loop
// plays. Under the 20 s the self-test gives START, on purpose.
export const SAMPLE_MS = 15000;
// What the self-test knows of Source/web/engine/WebProtocol.h: an Attach record, and three flags of a reply.
export const PROTOCOL = { magic: 0x50574346, version: 1, attach: 2, attachBytes: 20,
                          replyGated: 1 << 3, replyConfigured: 1 << 4, replyAttached: 1 << 5 };
const FADE_SECONDS = 0.005;                           // both ends of a file: its seam when it loops
export const SWAP_SECONDS = 0.03;                     // the old source out, then the new one in
const EDITOR_MS = 60000;                              // START waits this long for the editor: 1.4 MB on a slow line
const REPOSITORY = 'https://github.com/Snipet/FCompressor';   // a link to follow, never a load

// ---- what the page says ---------------------------------------------------------------------------------------------
export const SAY = {
  start: 'START',
  resume: 'RESUME',
  idle: 'A LOOP PLAYS THROUGH THE COMPRESSOR. SOUND STARTS WHEN YOU PRESS START.',
  loading: 'LOADING',
  playing: 'PLAYING',
  paused: 'AUDIO IS PAUSED BY THE BROWSER. PRESS RESUME.',
  noWebAssembly: 'THIS BROWSER HAS NO WEBASSEMBLY, WHICH THE DEMO IS MADE OF. TRY A CURRENT DESKTOP BROWSER.',
  insecure: 'THE DEMO NEEDS HTTPS (OR LOCALHOST): ON THIS ADDRESS THE BROWSER GIVES A PAGE NO AUDIO WORKLET.',
  noWorklet: 'THIS BROWSER HAS NO AUDIO WORKLET, WHICH THE COMPRESSOR RUNS IN. TRY A CURRENT DESKTOP BROWSER.',
  noWebgl2: 'THIS BROWSER GIVES THE PAGE NO WEBGL2, WHICH THE EDITOR IS DRAWN WITH. TRY A CURRENT DESKTOP BROWSER WITH '
          + 'HARDWARE ACCELERATION ON.',
  sample: 'SAMPLE LOOP',
  synth: 'SYNTH LOOP',
  source: (name) => `SOURCE: ${name}`,
  sampleLoading: 'LOADING THE SAMPLE LOOP.',
  sampleLost: 'THE SAMPLE LOOP DID NOT LOAD. THE SYNTH LOOP PLAYS INSTEAD.',
  sampleUnchanged: 'THE SAMPLE LOOP DID NOT LOAD. THE SOURCE IS UNCHANGED.',
  startFirst: 'PRESS START FIRST, THEN CHOOSE A FILE.',
  tooLarge: (name) => `${name} IS LARGER THAN 32 MB. THE SOURCE IS UNCHANGED.`,
  tooShort: (name) => `${name} IS SHORTER THAN 0.1 S. THE SOURCE IS UNCHANGED.`,
  tooLong: (name) => `${name} IS LONGER THAN 10 MINUTES. THE SOURCE IS UNCHANGED.`,
  notAudio: (name) => `THIS BROWSER COULD NOT READ ${name} AS AUDIO (WAV, MP3, FLAC AND M4A USUALLY WORK). THE SOURCE `
                    + 'IS UNCHANGED.',
  failed: (why) => `THE DEMO COULD NOT START: ${why}`,
  stopped: (why) => `THE DEMO STOPPED: ${why} RELOAD THE PAGE TO START IT AGAIN.`,
  built: (commit, day) => ` BUILT FROM COMMIT ${commit} ON ${day}.`,
  builtDirty: (commit, day) => ` BUILT FROM COMMIT ${commit} PLUS LOCAL CHANGES ON ${day}.`,
};

// What the browser lacks, as the key of what to say ('' when nothing; 'file' is said by index.html itself, where a
// module script never runs). The order is the order a reader can act on: a file: address is a secure context, and an
// insecure one has no worklet whatever the browser.
export function missing(has) {
  if (!has.webAssembly) return 'noWebAssembly';
  if (has.protocol === 'file:') return 'file';
  if (!has.secure) return 'insecure';
  if (!has.worklet) return 'noWorklet';
  if (!has.webgl2) return 'noWebgl2';
  return '';
}

// The editor's status once it is ready (Module.fcmpStatus(), parsed), as what the page does about it: '' (go on: it
// draws, or its context is lost for the moment, which is `ok` 0 with no error: the sink restores it by itself),
// 'noWebgl2' (the canvas was given no context: FunkGui's WebGlSink says "the browser gave no WebGL2 context for
// ...", and SAY.noWebgl2 is true), or the sink's own reason (a shader that did not compile, a program that did not
// link, ...), which the page says as it is: the browser has WebGL2 then, and the reason is what diagnoses it.
export function statusFault(status) {
  const error = String(status.error || '');
  if (status.ok || error === '') return '';
  return /no WebGL2 context/.test(error) ? 'noWebgl2' : error;
}

// An uncaught error or an unhandled rejection as the editor's failure: its reason, or '' when it is not the editor's.
// `what` is an ErrorEvent, or { error: a rejection's reason }. Until Module.fcmpReady has run, anything uncaught is
// the editor's: the glue leaves a main() that threw or trapped by throwing again, and the page's own promises are all
// caught. Afterwards only a wasm trap is, and what was thrown in the editor's two files (a frame, an EM_JS body).
export function editorFault(ready, what) {
  const error = what.error;
  const reason = error && error.message ? `${error.name || 'Error'}: ${error.message}`
                                        : String(what.message || error || 'no reason given');
  if (!ready) return reason;
  const trap = typeof WebAssembly === 'object' && error instanceof WebAssembly.RuntimeError;
  const from = `${what.filename || ''} ${(error && error.stack) || ''}`;
  return trap || /\bfcmp-ui\.(?:js|wasm)\b/.test(from) ? reason : '';
}

// A file the page will not play, as the key of what to say ('' when it may be decoded, or played).
export function fileRefusal(bytes) {
  return bytes > MAX_FILE_BYTES ? 'tooLarge' : '';
}
export function lengthRefusal(seconds) {
  if (!(seconds >= MIN_FILE_SECONDS)) return 'tooShort';
  return seconds > MAX_FILE_SECONDS ? 'tooLong' : '';
}

// What a chosen or dropped file does, by the page's state: 'play' while the demo runs; 'keep' while START is loading
// (the file is kept, and plays once the demo runs); 'startFirst' before START (the page says SAY.startFirst); and ''
// in any other state (the demo cannot run here, or has ended: the overlay says so).
export function fileChoice(state) {
  if (state === 'running') return 'play';
  if (state === 'loading') return 'keep';
  return state === 'idle' ? 'startFirst' : '';
}

// 5 ms linear fades at both ends of one channel, in place: a file loops, and its ends rarely meet.
export function fadeEnds(samples, sampleRate) {
  const n = Math.min(Math.round(FADE_SECONDS * sampleRate), samples.length >> 1);
  for (let i = 0; i < n; i += 1) {
    samples[i] *= i / n;
    samples[samples.length - 1 - i] *= i / n;
  }
}

// How a new source takes the place of the one before it: { at, old }. `now` is the context's time, and `started` the
// time the source before it starts at (null when there is none). `at` is when the new source starts, and `old` what
// becomes of the other one:
//   'none'   there is none: the new source starts at once, with no fade.
//   'fade'   it plays: it fades out over SWAP_SECONDS, and the new source starts then and fades in.
//   'drop'   it has not begun (a second choice within SWAP_SECONDS of the first): it is stopped at once and never
//            plays, and the new source takes its start time, when the source before both has faded out. Faded out
//            like one that plays, it would begin part of the way down that fade, at up to full level.
export function swapPlan(now, started) {
  if (started === null) return { at: now, old: 'none' };
  if (started > now) return { at: started, old: 'drop' };
  return { at: now + SWAP_SECONDS, old: 'fade' };
}

// Which of the three source buttons can be pressed: { loop, synth, open }, true where enabled. `running` is whether
// the demo runs, `kind` what plays ('sample', 'synth' or 'file'), `sampleLoaded` whether the sample loop is loaded.
// None before START, and none once the demo has failed or stopped. While it runs a file can always be opened, and a
// loop's button is enabled unless that loop is what plays. SAMPLE LOOP is enabled whenever the sample loop is not
// loaded: a press tries again.
export function sourceButtons(running, kind, sampleLoaded) {
  if (!running) return { loop: false, synth: false, open: false };
  return { loop: kind !== 'sample' || !sampleLoaded, synth: kind !== 'synth', open: true };
}

// The self-test's rule for the sample loop fitted to another rate (its row sample.fit). `file` is what readWav
// returns and `fitted` what fitLoop makes of it. The frames are Math.round(frames * rate / the file's rate); each
// side's RMS is within FIT.level dB of the file's (the page plays the loop at unity); and each side's last
// millisecond is under FIT.end dBFS RMS (the file ends in near silence, so the loop's seam needs no fade).
// { ok, frames (wanted), level (dB from the file's, left and right), end (dBFS, left and right) }. A loop with no
// frame gives no number, and no pass.
export const FIT = { level: 0.01, end: -60 };
export function fitRule(file, fitted) {
  const rms = (samples, from = 0) => {
    let squares = 0;
    for (let i = from; i < samples.length; i += 1) squares += samples[i] * samples[i];
    return Math.sqrt(squares / (samples.length - from));
  };
  const db = (ratio) => 20 * Math.log10(ratio);
  const frames = Math.round(file.frames * fitted.sampleRate / file.sampleRate);
  const level = [db(rms(fitted.left) / rms(file.left)), db(rms(fitted.right) / rms(file.right))];
  const last = Math.max(0, frames - Math.round(fitted.sampleRate / 1000));
  const end = [db(rms(fitted.left, last)), db(rms(fitted.right, last))];
  const whole = fitted.frames === frames && fitted.left.length === frames && fitted.right.length === frames;
  return { ok: whole && level.every((d) => Math.abs(d) <= FIT.level) && end.every((d) => d < FIT.end),
           frames, level, end };
}

// built-from.txt's line, "site <sha|none> <clean|dirty> <UTC time>" (cmake/FcmpBuiltFrom.cmake), as what the footer
// says. A link only for `clean`: `none` has no commit, and `dirty` is not the commit it names (the tree had
// uncommitted changes, or the FunkGui the modules were linked from was not the pinned one).
export function builtFrom(line) {
  const m = /^site ([0-9a-f]{40}) (clean|dirty) (\d{4}-\d{2}-\d{2})T\d{2}:\d{2}:\d{2}Z\s*$/.exec(line);
  if (!m) return null;
  const commit = m[1].slice(0, 12).toUpperCase();
  return m[2] === 'clean' ? { text: SAY.built(commit, m[3]), commit, href: `${REPOSITORY}/tree/${m[1]}` }
                          : { text: SAY.builtDirty(commit, m[3]), commit, href: '' };
}

// The self-test's pixel rule, by the class of the WebGL renderer. A GPU's frame is SoftRaster's within 2 of 255 in
// every sample. A software renderer filters the atlas more coarsely (SwiftShader: up to 8 on a glyph's edge) and gets
// the bound FunkGui holds its own sink's page to (its test/web/page.cpp: kWorst, kOverPerMille): no sample over 16, and
// at most 10 per mille over 2. Software is what the renderer's name says it is: any other name, and none, is a GPU.
// `pixels` is Module.fcmpSelftest()'s {frames, largest, over2, samples}; where no frame was drawn and read there is
// nothing to judge, and no pass.
export const SOFTWARE_PIXELS = { worst: 16, perMille: 10 };
export function softwareRenderer(name) {
  return /swiftshader|llvmpipe|softpipe|software|basic render/i.test(String(name));
}
export function pixelRule(pixels, renderer) {
  const software = softwareRenderer(renderer);
  const share = pixels.samples > 0 ? 1000 * pixels.over2 / pixels.samples : Infinity;      // per mille over 2
  const read = pixels.frames >= 1 && pixels.samples > 0;
  const within = software ? pixels.largest <= SOFTWARE_PIXELS.worst && share <= SOFTWARE_PIXELS.perMille
                          : pixels.over2 === 0;
  return { ok: read && within, read, software, share,
           rule: software ? `software: at most ${SOFTWARE_PIXELS.worst}, and ${SOFTWARE_PIXELS.perMille} per mille `
                            + 'over 2'
                          : 'a GPU: none over 2' };
}

// The Panel at rest, for the self-test's still frame: it no longer asks for the full frame rate (Module.fcmpA11y's
// fullRate 0: what Scripts/web/scenario calls a still frame). Before START no reply has come, so no trace, dot or
// HISTORY moves; but the IN and OUT meters fall from the facade's first frame for some 2.5 s after the editor is
// ready, and the first-use hint holds the full rate for 6 s. Asked every REST.step ms, for at most REST.bound ms. A
// module that does not say (no fcmpA11y, or no fullRate in it) is given a quiet time of REST.quiet ms instead, longer
// than those: then the rest is assumed, not seen, and `how` says so. wait(ms) sleeps; now() is in ms.
// {still, ms, how, fullRate}: how is 'fullRate' (and fullRate is the last answer) or 'quiet'.
export const REST = { bound: 20000, step: 50, quiet: 7000 };
export async function untilRest(module, wait, now) {
  const t0 = now();
  const fullRate = () => (typeof module.fcmpA11y === 'function' ? JSON.parse(module.fcmpA11y()).fullRate : undefined);
  let rate = fullRate();
  if (typeof rate !== 'number') {
    await wait(REST.quiet);
    return { still: true, ms: Math.round(now() - t0), how: 'quiet' };
  }
  for (;;) {
    const ms = Math.round(now() - t0);
    if (rate === 0 || ms >= REST.bound) return { still: rate === 0, ms, how: 'fullRate', fullRate: rate };
    await wait(REST.step);
    rate = fullRate();
  }
}

// The self-test's verdict. show(title, line) is told the title after every change and each log line once. A failing
// row is named by the title when the run finishes (the rows after it are still logged); an uncaught error fails the
// run at once. Once there is a failure the title is never PASS again.
export class Verdict {
  constructor(test, show) {
    this.test = test;
    this.show = show;
    this.failure = '';
    this.finished = false;
    this.urgent = false;
    this.show(this.title(), '');
  }

  title() {
    if (this.failure !== '' && (this.finished || this.urgent)) return `FAIL: ${this.failure}`;
    return this.finished ? 'PASS' : 'RUNNING';
  }

  row(ok, name, detail = '') {
    if (!ok && this.failure === '') this.failure = name;
    this.show(this.title(), `${ok ? 'PASS' : 'FAIL'}     ${this.test} ${name}${detail ? ': ' + detail : ''}`);
    return ok;
  }

  note(text) {
    this.show(this.title(), `NOTE     ${text}`);
  }

  uncaught(what) {
    this.urgent = true;
    this.row(false, 'uncaught', what);
  }

  finish() {
    this.finished = true;
    this.show(this.title(), '');
  }
}

// ---- the page -------------------------------------------------------------------------------------------------------
function boot() {
  globalThis.fcmpBooted = true;                       // index.html's "the page did not load" stands down
  const $ = (id) => document.getElementById(id);
  const selftest = new URLSearchParams(location.search).get('selftest') === '1'
                || location.pathname.split('/').pop() === 'fcmp-ui.html';
  const sleep = (ms) => new Promise((done) => setTimeout(done, ms));
  const upper = (error) => String((error && error.message) || error).toUpperCase();
  const within = (ms, what, promise) => {
    let timer = 0;
    const late = new Promise((_, reject) => {
      timer = setTimeout(() => reject(new Error(`${what}: no answer in ${ms} ms`)), ms);
    });
    return Promise.race([promise, late]).finally(() => clearTimeout(timer));
  };
  class Refusal extends Error {}                      // its message is shown as it is

  // The self-test's verdict exists before anything else can go wrong: whatever is thrown from here on is its FAIL.
  let verdict = null;
  if (selftest) {
    const log = $('funkgui-log');
    log.hidden = false;
    verdict = new Verdict('web.selftest', (title, line) => {
      if (line) log.textContent += `${line}\n`;                 // the line first: a runner reads the log at the verdict
      document.title = title;
    });
    window.addEventListener('error', (event) => verdict.uncaught(`error: ${event.message}`));
    window.addEventListener('unhandledrejection', (event) => verdict.uncaught(`rejection: ${event.reason}`));
  }

  let state = 'idle';                                 // idle, refused, loading, running, failed, stopped
  let context = null;
  let node = null;
  let source = null;
  let gain = null;
  let startsAt = 0;                                   // the context's time at which `source` starts
  let playing = null;                                 // what plays: fcmpPage.source() answers it
  let waiting = null;                                 // a file dropped while START was loading: it plays once it runs
  let sampleBuffer = null;                            // the sample loop at the context's rate, once it is loaded
  let sampleLoad = null;                              // the load of it that is running, if one is
  let sampleAbort = null;                             // what gives that load up: its fetch's AbortController
  let sampleFailed = false;                           // a load of it has failed: the next one asks the server anew
  let synthBuffer = null;                             // the synth loop, made when it is first needed
  let engine = null;                                  // the bytes of fcmp-engine.wasm, fetched once
  let opening = 0;                                    // the latest source asked for: an older, slower one is dropped
  let fault = null;                                   // what went wrong while START was loading: start() ends on it

  // The status line is a live region: a change is announced only when the region is in the accessibility tree before
  // its text changes. So the overlay is never hidden: while the demo plays it is `away` (demo.css: out of sight, and
  // still rendered), and the line says so. The same words are not written twice (that could be announced twice).
  const say = (text) => {
    if ($('fcmp-status').textContent !== text) $('fcmp-status').textContent = text;
  };
  const overlay = (text, button = '') => {
    $('fcmp-overlay').classList.remove('away');
    say(text);
    $('fcmp-start').textContent = button;
    $('fcmp-start').hidden = button === '';
  };
  const overlayAway = (text) => {
    say(text);
    $('fcmp-start').hidden = true;
    $('fcmp-overlay').classList.add('away');
  };
  const notice = (text) => {
    $('fcmp-notice').textContent = text;
  };

  // ---- what the browser must have -----------------------------------------------------------------------------------
  // WebGL2 is asked of a canvas of its own: a context taken on the editor's canvas would be that canvas's one context.
  const webgl2 = () => {
    const gl = document.createElement('canvas').getContext('webgl2');
    const lose = gl && gl.getExtension('WEBGL_lose_context');
    if (lose) lose.loseContext();
    return !!gl;
  };
  const lack = missing({
    webAssembly: typeof WebAssembly === 'object' && typeof WebAssembly.instantiate === 'function',
    protocol: location.protocol,
    secure: globalThis.isSecureContext === true,
    worklet: typeof AudioContext === 'function' && 'audioWorklet' in AudioContext.prototype
          && typeof AudioWorkletNode === 'function',
    webgl2: webgl2(),
  });
  const lackText = lack === '' ? '' : lack === 'file' ? $('fcmp-status').dataset.file : SAY[lack];
  // The WebGL renderer's name, which the self-test's pixel rule goes by: asked of a canvas of its own as well, and
  // only when the self-test runs. '' when the browser gives no context.
  const rendererName = () => {
    const gl = document.createElement('canvas').getContext('webgl2');
    if (!gl) return '';
    const info = gl.getExtension('WEBGL_debug_renderer_info');
    const name = String(gl.getParameter(info ? info.UNMASKED_RENDERER_WEBGL : gl.RENDERER) || '');
    const lose = gl.getExtension('WEBGL_lose_context');
    if (lose) lose.loseContext();
    return name;
  };

  // ---- the editor module --------------------------------------------------------------------------------------------
  // The page's half of the seam: Module.fcmpReady and Module.onAbort exist before fcmp-ui.js runs. That file is a
  // classic script reading the global Module, so the object goes on globalThis first and the script is added after.
  let editorReady = false;                            // Module.fcmpReady has run
  let editorGone = null;                              // why the editor is no more
  let editorLost = null;
  const editor = new Promise((ready, lost) => {
    // The editor is no more. Said once (abort() calls onAbort and then throws, which is an uncaught error as well).
    // While START is loading, start() ends on it after its next await: the longest one, for the sample loop, is
    // given up.
    editorLost = (error) => {
      if (editorGone !== null) return;
      editorGone = error;
      lost(error);
      if (state === 'running') stop(error);
      else if (state === 'idle') fail(error);
      else if (state === 'loading') giveUpSample();
    };
    globalThis.Module = {
      fcmpReady: () => {
        editorReady = true;
        ready();
      },
      onAbort: (what) => editorLost(new Error(`the editor stopped (${what || 'no reason given'})`)),
    };
  }).then(() => {
    // The probe above said WebGL2 exists; the editor's own canvas may still have been refused a context, and the
    // sink has other reasons not to draw.
    const reason = statusFault(JSON.parse(Module.fcmpStatus()));
    if (reason !== '') throw reason === 'noWebgl2' ? new Refusal(SAY.noWebgl2) : new Error(reason);
    $('fcmp-stage').classList.remove('waiting');
  });
  editor.catch((error) => {
    if (state === 'idle') fail(error);
  });
  const loadEditor = () => {
    // Only abort() reaches Module.onAbort: an exception or a trap in the editor's main() or in one of its frames is
    // an uncaught error or an unhandled rejection (editorFault says which are the editor's). Beside the self-test's
    // own listeners, which make any of them its FAIL.
    const uncaught = (what) => {
      const reason = editorFault(editorReady, what);
      if (reason !== '') editorLost(new Error(`the editor ${editorReady ? 'stopped' : 'did not start'} (${reason})`));
    };
    window.addEventListener('error', uncaught);
    window.addEventListener('unhandledrejection', (event) => uncaught({ error: event.reason }));
    const script = document.createElement('script');
    script.src = 'fcmp-ui.js';
    script.onerror = () => Module.onAbort('fcmp-ui.js did not load');
    document.body.appendChild(script);
  };

  // ---- the worklet --------------------------------------------------------------------------------------------------
  const engineBytes = async () => {
    if (engine === null) {
      const response = await fetch('fcmp-engine.wasm');
      if (!response.ok) throw new Error(`fcmp-engine.wasm: HTTP ${response.status}`);
      engine = await response.arrayBuffer();
    }
    return engine;
  };
  // The node and its processor's first message. The bytes are cloned into the processor: `engine` stays whole.
  const makeNode = (ctx, wasm) => within(10000, 'the audio processor', new Promise((made, failed) => {
    const n = new AudioWorkletNode(ctx, 'fcmp-engine', {
      numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [2], channelCount: 2, channelCountMode: 'explicit',
      channelInterpretation: 'speakers', processorOptions: { wasm } });
    n.onprocessorerror = () => failed(new Error('the audio processor stopped'));
    const hear = (event) => {
      const m = event.data;
      if (!m || (m.fcmp !== 'ready' && m.fcmp !== 'error')) return;
      n.port.removeEventListener('message', hear);
      if (m.fcmp === 'ready') made({ node: n, ready: m });
      else failed(new Error(m.error));
    };
    // addEventListener and start(), never onmessage: after Module.fcmpPort.connect that property is the module's.
    n.port.addEventListener('message', hear);
    n.port.start();
  }));
  const ask = (n, what) => within(10000, `the worklet's ${what}`, new Promise((answered) => {
    const hear = (event) => {
      if (!event.data || event.data.fcmp !== what) return;
      n.port.removeEventListener('message', hear);
      answered(event.data);
    };
    n.port.addEventListener('message', hear);
    n.port.start();
    n.port.postMessage({ fcmp: what });
  }));

  // ---- sources ------------------------------------------------------------------------------------------------------
  const showButtons = () => {
    const on = sourceButtons(state === 'running', playing === null ? '' : playing.kind, sampleBuffer !== null);
    $('fcmp-loop').disabled = !on.loop;
    $('fcmp-synth').disabled = !on.synth;
    $('fcmp-open').disabled = !on.open;
  };
  // A loop of loop.js or sample.js as a buffer of the context: exactly its frames, at the context's rate.
  const bufferOf = (loop) => {
    const buffer = context.createBuffer(2, loop.frames, context.sampleRate);
    buffer.copyToChannel(loop.left, 0);
    buffer.copyToChannel(loop.right, 1);
    return buffer;
  };
  // The sample loop: fetched, read and fitted to the context's rate (sample.js). The promise never rejects: it gives
  // the buffer, or null when the response is not ok, the fetch fails, readWav refuses the bytes, or the bytes are not
  // all here SAMPLE_MS after the fetch began (the fetch is then given up). giveUpSample() gives the fetch up before
  // that, and the load gives null at once: the demo has ended, or START is ending on a failure, and nothing is read
  // or fitted for a demo that is over. One load runs at a time, and a second asker waits for the first; a load that
  // failed is forgotten, so the next asker tries again. A load after a failed one asks the server and not the
  // browser's cache ('reload'): the cache may hold the answer that failed (a file that was cut short and came with
  // status 200, or a 404 the server lets it keep), and gives that answer again for as long as it keeps it. Seen with
  // python3 -m http.server and a cut file one hour old: Chrome 154 asked the server once and answered the two loads
  // after that from its cache, the second when the whole file was back.
  // What it costs: readWav and fitLoop run on this thread, in one task, when the last byte has come. At 48 kHz on an
  // Apple M5 the fit takes 60 to 80 ms, in Chrome 154 and under node alike, and readWav 3 ms (the self-test's NOTE
  // has this browser's time for the fit). Until it ends the editor draws no frame and the page answers no press. It
  // runs after an await, so it never stands between a click and what the click must do at once (START makes and
  // resumes its context before it asks for the loop), and the audio thread plays on.
  const loadSample = () => {
    if (sampleBuffer !== null) return Promise.resolve(sampleBuffer);
    if (sampleLoad !== null) return sampleLoad;
    const abort = new AbortController();
    const timer = setTimeout(() => abort.abort(), SAMPLE_MS);
    const load = (async () => {
      const response = await fetch(SAMPLE_URL, { signal: abort.signal, cache: sampleFailed ? 'reload' : 'default' });
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      const bytes = await response.arrayBuffer();
      if (abort.signal.aborted) throw new Error('given up');
      const file = readWav(bytes);
      sampleBuffer = bufferOf(fitLoop(file, context.sampleRate));
      return sampleBuffer;
    })().catch(() => {
      sampleFailed = true;
      return null;
    }).finally(() => {
      clearTimeout(timer);
      sampleLoad = null;
      sampleAbort = null;
    });
    sampleLoad = load;
    sampleAbort = abort;
    return load;
  };
  const giveUpSample = () => {
    if (sampleAbort !== null) sampleAbort.abort();
  };
  // The synth loop is made at its first use, not at every START: making it holds this thread for about 50 ms (at
  // 48 kHz in Chrome 154 on an Apple M5; 70 ms under node), and most visitors never hear it.
  const synth = () => {
    if (synthBuffer === null) synthBuffer = bufferOf(synthLoop(context.sampleRate));
    return synthBuffer;
  };
  // `kind` is 'sample', 'synth' or 'file'; `name` is what the page says after SOURCE. The swap is swapPlan()'s.
  const play = (buffer, kind, name) => {
    const now = context.currentTime;
    const plan = swapPlan(now, source === null ? null : startsAt);
    if (plan.old === 'fade') {
      const old = gain;
      old.gain.cancelScheduledValues(now);
      old.gain.setValueAtTime(old.gain.value, now);
      old.gain.linearRampToValueAtTime(0, plan.at);
      source.onended = () => old.disconnect();
      source.stop(plan.at);
    } else if (plan.old === 'drop') {
      source.stop();                                   // before its start time: it renders nothing
      gain.disconnect();
    }
    gain = context.createGain();
    if (plan.at > now) {
      gain.gain.setValueAtTime(0, plan.at);
      gain.gain.linearRampToValueAtTime(1, plan.at + SWAP_SECONDS);
    }
    source = context.createBufferSource();
    source.buffer = buffer;
    source.loop = true;
    source.connect(gain).connect(node);
    source.start(plan.at);
    startsAt = plan.at;
    playing = { kind, name, frames: buffer.length, sampleRate: buffer.sampleRate };
    $('fcmp-source-name').textContent = SAY.source(name);
    showButtons();
  };
  // SAMPLE LOOP was pressed. When the loop is not loaded it is loaded first, and whatever plays goes on meanwhile. A
  // later choice (SYNTH LOOP, a file, another press) wins over a load that is still running: its `opening` turn.
  const playSample = async () => {
    const turn = (opening += 1);
    notice(sampleBuffer === null ? SAY.sampleLoading : '');
    const buffer = await loadSample();
    if (turn !== opening || state !== 'running') return;
    if (buffer === null) {
      notice(SAY.sampleUnchanged);
      return;
    }
    notice('');
    play(buffer, 'sample', SAY.sample);
  };
  // A file was chosen or dropped: what it does is fileChoice()'s, by the page's state.
  const openFile = async (file) => {
    const choice = fileChoice(state);
    notice(choice === 'startFirst' ? SAY.startFirst : '');
    if (choice === 'keep') waiting = file;             // START is loading: start() opens it once the demo runs
    if (choice !== 'play') return;
    const turn = (opening += 1);
    const name = file.name.toUpperCase();
    let refusal = fileRefusal(file.size);
    let decoded = null;
    if (refusal === '') {
      try {
        decoded = await context.decodeAudioData(await file.arrayBuffer());
        refusal = lengthRefusal(decoded.duration);
      } catch (error) {
        refusal = 'notAudio';
      }
    }
    if (turn !== opening || state !== 'running') return;
    if (refusal !== '') {
      notice(SAY[refusal](name));
      return;
    }
    for (let c = 0; c < decoded.numberOfChannels; c += 1) fadeEnds(decoded.getChannelData(c), decoded.sampleRate);
    play(decoded, 'file', name);
  };

  // ---- start, pause, stop -------------------------------------------------------------------------------------------
  const start = async () => {
    if (state !== 'idle') return;
    state = 'loading';
    overlay(SAY.loading);
    notice('');
    // Before the first await: a context made and resumed inside the click may run. resume() is not awaited, because
    // without a gesture it never settles; onstatechange says when the context runs.
    try {
      context = new AudioContext({ sampleRate: 48000, latencyHint: 'interactive' });
    } catch (error) {
      context = new AudioContext({ latencyHint: 'interactive' });        // a rate this device will not do
    }
    const resumed = context.resume().catch(() => {});
    context.onstatechange = contextChanged;
    // The sample loop loads beside the worklet and the engine. Asked for here, after the context is made and resumed:
    // it needs the context's rate, and no gesture. It never rejects, so nothing of it can throw out of this start.
    const sample = loadSample();
    // Asked after every await. A failure of the engine or the editor that came meanwhile ends this start (the worklet
    // says its error once, and stop() acts only on a demo that runs, so it is remembered: `fault`). And a start that
    // was given up meanwhile (fail(), after the self-test's time limit) goes no further.
    const wanted = () => {
      if (fault !== null) throw fault;
      if (editorGone !== null) throw editorGone;
      return state === 'loading';
    };
    let connected = false;
    try {
      await context.audioWorklet.addModule('fcmp-worklet.js');
      if (!wanted()) return;
      const wasm = await engineBytes();
      if (!wanted()) return;
      const made = await makeNode(context, wasm);
      if (!wanted()) return;
      node = made.node;
      node.onprocessorerror = () => stop(new Error('the audio processor stopped'));
      node.port.addEventListener('message', (event) => {
        if (event.data && event.data.fcmp === 'error') stop(new Error(`the engine stopped (${event.data.error})`));
      });
      node.connect(context.destination);
      // The buffer, or null: at most SAMPLE_MS after the click, however slow the file is. And null at once when the
      // engine or the editor fails meanwhile (stop() and editorLost give the load up): wanted() then ends this start.
      const loaded = await sample;
      if (!wanted()) return;
      // Drawing since the page loaded, as a rule: the port is all it lacks. Bounded, for an editor that never says
      // it is ready and never fails either.
      await within(EDITOR_MS, 'the editor', editor);
      if (!wanted()) return;
      Module.fcmpPort.connect(node.port, context.sampleRate, QUANTUM);
      connected = true;
      if (loaded !== null) {
        play(loaded, 'sample', SAY.sample);
      } else {                                         // a sample loop that fails never fails START
        play(synth(), 'synth', SAY.synth);
        notice(SAY.sampleLost);
      }
      await Promise.race([resumed, sleep(250)]);       // a context that is about to run shows no RESUME on the way
      if (!wanted()) return;
      state = 'running';
      showButtons();
      contextChanged();
      if (waiting !== null) openFile(waiting);         // a file dropped while this was loading: it wins
      return made.ready;
    } finally {
      // Not running after all: an editor that lives must not go on posting to an engine nobody hears.
      if (connected && state !== 'running' && editorGone === null) Module.fcmpPort.disconnect();
    }
  };
  function contextChanged() {
    if (state !== 'running') return;
    if (context.state === 'closed') {                  // not by this page: nothing can resume it
      stop(new Error('the audio context closed'));
    } else if (context.state === 'running') {
      overlayAway(SAY.playing);
    } else {                                           // suspended (no gesture yet, or by the browser) or interrupted
      overlay(SAY.paused, SAY.resume);
      $('fcmp-start').focus();                         // the way on: by then the focus is on nothing (START is gone)
    }
  }
  const silence = () => {
    showButtons();                                     // the demo no longer runs: all three are disabled
    notice('');                                        // what it said of the source is no longer true
    giveUpSample();                                    // a load of the sample loop that still runs has no use
    if (context !== null) context.close().catch(() => {});
  };
  function fail(error) {
    if (state !== 'idle' && state !== 'loading') return;
    state = error instanceof Refusal ? 'refused' : 'failed';
    overlay(error instanceof Refusal ? error.message : SAY.failed(upper(error)));
    silence();
  }
  function stop(error) {
    if (state === 'loading' && fault === null) {       // START is loading: start() ends on it, after its next await
      fault = error;
      giveUpSample();                                  // the longest of them, which is then over at once
    }
    if (state !== 'running') return;
    state = 'stopped';
    overlay(SAY.stopped(`${upper(error)}.`));
    if (editorGone === null) Module.fcmpPort.disconnect();      // the editor lives on and drops what it would post
    silence();
  }

  // ---- the self-test ------------------------------------------------------------------------------------------------
  const hex8 = (value) => value.toString(16).padStart(8, '0');
  async function runSelftest() {
    const row = verdict.row.bind(verdict);
    const note = verdict.note.bind(verdict);
    note(navigator.userAgent);
    row(lack === '', 'browser', lack === '' ? 'WebAssembly, a secure context, AudioWorklet, WebGL2' : lackText);
    if (lack !== '' && lack !== 'noWebgl2') return;

    // 1. The engine on this thread: the self-check's hash is the native build's.
    const wasm = await engineBytes();
    const x = (await WebAssembly.instantiate(wasm, {})).instance.exports;
    x._initialize();
    const f32 = new Float32Array(x.memory.buffer);
    const u32 = new Uint32Array(x.memory.buffer);
    const scratch = x.malloc(4 * QUANTUM * 4 + 32);
    const [inL, inR, outL, outR] = [0, 1, 2, 3].map((i) => scratch + i * QUANTUM * 4);
    const rc = x.fcmp_web_selfcheck(scratch);
    const mainHash = hex8(u32[scratch / 4 + 1]) + hex8(u32[scratch / 4]);
    row(rc === 0 && mainHash === SELF_CHECK_HASH, 'engine.selfcheck.main', `${mainHash} (want ${SELF_CHECK_HASH})`);

    // 2. The shipped worklet in a real AudioWorklet. An OfflineAudioContext needs no gesture and renders exactly, and a
    //    looping source at the context's rate hands the buffer's samples over unchanged: 10 s through the worklet must
    //    equal the engine driven here with the same quanta, sample for sample.
    const seconds = 10;
    const loop = synthLoop(48000);
    const offline = new OfflineAudioContext({ numberOfChannels: 2, length: 48000 * seconds, sampleRate: 48000 });
    await offline.audioWorklet.addModule('fcmp-worklet.js');
    const made = await makeNode(offline, wasm);
    const check = await ask(made.node, 'selfcheck');                // before a source plays: the worklet's own rule
    row(check.rc === 0 && check.hash === SELF_CHECK_HASH, 'engine.selfcheck.worklet',
        `${check.hash} in ${check.ms} ms (want ${SELF_CHECK_HASH}); abi ${made.ready.abi}, `
        + `latency ${made.ready.latency}, ${made.ready.sampleRate} Hz`);
    const buffer = offline.createBuffer(2, loop.frames, 48000);
    buffer.copyToChannel(loop.left, 0);
    buffer.copyToChannel(loop.right, 1);
    const player = offline.createBufferSource();
    player.buffer = buffer;
    player.loop = true;
    player.connect(made.node).connect(offline.destination);
    player.start();
    const t0 = performance.now();
    const rendered = await within(30000, 'the offline render', offline.startRendering());
    const renderMs = performance.now() - t0;
    const gotL = rendered.getChannelData(0);
    const gotR = rendered.getChannelData(1);
    const reference = x.fcmp_web_create();
    x.fcmp_web_configure(reference, 48000, QUANTUM);
    let differ = 0;
    let peak = 0;
    for (let at = 0; at + QUANTUM <= gotL.length; at += QUANTUM) {
      const from = at % loop.frames;                              // the loop is a whole number of quanta
      f32.set(loop.left.subarray(from, from + QUANTUM), inL / 4);
      f32.set(loop.right.subarray(from, from + QUANTUM), inR / 4);
      x.fcmp_web_process(reference, inL, inR, outL, outR, QUANTUM);
      for (let i = 0; i < QUANTUM; i += 1) {
        if (!Object.is(gotL[at + i], f32[outL / 4 + i]) || !Object.is(gotR[at + i], f32[outR / 4 + i])) differ += 1;
        peak = Math.max(peak, Math.abs(gotL[at + i]), Math.abs(gotR[at + i]));
      }
    }
    x.fcmp_web_destroy(reference);
    const stats = await ask(made.node, 'stats');
    row(differ === 0 && peak > 0.05 && peak < 2 && stats.quanta === seconds * 375 && stats.oddQuanta === 0 && stats.ok,
        'worklet.render', `${seconds} s of the loop through the worklet: ${differ} of ${gotL.length} frames differ `
        + `from the engine driven directly, peak ${peak.toFixed(3)}, ${stats.quanta} quanta of ${stats.lastFrames}`);
    note(`worklet load: ${seconds} s rendered in ${renderMs.toFixed(0)} ms, `
         + `${(seconds * 1000 / renderMs).toFixed(0)}x real time (default Mode, STD)`);

    // 3. The cost of silence, as web.engine.tail judges it (Tools/web/enginecheck.cpp), through the same ABI: wasm
    //    cannot flush denormals, so with the gate off a decaying tail must not cost more than the signal did. 2 s of
    //    the loop, then 10 s of zeros, in 1/3 s windows; each window's best of three runs; the worst pair of adjacent
    //    windows against the signal, at x3.
    {
      const WINDOW = 125;
      const ACTIVE = 6;
      const SILENT = 30;
      const attach = scratch + 4 * QUANTUM * 4;
      const run = (silentWindows) => {
        const e = x.fcmp_web_create();
        x.fcmp_web_configure(e, 48000, QUANTUM);
        x.fcmp_web_set_gate(e, 0);
        u32[attach / 4] = PROTOCOL.magic;                             // Attach: the editor is listening
        u32[attach / 4 + 1] = (PROTOCOL.attach << 16) | PROTOCOL.version;
        u32[attach / 4 + 2] = PROTOCOL.attachBytes;
        u32[attach / 4 + 3] = 0;
        u32[attach / 4 + 4] = 1;
        x.fcmp_web_post(e, attach, PROTOCOL.attachBytes);
        const active = [];
        const silent = [];
        for (let w = 0; w < ACTIVE + silentWindows; w += 1) {
          if (w === ACTIVE) f32.fill(0, inL / 4, inL / 4 + 2 * QUANTUM);
          const from = performance.now();
          for (let q = 0; q < WINDOW; q += 1) {
            if (w < ACTIVE) {
              const at = (w * WINDOW + q) * QUANTUM;
              f32.set(loop.left.subarray(at, at + QUANTUM), inL / 4);
              f32.set(loop.right.subarray(at, at + QUANTUM), inR / 4);
            }
            x.fcmp_web_process(e, inL, inR, outL, outR, QUANTUM);
          }
          (w < ACTIVE ? active : silent).push((performance.now() - from) / WINDOW);
        }
        x.fcmp_web_destroy(e);
        active.sort((a, b) => a - b);
        return { active: active[(ACTIVE - 1) >> 1], silent };
      };
      run(3);                                                       // untimed: the code is compiled while this runs
      let best = null;
      for (let i = 0; i < 3; i += 1) {
        await sleep(0);
        const t = run(SILENT);
        best = best === null ? t : { active: Math.min(best.active, t.active),
                                     silent: best.silent.map((s, w) => Math.min(s, t.silent[w])) };
      }
      let worst = 0;
      let worstAt = 0;
      for (let w = 0; w + 1 < SILENT; w += 1) {
        const pair = Math.min(best.silent[w], best.silent[w + 1]);
        if (pair > worst) [worst, worstAt] = [pair, w];
      }
      row(best.active > 0 && worst < 3 * best.active, 'engine.silence',
          `signal ${(best.active * 1000).toFixed(1)} us a quantum, silence at worst ${(worst * 1000).toFixed(1)} us `
          + `over 2/3 s from ${(worstAt / 3).toFixed(1)} s (x${(worst / best.active).toFixed(2)}, limit x3; `
          + 'best of 3 runs, gate off)');
    }

    // 4. The sample loop, as START loads it: the page's own fetch of the file gives the bytes the repository holds
    //    (their SHA-256), readWav reads them, and fitLoop's loop at 48 kHz is the file's by fitRule(). worklet.render
    //    and engine.silence, above, keep the synth loop: at 48 kHz it is a whole number of quanta, and the sample loop
    //    is not.
    {
      let file = null;
      try {
        const response = await within(SAMPLE_MS, 'the sample loop', fetch(SAMPLE_URL));
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        const bytes = await within(SAMPLE_MS, 'the sample loop', response.arrayBuffer());
        const digest = new Uint8Array(await crypto.subtle.digest('SHA-256', bytes));
        const sha = Array.from(digest, (byte) => byte.toString(16).padStart(2, '0')).join('');
        file = readWav(bytes);
        row(sha === SAMPLE_SHA256 && file.sampleRate === SAMPLE_RATE && file.frames === SAMPLE_FRAMES, 'sample.read',
            `${SAMPLE_URL}: ${bytes.byteLength} bytes, SHA-256 ${sha} (want ${SAMPLE_SHA256}); ${file.sampleRate} Hz, `
            + `${file.frames} frames (want ${SAMPLE_RATE} and ${SAMPLE_FRAMES})`);
      } catch (error) {
        row(false, 'sample.read', `${SAMPLE_URL}: ${(error && error.message) || error}`);
      }
      if (file !== null) {
        const t0 = performance.now();
        const fitted = fitLoop(file, 48000);
        const fitMs = performance.now() - t0;
        const judged = fitRule(file, fitted);
        const two = (values, digits) => values.map((v) => v.toFixed(digits)).join(' and ');
        row(judged.ok && fitted.frames === 371614, 'sample.fit',
            `${fitted.frames} frames at 48000 Hz (want 371614); the RMS is ${two(judged.level, 5)} dB from `
            + `the file's (left and right; at most ${FIT.level}); the last 1 ms is at ${two(judged.end, 1)} dBFS `
            + `(under ${FIT.end})`);
        note(`sample loop: fitLoop took ${fitMs.toFixed(0)} ms on the main thread (${file.frames} frames at `
             + `${file.sampleRate} Hz to ${fitted.frames} at 48000 Hz)`);
      }
    }
    if (lack !== '') return;

    // 5. The editor's own check (the seam's Module.fcmpSelftest), before START: the atlas it draws with is the
    //    committed bake, and one frame through the WebGL2 sink, read back inside that call, is SoftRaster's by
    //    pixelRule(). A still frame, on purpose: it is asked for once the Panel is at rest (untilRest(): before START
    //    the meters' first fall and the first-use hint keep it moving for some 6 s; a Panel that does not rest within
    //    the bound fails the row; a module that cannot say gets a quiet time, and the NOTE says which), in the same
    //    task as the answer that said so, so no frame of the host's comes between. After START the engine answers,
    //    and a moving trace is a pixel off SoftRaster's in a share of the frames that depends on the state, which
    //    says nothing about the sink. An editor that is not there is page.start's failure below.
    let drawing = false;
    try {
      await within(20000, 'the editor', editor);
      drawing = true;
    } catch (error) {
      fail(error);
    }
    if (drawing && typeof Module.fcmpSelftest !== 'function') {
      row(false, 'editor.selftest', 'the module does not provide Module.fcmpSelftest');
    } else if (drawing) {
      const renderer = rendererName();
      note(`renderer: ${renderer || 'not named'} (${softwareRenderer(renderer) ? 'software' : 'judged as a GPU'})`);
      const rest = await untilRest(Module, sleep, () => performance.now());
      note(rest.how === 'fullRate'
        ? `the still frame: the Panel ${rest.still ? 'at rest' : 'NOT at rest'} after ${rest.ms} ms (Module.fcmpA11y's `
          + `fullRate ${rest.fullRate}; at most ${REST.bound} ms)`
        : `the still frame: after a quiet time of ${rest.ms} ms (the module does not say whether the Panel is at rest: `
          + 'no Module.fcmpA11y with fullRate)');
      let self = null;
      try {
        self = JSON.parse(Module.fcmpSelftest());
      } catch (error) {
        row(false, 'editor.selftest', `Module.fcmpSelftest() did not return JSON (${error})`);
      }
      if (self !== null) {
        const pixels = self.pixels || {};
        const judged = pixelRule(pixels, renderer);
        row(self.atlasHash === ATLAS_HASH, 'editor.atlas', `${self.atlasHash} (want ${ATLAS_HASH})`);
        const numbers = `${pixels.samples} samples against SoftRaster, the largest difference ${pixels.largest} of `
                        + `255, ${pixels.over2} over 2 (${judged.share.toFixed(2)} per mille; ${judged.rule})`;
        const moving = rest.still ? '' : `; the Panel was not at rest within ${rest.ms} ms`;
        const atRest = rest.how === 'quiet' ? `after a quiet time of ${rest.ms} ms` : 'the Panel at rest';
        row(judged.ok && rest.still, 'editor.pixels',
            !judged.read ? `no still frame was drawn and read back before START (frames ${pixels.frames}, samples `
                           + `${pixels.samples}${moving}): nothing to judge. A hidden document draws none`
            : !rest.still ? `the Panel was not at rest within ${rest.ms} ms: no still frame before START (what was `
                            + `read of the moving one: ${pixels.frames} frame(s), ${numbers})`
            : `${pixels.frames} still frame(s), before START, ${atRest}, read back through the sink: ${numbers}`);
      }
    }

    // 6. The page itself, and the live context (without a gesture it stays suspended, which is the RESUME state:
    //    messages are answered all the same, so the link is judged either way; the audio only when it runs).
    try {
      await within(20000, 'the start', start());
    } catch (error) {
      fail(error);
    }
    const started = state === 'running';
    if (!row(started, 'page.start', started ? `${context.sampleRate} Hz, the context is ${context.state}`
                                            : $('fcmp-status').textContent)) return;
    // What START plays is the sample loop, as one period at the context's rate, and the page says so.
    {
      const plays = globalThis.fcmpPage.source() || {};
      const frames = Math.round(SAMPLE_FRAMES * context.sampleRate / SAMPLE_RATE);
      const name = $('fcmp-source-name').textContent;
      const told = $('fcmp-notice').textContent;
      const off = (id) => $(id).disabled;
      row(plays.kind === 'sample' && plays.name === SAY.sample && plays.frames === frames
          && plays.sampleRate === context.sampleRate && name === SAY.source(SAY.sample) && told === ''
          && off('fcmp-loop') && !off('fcmp-synth'), 'page.source',
          `the ${plays.kind || 'no'} source plays: ${plays.frames} frames at ${plays.sampleRate} Hz (want `
          + `${frames} at ${context.sampleRate}); the page says "${name}", the notice is "${told}"; SAMPLE LOOP is `
          + `${off('fcmp-loop') ? 'disabled' : 'ENABLED'}, SYNTH LOOP ${off('fcmp-synth') ? 'DISABLED' : 'enabled'}`);
    }
    // What the page says of itself is true and can be heard: the status line is rendered (a live region that is
    // display: none announces nothing) and says PLAYING while the context runs; otherwise it says how to go on, and
    // the button that does has the focus.
    {
      const status = $('fcmp-status');
      const heard = status.getClientRects().length > 0 && getComputedStyle(status).visibility === 'visible';
      const runs = context.state === 'running';
      const button = $('fcmp-start');
      const focused = document.activeElement === button;
      row(heard && (runs ? status.textContent === SAY.playing && button.hidden
                         : status.textContent === SAY.paused && !button.hidden && focused),
          'page.status', `the status line says "${status.textContent}" and is ${heard ? '' : 'NOT '}rendered; `
          + (button.hidden ? 'no button' : `${button.textContent} ${focused ? 'has' : 'has NOT'} the focus`));
    }
    // Watched for 0.6 s, and for up to 3 s until both counts have moved.
    const before = JSON.parse(Module.fcmpStatus());
    let after = before;
    for (let i = 0; i < 60 && (i < 12 || !(after.frames > before.frames && after.replies > before.replies)); i += 1) {
      await sleep(50);
      after = JSON.parse(Module.fcmpStatus());
    }
    const live = await ask(node, 'stats');
    const linked = PROTOCOL.replyConfigured | PROTOCOL.replyAttached;
    note(`editor: ${JSON.stringify(after)}`);
    note(`worklet: ${JSON.stringify(live)}`);
    row(after.ok === 1 && after.frames > before.frames, 'editor.draws',
        `${after.frames - before.frames} frames drawn while watched (${after.frames} in all), ${after.fps} fps, `
        + `zoom ${after.zoom}`);
    row(after.replies > before.replies && after.refused === 0 && after.prepared === 1
        && after.rate === context.sampleRate && (after.flags & linked) === linked && live.ok === true
        && live.replies > 0 && live.refused === 0, 'editor.link',
        `${after.replies - before.replies} replies while watched (${after.replies} in all), `
        + `flags 0x${after.flags.toString(16)}, ${after.rate} Hz; the worklet took ${live.records} records, `
        + `refused ${live.refused}`);
    if (context.state === 'running') {
      await sleep(300);
      const later = await ask(node, 'stats');
      const open = (JSON.parse(Module.fcmpStatus()).flags & PROTOCOL.replyGated) === 0;
      row(later.quanta > live.quanta && later.lastFrames === QUANTUM && later.inChannels === 2
          && later.outChannels === 2 && open, 'page.audio',
          `${later.quanta - live.quanta} quanta of ${later.lastFrames} in 0.3 s, ${later.inChannels} channels in, `
          + `${later.outChannels} out, the gate is ${open ? 'open' : 'closed'}; `
          + `base latency ${(context.baseLatency * 1000).toFixed(1)} ms, `
          + `output latency ${(context.outputLatency * 1000).toFixed(1)} ms`);
    } else {
      note(`the context is ${context.state} (no gesture): the live audio is not judged, and the page shows `
           + `${$('fcmp-start').textContent}`);
    }
  }

  // ---- wiring -------------------------------------------------------------------------------------------------------
  globalThis.fcmpPage = { state: () => state, context: () => context, node: () => node,
                          source: () => (playing === null ? null : { ...playing }),
                          stats: () => ask(node, 'stats') };
  fetch('built-from.txt').then((response) => (response.ok ? response.text() : '')).then((line) => {
    const built = builtFrom(line);
    if (built === null) return;
    const [before, after] = built.text.split(built.commit);
    let commit = built.commit;
    if (built.href !== '') {
      commit = document.createElement('a');
      commit.href = built.href;
      commit.textContent = built.commit;
    }
    $('fcmp-built').replaceChildren(before, commit, after);
  }).catch(() => {});

  if (lack === '') {
    overlay(SAY.idle, SAY.start);
    loadEditor();
  } else {
    state = 'refused';
    overlay(lackText);
  }
  $('fcmp-start').addEventListener('click', () => {
    if (state === 'idle') start().catch(fail);
    else if (state === 'running') context.resume().catch(() => {});
  });
  $('fcmp-loop').addEventListener('click', () => {
    if (state === 'running') playSample();
  });
  $('fcmp-synth').addEventListener('click', () => {
    if (state !== 'running') return;
    opening += 1;
    notice('');
    play(synth(), 'synth', SAY.synth);
  });
  $('fcmp-open').addEventListener('click', () => $('fcmp-file').click());
  $('fcmp-file').addEventListener('change', (event) => {
    if (event.target.files[0]) openFile(event.target.files[0]);
    event.target.value = '';
  });
  const carriesFiles = (event) => !!event.dataTransfer && Array.from(event.dataTransfer.types).includes('Files');
  window.addEventListener('dragover', (event) => {
    if (!carriesFiles(event)) return;
    event.preventDefault();
    document.body.classList.add('dragging');
  });
  window.addEventListener('dragleave', () => document.body.classList.remove('dragging'));
  window.addEventListener('drop', (event) => {
    if (!carriesFiles(event)) return;
    event.preventDefault();
    document.body.classList.remove('dragging');
    if (event.dataTransfer.files[0]) openFile(event.dataTransfer.files[0]);
  });

  if (verdict !== null) {
    const watchdog = setTimeout(() => verdict.uncaught('no verdict within 75 s'), 75000);
    runSelftest().catch((error) => verdict.row(false, 'selftest.ran', String((error && error.stack) || error)))
      .then(() => {
        clearTimeout(watchdog);
        verdict.finish();
      });
  }
}

if (typeof document !== 'undefined') boot();
