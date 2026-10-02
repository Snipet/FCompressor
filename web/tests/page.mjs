// web/tests/page.mjs: the page against the repository, and its logic that needs no browser (ADR-93, web Sprint D).
//
// FCMP_WEB_TEST name=web.page timeout=60 args={source},{build}/site
//
//   node page.mjs <repository> <build>/site
//
// web/main.js is imported as it is (web/package.json: an ES module; it starts the page only where there is a
// document). The rows:
//   constants.*    what the page and the worklet know by value equals the repository's: the engine's self-check hash
//                  (Tools/web/enginecheck.cpp; the same in every web test), the atlas hash (the ui.font golden), the
//                  protocol's numbers (WebProtocol.h, WebEngine.h), and one block size in the page and the worklet
//   site.*         the site's page files are the repository's (fcmp-ui.html is index.html); every file index.html and
//                  main.js name is in the site; the footer links the licences
//   licences.*     the licence files are byte-equal to their sources, and THIRD-PARTY.txt holds each of the
//                  toolchain's texts whole
//   wording.*      what the page says is upper case, in the HTML and in main.js's table; the differences are listed
//                  and BYPASS is the editor's own
//   refusals.*     what the browser lacks, in the order a reader can act on; and index.html's own script, run here
//                  against a stand-in document: it says that the page did not load when main.js never runs (and
//                  gives the self-test its FAIL), and nothing once main.js has booted
//   editor.*       what the editor's status means to the page (no WebGL2 only when the sink says so, any other reason
//                  as it is, a lost context nothing), and which uncaught errors are the editor's failure
//   pixels.*       the self-test's pixel rule (web lead phase): the software bound is FunkGui's for its own sink's
//                  page; a renderer's class is its name's, and an unknown name is a GPU; both sides of each number (a
//                  GPU: none over 2 of 255; software: none over 16, at most 10 per mille over 2); no frame, no pass;
//                  and the frame is asked for before the demo starts, which the row says
//   files.*        the limits of a dropped file, and the fades at its ends
//   built_from.*   the footer's line: a link only for `clean`, nothing for `none` or an unreadable line
//   verdict.*      the self-test's title: RUNNING, then PASS or FAIL: <the first failing row>; an uncaught error is a
//                  FAIL at once; no PASS ever replaces a FAIL
// Output: PASS/FAIL/NOTE lines, as the probes print them. Exit 0 pass, 1 fail, 2 usage.
import { existsSync, readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { pathToFileURL } from 'node:url';

const TEST = 'web.page';

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

const [source, site] = process.argv.slice(2);
if (!source || !site) {
  console.error('usage: node page.mjs <repository> <build>/site');
  process.exit(2);
}
const read = (...parts) => readFileSync(join(...parts), 'utf8');
const found = (text, pattern) => (pattern.exec(text) || [])[1];
const page = await import(pathToFileURL(join(source, 'web', 'main.js')).href);
const index = read(source, 'web/index.html');

// ---- constants -----------------------------------------------------------------------------------------------------
{
  const native = found(read(source, 'Tools/web/enginecheck.cpp'), /kSelfCheckHash = 0x([0-9a-f]{16})ull/);
  const tests = ['engine.mjs', 'worklet.mjs'];
  const others = tests.map((f) => found(read(source, 'web/tests', f), /SELF_CHECK_HASH = '([0-9a-f]{16})'/));
  row(!!native && page.SELF_CHECK_HASH === native && others.every((h) => h === native), 'constants.selfcheck',
      `main.js ${page.SELF_CHECK_HASH}, enginecheck.cpp ${native}, ${tests.join(' and ')} ${others.join(' and ')}`);

  const golden = found(read(source, 'tests/golden/base/global/ui.font.txt'), /^font\.atlas\.hash\t([0-9a-f]{16})\t/m);
  row(!!golden && page.ATLAS_HASH === golden, 'constants.atlas',
      `main.js ${page.ATLAS_HASH}, the ui.font golden ${golden}`);

  const protocol = read(source, 'Source/web/engine/WebProtocol.h');
  const engine = read(source, 'Source/web/engine/WebEngine.h');
  const worklet = read(source, 'web/fcmp-worklet.js');
  const number = (text, pattern) => Number(found(text, pattern));
  const own = (name) => number(worklet, new RegExp(`^const ${name} = (-?\\d+);`, 'm'));
  const bit = (name) => 1 << number(protocol, new RegExp(`${name}\\s*= 1u << (\\d+)`));
  const pairs = [
    ['the worklet\'s MESSAGE_BYTES', own('MESSAGE_BYTES'), number(protocol, /\(kMaxMessageBytes == (\d+)\)/)],
    ['HEADER_BYTES', own('HEADER_BYTES'), number(protocol, /sizeof\(Header\) == (\d+)\)/)],
    ['BYTES_FIELD', own('BYTES_FIELD'), number(protocol, /offsetof\(Header, bytes\) == (\d+)/)],
    ['BAD_ARGUMENT', own('BAD_ARGUMENT'), number(engine, /kPostBadArgument = (-\d+)/)],
    ['BAD_SIZE', own('BAD_SIZE'), number(engine, /kPostBadSize = (-\d+)/)],
    ['main.js\'s magic', page.PROTOCOL.magic, number(protocol, /kMagic = (0x[0-9a-f]+)u/)],
    ['version', page.PROTOCOL.version, number(protocol, /kVersion = (\d+);/)],
    ['attach', page.PROTOCOL.attach, number(protocol, /\battach = (\d+),/)],
    ['attachBytes', page.PROTOCOL.attachBytes, number(protocol, /sizeof\(AttachMsg\) == (\d+)\)/)],
    ['replyGated', page.PROTOCOL.replyGated, bit('kReplyGated')],
    ['replyConfigured', page.PROTOCOL.replyConfigured, bit('kReplyConfigured')],
    ['replyAttached', page.PROTOCOL.replyAttached, bit('kReplyAttached')],
  ];
  const wrong = pairs.filter(([, mine, theirs]) => !(Number.isFinite(theirs) && mine === theirs))
                     .map(([name, mine, theirs]) => `${name}: ${mine}, the header says ${theirs}`);
  row(wrong.length === 0, 'constants.protocol',
      wrong.length === 0 ? `${pairs.length} values equal WebProtocol.h's and WebEngine.h's` : wrong.join('; '));
  row(page.QUANTUM === own('QUANTUM') && page.QUANTUM === 128, 'constants.quantum',
      `main.js ${page.QUANTUM}, fcmp-worklet.js ${own('QUANTUM')}`);
}

