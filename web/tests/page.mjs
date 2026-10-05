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
//                  protocol's numbers (WebProtocol.h, WebEngine.h), one block size in the page and the worklet, and
//                  the sample loop's SHA-256, rate and frames (web/audio/loop.wav, read by web/sample.js)
//   site.*         the site's page files are the repository's (fcmp-ui.html is index.html), sample.js and the sample
//                  loop among them; every file index.html, main.js and sample.js name is in the site; the footer
//                  links the licences and says whose the sample loop is
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
//                  the Panel's rest (Module.fcmpA11y's fullRate 0, bounded; a quiet time where the module does not
//                  say); and the frame is asked for at rest, in that task, before the demo starts, which the row says
//   files.*        the limits of a dropped file, and the fades at its ends
//   source.*       the sample loop and the synth loop (docs/sprints/web-loop.md, "The page"): what the page says of
//                  them, and nothing of the old name; index.html's controls, in their order; which buttons are
//                  enabled, in every state; the time the sample loop has, under the self-test's time for START; the
//                  self-test's rule for the fitted loop, on the file and on five wrong fits; the self-test's rows in
//                  their order; START makes and resumes its context, and asks for the loop, before it first waits;
//                  and a load after a failed one asks the server, not the browser's cache
//   built_from.*   the footer's line: a link only for `clean`, nothing for `none` or an unreadable line
//   verdict.*      the self-test's title: RUNNING, then PASS or FAIL: <the first failing row>; an uncaught error is a
//                  FAIL at once; no PASS ever replaces a FAIL
// Output: PASS/FAIL/NOTE lines, as the probes print them. Exit 0 pass, 1 fail, 2 usage.
import { createHash } from 'node:crypto';
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
const sample = await import(pathToFileURL(join(source, 'web', 'sample.js')).href);
const index = read(source, 'web/index.html');
// The sample loop's file, as the repository holds it and as sample.js reads it.
const wav = readFileSync(join(source, 'web', sample.SAMPLE_URL));
const loop = sample.readWav(wav);

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

  const sha = createHash('sha256').update(wav).digest('hex');
  row(page.SAMPLE_SHA256 === sha && page.SAMPLE_RATE === loop.sampleRate && page.SAMPLE_FRAMES === loop.frames,
      'constants.sample',
      `main.js ${page.SAMPLE_SHA256}, ${page.SAMPLE_RATE} Hz, ${page.SAMPLE_FRAMES} frames; web/${sample.SAMPLE_URL} `
      + `${sha}, ${loop.sampleRate} Hz, ${loop.frames} frames`);
}

