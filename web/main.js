// web/main.js: the page of the browser demo (ADR-93, web Sprint D). An ES module, and the page's only script file
// besides the editor module's glue (fcmp-ui.js), which this file adds once its half of the seam is in place
// (docs/sprints/web-d.md, "The seam between the module and the page").
//
// What the page does:
//   before START   says what the browser lacks, if anything (no WebAssembly, a file: address, an insecure context, no
//                  AudioWorklet, no WebGL2), and otherwise loads the editor, which draws behind the overlay.
//   START          creates the AudioContext inside the click (48 kHz asked for; the rate it gets is the rate used),
//                  loads fcmp-worklet.js and the engine's bytes, hands the worklet's port to the editor module and
//                  plays the built-in loop (loop.js) through a gain node into the engine.
//   a file         chosen or dropped: decoded by the browser at the context's rate, faded at both ends and looped in
//                  place of the source. A file that is too large, too short, too long or not audio leaves the source
//                  as it was and says so.
//   RESUME         whenever the context is not running (no gesture yet, or the browser paused it).
//   the footer     the commit the site was built from (built-from.txt), linked only when the tree was clean.
// Every URL here is relative, so the site works from any path, and nothing is loaded from another origin
// (web/tests/size.mjs fails otherwise). What the page says is upper case, as the editor speaks.
//
// ?selftest=1, and the path fcmp-ui.html (which FunkGui's page runner opens): the checks of runSelftest() below.
// document.title is RUNNING, then PASS or "FAIL: <the first failing row>", and the rows are in #funkgui-log. An
// uncaught error or an unhandled rejection is a FAIL at once, and no PASS replaces it.
//
// Of the seam the page uses Module.fcmpPort.connect and disconnect, Module.fcmpStatus and Module.fcmpSelftest. It
// never pulls (the module does, once a frame), and it calls neither Module.fcmpResetEngine (a new source runs into
// the engine as the next song would into the plugin) nor Module.fcmpShutdown (the module ends itself when the page
// goes).
//
// The pieces with no browser in them are exported, and web/tests/page.mjs runs them under node; the page itself
// starts only where there is a document. For a driver (a DevTools session), globalThis.fcmpPage answers state(),
// context(), node() and stats(), the worklet's counters.
import { synthLoop } from './loop.js';

export const SELF_CHECK_HASH = '5a96ce217d29ca6f';    // Tools/web/enginecheck.cpp kSelfCheckHash
export const ATLAS_HASH = 'b744c79b9bb755d0';         // tests/golden/base/global/ui.font.txt font.atlas.hash
export const QUANTUM = 128;                           // fcmp-worklet.js QUANTUM: the engine's block
export const MAX_FILE_BYTES = 32 * 1024 * 1024;
export const MIN_FILE_SECONDS = 0.1;
export const MAX_FILE_SECONDS = 600;
// What the self-test knows of Source/web/engine/WebProtocol.h: an Attach record, and three flags of a reply.
export const PROTOCOL = { magic: 0x50574346, version: 1, attach: 2, attachBytes: 20,
                          replyGated: 1 << 3, replyConfigured: 1 << 4, replyAttached: 1 << 5 };
const FADE_SECONDS = 0.005;                           // both ends of a file: its seam when it loops
const SWAP_SECONDS = 0.03;                            // the old source out, then the new one in
const REPOSITORY = 'https://github.com/Snipet/FCompressor';   // a link to follow, never a load