// ---- the site ------------------------------------------------------------------------------------------------------
const same = (a, b) => existsSync(a) && existsSync(b) && readFileSync(a).equals(readFileSync(b));
{
  const pageFiles = ['index.html', 'demo.css', 'main.js', 'loop.js', 'fcmp-worklet.js'];
  const stale = pageFiles.filter((f) => !same(join(source, 'web', f), join(site, f)));
  if (!same(join(source, 'web/index.html'), join(site, 'fcmp-ui.html'))) stale.push('fcmp-ui.html (index.html again)');
  row(stale.length === 0, 'site.is_the_source',
      stale.length === 0 ? `${pageFiles.length + 1} files` : `not the repository's: ${stale.join(', ')}`);
  row(!existsSync(join(site, 'package.json')) && !existsSync(join(site, 'tests')), 'site.no_tests',
      'no package.json, no tests');

  // Every file the page names: index.html's attributes, and main.js's imports and strings that name a file.
  const named = new Set();
  for (const m of index.matchAll(/\b(?:src|href)\s*=\s*"([^"]*)"/g)) if (!/^[a-z]+:/i.test(m[1])) named.add(m[1]);
  const main = read(source, 'web/main.js');
  for (const m of main.matchAll(/'(?:\.\/)?([\w\-/]+\.(?:js|wasm|txt|css|html))'/g)) named.add(m[1]);
  const lost = [...named].filter((f) => !existsSync(join(site, f)));
  row(named.size >= 9 && lost.length === 0, 'site.names',
      lost.length === 0 ? `${named.size} files named by index.html and main.js: ${[...named].sort().join(', ')}`
                        : `not in the site: ${lost.join(', ')}`);

  const links = ['licences/GPL-3.0.txt', 'licences/JetBrainsMono-OFL.txt', 'licences/THIRD-PARTY.txt'];
  const footer = found(index, /<footer>([\s\S]*?)<\/footer>/) || '';
  row(links.every((l) => footer.includes(`href="${l}"`)) && footer.includes('id="fcmp-built"'), 'site.footer',
      'the footer links the three licences and holds the built commit');
}

