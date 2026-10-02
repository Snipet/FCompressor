// main.js (SCOUT SCRATCH prototype): the page. States: idle -> loading -> running; refusals (no WebGL2, no AudioWorklet),
// notices (a file that does not decode, a paused context). ?selftest=1 runs the checks and reports in document.title.
'use strict';
(() => {
  const SELF_CHECK_HASH = '5a96ce217d29ca6f';      // Tools/web/enginecheck.cpp kSelfCheckHash
  const ATLAS_HASH = 'b744c79b9bb755d0';           // tests/golden/base/global/ui.font.txt font.atlas.hash
  const MAX_FILE_BYTES = 64 * 1024 * 1024;
  const $ = (id) => document.getElementById(id);
  const params = new URLSearchParams(location.search);
  const selftest = params.get('selftest') === '1' || /\/fcmp-ui\.html$/.test(location.pathname);
  const status = (text) => { $('fcmp-status').textContent = text; };
  const notice = (text) => { $('fcmp-notice').textContent = text; };
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  const editorReady = new Promise((done, fail) => { window.fcmpEditor = { done, fail }; if (window.fcmpEditorState === 'ready') done(); else if (window.fcmpEditorState) fail(new Error(window.fcmpEditorState)); });
  let ctx = null, node = null, source = null, gain = null, loopBuffer = null, wasmBytes = null, state = 'idle';

  // ---- what the browser must have ---------------------------------------------------------------------------------
  function missing() {
    if (typeof WebAssembly !== 'object') return 'THIS BROWSER HAS NO WEBASSEMBLY.';
    if (!window.isSecureContext) return 'THE DEMO NEEDS HTTPS (OR LOCALHOST): THIS PAGE WAS NOT LOADED SECURELY, SO THE BROWSER GIVES IT NO AUDIO WORKLET.';
    if (!window.AudioContext || !window.AudioWorkletNode) return 'THIS BROWSER HAS NO AUDIO WORKLET. TRY A CURRENT CHROME, FIREFOX, SAFARI OR EDGE ON A DESKTOP.';
    // A canvas of its own: the editor's canvas must get its one context from the editor.
    if (!document.createElement('canvas').getContext('webgl2')) return 'THIS BROWSER GIVES THE PAGE NO WEBGL2, WHICH THE EDITOR IS DRAWN WITH. TRY A CURRENT DESKTOP BROWSER WITH HARDWARE ACCELERATION ON.';
    return '';
  }

  // ---- the worklet ----------------------------------------------------------------------------------------------------
  function ask(n, q, extra = {}) {
    return new Promise((done) => {
      const on = (e) => { if (e.data && e.data.fcmp === q) { n.port.removeEventListener('message', on); done(e.data); } };
      n.port.addEventListener('message', on);
      n.port.start();
      n.port.postMessage({ fcmp: q, ...extra });
    });
  }
  function makeNode(c, wasm) {
    return new Promise((done, fail) => {
      const n = new AudioWorkletNode(c, 'fcmp-engine', { numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [2],
        channelCount: 2, channelCountMode: 'explicit', channelInterpretation: 'speakers', processorOptions: { wasm } });
      n.onprocessorerror = () => fail(new Error('the audio processor stopped with an error'));
      const on = (e) => {
        if (!e.data || !(e.data.fcmp === 'ready' || e.data.fcmp === 'error')) return;
        n.port.removeEventListener('message', on);
        if (e.data.fcmp === 'error') fail(new Error(e.data.error)); else done([n, e.data]);
      };
      n.port.addEventListener('message', on);
      n.port.start();
    });
  }
  async function engineBytes() {
    if (!wasmBytes) {
      const r = await fetch('fcmp-engine.wasm');
      if (!r.ok) throw new Error('fcmp-engine.wasm: HTTP ' + r.status);
      wasmBytes = await r.arrayBuffer();
    }
    return wasmBytes;
  }

  // ---- sources --------------------------------------------------------------------------------------------------------
  function play(buffer, name) {
    const now = ctx.currentTime;
    if (source) {                                   // 30 ms out, then gone
      gain.gain.setValueAtTime(1, now); gain.gain.linearRampToValueAtTime(0, now + 0.03);
      source.stop(now + 0.04);
    }
    gain = ctx.createGain();
    gain.gain.setValueAtTime(0, now + 0.04); gain.gain.linearRampToValueAtTime(1, now + 0.07);
    source = ctx.createBufferSource();
    source.buffer = buffer; source.loop = true;
    source.connect(gain).connect(node);
    source.start(now + 0.04);
    $('fcmp-source-name').textContent = 'SOURCE   ' + name;
    $('fcmp-loop').disabled = buffer === loopBuffer;
  }
  async function openFile(file) {
    notice('');
    if (state !== 'running') { notice('START THE DEMO FIRST, THEN DROP THE FILE.'); return; }
    if (file.size > MAX_FILE_BYTES) { notice(`${file.name.toUpperCase()} IS LARGER THAN 64 MB. THE LOOP KEEPS PLAYING.`); return; }
    try {
      const decoded = await ctx.decodeAudioData(await file.arrayBuffer());
      if (decoded.length < ctx.sampleRate / 10) throw new Error('shorter than 0.1 s');
      play(decoded, file.name.toUpperCase());
    } catch (e) {
      notice(`THIS BROWSER COULD NOT READ ${file.name.toUpperCase()} AS AUDIO (WAV, MP3, FLAC AND M4A USUALLY WORK). THE SOURCE IS UNCHANGED.`);
    }
  }

  // ---- start ----------------------------------------------------------------------------------------------------------
  function loadScript(src) {
    return new Promise((done, fail) => {
      const s = document.createElement('script');
      s.src = src; s.onload = done; s.onerror = () => fail(new Error(src + ' did not load'));
      document.body.appendChild(s);
    });
  }
  async function start() {
    if (state !== 'idle') return;
    state = 'loading';
    $('fcmp-start').hidden = true;
    status('LOADING');
    // In the click: the context is created inside the user's gesture, so it may start.
    try { ctx = new AudioContext({ sampleRate: 48000, latencyHint: 'interactive' }); }
    catch (e) { ctx = new AudioContext({ latencyHint: 'interactive' }); }           // a rate the device will not do
    const resumed = ctx.resume();
    ctx.onstatechange = onContextState;
    await ctx.audioWorklet.addModule('fcmp-worklet.js');
    let ready;
    [node, ready] = await makeNode(ctx, await engineBytes());
    node.connect(ctx.destination);
    const loop = fcmpSynthLoop(ctx.sampleRate);
    loopBuffer = ctx.createBuffer(2, loop.frames, ctx.sampleRate);
    loopBuffer.copyToChannel(loop.left, 0); loopBuffer.copyToChannel(loop.right, 1);
    // The editor has been running since the page loaded (fcmp-ui.js, index.html): hand it the worklet's port.
    await editorReady;
    if (!JSON.parse(Module.fcmpStatus()).ok) throw new Error('NO WEBGL2: ' + JSON.parse(Module.fcmpStatus()).error);
    Module.fcmpPort.connect(node.port, ctx.sampleRate, 128);
    play(loopBuffer, 'BUILT-IN LOOP');
    await Promise.race([resumed, sleep(250)]);
    state = 'running';
    $('fcmp-overlay').hidden = true;
    onContextState();
    return ready;
  }
  function onContextState() {
    if (!ctx || state !== 'running') return;
    if (ctx.state === 'running') { $('fcmp-overlay').hidden = true; return; }
    // suspended (no gesture yet, or the browser paused it) or interrupted (Safari: another app took the output)
    $('fcmp-overlay').hidden = false;
    $('fcmp-start').hidden = false;
    $('fcmp-start').textContent = 'RESUME';
    status('AUDIO IS PAUSED BY THE BROWSER. CLICK TO RESUME.');
    $('fcmp-start').onclick = () => ctx.resume();
  }
  function refuse(e) {
    state = 'failed';
    $('fcmp-overlay').hidden = false;
    $('fcmp-start').hidden = true;
    status('THE DEMO COULD NOT START: ' + String(e && e.message || e).toUpperCase());
  }

  // ---- selftest ---------------------------------------------------------------------------------------------------------
  async function runSelftest() {
    const logEl = $('funkgui-log');
    logEl.hidden = false;
    let failure = '';
    const row = (ok, name, detail = '') => { logEl.textContent += `${ok ? 'PASS' : 'FAIL'}     web.page ${name}${detail ? ': ' + detail : ''}\n`; if (!ok && !failure) failure = name; };
    const say = (text) => { logEl.textContent += `NOTE     ${text}\n`; };
    document.title = 'RUNNING';
    try {
      say(navigator.userAgent);
      const lack = missing();
      row(lack === '', 'browser', lack || 'WebAssembly, a secure context, AudioWorklet, WebGL2');
      const wasm = await engineBytes();
      // 1. the engine on the main thread
      const x = (await WebAssembly.instantiate(wasm, {})).instance.exports;
      x._initialize();
      const hex8 = (v) => v.toString(16).padStart(8, '0');
      const p = x.malloc(8);
      const rc = x.fcmp_web_selfcheck(p);
      const u = new Uint32Array(x.memory.buffer);
      const mainHash = hex8(u[p / 4 + 1]) + hex8(u[p / 4]);
      row(rc === 0 && mainHash === SELF_CHECK_HASH, 'engine.selfcheck.main', `${mainHash} (want ${SELF_CHECK_HASH})`);
      // 2. the real worklet in an OfflineAudioContext: needs no gesture
      const seconds = 10;
      const off = new OfflineAudioContext({ numberOfChannels: 2, length: 48000 * seconds, sampleRate: 48000 });
      await off.audioWorklet.addModule('fcmp-worklet.js');
      const [n, ready] = await makeNode(off, wasm);
      const sc = await ask(n, 'selfcheck');
      row(sc.rc === 0 && sc.hash === SELF_CHECK_HASH, 'engine.selfcheck.worklet', `${sc.hash} in ${sc.ms} ms; abi ${ready.abi}, latency ${ready.latency}`);
      const l = fcmpSynthLoop(48000);
      const b = off.createBuffer(2, l.frames, 48000);
      b.copyToChannel(l.left, 0); b.copyToChannel(l.right, 1);
      const s = off.createBufferSource(); s.buffer = b; s.loop = true; s.connect(n); n.connect(off.destination); s.start();
      const t0 = performance.now();
      const out = await off.startRendering();
      const ms = performance.now() - t0;
      let peak = 0, finite = true; const d = out.getChannelData(0);
      for (let i = 0; i < d.length; i += 1) { peak = Math.max(peak, Math.abs(d[i])); finite = finite && Number.isFinite(d[i]); }
      const st = await ask(n, 'stats');
      row(finite && peak > 0.05 && peak < 2 && st.quanta === seconds * 375 && st.oddQuanta === 0, 'worklet.render',
          `${seconds} s of the loop: peak ${peak.toFixed(3)}, ${st.quanta} quanta of ${st.lastFrames}`);
      say(`worklet load: ${seconds} s in ${ms.toFixed(0)} ms = ${(seconds * 1000 / ms).toFixed(0)}x real time (default Mode, STD)`);
      // 3. flush-to-zero: a burst, then silence with the gate off; the cost of silence against the cost of signal
      {
        const e = x.fcmp_web_create(); const buf = x.malloc(4 * 128 * 4);
        x.fcmp_web_configure(e, 48000, 128); x.fcmp_web_set_gate(e, 0);
        const f = new Float32Array(x.memory.buffer);
        const run = (quanta, level) => {
          const a = performance.now();
          for (let q = 0; q < quanta; q += 1) {
            for (let i = 0; i < 128; i += 1) { const v = level * Math.sin(0.05 * (q * 128 + i)); f[buf / 4 + i] = v; f[buf / 4 + 128 + i] = v; }
            x.fcmp_web_process(e, buf, buf + 512, buf + 1024, buf + 1536, 128);
          }
          return (performance.now() - a) / quanta;
        };
        run(375, 0.5);
        const active = run(750, 0.5);
        let worst = 0;
        for (let w = 0; w < 24; w += 1) worst = Math.max(worst, run(125, 0));      // 8 s of silence in 1/3 s windows
        row(worst < 3 * active + 0.02, 'engine.flush', `silence costs at most ${(worst / active).toFixed(2)}x the signal (${(active * 1000).toFixed(1)} us a quantum), gate off`);
      }
      // 4. the editor (and the live context; it stays suspended without a gesture, which is not judged)
      await start().catch((e) => row(false, 'page.start', String(e)));
      if (state === 'running') {
        await sleep(600);
        const s = JSON.parse(Module.fcmpStatus());
        say('editor: ' + JSON.stringify(s));
        row(s.ok === 1 && s.frames > 0, 'editor.draws', `${s.frames} frames drawn at ${s.fps} fps, zoom ${s.zoom}`);
        row(s.replies > 0 && s.refused === 0 && s.prepared === 1 && s.rate === ctx.sampleRate, 'editor.link', `${s.replies} replies, flags 0x${s.flags.toString(16)}, posted ${s.posted}, rate ${s.rate}`);
        // a readPixels of the canvas from outside the sink sees nothing unless preserveDrawingBuffer: note only
        say(`context: state ${ctx.state}, ${ctx.sampleRate} Hz, baseLatency ${ctx.baseLatency}, outputLatency ${ctx.outputLatency}`);
        const live = await ask(node, 'stats');
        say('live worklet: ' + JSON.stringify(live));
      }
    } catch (e) {
      row(false, 'selftest.ran', String(e && e.stack || e));
    }
    document.title = failure ? 'FAIL: ' + failure : 'PASS';
  }

  // ---- wiring -----------------------------------------------------------------------------------------------------------
  window.addEventListener('error', (e) => { if (selftest && !document.title.startsWith('FAIL')) document.title = 'FAIL: uncaught ' + e.message; });
  window.addEventListener('unhandledrejection', (e) => { if (selftest && !document.title.startsWith('FAIL')) document.title = 'FAIL: rejection ' + e.reason; });
  fetch('built-from.txt').then((r) => r.ok ? r.text() : '').then((t) => {
    const m = /^site ([0-9a-f]{40}) (clean|dirty) (\S+)/.exec(t);
    if (!m) return;
    $('fcmp-built').innerHTML = `BUILT FROM <a href="https://github.com/Snipet/FCompressor/tree/${m[1]}">${m[1].slice(0, 12).toUpperCase()}</a>${m[2] === 'dirty' ? ' PLUS UNCOMMITTED CHANGES' : ''}, ${m[3]}`;
  }).catch(() => {});
  const lack = missing();
  if (lack) { $('fcmp-start').hidden = true; status(lack); if (selftest) runSelftest(); return; }
  $('fcmp-start').onclick = () => start().catch(refuse);
  $('fcmp-loop').onclick = () => { if (state === 'running') play(loopBuffer, 'BUILT-IN LOOP'); };
  $('fcmp-file').onchange = (e) => { if (e.target.files[0]) openFile(e.target.files[0]); e.target.value = ''; };
  window.addEventListener('dragover', (e) => { e.preventDefault(); document.body.classList.add('dragging'); });
  window.addEventListener('dragleave', () => document.body.classList.remove('dragging'));
  window.addEventListener('drop', (e) => { e.preventDefault(); document.body.classList.remove('dragging'); const f = e.dataTransfer && e.dataTransfer.files[0]; if (f) openFile(f); });
  if (selftest) runSelftest();
  window.fcmpDebug = { ctx: () => ctx, node: () => node, ask, state: () => state };
})();