// ---- what the page says ---------------------------------------------------------------------------------------------
export const SAY = {
  start: 'START',
  resume: 'RESUME',
  idle: 'A LOOP PLAYS THROUGH THE COMPRESSOR. SOUND STARTS WHEN YOU PRESS START.',
  loading: 'LOADING',
  paused: 'AUDIO IS PAUSED BY THE BROWSER. PRESS RESUME.',
  noWebAssembly: 'THIS BROWSER HAS NO WEBASSEMBLY, WHICH THE DEMO IS MADE OF. TRY A CURRENT DESKTOP BROWSER.',
  insecure: 'THE DEMO NEEDS HTTPS (OR LOCALHOST): ON THIS ADDRESS THE BROWSER GIVES A PAGE NO AUDIO WORKLET.',
  noWorklet: 'THIS BROWSER HAS NO AUDIO WORKLET, WHICH THE COMPRESSOR RUNS IN. TRY A CURRENT DESKTOP BROWSER.',
  noWebgl2: 'THIS BROWSER GIVES THE PAGE NO WEBGL2, WHICH THE EDITOR IS DRAWN WITH. TRY A CURRENT DESKTOP BROWSER WITH '
          + 'HARDWARE ACCELERATION ON.',
  loop: 'BUILT-IN LOOP',
  source: (name) => `SOURCE: ${name}`,
  startFirst: 'PRESS START FIRST, THEN CHOOSE A FILE.',
  tooLarge: (name) => `${name} IS LARGER THAN 32 MB. THE SOURCE IS UNCHANGED.`,
  tooShort: (name) => `${name} IS SHORTER THAN 0.1 S. THE SOURCE IS UNCHANGED.`,
  tooLong: (name) => `${name} IS LONGER THAN 10 MINUTES. THE SOURCE IS UNCHANGED.`,
  notAudio: (name) => `THIS BROWSER COULD NOT READ ${name} AS AUDIO (WAV, MP3, FLAC AND M4A USUALLY WORK). THE SOURCE `
                    + 'IS UNCHANGED.',
  failed: (why) => `THE DEMO COULD NOT START: ${why}`,
  stopped: (why) => `THE DEMO STOPPED: ${why} RELOAD THE PAGE TO START IT AGAIN.`,
  built: (commit, day) => ` BUILT FROM COMMIT ${commit} ON ${day}.`,
  builtDirty: (commit, day) => ` BUILT FROM COMMIT ${commit} PLUS UNCOMMITTED CHANGES ON ${day}.`,
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

// A file the page will not play, as the key of what to say ('' when it may be decoded, or played).
export function fileRefusal(bytes) {
  return bytes > MAX_FILE_BYTES ? 'tooLarge' : '';
}
export function lengthRefusal(seconds) {
  if (!(seconds >= MIN_FILE_SECONDS)) return 'tooShort';
  return seconds > MAX_FILE_SECONDS ? 'tooLong' : '';
}

// 5 ms linear fades at both ends of one channel, in place: a file loops, and its ends rarely meet.
export function fadeEnds(samples, sampleRate) {
  const n = Math.min(Math.round(FADE_SECONDS * sampleRate), samples.length >> 1);
  for (let i = 0; i < n; i += 1) {
    samples[i] *= i / n;
    samples[samples.length - 1 - i] *= i / n;
  }
}

// built-from.txt's line, "site <sha|none> <clean|dirty> <UTC time>" (cmake/FcmpBuiltFrom.cmake), as what the footer
// says. A link only for a clean tree: `none` has no commit, and a dirty tree is not the commit it names.
export function builtFrom(line) {
  const m = /^site ([0-9a-f]{40}) (clean|dirty) (\d{4}-\d{2}-\d{2})T\d{2}:\d{2}:\d{2}Z\s*$/.exec(line);
  if (!m) return null;
  const commit = m[1].slice(0, 12).toUpperCase();
  return m[2] === 'clean' ? { text: SAY.built(commit, m[3]), commit, href: `${REPOSITORY}/tree/${m[1]}` }
                          : { text: SAY.builtDirty(commit, m[3]), commit, href: '' };
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
  let loopBuffer = null;
  let engine = null;                                  // the bytes of fcmp-engine.wasm, fetched once
  let opening = 0;                                    // the latest file asked for: an older, slower one is dropped

  const overlay = (text, button = '') => {
    $('fcmp-status').textContent = text;
    $('fcmp-start').textContent = button;
    $('fcmp-start').hidden = button === '';
    $('fcmp-overlay').hidden = false;
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

  // ---- the editor module --------------------------------------------------------------------------------------------
  // The page's half of the seam: Module.fcmpReady and Module.onAbort exist before fcmp-ui.js runs. That file is a
  // classic script reading the global Module, so the object goes on globalThis first and the script is added after.
  let editorGone = null;                              // an abort while START is loading: start() ends on it
  const editor = new Promise((ready, abort) => {
    globalThis.Module = {
      fcmpReady: ready,
      onAbort: (what) => {
        editorGone = new Error(`the editor stopped (${what})`);
        abort(editorGone);
        if (state === 'running') stop(editorGone);
        else if (state === 'idle') fail(editorGone);
      },
    };
  }).then(() => {
    // The probe above said WebGL2 exists; the editor's own canvas may still have been refused a context.
    if (!JSON.parse(Module.fcmpStatus()).ok) throw new Refusal(SAY.noWebgl2);
    $('fcmp-stage').classList.remove('waiting');
  });
  editor.catch((error) => {
    if (state === 'idle') fail(error);
  });
  const loadEditor = () => {
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
  const play = (buffer, name) => {
    const now = context.currentTime;
    let at = now;
    if (source !== null) {
      const old = gain;
      old.gain.cancelScheduledValues(now);
      old.gain.setValueAtTime(old.gain.value, now);
      old.gain.linearRampToValueAtTime(0, now + SWAP_SECONDS);
      source.onended = () => old.disconnect();
      source.stop(now + SWAP_SECONDS);
      at = now + SWAP_SECONDS;
    }
    gain = context.createGain();
    if (at > now) {
      gain.gain.setValueAtTime(0, at);
      gain.gain.linearRampToValueAtTime(1, at + SWAP_SECONDS);
    }
    source = context.createBufferSource();
    source.buffer = buffer;
    source.loop = true;
    source.connect(gain).connect(node);
    source.start(at);
    $('fcmp-source-name').textContent = SAY.source(name);
    $('fcmp-loop').disabled = buffer === loopBuffer;
  };
  const openFile = async (file) => {
    notice(state === 'idle' ? SAY.startFirst : '');
    if (state !== 'running') return;
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
    play(decoded, name);
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
    await context.audioWorklet.addModule('fcmp-worklet.js');
    const made = await makeNode(context, await engineBytes());
    node = made.node;
    node.onprocessorerror = () => stop(new Error('the audio processor stopped'));
    node.port.addEventListener('message', (event) => {
      if (event.data && event.data.fcmp === 'error') stop(new Error(`the engine stopped (${event.data.error})`));
    });
    node.connect(context.destination);
    const loop = synthLoop(context.sampleRate);
    loopBuffer = context.createBuffer(2, loop.frames, context.sampleRate);
    loopBuffer.copyToChannel(loop.left, 0);
    loopBuffer.copyToChannel(loop.right, 1);
    await editor;                                      // drawing since the page loaded; the port is all it lacks
    if (editorGone !== null) throw editorGone;
    Module.fcmpPort.connect(node.port, context.sampleRate, QUANTUM);
    play(loopBuffer, SAY.loop);
    await Promise.race([resumed, sleep(250)]);         // a context that is about to run shows no RESUME on the way
    state = 'running';
    $('fcmp-open').disabled = false;
    contextChanged();
    return made.ready;
  };
  function contextChanged() {
    if (state !== 'running') return;
    if (context.state === 'running') $('fcmp-overlay').hidden = true;
    else overlay(SAY.paused, SAY.resume);              // suspended (no gesture yet, or by the browser) or interrupted
  }
  const silence = () => {
    $('fcmp-loop').disabled = true;
    $('fcmp-open').disabled = true;
    if (context !== null) context.close().catch(() => {});
  };
  function fail(error) {
    if (state !== 'idle' && state !== 'loading') return;
    state = error instanceof Refusal ? 'refused' : 'failed';
    overlay(error instanceof Refusal ? error.message : SAY.failed(upper(error)));
    silence();
  }
  function stop(error) {
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
    if (lack !== '') return;

    // 4. The page itself: the editor, and the live context (without a gesture it stays suspended, which is the RESUME
    //    state: messages are answered all the same, so the link is judged either way; the audio only when it runs).
    try {
      await within(20000, 'the start', start());
    } catch (error) {
      fail(error);
    }
    const started = state === 'running';
    if (!row(started, 'page.start', started ? `${context.sampleRate} Hz, the context is ${context.state}`
                                            : $('fcmp-status').textContent)) return;
    // The editor's own check (the seam's Module.fcmpSelftest): the atlas it draws with is the committed bake, and one
    // frame through the WebGL2 sink, read back inside that call, is SoftRaster's within 2 of 255 a channel.
    if (typeof Module.fcmpSelftest !== 'function') {
      row(false, 'editor.selftest', 'the module does not provide Module.fcmpSelftest');
    } else {
      let self = null;
      try {
        self = JSON.parse(Module.fcmpSelftest());
      } catch (error) {
        row(false, 'editor.selftest', `Module.fcmpSelftest() did not return JSON (${error})`);
      }
      if (self !== null) {
        const pixels = self.pixels || {};
        row(self.atlasHash === ATLAS_HASH, 'editor.atlas', `${self.atlasHash} (want ${ATLAS_HASH})`);
        row(pixels.frames >= 1 && pixels.samples > 0 && pixels.over2 === 0, 'editor.pixels',
            `${pixels.frames} frame(s) read back through the sink: ${pixels.samples} samples against SoftRaster, the `
            + `largest difference ${pixels.largest} of 255, ${pixels.over2} over 2`);
      }
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
        && after.rate === context.sampleRate && (after.flags & linked) === linked && live.replies > 0
        && live.refused === 0, 'editor.link',
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
    if (state !== 'running') return;
    opening += 1;
    notice('');
    play(loopBuffer, SAY.loop);
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