// ---- the licences: FCompressor's, the typeface's (from the FunkGui the build used), and the toolchain's texts ------
{
  const build = dirname(site);
  const cache = existsSync(join(build, 'CMakeCache.txt')) ? read(build, 'CMakeCache.txt') : '';
  row(same(join(source, 'LICENSE'), join(site, 'licences/GPL-3.0.txt')), 'licences.gpl',
      'licences/GPL-3.0.txt is LICENSE');
  const funkgui = found(cache, /^FunkGui_SOURCE_DIR:STATIC=(.+)$/m);
  const font = funkgui ? join(funkgui, 'fonts/JetBrainsMono-LICENSE.txt') : '';
  row(!!font && same(font, join(site, 'licences/JetBrainsMono-OFL.txt')), 'licences.typeface',
      font ? `licences/JetBrainsMono-OFL.txt is ${font}` : `no FunkGui_SOURCE_DIR in ${build}/CMakeCache.txt`);

  const toolchain = found(cache, /^CMAKE_TOOLCHAIN_FILE:[A-Z]+=(.+)\/cmake\/Modules\/Platform\/Emscripten\.cmake$/m);
  const third = existsSync(join(site, 'licences/THIRD-PARTY.txt')) ? read(site, 'licences/THIRD-PARTY.txt') : '';
  const parts = [['Emscripten', ['LICENSE', '../LICENSE']], ['musl', ['system/lib/libc/musl/COPYRIGHT']],
                 ['libc++', ['system/lib/libcxx/LICENSE.TXT']], ['libc++abi', ['system/lib/libcxxabi/LICENSE.TXT']],
                 ['compiler-rt', ['system/lib/compiler-rt/LICENSE.TXT']]];
  const short = parts.filter(([name, paths]) => {
    const path = toolchain ? paths.map((p) => join(toolchain, p)).find((p) => existsSync(p)) : undefined;
    return !third.includes(`\n${name}\n====`) || !path || !third.includes(readFileSync(path, 'utf8'));
  });
  let said = `no Emscripten toolchain in ${build}/CMakeCache.txt`;
  if (toolchain) {
    said = short.length === 0 ? `${parts.map(([name]) => name).join(', ')}: each text whole, from ${toolchain}`
                              : `not whole: ${short.map(([name]) => name).join(', ')}`;
  }
  row(!!toolchain && short.length === 0, 'licences.third_party', said);
}

// ---- wording -------------------------------------------------------------------------------------------------------
{
  // The text a reader sees: the body without its scripts and tags, and the texts its attributes carry.
  const body = found(index, /<body>([\s\S]*)<\/body>/) || '';
  const shown = body.replace(/<script\b[\s\S]*?<\/script>/g, ' ').replace(/<[^>]*>/g, ' ')
              + [...body.matchAll(/\b(?:data-[a-z]+|aria-label)="([^"]*)"/g)].map((m) => m[1]).join(' ');
  const lower = shown.match(/[^\s]*[a-z][^\s]*/g) || [];
  row(shown.trim().length > 400 && lower.length === 0, 'wording.html',
      lower.length === 0 ? 'the page\'s text is upper case' : `not upper case: ${lower.slice(0, 6).join(' ')}`);

  const said = Object.entries(page.SAY)
                     .map(([key, text]) => [key, typeof text === 'function' ? text('NAME', 'DAY') : text]);
  const mixed = said.filter(([, text]) => typeof text !== 'string' || text === '' || text !== text.toUpperCase())
                    .map(([key]) => key);
  row(mixed.length === 0, 'wording.main',
      mixed.length === 0 ? `${said.length} texts, upper case` : `not upper case: ${mixed.join(', ')}`);

  const list = found(index, /WHAT DIFFERS FROM THE PLUGIN<\/h2>\s*<ul>([\s\S]*?)<\/ul>/) || '';
  const differs = [...list.matchAll(/<li>/g)].length;
  row(differs === 10 && body.includes('BYPASS') && !/<button[^>]*>[^<]*BYPASS/.test(body), 'wording.differences',
      `${differs} differences listed; BYPASS is named in the text, and the page has no button of its own for it`);
}

