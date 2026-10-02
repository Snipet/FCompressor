// web/fcmp-worklet.js: the audio thread of the browser demo (ADR-93, web Sprint D). Plain JavaScript and an ES module
// (audioWorklet.addModule always loads one; web/package.json makes node read it the same way, so
// web/tests/worklet.mjs runs this very file).
//
// The page makes the node with
//   new AudioWorkletNode(context, 'fcmp-engine', { numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [2],
//       channelCount: 2, channelCountMode: 'explicit', channelInterpretation: 'speakers',
//       processorOptions: { wasm: <the bytes of fcmp-engine.wasm, an ArrayBuffer> } })
// (without outputChannelCount an unconnected node has one output channel; with the explicit count of 2 a mono source
// arrives as two channels and a stopped one as none).
//
// The constructor compiles the engine (Source/web/engine/WebEngine.h: a standalone module, nothing imported),
// creates one engine at its default values, configures it at the context's rate for 128-frame calls, and makes its
// views on the module's memory once: that memory never grows (cmake/FcmpWeb.cmake: ALLOW_MEMORY_GROWTH=0), so they
// stay valid. Then it says { fcmp: 'ready', abi, latency, sampleRate }, or { fcmp: 'error', error }.
//
// process() is the audio thread's callback: it copies the input in, calls fcmp_web_process and copies the output out.
// NOTHING IN IT ALLOCATES: no array or object literal, no closure, no spread, no destructuring, no subarray(); it
// posts no message, calls no console function and never throws (an engine that trapped is silence from then on, and
// the next message on the port says so). Any frame count; no input, a mono input or an empty one; one or two output
// channels.
//
// The port (docs/sprints/web-d.md, "The seam between the module and the page"):
//   an ArrayBuffer      one WebProtocol record, from the editor module. A script only moves its bytes: at most 140
//                       are copied to the engine's inbox and posted with n = Header::bytes, the u32 at offset 8 and the
//                       one field read here (a Pull arrives in a carrier as large as the largest reply, so the
//                       buffer's own length is not the record's), and never with more than was copied. When
//                       fcmp_web_post returns r > 0, the first r bytes of the reply are copied into the same buffer,
//                       which is transferred back. Otherwise nothing is sent; a negative r (a refusal) is counted,
//                       and so is a reply its buffer cannot hold.
//   { fcmp: 'stats' }       answered with { fcmp: 'stats', ok, quanta, oddQuanta, lastFrames, inChannels,
//                           outChannels, records, replies, refused, lastRefusal, latency }
//   { fcmp: 'selfcheck' }   answered with { fcmp: 'selfcheck', rc, hash, ms }: the engine's self-check, 30 to 60 ms on
//                           this thread, so it runs only on this request and never while a source plays (the last
//                           quantum had an input channel): then rc is -2 and nothing ran.

const QUANTUM = 128;            // frames per fcmp_web_process call; any other quantum goes through in pieces of it
const MESSAGE_BYTES = 140;      // WebProtocol.h kMaxMessageBytes: the largest record the engine accepts
const HEADER_BYTES = 16;        // sizeof(Header)
const BYTES_FIELD = 8;          // offsetof(Header, bytes)
const BAD_ARGUMENT = -1;        // WebEngine.h kPostBadArgument
const BAD_SIZE = -5;            // WebEngine.h kPostBadSize
const SELFCHECK_BUSY = -2;

const hex8 = (value) => value.toString(16).padStart(8, '0');

class FcmpEngineProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    this.ok = false;
    this.fault = null;          // what stopped the engine inside process(): said by the next message, never from there
    this.quanta = 0;
    this.oddQuanta = 0;         // quanta of another frame count than QUANTUM
    this.lastFrames = 0;
    this.inChannels = 0;        // of the last quantum: 0 is no source
    this.outChannels = 0;
    this.records = 0;
    this.replies = 0;
    this.refused = 0;
    this.lastRefusal = 0;
    this.latency = 0;
    this.port.onmessage = (event) => this.receive(event.data);
    try {
      const x = new WebAssembly.Instance(new WebAssembly.Module(options.processorOptions.wasm), {}).exports;
      x._initialize();
      this.x = x;
      this.engine = x.fcmp_web_create();
      if (this.engine === 0) throw new Error('fcmp_web_create failed');
      const audio = x.malloc(4 * QUANTUM * 4);
      this.pMessage = x.malloc(MESSAGE_BYTES);
      this.pHash = x.malloc(8);
      if (audio === 0 || this.pMessage === 0 || this.pHash === 0) throw new Error('the engine module is out of memory');
      this.pInL = audio;
      this.pInR = audio + QUANTUM * 4;
      this.pOutL = audio + 2 * QUANTUM * 4;
      this.pOutR = audio + 3 * QUANTUM * 4;
      const memory = x.memory.buffer;
      this.inL = new Float32Array(memory, this.pInL, QUANTUM);
      this.inR = new Float32Array(memory, this.pInR, QUANTUM);
      this.outL = new Float32Array(memory, this.pOutL, QUANTUM);
      this.outR = new Float32Array(memory, this.pOutR, QUANTUM);
      this.heap = new Uint8Array(memory);
      this.words = new Uint32Array(memory);
      this.latency = x.fcmp_web_configure(this.engine, sampleRate, QUANTUM);
      if (this.latency < 0) throw new Error(`fcmp_web_configure failed at ${sampleRate} Hz`);
      this.pReply = x.fcmp_web_reply(this.engine);
      this.ok = true;
      this.port.postMessage({ fcmp: 'ready', abi: x.fcmp_web_abi_version(), latency: this.latency, sampleRate });
    } catch (error) {
      this.port.postMessage({ fcmp: 'error', error: String(error) });
    }
  }

  // ---- the port: between two quanta, on the audio thread ------------------------------------------------------------
  receive(data) {
    try {
      if (this.fault !== null) {
        this.port.postMessage({ fcmp: 'error', error: String(this.fault) });
        this.fault = null;
      }
      if (data instanceof ArrayBuffer) this.record(data);
      else if (data && data.fcmp === 'stats') this.stats();
      else if (data && data.fcmp === 'selfcheck') this.selfcheck();
    } catch (error) {
      this.ok = false;
      this.port.postMessage({ fcmp: 'error', error: String(error) });
    }
  }

  record(buffer) {
    this.records += 1;
    if (!this.ok) {
      this.refuse(BAD_ARGUMENT);
      return;
    }
    const bytes = new Uint8Array(buffer);
    const copied = Math.min(bytes.length, MESSAGE_BYTES);
    this.heap.set(copied === bytes.length ? bytes : bytes.subarray(0, copied), this.pMessage);
    // Never more than was copied: what lies behind it in the inbox is an earlier record's.
    const said = copied >= HEADER_BYTES ? this.words[(this.pMessage + BYTES_FIELD) >> 2] : copied;
    const r = this.x.fcmp_web_post(this.engine, this.pMessage, Math.min(said, copied));
    if (r > 0 && r <= bytes.length) {
      bytes.set(this.heap.subarray(this.pReply, this.pReply + r));
      this.port.postMessage(buffer, [buffer]);
      this.replies += 1;
    } else if (r !== 0) {
      this.refuse(r > 0 ? BAD_SIZE : r);
    }
  }

  refuse(code) {
    this.refused += 1;
    this.lastRefusal = code;
  }

  stats() {
    this.port.postMessage({ fcmp: 'stats', ok: this.ok, quanta: this.quanta, oddQuanta: this.oddQuanta,
                            lastFrames: this.lastFrames, inChannels: this.inChannels, outChannels: this.outChannels,
                            records: this.records, replies: this.replies, refused: this.refused,
                            lastRefusal: this.lastRefusal,
                            latency: this.ok ? this.x.fcmp_web_latency(this.engine) : 0 });
  }

  selfcheck() {
    if (!this.ok || this.inChannels > 0) {
      this.port.postMessage({ fcmp: 'selfcheck', rc: this.ok ? SELFCHECK_BUSY : BAD_ARGUMENT, hash: '', ms: 0 });
      return;
    }
    const t0 = Date.now();                                   // the worklet's scope has no performance.now()
    const rc = this.x.fcmp_web_selfcheck(this.pHash);
    const at = this.pHash >> 2;
    this.port.postMessage({ fcmp: 'selfcheck', rc, hash: hex8(this.words[at + 1]) + hex8(this.words[at]),
                            ms: Date.now() - t0 });
  }

  // ---- the audio callback: see the rules at the top -----------------------------------------------------------------
  process(inputs, outputs) {
    const out = outputs[0];
    if (!this.ok || out === undefined || out.length === 0) return true;     // silence: the arrays arrive zeroed
    const outL = out[0];
    const outR = out.length > 1 ? out[1] : null;
    const frames = outL.length;
    const input = inputs[0];
    const channels = input === undefined ? 0 : input.length;
    // No channel is no source and one is mono; a channel of another length than the output is not read.
    const inL = channels > 0 && input[0].length === frames ? input[0] : null;
    const inR = inL !== null && channels > 1 && input[1].length === frames ? input[1] : null;
    const pInL = inL !== null ? this.pInL : 0;
    const pInR = inR !== null ? this.pInR : 0;
    const pOutR = outR !== null ? this.pOutR : 0;
    try {
      if (frames === QUANTUM) {
        if (inL !== null) this.inL.set(inL);
        if (inR !== null) this.inR.set(inR);
        this.x.fcmp_web_process(this.engine, pInL, pInR, this.pOutL, pOutR, QUANTUM);
        outL.set(this.outL);
        if (outR !== null) outR.set(this.outR);
      } else {
        // Another quantum: pieces of at most QUANTUM, copied by index (set() would need a view per piece).
        for (let at = 0; at < frames; at += QUANTUM) {
          const n = Math.min(QUANTUM, frames - at);
          if (inL !== null) for (let i = 0; i < n; i += 1) this.inL[i] = inL[at + i];
          if (inR !== null) for (let i = 0; i < n; i += 1) this.inR[i] = inR[at + i];
          this.x.fcmp_web_process(this.engine, pInL, pInR, this.pOutL, pOutR, n);
          for (let i = 0; i < n; i += 1) outL[at + i] = this.outL[i];
          if (outR !== null) for (let i = 0; i < n; i += 1) outR[at + i] = this.outR[i];
        }
        this.oddQuanta += 1;
      }
    } catch (error) {
      this.ok = false;
      this.fault = error;
      outL.fill(0);
      if (outR !== null) outR.fill(0);
    }
    this.quanta += 1;
    this.lastFrames = frames;
    this.inChannels = channels;
    this.outChannels = out.length;
    return true;
  }
}

registerProcessor('fcmp-engine', FcmpEngineProcessor);
