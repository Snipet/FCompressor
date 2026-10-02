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
//   refusals.*     what the browser lacks, in the order a reader can act on
//   files.*        the limits of a dropped file, and the fades at its ends
//   built_from.*   the footer's line: a link only for a clean tree, nothing for `none` or an unreadable line
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
              + [...body.matchAll(/\b(?:data-file|aria-label)="([^"]*)"/g)].map((m) => m[1]).join(' ');
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
  row(differs === 9 && body.includes('BYPASS') && !/<button[^>]*>[^<]*BYPASS/.test(body), 'wording.differences',
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
      && dirty.text.includes('UNCOMMITTED') && none === null && others.every((o) => o === null),
      'built_from.link_only_when_clean',
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