// ---- refusals ------------------------------------------------------------------------------------------------------
{
  const all = { webAssembly: true, protocol: 'https:', secure: true, worklet: true, webgl2: true };
  const none = { webAssembly: false, protocol: 'file:', secure: false, worklet: false, webgl2: false };
  const cases = [['', {}], ['', { protocol: 'http:' }], ['noWebgl2', { webgl2: false }],
                 ['noWorklet', { worklet: false, webgl2: false }],
                 ['insecure', { secure: false, worklet: false, webgl2: false }],
                 ['file', { ...none, webAssembly: true }], ['noWebAssembly', none]];
  const wrong = cases.filter(([want, lacking]) => page.missing({ ...all, ...lacking }) !== want);
  const texts = ['noWebgl2', 'noWorklet', 'insecure', 'noWebAssembly'].every((key) => typeof page.SAY[key] === 'string')
             && /data-file="[^"]+"/.test(index);
  row(wrong.length === 0 && texts, 'refusals.order',
      wrong.length === 0 ? 'no WebAssembly, then a file: address, an insecure context, no AudioWorklet, no WebGL2; '
                           + 'each has its text'
                         : `wrong for: ${wrong.map(([want]) => want || 'nothing missing').join(', ')}`);

  // index.html's own words. Its classic script, the module tag's onerror and the nomodule script are run as a browser
  // would, against a document that is one status element and a title.
  const scripts = [...index.matchAll(/<script\b([^>]*)>([\s\S]*?)<\/script>/g)]
                    .map((m) => ({ tag: m[1], code: m[2] }));
  const moduleAt = scripts.findIndex((s) => /\btype="module"/.test(s.tag) && /\bsrc="main\.js"/.test(s.tag));
  const classic = scripts.slice(0, Math.max(moduleAt, 0)).map((s) => s.code).join('\n');
  const onerror = moduleAt < 0 ? undefined : found(scripts[moduleAt].tag, /\bonerror="([^"]*)"/);
  const nomodule = (scripts.find((s) => /\bnomodule\b/.test(s.tag)) || {}).code;
  const data = (name) => found(index, new RegExp(`id="fcmp-status"[^>]*\\bdata-${name}="([^"]*)"`));
  const visit = (protocol, pathname, search, steps) => {
    const status = { textContent: '', getAttribute: (name) => found(index, new RegExp(`\\b${name}="([^"]*)"`)) };
    const doc = { title: 'FCompressor: web demo', getElementById: (id) => (id === 'fcmp-status' ? status : null) };
    const listeners = [];
    const win = { addEventListener: (type, heard) => type === 'error' && listeners.push(heard) };
    let threw = '';
    try {
      const own = new Function('window', 'document', 'location', `${classic}
        return { lost: fcmpLost, booted: function () { fcmpBooted = true; } };`)(win, doc,
                                                                                { protocol, pathname, search });
      const run = (code) => new Function('fcmpLost', code)(own.lost);
      steps({ tag: () => run(onerror),
              old: () => run(nomodule),
              window: () => listeners.forEach((heard) => heard({})),
              boot: () => {
                own.booted();
                status.textContent = 'MAIN';
                doc.title = 'RUNNING';
              } });
    } catch (error) {
      threw = ` (${error})`;
    }
    return `${status.textContent} | ${doc.title}${threw}`;
  };
  const lost = `${data('lost')} | `;
  const plain = 'FCompressor: web demo';
  const fail = 'FAIL: the page did not load';
  const visits = [
    ['while it loads', visit('https:', '/demo/index.html', '', () => {}), `${page.SAY.loading} | ${plain}`],
    ['main.js or an import is not there', visit('https:', '/demo/', '', (v) => v.tag()), lost + plain],
    ['main.js does not parse', visit('https:', '/index.html', '', (v) => v.window()), lost + plain],
    ['the self-test, by its path', visit('http:', '/fcmp-ui.html', '', (v) => v.tag()), lost + fail],
    ['the self-test, a parse error', visit('http:', '/a/fcmp-ui.html', '', (v) => v.window()), lost + fail],
    ['the self-test, by the query', visit('http:', '/index.html', '?a=b&selftest=1', (v) => v.tag()), lost + fail],
    ['another query', visit('http:', '/index.html', '?selftest=10', (v) => v.tag()), lost + plain],
    ['a browser without module scripts', visit('https:', '/index.html', '', (v) => v.old()),
     `${data('old')} | ${plain}`],
    ['a file: address', visit('file:', '/Users/x/site/index.html', '', () => {}), `${data('file')} | ${plain}`],
    ['a file: address, the tag\'s error', visit('file:', '/site/index.html', '', (v) => v.tag()),
     `${data('file')} | ${plain}`],
    ['once main.js has booted', visit('http:', '/fcmp-ui.html', '', (v) => {
      v.boot();
      v.tag();
      v.window();
      v.old();
    }), 'MAIN | RUNNING'],
  ];
  const off = visits.filter(([, got, want]) => got !== want);
  const boots = /\nfunction boot\(\) \{\n\s*globalThis\.fcmpBooted = true;/.test(read(source, 'web/main.js'));
  row(moduleAt > 0 && !!onerror && !!nomodule && !!data('lost') && !!data('old') && !!data('file') && boots
      && off.length === 0, 'refusals.not_loaded',
      off.length === 0 ? `index.html says "${data('lost')}" by itself in ${visits.length} cases (a missing or broken `
                         + 'main.js or import, a browser without module scripts, a file: address), the self-test\'s '
                         + `title is "${fail}", and it says nothing once main.js has booted (boot() says so first: `
                         + `${boots})`
                       : off.map(([name, got, want]) => `${name}: "${got}", want "${want}"`).join('; '));
}

