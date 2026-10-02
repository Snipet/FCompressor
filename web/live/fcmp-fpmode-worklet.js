// web/live/fcmp-fpmode-worklet.js: the floating-point environment of a thread, from JavaScript doubles (ADR-93, the
// web lead phase). A test-only processor of fcmp-tail.js, and a module that page imports for fpMode() itself, so the
// main thread is asked the same question.
//
// A wasm engine cannot set flush-to-zero or denormals-are-zero, but a browser may set either on its audio thread, and
// then the engine's values in the denormal range are not the ones node computes. fpMode() answers for the thread it
// runs on with two products whose operands are read from an array, so no compiler folds them, and whose results are
// judged by their bits, so no comparison takes part:
//   flush-to-zero        DBL_MIN * 0.5 is a denormal; it is a zero only where results are flushed
//   denormals-are-zero   1e-310 is a denormal and 1e-310 * 1e300 is 1e-10; it is a zero only where a denormal operand
//                        is read as zero
// The processor asks once in its constructor, once in its first process() and in every message handler, and answers
// any message with { fcmp: 'fpmode', inConstructor, inHandler, inProcess } (inProcess -1: process() has not run). It
// writes no sample: its output is the silence it was handed.
const OPERANDS = new Float64Array([2.2250738585072014e-308, 0.5, 1e-310, 1e300]);
const PRODUCTS = new Float64Array(2);
const BITS = new Uint32Array(PRODUCTS.buffer);

// Bit 0: flush-to-zero is on; bit 1: denormals-are-zero is on.
export function fpMode() {
  PRODUCTS[0] = OPERANDS[0] * OPERANDS[1];
  PRODUCTS[1] = OPERANDS[2] * OPERANDS[3];
  return ((BITS[0] | BITS[1]) === 0 ? 1 : 0) | ((BITS[2] | BITS[3]) === 0 ? 2 : 0);
}

if (typeof AudioWorkletProcessor === 'function' && typeof registerProcessor === 'function') {
  registerProcessor('fcmp-fpmode', class extends AudioWorkletProcessor {
    constructor() {
      super();
      this.inProcess = -1;
      this.inConstructor = fpMode();
      this.port.onmessage = () => this.port.postMessage({ fcmp: 'fpmode', inConstructor: this.inConstructor,
                                                          inHandler: fpMode(), inProcess: this.inProcess });
    }

    process() {
      if (this.inProcess < 0) this.inProcess = fpMode();
      return true;
    }
  });
}