// ---- the site ------------------------------------------------------------------------------------------------------
const same = (a, b) => existsSync(a) && existsSync(b) && readFileSync(a).equals(readFileSync(b));
{
  const pageFiles = ['index.html', 'demo.css', 'main.js', 'loop.js', 'sample.js', 'fcmp-worklet.js',
                     sample.SAMPLE_URL];
  const stale = pageFiles.filter((f) => !same(join(source, 'web', f), join(site, f)));
  if (!same(join(source, 'web/index.html'), join(site, 'fcmp-ui.html'))) stale.push('fcmp-ui.html (index.html again)');
  row(stale.length === 0, 'site.is_the_source',
      stale.length === 0 ? `${pageFiles.length + 1} files` : `not the repository's: ${stale.join(', ')}`);
  row(!existsSync(join(site, 'package.json')) && !existsSync(join(site, 'tests')), 'site.no_tests',
      'no package.json, no tests');

  // Every file the page names: index.html's attributes, main.js's imports and strings that name a file, and the
  // sample loop, which sample.js names.
  const named = new Set([sample.SAMPLE_URL]);
  for (const m of index.matchAll(/\b(?:src|href)\s*=\s*"([^"]*)"/g)) if (!/^[a-z]+:/i.test(m[1])) named.add(m[1]);
  const main = read(source, 'web/main.js');
  for (const m of main.matchAll(/'(?:\.\/)?([\w\-/]+\.(?:js|wasm|txt|css|html))'/g)) named.add(m[1]);
  const lost = [...named].filter((f) => !existsSync(join(site, f)));
  row(named.size >= 11 && named.has('sample.js') && named.has('audio/loop.wav') && lost.length === 0, 'site.names',
      lost.length === 0 ? `${named.size} files named by index.html, main.js and sample.js: `
                          + [...named].sort().join(', ')
                        : `not in the site: ${lost.join(', ')}`);

  const links = ['licences/GPL-3.0.txt', 'licences/JetBrainsMono-OFL.txt', 'licences/THIRD-PARTY.txt'];
  const footer = found(index, /<footer>([\s\S]*?)<\/footer>/) || '';
  row(links.every((l) => footer.includes(`href="${l}"`)) && footer.includes('id="fcmp-built"'), 'site.footer',
      'the footer links the three licences and holds the built commit');
  // The footer's first sentence, as a reader sees it, and its link.
  const first = 'FCOMPRESSOR IS FREE SOFTWARE UNDER THE GNU GPL VERSION 3, AND SO IS THE SAMPLE LOOP, WHICH SEAN FUNK '
              + 'MADE FOR THIS DEMO.';
  const seen = footer.replace(/<[^>]*>/g, '').replace(/\s+/g, ' ').trim();
  row(seen.startsWith(`${first} `) && footer.includes('<a href="licences/GPL-3.0.txt">GNU GPL VERSION 3</a>'),
      'site.footer.loop', `the footer begins "${seen.slice(0, first.length)}", with the link on GNU GPL VERSION 3`);
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

  // The Panel at rest (untilRest()), on a clock of its own: Module.fcmpA11y's fullRate asked every REST.step ms until
  // it is 0, for at most REST.bound ms; a module that does not say gets REST.quiet ms, and the answer says so.
  const rested = async (module) => {
    let t = 0;
    const r = await page.untilRest(module, async (ms) => { t += ms; }, () => t);
    return { ...r, t };
  };
  const answering = (rates) => {
    const m = { asked: 0 };
    m.fcmpA11y = () => JSON.stringify({ screen: 0, fullRate: rates[Math.min(m.asked++, rates.length - 1)] });
    return m;
  };
  const { step, bound, quiet } = page.REST;
  const settles = answering([1, 1, 1, 0]);
  const atOnce = answering([0]);
  const never = answering([1]);
  const unsaid = { fcmpA11y: () => '{"screen":0}' };
  const cases = [
    ['rests after three answers', await rested(settles), { still: true, ms: 3 * step, how: 'fullRate' }, settles, 4],
    ['at rest at once', await rested(atOnce), { still: true, ms: 0, how: 'fullRate' }, atOnce, 1],
    ['never rests', await rested(never), { still: false, ms: bound, how: 'fullRate' }, never, bound / step + 1],
    ['no fcmpA11y', await rested({}), { still: true, ms: quiet, how: 'quiet' }, null, 0],
    ['no fullRate in it', await rested(unsaid), { still: true, ms: quiet, how: 'quiet' }, null, 0],
  ];
  const restWrong = cases.filter(([, got, want, m, asked]) => got.still !== want.still || got.ms !== want.ms
                                                              || got.how !== want.how || got.t !== want.ms
                                                              || (m !== null && m.asked !== asked))
                         .map(([what, got]) => `${what}: ${JSON.stringify(got)}`);
  row(restWrong.length === 0 && bound > 6000 && quiet > 6000 && step > 0, 'pixels.rest',
      restWrong.length === 0 ? `untilRest(): fullRate asked every ${step} ms until it is 0 (at once when it is), for `
                               + `at most ${bound} ms, and then not at rest; a module with no fcmpA11y or no fullRate `
                               + `in it gets a quiet time of ${quiet} ms (both longer than the first-use hint's 6 s)`
                             : restWrong.join('; '));

  // The frame is a still one: the self-test waits for the Panel's rest and asks the editor in the same task, before it
  // starts the demo, and says so. The page's row is pixelRule()'s verdict on a frame of a Panel at rest.
  const selftest = main.slice(main.indexOf('async function runSelftest()'));
  const asks = [...selftest.matchAll(/JSON\.parse\(Module\.fcmpSelftest\(\)\)/g)].map((m) => m.index);
  const rests = [...selftest.matchAll(/const rest = await untilRest\(Module, sleep, /g)].map((m) => m.index);
  const starts = [...selftest.matchAll(/await within\(\d+, 'the start', start\(\)\)/g)].map((m) => m.index);
  const before = asks.length === 1 && starts.length === 1 && asks[0] < starts[0];
  const waits = rests.length === 1 && asks.length === 1 && rests[0] < asks[0]
             && !/\bawait\b/.test(selftest.slice(rests[0] + 'const rest = await'.length, asks[0]));
  const theRow = 'row(judged.ok && rest.still, \'editor.pixels\',';
  const rowText = selftest.slice(selftest.indexOf(theRow) + theRow.length);
  const says = selftest.includes(theRow)
            && selftest.includes('const atRest = rest.how === \'quiet\' ? `after a quiet time of ${rest.ms} ms` '
                                 + ': \'the Panel at rest\';')
            && /^[^;]*: `\$\{pixels\.frames\} still frame\(s\), before START, \$\{atRest\},/.test(rowText);
  const judged = selftest.includes('const judged = pixelRule(pixels, renderer);') && says;
  row(before && waits && judged, 'pixels.still_frame',
      `runSelftest() asks Module.fcmpSelftest() ${asks.length} time(s) and starts the demo ${starts.length} time(s), `
      + `the question ${before ? 'before' : 'NOT BEFORE'} the start; it waits for untilRest() ${rests.length} time(s), `
      + `${waits ? 'before the question and in its task' : 'NOT JUST BEFORE THE QUESTION'}; editor.pixels is `
      + 'pixelRule()\'s verdict on a Panel at rest and says "still frame(s), before START, the Panel at rest" (or '
      + `"after a quiet time" where the module does not say): ${judged}`);
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

// ---- the sample loop and the synth loop ----------------------------------------------------------------------------
{
  const main = read(source, 'web/main.js');
  // What the page says of the two loops, word for word (docs/sprints/web-loop.md, "The page"), and nothing of the
  // name the synth loop had.
  const texts = [['sample', 'SAMPLE LOOP'], ['synth', 'SYNTH LOOP'], ['sampleLoading', 'LOADING THE SAMPLE LOOP.'],
                 ['sampleLost', 'THE SAMPLE LOOP DID NOT LOAD. THE SYNTH LOOP PLAYS INSTEAD.'],
                 ['sampleUnchanged', 'THE SAMPLE LOOP DID NOT LOAD. THE SOURCE IS UNCHANGED.']];
  const off = texts.filter(([key, text]) => page.SAY[key] !== text).map(([key]) => key);
  const old = [['index.html', index], ['main.js', main], ['demo.css', read(source, 'web/demo.css')]]
                .filter(([, text]) => /built-?in/i.test(text)).map(([name]) => name);
  row(off.length === 0 && page.SAY.source(page.SAY.sample) === 'SOURCE: SAMPLE LOOP' && !('loop' in page.SAY)
      && old.length === 0, 'source.texts',
      off.length + old.length === 0 ? `${texts.length} texts as the manifest gives them; no file of the page says `
                                      + 'BUILT-IN'
                                    : `not as the manifest gives them: [${off.join(', ')}]; BUILT-IN is still said `
                                      + `in: [${old.join(', ')}]`);

  // index.html's source line: the name, the three buttons, the file input, the sentence about dropping a file and
  // the notice, in this order. The buttons are disabled until main.js enables them.
  const block = found(index, /<div id="fcmp-source">([\s\S]*?)<\/div>/) || '';
  const parts = [...block.matchAll(/<(span|button|input|p)\b([^>]*)>([^<]*)/g)].map((m) => {
    const id = found(m[2], /\bid="([^"]*)"/) || '';
    return `${m[1]}#${id}${/\bdisabled\b/.test(m[2]) ? ' disabled' : ''}${/\bhidden\b/.test(m[2]) ? ' hidden' : ''}`
           + `: ${m[3].trim()}`;
  });
  const want = [`span#fcmp-source-name: ${page.SAY.source(page.SAY.sample)}`,
                `button#fcmp-loop disabled: ${page.SAY.sample}`, `button#fcmp-synth disabled: ${page.SAY.synth}`,
                'button#fcmp-open disabled: OPEN AN AUDIO FILE', 'input#fcmp-file hidden: ',
                'span#: OR DROP ONE ON THE PAGE. IT STAYS IN THIS BROWSER.', 'p#fcmp-notice: '];
  row(parts.length === want.length && parts.every((part, i) => part === want[i]), 'source.controls',
      `#fcmp-source holds: ${parts.join(' | ')}`);

  // Which buttons are enabled, in every state: [the demo runs, what plays, the sample loop is loaded] and the
  // buttons that can be pressed. Nothing plays ('') only before START; the sample loop cannot play unloaded, and the
  // rule answers for it all the same.
  const states = [
    [false, '', false, ''], [false, '', true, ''], [false, 'sample', false, ''], [false, 'sample', true, ''],
    [false, 'synth', false, ''], [false, 'synth', true, ''], [false, 'file', false, ''], [false, 'file', true, ''],
    [true, 'sample', true, 'synth open'], [true, 'synth', true, 'loop open'], [true, 'synth', false, 'loop open'],
    [true, 'file', true, 'loop synth open'], [true, 'file', false, 'loop synth open'],
    [true, 'sample', false, 'loop synth open'], [true, '', false, 'loop synth open'],
    [true, '', true, 'loop synth open'],
  ];
  const pressable = (on) => ['loop', 'synth', 'open'].filter((key) => on[key] === true).join(' ');
  const strict = (on) => Object.keys(on).sort().join(' ') === 'loop open synth'
                         && Object.values(on).every((value) => typeof value === 'boolean');
  const wrong = states.filter(([running, kind, loaded, enabled]) => {
    const on = page.sourceButtons(running, kind, loaded);
    return !strict(on) || pressable(on) !== enabled;
  }).map(([running, kind, loaded]) => `${running ? 'running' : 'not running'}, ${kind || 'nothing'} plays, the `
                                      + `sample loop ${loaded ? 'loaded' : 'not loaded'}: `
                                      + `"${pressable(page.sourceButtons(running, kind, loaded))}"`);
  row(wrong.length === 0, 'source.buttons',
      wrong.length === 0 ? `${states.length} states: none before START or once the demo has ended; while it runs a `
                           + 'file can be opened, the loop that plays has its button disabled and the other enabled, '
                           + 'and SAMPLE LOOP is enabled whenever the sample loop is not loaded'
                         : `wrong for: ${wrong.join('; ')}`);

  // The sample loop's time is under the time the self-test gives START, so a loop that never comes still lets START
  // end in time, with the synth loop. The fetch is given up by that timer.
  const selftest = main.slice(main.indexOf('async function runSelftest()'));
  const startMs = Number(found(selftest, /await within\((\d+), 'the start', start\(\)\)/));
  const gives = main.includes('const timer = setTimeout(() => abort.abort(), SAMPLE_MS);')
             && /await fetch\(SAMPLE_URL, \{ signal: abort\.signal,[^}]*\}\);/.test(main);
  row(page.SAMPLE_MS === 15000 && startMs >= page.SAMPLE_MS + 5000 && gives, 'source.wait',
      `the sample loop has ${page.SAMPLE_MS} ms and its fetch is ${gives ? 'given up then' : 'NOT GIVEN UP THEN'}; `
      + `the self-test gives START ${startMs} ms`);

  // The self-test's rule for the fitted loop: the file fitted to 48 kHz passes, and a loop that is short of a frame,
  // louder by 0.02 dB on one side, not silent at its end, or empty does not.
  const fitted = sample.fitLoop(loop, 48000);
  const copy = (from, change) => {
    const made = { sampleRate: from.sampleRate, frames: from.frames, left: from.left.slice(),
                   right: from.right.slice() };
    change(made);
    return made;
  };
  const gain = (db) => (made) => { made.left = made.left.map((v) => v * 10 ** (db / 20)); };
  const cases = [
    ['the file fitted to 48000 Hz', true, fitted],
    ['the file at its own rate', true, sample.fitLoop(loop, loop.sampleRate)],
    ['one side louder by 0.005 dB', true, copy(fitted, gain(0.005))],
    ['one side louder by 0.02 dB', false, copy(fitted, gain(0.02))],
    ['one side quieter by 0.02 dB', false, copy(fitted, gain(-0.02))],
    ['one frame short', false, copy(fitted, (made) => {
      made.frames -= 1;
      made.left = made.left.slice(0, -1);
      made.right = made.right.slice(0, -1);
    })],
    ['-40 dBFS in its last millisecond', false, copy(fitted, (made) => made.right.fill(0.01, made.frames - 48))],
    ['no frame', false, { sampleRate: 48000, frames: 0, left: new Float32Array(0), right: new Float32Array(0) }],
  ];
  const misjudged = cases.filter(([, want2, made]) => page.fitRule(loop, made).ok !== want2).map(([what]) => what);
  const judged = page.fitRule(loop, fitted);
  const two = (values, digits) => values.map((d) => d.toFixed(digits)).join(' and ');
  row(misjudged.length === 0 && judged.frames === 371614 && fitted.frames === 371614 && page.FIT.level === 0.01
      && page.FIT.end === -60, 'source.fit_rule',
      misjudged.length === 0 ? `fitRule(): ${fitted.frames} frames at 48000 Hz, the RMS ${two(judged.level, 5)} dB `
                               + `from the file's (at most ${page.FIT.level}), the last 1 ms at ${two(judged.end, 1)} `
                               + `dBFS (under ${page.FIT.end}); ${cases.filter(([, ok]) => !ok).length} wrong fits do `
                               + 'not pass'
                             : `misjudged: ${misjudged.join('; ')}`);

  // The self-test's rows, in the manifest's order: the sample loop's two after engine.silence and before the
  // editor's, page.source after page.start. worklet.render and engine.silence keep the synth loop.
  const order = ['\'worklet.render\'', '\'engine.silence\'', '\'sample.read\'', '\'sample.fit\'', '\'editor.atlas\'',
                 '\'editor.pixels\'', '\'page.start\'', '\'page.source\'', '\'page.status\''];
  const places = order.map((name) => selftest.indexOf(name));
  const inOrder = places.every((at, i) => at >= 0 && (i === 0 || at > places[i - 1]));
  const synth = selftest.indexOf('const loop = synthLoop(48000);');
  const reads = selftest.includes('fetch(SAMPLE_URL)') && selftest.includes('crypto.subtle.digest(\'SHA-256\', bytes)')
             && selftest.includes('sha === SAMPLE_SHA256') && selftest.includes('fitRule(file, fitted)')
             && selftest.includes('globalThis.fcmpPage.source()');
  row(inOrder && synth >= 0 && synth < places[0] && reads, 'source.selftest',
      `runSelftest() names its rows in the order ${order.join(', ').replace(/'/g, '')}: ${inOrder}; the first two use `
      + `synthLoop(48000): ${synth >= 0 && synth < places[0]}; sample.read holds the fetched bytes' SHA-256 and `
      + `page.source asks fcmpPage.source(): ${reads}`);

  // START: the context is made and resumed inside the click, and the sample loop asked for, before the first await.
  // What the load does after its own awaits (readWav, fitLoop) can then delay neither.
  const between = (from, to) => main.slice(main.indexOf(from), main.indexOf(to));
  const start = between('  const start = async () => {', '  function contextChanged() {');
  const at = ['new AudioContext(', 'context.resume()', 'const sample = loadSample();', 'await ']
               .map((text) => start.indexOf(text));
  const load = between('  const loadSample = () => {', '  const synth = () => {');
  const fits = load.indexOf('fitLoop(');
  const awaits = load.indexOf('await fetch(');
  const calls = main.slice(0, main.indexOf('async function runSelftest()')).split('fitLoop(').length - 1;
  const inOrderToo = at.every((place, i) => place >= 0 && (i === 0 || place > at[i - 1]));
  const caught = /\}\)\(\)\.catch\(\(\) => \{\s*sampleFailed = true;\s*return null;\s*\}\)\.finally\(/.test(load);
  row(inOrderToo && awaits >= 0 && fits > awaits && calls === 1 && start.includes('const loaded = await sample;')
      && caught, 'source.start',
      `start() makes the context, resumes it and asks for the sample loop before its first await: ${inOrderToo}; `
      + `the page calls fitLoop in ${calls} place(s), loadSample(), after its fetch: ${fits > awaits && awaits >= 0}; `
      + `that load never rejects: ${caught}`);

  // A load after a failed one asks the server, not the browser's cache: the cache may hold the answer that failed.
  // The first load is an ordinary fetch. The failure is noted in one place, where the load gives null.
  const modes = found(load, /await fetch\(SAMPLE_URL, \{ signal: abort\.signal, cache: ([^}]*) \}\);/) || '';
  const noted = main.split('sampleFailed = true;').length - 1;
  const fresh = main.includes('  let sampleFailed = false;');
  row(modes === 'sampleFailed ? \'reload\' : \'default\'' && noted === 1 && caught && fresh, 'source.retry',
      `loadSample() fetches with the cache mode "${modes}"; sampleFailed starts false: ${fresh}, and is set in `
      + `${noted} place(s), where a load gives null: ${caught}`);
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
// The exit code is set and node ends by itself: no process.exit() here. Since the rows of the fitted loop, this test
// ends just after heavy work on large arrays, and node 24.15 can then hang in process.exit(): it joins V8's compiler
// thread, which waits for a collection that the main thread no longer runs. Seen in 11 of 3000 runs (8 at once: the
// last line printed, then no exit until the test's time limit); 0 of 3000 when node shuts V8 down in order, as here.
process.exitCode = failed === 0 ? 0 : 1;