// ---- the editor's status and its failures --------------------------------------------------------------------------
{
  const context = 'the browser gave no WebGL2 context for \'#fcmp-canvas\'';       // FunkGui's WebGlSink.cpp
  const shader = 'the fragment shader did not compile: ERROR: 0:7: syntax error';
  const cases = [['', { ok: 1, error: '' }], ['', { ok: 1, error: 'stale' }], ['', { ok: 0, error: '' }],
                 ['', { ok: 0 }], ['noWebgl2', { ok: 0, error: context }], [shader, { ok: 0, error: shader }],
                 ['the program did not link: x', { ok: 0, error: 'the program did not link: x' }],
                 ['no canvas matches \'#fcmp-canvas\'', { ok: 0, error: 'no canvas matches \'#fcmp-canvas\'' }]];
  const wrong = cases.filter(([want, status]) => page.statusFault(status) !== want)
                     .map(([, status]) => `${JSON.stringify(status)} -> ${JSON.stringify(page.statusFault(status))}`);
  // The words statusFault() looks for are the sink's, in the FunkGui this build used.
  const build = dirname(site);
  const cache = existsSync(join(build, 'CMakeCache.txt')) ? read(build, 'CMakeCache.txt') : '';
  const funkgui = found(cache, /^FunkGui_SOURCE_DIR:STATIC=(.+)$/m);
  const sinkFile = funkgui ? join(funkgui, 'src/web/WebGlSink.cpp') : '';
  const sink = sinkFile && existsSync(sinkFile) ? read(sinkFile) : '';
  const sinkSays = sink.includes('"the browser gave no WebGL2 context for \'"');
  row(wrong.length === 0 && sinkSays, 'editor.status',
      wrong.length === 0 ? 'no WebGL2 only for the sink\'s "no WebGL2 context" (the build\'s WebGlSink.cpp '
                           + `${sinkSays ? 'says it' : 'DOES NOT SAY IT'}); any other reason is said as it is; ok 0 `
                           + 'without an error (a lost context) stops nothing'
                         : `wrong: ${wrong.join('; ')}`);

  const trap = new WebAssembly.RuntimeError('unreachable');
  const thrown = new TypeError('x is not a function');
  const at = (message, where) => Object.assign(new Error(message), { stack: `Error: ${message}\n    at ${where}` });
  const glue = at('in a frame', 'http://h/demo/fcmp-ui.js:1:2');
  const wasm = at('in wasm', 'http://h/fcmp-ui.wasm:wasm-function[7]:0x1');
  const own = at('the page', 'http://h/demo/main.js:1:2');
  const exit = { name: 'ExitStatus', message: 'Program terminated with exit(1)', status: 1 };
  const faults = [
    ['TypeError: x is not a function', false, { message: 'Uncaught TypeError: x is not a function', error: thrown }],
    ['Error: the page', false, { error: own }],
    ['ExitStatus: Program terminated with exit(1)', false, { error: exit }],
    ['a string', false, { error: 'a string' }],
    ['Script error.', false, { message: 'Script error.', error: null }],
    ['no reason given', false, { error: undefined }],
    ['RuntimeError: unreachable', true, { error: trap }],
    ['Error: in a frame', true, { error: glue }],
    ['Error: in wasm', true, { error: wasm }],
    ['TypeError: x is not a function', true, { message: 'Uncaught', filename: 'http://h/a/fcmp-ui.js', error: thrown }],
    ['Script error.', true, { message: 'Script error.', filename: 'http://h/fcmp-ui.js?v=2', error: null }],
    ['', true, { error: own }],
    ['', true, { message: 'Uncaught', filename: 'http://h/demo/main.js', error: thrown }],
    ['', true, { message: 'Uncaught', filename: 'http://h/not-fcmp-ui.json', error: 'a string' }],
    ['', true, { error: undefined }],
  ];
  const missed = faults.filter(([want, ready, what]) => page.editorFault(ready, what) !== want)
                       .map(([want, ready, what]) => `${ready ? 'after' : 'before'} ready, want "${want}", got `
                                                     + `"${page.editorFault(ready, what)}"`);
  row(missed.length === 0, 'editor.uncaught',
      missed.length === 0 ? `${faults.length} cases: before the editor is ready anything uncaught is its failure, with `
                            + 'the reason; afterwards a wasm trap and what its two files threw, and nothing of the '
                            + 'page\'s own'
                          : missed.join('; '));
}

