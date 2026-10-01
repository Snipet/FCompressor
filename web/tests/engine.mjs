// web/tests/engine.mjs: the shipped engine module, loaded as the AudioWorklet will load it (ADR-93).
//
// FCMP_WEB_TEST name=web.engine.abi timeout=120 args={engine},{build}
//
//   node engine.mjs <fcmp-engine.wasm> <build directory>
//
// fcmp-engine.wasm is a standalone module: no JavaScript glue, nothing imported. This script
//   1. checks that the import list is empty and that the export list is the C ABI (Source/web/engine/WebEngine.h) plus
//      memory, malloc, free and the runtime's own few names, nothing else;
//   2. instantiates it with an EMPTY import object and calls _initialize, as the worklet does;
//   3. runs the module's self-check and compares the hash with the constant recorded from the native build (the same
//      constant as Tools/web/enginecheck.cpp's kSelfCheckHash), and with what `fcmp_web_check selfcheck` of this build
//      directory computed: the standalone module and the node program must agree, and both must be the native value;
//   4. drives one engine through create, configure, process and destroy with buffers from the exported malloc.
// Output: PASS/FAIL/NOTE lines, as the probes print them. Exit 0 pass, 1 fail, 2 usage.
import { spawnSync } from 'node:child_process';
import { existsSync, readFileSync } from 'node:fs';
import { join } from 'node:path';

const TEST = 'web.engine.abi';

// The C ABI self-check's hash from the native arm64 build. Keep equal to Tools/web/enginecheck.cpp's kSelfCheckHash.
const SELF_CHECK_HASH = '5a96ce217d29ca6f';

// The C ABI: every function WebEngine.h marks with FCMP_WEB_EXPORT.
const ABI = [
  'fcmp_web_abi_version',
  'fcmp_web_create',
  'fcmp_web_destroy',
  'fcmp_web_configure',
  'fcmp_web_process',
  'fcmp_web_post',
  'fcmp_web_reply',
  'fcmp_web_latency',
  'fcmp_web_set_gate',
  'fcmp_web_selfcheck',
];
// What the link adds (cmake/FcmpWeb.cmake: STANDALONE_WASM, --no-entry, EXPORTED_FUNCTIONS=_malloc,_free).
const REQUIRED_RUNTIME = ['memory', 'malloc', 'free', '_initialize'];
// Emscripten's own exports that may or may not be present; anything outside these lists fails the test.
const OPTIONAL_RUNTIME = [
  '__indirect_function_table',
  '_emscripten_stack_restore',
  '_emscripten_stack_alloc',
  'emscripten_stack_get_current',
  'emscripten_stack_get_base',
  'emscripten_stack_get_end',
  'emscripten_stack_init',
  'emscripten_stack_get_free',
];

let passed = 0;
let failed = 0;
function row(ok, name, detail = '') {
  console.log(`${ok ? 'PASS' : 'FAIL'}     ${TEST} ${name}${detail ? ': ' + detail : ''}`);
  if (ok) passed += 1;
  else failed += 1;
  return ok;
}
function note(text) {
  console.log(`NOTE     ${text}`);
}
function finish() {
  console.log(`${failed === 0 ? 'PASS' : 'FAIL'}     ${TEST}: ${passed} row(s) passed, ${failed} failed`);
  process.exit(failed === 0 ? 0 : 1);
}

const [enginePath, buildDir] = process.argv.slice(2);
if (!enginePath || !buildDir) {
  console.error('usage: node engine.mjs <fcmp-engine.wasm> <build directory>');
  process.exit(2);
}

// ---- 1. imports and exports ------------------------------------------------------------------------------------------
const bytes = readFileSync(enginePath);
note(`${enginePath}: ${bytes.length} bytes`);
const module = new WebAssembly.Module(bytes);
const imports = WebAssembly.Module.imports(module).map((i) => `${i.module}.${i.name} (${i.kind})`);
row(imports.length === 0, 'imports.none', imports.length === 0 ? 'the import list is empty' : imports.join(', '));

const exported = WebAssembly.Module.exports(module);
const names = exported.map((e) => e.name);
note(`exports: ${exported.map((e) => `${e.name} (${e.kind})`).join(', ')}`);
const missingAbi = ABI.filter((n) => !names.includes(n));
row(missingAbi.length === 0, 'exports.abi', missingAbi.length === 0 ? `${ABI.length} functions` : `missing ${missingAbi.join(', ')}`);
const missingRuntime = REQUIRED_RUNTIME.filter((n) => !names.includes(n));
row(missingRuntime.length === 0, 'exports.runtime', missingRuntime.length === 0 ? REQUIRED_RUNTIME.join(', ') : `missing ${missingRuntime.join(', ')}`);
const allowed = new Set([...ABI, ...REQUIRED_RUNTIME, ...OPTIONAL_RUNTIME]);
const extra = names.filter((n) => !allowed.has(n));
row(extra.length === 0, 'exports.nothing_else', extra.length === 0 ? `${names.length} exports` : `unexpected ${extra.join(', ')}`);
const kinds = Object.fromEntries(exported.map((e) => [e.name, e.kind]));
row(kinds.memory === 'memory' && [...ABI, 'malloc', 'free', '_initialize'].every((n) => kinds[n] === 'function'), 'exports.kinds');
if (missingAbi.length > 0 || missingRuntime.length > 0) finish();

