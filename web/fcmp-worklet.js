// fcmp-worklet.js (SCOUT SCRATCH): the AudioWorklet side of the demo. Plain JavaScript, a classic worklet script.
//
// The node is made with
//   new AudioWorkletNode(ctx, 'fcmp-engine', { numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [2],
//       channelCount: 2, channelCountMode: 'explicit', channelInterpretation: 'speakers',
//       processorOptions: { wasm: <ArrayBuffer of fcmp-engine.wasm> } })
// Port, to the worklet:   { b: ArrayBuffer, n }   one WebProtocol record in b's first n bytes (b is transferred)
//                         { q: 'selfcheck' | 'stats' | 'gate', ... }   the page's own questions
// Port, from the worklet: { ready: 1, abi, latency, rate, cap } once, or { error: text } once
//                         { b, n }                the reply to a Pull, in the buffer that carried it (transferred back)
//                         { refused: code, n }    fcmp_web_post said no (a negative PostError)
//                         { q: ..., ... }         the answers
'use strict';

const CAP = 128;            // frames per fcmp_web_process call; a larger quantum is processed in chunks
const MSG_BYTES = 140;      // WebProtocol kMaxMessageBytes

class FcmpEngineProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    this.ok = false;
    this.quanta = 0;
    this.oddQuanta = 0;       // quanta whose frame count was not CAP
    this.lastFrames = 0;
    this.inChannels = -1;
    this.outChannels = -1;
    this.clockMs = 0;         // Date.now() differences around process(), summed (an unbiased estimate over many)
    this.clockOn = false;
    this.refused = 0;
    this.lastRefusal = 0;
    this.isBuffer = (m) => Object.prototype.toString.call(m) === '[object ArrayBuffer]';
    try {
      const t0 = Date.now();
      const wasm = options.processorOptions.wasm;
      const module = new WebAssembly.Module(wasm);                 // synchronous: allowed off the main thread
      const x = new WebAssembly.Instance(module, {}).exports;
      x._initialize();
      this.compileMs = Date.now() - t0;
      this.x = x;
      this.engine = x.fcmp_web_create();
      if (this.engine === 0) throw new Error('fcmp_web_create failed');
      const audio = x.malloc(4 * CAP * 4);
      this.msg = x.malloc(MSG_BYTES);
      this.hash = x.malloc(8);
      if (audio === 0 || this.msg === 0 || this.hash === 0) throw new Error('malloc failed');
      this.pInL = audio;
      this.pInR = audio + CAP * 4;
      this.pOutL = audio + 2 * CAP * 4;
      this.pOutR = audio + 3 * CAP * 4;
      // The module's memory never grows (ALLOW_MEMORY_GROWTH=0), so these views stay valid for its life.
      const buffer = x.memory.buffer;
      this.vInL = new Float32Array(buffer, this.pInL, CAP);
      this.vInR = new Float32Array(buffer, this.pInR, CAP);
      this.vOutL = new Float32Array(buffer, this.pOutL, CAP);
      this.vOutR = new Float32Array(buffer, this.pOutR, CAP);
      this.bytes = new Uint8Array(buffer);
      this.words = new Uint32Array(buffer);
      this.latency = x.fcmp_web_configure(this.engine, sampleRate, CAP);
      if (this.latency < 0) throw new Error('fcmp_web_configure failed');
      this.ok = true;
      this.port.onmessage = (e) => this.onMessage(e.data);
      this.port.postMessage({ fcmp: 'ready', abi: x.fcmp_web_abi_version(), latency: this.latency, rate: sampleRate,
                              cap: CAP, compileMs: this.compileMs, wasmBytes: wasm.byteLength,
                              hasPerformance: typeof performance !== 'undefined',
                              globals: Object.getOwnPropertyNames(globalThis).filter((k) => /current|sample|render|port|register/i.test(k)).join(',') });
    } catch (error) {
      this.port.postMessage({ fcmp: 'error', error: String(error) });
    }
  }

  onMessage(m) {
    const x = this.x;
    if (this.isBuffer(m)) {
      // A record at its exact size, or a carrier whose first Header::bytes bytes are the record (a Pull in a
      // reply-sized buffer). Header::bytes, a u32 at offset 8, is the one field this script reads.
      const copied = Math.min(m.byteLength, MSG_BYTES);
      this.bytes.set(new Uint8Array(m, 0, copied), this.msg);
      const n = Math.min(this.words[(this.msg >> 2) + 2], copied);
      const r = x.fcmp_web_post(this.engine, this.msg, n);
      if (r > 0 && m.byteLength >= r) {
        const p = x.fcmp_web_reply(this.engine);
        new Uint8Array(m, 0, r).set(this.bytes.subarray(p, p + r));
        this.port.postMessage(m, [m]);
      } else if (r !== 0) {
        this.refused += 1;
        this.lastRefusal = r > 0 ? -5 : r;
      }
      return;
    }
    m = { q: m.fcmp, on: m.on };
    if (m.q === 'selfcheck') {
      const t0 = Date.now();
      const rc = x.fcmp_web_selfcheck(this.hash);
      const hex8 = (v) => v.toString(16).padStart(8, '0');
      this.port.postMessage({ fcmp: 'selfcheck', rc, hash: hex8(this.words[this.hash / 4 + 1]) + hex8(this.words[this.hash / 4]),
                              ms: Date.now() - t0 });
    } else if (m.q === 'stats') {
      this.port.postMessage({ fcmp: 'stats', refused: this.refused, lastRefusal: this.lastRefusal, quanta: this.quanta, oddQuanta: this.oddQuanta, lastFrames: this.lastFrames,
                              inChannels: this.inChannels, outChannels: this.outChannels, clockMs: this.clockMs,
                              latency: x.fcmp_web_latency(this.engine), frame: currentFrame, time: currentTime });
    } else if (m.q === 'clock') {
      this.clockOn = !!m.on;
      this.clockMs = 0;
      this.quanta = 0;
    } else if (m.q === 'gate') {
      x.fcmp_web_set_gate(this.engine, m.on ? 1 : 0);
    }
  }

  process(inputs, outputs) {
    const out = outputs[0];
    const outL = out[0];
    if (!this.ok || outL === undefined) return true;      // silence (the arrays arrive zeroed)
    const outR = out.length > 1 ? out[1] : undefined;
    const input = inputs[0];
    const inL = input !== undefined && input.length > 0 ? input[0] : undefined;
    const inR = input !== undefined && input.length > 1 ? input[1] : undefined;
    const frames = outL.length;
    const t0 = this.clockOn ? Date.now() : 0;
    const x = this.x;
    if (frames === CAP) {
      if (inL !== undefined) this.vInL.set(inL);
      if (inR !== undefined) this.vInR.set(inR);
      x.fcmp_web_process(this.engine, inL !== undefined ? this.pInL : 0, inR !== undefined ? this.pInR : 0,
                         this.pOutL, outR !== undefined ? this.pOutR : 0, CAP);
      outL.set(this.vOutL);
      if (outR !== undefined) outR.set(this.vOutR);
    } else {
      // Any other quantum: chunks of at most CAP, copied by index (no view is made here).
      for (let at = 0; at < frames; at += CAP) {
        const n = Math.min(CAP, frames - at);
        if (inL !== undefined) for (let i = 0; i < n; i += 1) this.vInL[i] = inL[at + i];
        if (inR !== undefined) for (let i = 0; i < n; i += 1) this.vInR[i] = inR[at + i];
        x.fcmp_web_process(this.engine, inL !== undefined ? this.pInL : 0, inR !== undefined ? this.pInR : 0,
                           this.pOutL, outR !== undefined ? this.pOutR : 0, n);
        for (let i = 0; i < n; i += 1) outL[at + i] = this.vOutL[i];
        if (outR !== undefined) for (let i = 0; i < n; i += 1) outR[at + i] = this.vOutR[i];
      }
      this.oddQuanta += 1;
    }
    this.quanta += 1;
    this.lastFrames = frames;
    this.inChannels = input !== undefined ? input.length : -1;
    this.outChannels = out.length;
    if (this.clockOn) this.clockMs += Date.now() - t0;
    return true;
  }
}

registerProcessor('fcmp-engine', FcmpEngineProcessor);