// ---- the self-test's pixel rule ------------------------------------------------------------------------------------
{
  const main = read(source, 'web/main.js');
  // The numbers are FunkGui's for its own sink's page, in the FunkGui this build used, and "over 2" is what the
  // editor module counts.
  const build = dirname(site);
  const cache = existsSync(join(build, 'CMakeCache.txt')) ? read(build, 'CMakeCache.txt') : '';
  const funkgui = found(cache, /^FunkGui_SOURCE_DIR:STATIC=(.+)$/m);
  const sinkPage = funkgui && existsSync(join(funkgui, 'test/web/page.cpp')) ? read(funkgui, 'test/web/page.cpp') : '';
  const theirs = { tolerance: Number(found(sinkPage, /\bkTolerance = (\d+);/)),
                   worst: Number(found(sinkPage, /\bkWorst = (\d+);/)),
                   perMille: Number(found(sinkPage, /\bkOverPerMille = ([\d.]+);/)) };
  const counted = Number(found(read(source, 'Source/web/ui/WebMain.cpp'), /\bkPixelTolerance = (\d+);/));
  row(page.SOFTWARE_PIXELS.worst === theirs.worst && page.SOFTWARE_PIXELS.perMille === theirs.perMille
      && theirs.tolerance === 2 && counted === 2, 'pixels.bound',
      `main.js: no sample over ${page.SOFTWARE_PIXELS.worst}, at most ${page.SOFTWARE_PIXELS.perMille} per mille over `
      + `2; the build's FunkGui (test/web/page.cpp): kWorst ${theirs.worst}, kOverPerMille ${theirs.perMille}, `
      + `kTolerance ${theirs.tolerance}; WebMain.cpp counts the samples over ${counted}`);

  // The renderer's class is its name's. The first name of each list is what headless Chrome 154 said on an Apple M5
  // (--use-angle=swiftshader, --use-angle=metal); the others are names of the kind, not measurements.
  const software = ['ANGLE (Google, Vulkan 1.3.0 (SwiftShader Device (LLVM 10.0.0) (0x0000C0DE)), SwiftShader driver)',
                    'Google SwiftShader', 'llvmpipe (LLVM 15.0.7, 256 bits)', 'Mesa/X.org, llvmpipe, or similar',
                    'Gallium 0.4 on softpipe', 'Apple Software Renderer', 'Software Rasterizer',
                    'ANGLE (Microsoft, Microsoft Basic Render Driver Direct3D11 vs_5_0 ps_5_0, D3D11)', 'LLVMPIPE'];
  const gpus = ['ANGLE (Apple, ANGLE Metal Renderer: Apple M5, Unspecified Version)', 'Apple GPU', 'WebKit WebGL',
                'ANGLE (NVIDIA, NVIDIA GeForce RTX 3060 Direct3D11 vs_5_0 ps_5_0, D3D11)', 'Mali-G78',
                'ANGLE (Intel, Mesa Intel(R) UHD Graphics 630 (CFL GT2), OpenGL 4.6)', 'A RENDERER NOBODY HAS HEARD OF',
                '', undefined, null];
  const misread = [...software.filter((name) => !page.softwareRenderer(name)),
                   ...gpus.filter((name) => page.softwareRenderer(name))];
  row(misread.length === 0, 'pixels.renderer',
      misread.length === 0 ? `${software.length} names are software (SwiftShader, llvmpipe, softpipe, a software `
                             + `rasteriser, in any letter case); ${gpus.length} are not, an unknown name and no name `
                             + 'among them'
                           : `wrong for: ${misread.map((name) => JSON.stringify(name)).join(', ')}`);

  // Both sides of each number, in a frame of the editor's size at 100 % (960 x 640, four channels: what headless
  // Chrome reads back).
  const samples = 960 * 640 * 4;
  const frame = (largest, over2, more = {}) => ({ frames: 1, largest, over2, samples, ...more });
  const metal = gpus[0];
  const swift = software[0];
  const wrong = (cases, renderer) => cases.filter(([want, pixels]) => page.pixelRule(pixels, renderer).ok !== want)
                                          .map(([want, pixels]) => `${JSON.stringify(pixels)} is ${want ? 'not ' : ''}`
                                                                   + 'passed');
  const gpuCases = [[true, frame(0, 0)], [true, frame(1, 0)], [true, frame(2, 0)], [false, frame(3, 1)],
                    [false, frame(7, 1600)], [false, frame(255, samples)]];
  const gpuWrong = wrong(gpuCases, metal);
  row(gpuWrong.length === 0 && page.pixelRule(frame(1, 0), metal).rule === 'a GPU: none over 2', 'pixels.gpu',
      gpuWrong.length === 0 ? 'a GPU: a frame within 2 of 255 everywhere passes; one sample over 2 fails, and so does '
                              + 'what SwiftShader draws (7, 1600 over 2)'
                            : gpuWrong.join('; '));

  const most = samples / 100;                                    // 10 per mille of the samples, exactly
  const softCases = [[true, frame(0, 0)], [true, frame(7, 1600)], [true, frame(16, 1600)], [false, frame(17, 1)],
                     [false, frame(17, 0)], [true, frame(16, most)], [false, frame(16, most + 1)],
                     [false, frame(3, most + 1)], [false, frame(255, samples)]];
  const softWrong = wrong(softCases, swift);
  const said = page.pixelRule(frame(7, 1600), swift);
  row(softWrong.length === 0 && Number.isInteger(most) && said.software === true
      && Math.abs(said.share - 1000 * 1600 / samples) < 1e-12
      && said.rule === 'software: at most 16, and 10 per mille over 2', 'pixels.software',
      softWrong.length === 0 ? `software: 16 of 255 passes and 17 fails; ${most} of ${samples} samples over 2 (10 per `
                               + `mille) pass and ${most + 1} fail; SwiftShader's 7 with 1600 over 2 is `
                               + `${said.share.toFixed(2)} per mille and passes`
                             : softWrong.join('; '));

  // A renderer nobody named is held to the GPU's rule, whatever it draws.
  const unknown = ['A RENDERER NOBODY HAS HEARD OF', '', undefined];
  const lenient = unknown.filter((name) => page.pixelRule(frame(7, 1600), name).ok
                                           || page.pixelRule(frame(3, 1), name).ok
                                           || !page.pixelRule(frame(2, 0), name).ok
                                           || page.pixelRule(frame(2, 0), name).software);
  row(lenient.length === 0 && page.pixelRule(frame(7, 1600), swift).ok, 'pixels.unknown_is_a_gpu',
      lenient.length === 0 ? 'an unknown renderer and one with no name are judged as a GPU: the frame that passes as '
                             + 'software (7, 1600 over 2) fails there'
                           : `judged as software: ${lenient.map((name) => JSON.stringify(name)).join(', ')}`);

  // No frame, no pass: the module says frames 0 where it drew or read nothing (a hidden document, a lost context),
  // and its other numbers are then zeros, which would pass any bound.
  const nothing = [frame(0, 0, { frames: 0 }), frame(0, 0, { frames: 0, samples: 0 }), frame(0, 0, { samples: 0 }),
                   { frames: 1, largest: 0, over2: 0 }, { frames: 1, largest: 0, samples },
                   { largest: 0, over2: 0, samples }, {}];
  const passedEmpty = [metal, swift].flatMap((name) => nothing.filter((pixels) => page.pixelRule(pixels, name).ok));
  row(passedEmpty.length === 0, 'pixels.no_frame',
      passedEmpty.length === 0 ? `${nothing.length} answers with no frame, no samples or a number missing: none `
                                 + 'passes, as a GPU or as software'
                               : `passed: ${passedEmpty.map((pixels) => JSON.stringify(pixels)).join('; ')}`);

  // The frame is a still one: the self-test asks the editor before it starts the demo, and says so. The page's row is
  // pixelRule()'s verdict and nothing else.
  const selftest = main.slice(main.indexOf('async function runSelftest()'));
  const asks = [...selftest.matchAll(/JSON\.parse\(Module\.fcmpSelftest\(\)\)/g)].map((m) => m.index);
  const starts = [...selftest.matchAll(/await within\(\d+, 'the start', start\(\)\)/g)].map((m) => m.index);
  const before = asks.length === 1 && starts.length === 1 && asks[0] < starts[0];
  const theRow = 'row(judged.ok, \'editor.pixels\',';
  const says = selftest.includes(theRow)
            && /^\s*judged\.read \? `\$\{pixels\.frames\} still frame\(s\), before START,/
                 .test(selftest.slice(selftest.indexOf(theRow) + theRow.length));
  const judged = selftest.includes('const judged = pixelRule(pixels, renderer);') && says;
  row(before && judged, 'pixels.still_frame',
      `runSelftest() asks Module.fcmpSelftest() ${asks.length} time(s) and starts the demo ${starts.length} time(s), `
      + `the question ${before ? 'before' : 'NOT BEFORE'} the start; editor.pixels is pixelRule()'s verdict and says `
      + `"still frame(s), before START": ${judged}`);
}