// ---- 2. instantiate as the worklet does ------------------------------------------------------------------------------
let x;
try {
  x = new WebAssembly.Instance(module, {}).exports;
  x._initialize();
  row(true, 'instantiate.empty_imports');
} catch (error) {
  row(false, 'instantiate.empty_imports', String(error));
  finish();
}
row(x.fcmp_web_abi_version() >= 1, 'abi.version', `protocol version ${x.fcmp_web_abi_version()}`);

// ---- 3. the self-check -----------------------------------------------------------------------------------------------
const u32 = () => new Uint32Array(x.memory.buffer);
const f32 = () => new Float32Array(x.memory.buffer);
const hashPtr = x.malloc(8);
const rc = x.fcmp_web_selfcheck(hashPtr);
const hex8 = (v) => v.toString(16).padStart(8, '0');
const hash = hex8(u32()[hashPtr / 4 + 1]) + hex8(u32()[hashPtr / 4]);
x.free(hashPtr);
note(`selfcheck hash ${hash} constant ${SELF_CHECK_HASH}`);
row(rc === 0 && hash === SELF_CHECK_HASH, 'selfcheck.hash',
    rc !== 0 ? 'the self-check failed to run' : hash === SELF_CHECK_HASH ? hash : `${hash} is not the native build's ${SELF_CHECK_HASH}`);

// The node program of the same build: fcmp_web_check selfcheck prints "NOTE     selfcheck hash <got> constant <want>".
const check = join(buildDir, 'fcmp_web_check.js');
if (!existsSync(check)) {
  row(false, 'selfcheck.same_as_check_program', `${check} is not built`);
} else {
  const run = spawnSync(process.execPath, [check, 'selfcheck'], { encoding: 'utf8' });
  const m = /selfcheck hash ([0-9a-f]{16}) constant ([0-9a-f]{16})/.exec(run.stdout ?? '');
  if (!m) {
    row(false, 'selfcheck.same_as_check_program', `no hash line from ${check} (exit ${run.status})`);
  } else {
    row(m[1] === hash, 'selfcheck.same_as_check_program', m[1] === hash ? hash : `module ${hash}, fcmp_web_check ${m[1]}`);
    row(m[2] === SELF_CHECK_HASH, 'selfcheck.one_constant',
        m[2] === SELF_CHECK_HASH ? SELF_CHECK_HASH : `engine.mjs has ${SELF_CHECK_HASH}, enginecheck.cpp has ${m[2]}`);
  }
}

// ---- 4. one engine, as the worklet drives it -------------------------------------------------------------------------
const FRAMES = 128;
const engine = x.fcmp_web_create();
row(engine !== 0, 'engine.create');
const buffers = x.malloc(4 * FRAMES * 4);                 // inL, inR, outL, outR
const [inL, inR, outL, outR] = [0, 1, 2, 3].map((i) => buffers + i * FRAMES * 4);
const latency = x.fcmp_web_configure(engine, 48000, FRAMES);
row(latency >= 0 && latency === x.fcmp_web_latency(engine), 'engine.configure', `latency ${latency} samples`);
let finite = true;
let peak = 0;
for (let q = 0; q < 16; q += 1) {
  const view = f32();                                     // the memory does not grow, but a fresh view costs nothing
  for (let i = 0; i < FRAMES; i += 1) {
    const s = 0.25 * (((q * FRAMES + i) % 96) < 48 ? 1 : -1);   // a 500 Hz square wave
    view[inL / 4 + i] = s;
    view[inR / 4 + i] = -s;
  }
  x.fcmp_web_process(engine, inL, inR, outL, outR, FRAMES);
  for (let i = 0; i < FRAMES; i += 1) {
    const a = view[outL / 4 + i];
    const b = view[outR / 4 + i];
    finite = finite && Number.isFinite(a) && Number.isFinite(b);
    peak = Math.max(peak, Math.abs(a), Math.abs(b));
  }
}
row(finite && peak > 0.01 && peak < 4, 'engine.process', `16 quanta, output peak ${peak.toFixed(4)}`);
row(x.fcmp_web_post(engine, 0, 16) < 0 && x.fcmp_web_reply(engine) !== 0, 'engine.post_and_reply');
x.free(buffers);
x.fcmp_web_destroy(engine);
row(true, 'engine.destroy');

finish();