// ---- files ---------------------------------------------------------------------------------------------------------
{
  const refusals = ['tooLarge', 'tooShort', 'tooLong', 'notAudio'];
  row(page.fileRefusal(page.MAX_FILE_BYTES) === '' && page.fileRefusal(page.MAX_FILE_BYTES + 1) === 'tooLarge'
      && page.lengthRefusal(page.MIN_FILE_SECONDS) === '' && page.lengthRefusal(0.0999) === 'tooShort'
      && page.lengthRefusal(0) === 'tooShort' && page.lengthRefusal(NaN) === 'tooShort'
      && page.lengthRefusal(page.MAX_FILE_SECONDS) === ''
      && page.lengthRefusal(page.MAX_FILE_SECONDS + 0.001) === 'tooLong'
      && refusals.every((key) => page.SAY[key]('X.WAV').includes('THE SOURCE IS UNCHANGED')), 'files.limits',
      `${page.MAX_FILE_BYTES / 1048576} MB, ${page.MIN_FILE_SECONDS} s to ${page.MAX_FILE_SECONDS} s; every refusal `
      + 'says the source is unchanged');

  const ones = new Float32Array(48000).fill(1);
  page.fadeEnds(ones, 48000);
  let rising = true;
  for (let i = 1; i <= 240; i += 1) rising = rising && ones[i] >= ones[i - 1] && ones[48000 - 1 - i] >= ones[48000 - i];
  const tiny = new Float32Array(7).fill(1);
  page.fadeEnds(tiny, 48000);
  row(ones[0] === 0 && ones[47999] === 0 && rising && ones[239] < 1 && ones[240] === 1 && ones[47759] === 1
      && ones[24000] === 1 && tiny[0] === 0 && tiny[6] === 0 && tiny[3] === 1 && tiny.every((s) => s >= 0 && s <= 1),
      'files.fades', '5 ms in and out at 48 kHz, zero at both ends, untouched between; a buffer shorter than two '
      + 'fades stays in range');
}

// ---- built-from ----------------------------------------------------------------------------------------------------
{
  const sha = '7319ac752200f7ce166457e6a1de8e68fb35d157';
  const when = '2026-10-02T04:11:02Z';
  const clean = page.builtFrom(`site ${sha} clean ${when}\n`);
  const dirty = page.builtFrom(`site ${sha} dirty ${when}\n`);
  const none = page.builtFrom(`site none dirty ${when}\n`);
  const others = ['', `probes ${sha} clean ${when}`, '<html>404</html>', `site ${sha} clean`].map(page.builtFrom);
  row(!!clean && clean.href === `https://github.com/Snipet/FCompressor/tree/${sha}`
      && clean.text.includes('7319AC752200') && clean.text.includes('2026-10-02') && !!dirty && dirty.href === ''
      && dirty.text === ' BUILT FROM COMMIT 7319AC752200 PLUS LOCAL CHANGES ON 2026-10-02.' && none === null
      && others.every((o) => o === null), 'built_from.link_only_when_clean',
      `clean: "${clean && clean.text.trim()}" -> ${clean && clean.href}; dirty: "${dirty && dirty.text.trim()}", `
      + `${(dirty && dirty.href) || 'no link'}; none: ${none === null ? 'nothing' : JSON.stringify(none)}`);
}

// ---- the verdict ---------------------------------------------------------------------------------------------------
{
  const run = (steps) => {
    const titles = [];
    const lines = [];
    const verdict = new page.Verdict('t', (title, line) => {
      titles.push(title);
      if (line) lines.push(line);
    });
    steps(verdict);
    return { titles, lines, last: titles[titles.length - 1], all: titles.join(' > ') };
  };
  const pass = run((v) => {
    v.row(true, 'a', 'detail');
    v.note('n');
    v.row(true, 'b');
    v.finish();
  });
  row(pass.all === 'RUNNING > RUNNING > RUNNING > RUNNING > PASS'
      && pass.lines.join('|') === 'PASS     t a: detail|NOTE     n|PASS     t b', 'verdict.running_then_pass',
      pass.all);

  const rows = run((v) => {
    v.row(true, 'a');
    v.row(false, 'second');
    v.row(false, 'third');
    v.row(true, 'd');
    v.finish();
  });
  row(rows.last === 'FAIL: second' && rows.titles.slice(0, -1).every((t) => t === 'RUNNING') && rows.lines.length === 4,
      'verdict.first_failing_row', `${rows.all}: the rows after it are still logged`);

  const early = run((v) => {
    v.row(true, 'a');
    v.uncaught('error: x is not defined');
    v.row(true, 'b');
    v.row(true, 'c');
    v.finish();
  });
  row(early.all === 'RUNNING > RUNNING > FAIL: uncaught > FAIL: uncaught > FAIL: uncaught > FAIL: uncaught'
      && early.lines[1] === 'FAIL     t uncaught: error: x is not defined', 'verdict.uncaught_fails_at_once',
      early.all);

  const late = run((v) => {
    v.row(true, 'a');
    v.finish();
    v.uncaught('rejection: late');
    v.row(true, 'b');
    v.finish();
  });
  const named = run((v) => {
    v.row(false, 'first');
    v.uncaught('error: later');
    v.row(true, 'b');
    v.finish();
  });
  row(late.all === 'RUNNING > RUNNING > PASS > FAIL: uncaught > FAIL: uncaught > FAIL: uncaught'
      && named.all === 'RUNNING > RUNNING > FAIL: first > FAIL: first > FAIL: first', 'verdict.fail_is_never_replaced',
      `after a PASS: ${late.all}; a failing row before an uncaught error keeps its name: ${named.all}`);
}

note('the page in a browser: node <build>/_deps/funkgui-src/tools/web/check-page.mjs <build>/site --page fcmp-ui');
console.log(`${failed === 0 ? 'PASS' : 'FAIL'}     ${TEST}: ${passed} row(s) passed, ${failed} failed`);
process.exit(failed === 0 ? 0 : 1);
