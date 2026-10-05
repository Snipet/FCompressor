// web/tests/size.mjs: what the site holds, how large it is, and that it loads nothing from elsewhere (ADR-93, web
// Sprint D).
//
// FCMP_WEB_TEST name=web.size timeout=60 args={build}/site
//
//   node size.mjs <build>/site
//
// THE BUDGETS. Each file of the site with its size as last measured, raw and gzip -9, in bytes. A file may be at most
// 20 % larger than its entry, raw and gzip each. To re-measure (after the modules or the page change on purpose): run
// this test, and replace the table with the "measured now" lines it prints at the end. The sample loop has no budget:
// its bytes are held exactly (the row audio.loop).
//
// Measured at web Sprint D's merge (the three cards with their review fixes, FunkGui v0.14.0), Emscripten 6.0.3, on
// arm64 macOS (another host's build of the same toolchain may differ by a little: the 20 % is also for that).
// fcmp-ui.js, fcmp-ui.wasm and main.js were measured again at the web lead phase's merge (the module's frame and
// accessibility exports; the self-test's pixel rule by renderer class, on a still frame).
// The whole table was measured again on 2026-10-05, for the sample loop (docs/sprints/web-loop.md, card W-P): the site
// has 15 files with sample.js and audio/loop.wav; main.js and index.html (fcmp-ui.html is the same file) grew, and
// the other files came out as they were.
const MEASURED = {
  'audio/loop.wav': [2048600, 1955479],
  'built-from.txt': [73, 89],
  'demo.css': [3240, 1418],
  'fcmp-engine.wasm': [545704, 136515],
  'fcmp-ui.html': [4283, 2133],
  'fcmp-ui.js': [55742, 16153],
  'fcmp-ui.wasm': [1330842, 451250],
  'fcmp-worklet.js': [10548, 3556],
  'index.html': [4283, 2133],
  'licences/GPL-3.0.txt': [35149, 12091],
  'licences/JetBrainsMono-OFL.txt': [4399, 1969],
  'licences/THIRD-PARTY.txt': [62770, 9135],
  'loop.js': [7409, 2658],
  'main.js': [57268, 18685],
  'sample.js': [12751, 4597],
};
const HEADROOM = 1.2;
// The sample loop is the user's file, byte for byte (docs/sprints/web-loop.md, "The loop's contract"). Nobody edits or
// re-encodes it, so it never grows by 20 %: its size and its SHA-256 are held as they are.
const SAMPLE = { file: 'audio/loop.wav', bytes: 2048600,
                 sha256: '0327dec3cbc7de82cf3ed6d9f0533d7035681c20297b0c9cb7aae2bc9a8b7e52' };
//
// The rows:
//   files            the site is exactly the table's files: nothing missing, nothing else (no source map, no test,
//                    no package.json, no file a build left behind)
//   size.<file>      not empty, and within its raw and its gzip budget (every file but the sample loop)
//   audio.loop       the site's audio/loop.wav has the sample loop's size and SHA-256
//   urls.<file>      every HTML, JavaScript and CSS file: no URL written in it is absolute or of another origin, so
//                    the directory works from any path of any static host and nothing comes from elsewhere. A scan
//                    of the forms a URL is written in, not a proof about every load: a URL computed some other way
//                    (new URL(x, base), an array joined, a scheme from a variable) is out of its reach.
//                      HTML   src, href, action, poster, data, srcset (every candidate), formaction, ping and
//                             manifest attributes, each value trimmed as a browser trims it; style="" as CSS. An
//                             <a href> to the project's repository is a link to follow, not a load, and the one
//                             absolute URL allowed; `data:,` is the empty icon.
//                      JS     every import names './<file>' and no import() is computed; no string starts with
//                             '/', '../' or '://'; no '/' alone is joined with +; no template has '${...}/' (but the
//                             repository link's); no 'http:' or 'https:' alone; no location.origin or .host; no
//                             '<scheme>://' anywhere (comments included) but the repository in main.js: that URL
//                             itself or its '/tree/' stem, nothing longer and no other name that starts like it.
//                      CSS    no @import, every url() and every quoted string is relative (image-set() takes a bare
//                             string), in any letter case; no '<scheme>://'.
//                    fcmp-ui.js is the toolchain's glue (it holds file-system paths as strings): only the scheme rule
//                    is applied to it, with XML's namespace names allowed (they are names, never fetched).
// Output: PASS/FAIL/NOTE lines, as the probes print them. Exit 0 pass, 1 fail, 2 usage.
import { createHash } from 'node:crypto';
import { readdirSync, readFileSync, statSync } from 'node:fs';
import { join, relative, sep } from 'node:path';
import { gzipSync } from 'node:zlib';

const TEST = 'web.size';
const REPOSITORY = 'https://github.com/Snipet/FCompressor';
const NAMESPACES = 'http://www.w3.org/';

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

const [site] = process.argv.slice(2);
if (!site) {
  console.error('usage: node size.mjs <build>/site');
  process.exit(2);
}

// ---- the files ------------------------------------------------------------------------------------------------------
const walk = (dir) => readdirSync(dir).flatMap((name) => {
  const path = join(dir, name);
  return statSync(path).isDirectory() ? walk(path) : [path];
});
const files = walk(site).map((path) => relative(site, path).split(sep).join('/')).sort();
const expected = Object.keys(MEASURED);
const extra = files.filter((f) => !expected.includes(f));
const absent = expected.filter((f) => !files.includes(f));
row(extra.length === 0 && absent.length === 0, 'files',
    extra.length + absent.length === 0 ? `${files.length} files`
                                       : `not expected: [${extra.join(', ')}]; missing: [${absent.join(', ')}]`);

// ---- the sizes ------------------------------------------------------------------------------------------------------
const now = [];
let raw = 0;
let zipped = 0;
for (const f of files) {
  const bytes = readFileSync(join(site, f));
  const gz = gzipSync(bytes, { level: 9 }).length;
  raw += bytes.length;
  zipped += gz;
  now.push(`  '${f}': [${bytes.length}, ${gz}],`);
  if (!(f in MEASURED) || f === SAMPLE.file) continue;
  const [rawBudget, gzBudget] = MEASURED[f].map((m) => Math.ceil(m * HEADROOM));
  row(bytes.length > 0 && bytes.length <= rawBudget && gz <= gzBudget, `size.${f}`,
      `${bytes.length} bytes (budget ${rawBudget}), gzip ${gz} (budget ${gzBudget})`);
}
if (files.includes(SAMPLE.file)) {
  const bytes = readFileSync(join(site, SAMPLE.file));
  const sha = createHash('sha256').update(bytes).digest('hex');
  row(bytes.length === SAMPLE.bytes && sha === SAMPLE.sha256, 'audio.loop',
      `${SAMPLE.file}: ${bytes.length} bytes (the sample loop has ${SAMPLE.bytes}), SHA-256 ${sha}`
      + (sha === SAMPLE.sha256 ? '' : ` (the sample loop's is ${SAMPLE.sha256})`));
} else {
  row(false, 'audio.loop', `${SAMPLE.file} is not in the site`);
}
note(`the site: ${raw} bytes in ${files.length} files, gzip ${zipped}`);

// ---- the URLs -------------------------------------------------------------------------------------------------------
const SCHEME = /[A-Za-z][A-Za-z0-9+.-]*:\/\/[^\s"'`)<>]*/g;
// A URL as a browser reads it from an attribute or a url(): without tabs and line ends, and trimmed.
const trimmed = (url) => url.replace(/[\t\n\r]/g, '').trim();
// A URL a file may load: relative, inside the site.
const inside = (given) => {
  const url = trimmed(given);
  return url !== '' && !/^[A-Za-z][A-Za-z0-9+.-]*:/.test(url) && !url.startsWith('/') && !url.startsWith('\\')
      && !url.split(/[?#]/)[0].split('/').includes('..');
};
// The project's repository as the page links it: that URL, or the stem of a commit's tree (the footer adds the
// commit). Nothing longer: a file under it would be a load from another origin, and FCompressorX is not this project.
const repository = (url) => url === REPOSITORY || url === `${REPOSITORY}/tree/`;
const none = () => false;
function schemes(text, allowed) {
  return (text.match(SCHEME) || []).filter((url) => !allowed(url));
}
function script(text, allowed) {
  const bad = schemes(text, allowed);
  const imports = /\b(?:import|export)\b[^'"`;()]*?\bfrom\s*(['"])([^'"]*)\1|\bimport\s*\(?\s*(['"])([^'"]*)\3/g;
  for (const m of text.matchAll(imports)) {
    const spec = m[2] ?? m[4];
    if (!spec.startsWith('./') || !inside(spec)) bad.push(`import '${spec}'`);
  }
  for (const m of text.matchAll(/\bimport\s*\(\s*(?!['"])[^)]{0,24}/g)) bad.push(`a computed ${m[0]}...`);
  for (const m of text.matchAll(/(['"`])((?:\/|\.\.\/|:\/\/)[^'"`\s]+)/g)) bad.push(`the string ${m[1]}${m[2]}`);
  // A path or a URL put together: '/' alone joined with + (split('/') is not that), a template with '${...}/' (the
  // expression is then its origin or its root), a scheme alone, and the page's own origin as a base.
  for (const m of text.matchAll(/(['"`])\/\1\s*\+|\+\s*(['"`])\/\2/g)) bad.push(`a path joined from ${m[0]}`);
  for (const m of text.matchAll(/\$\{([^}]*)\}\/[^\s'"`$]*/g)) {
    if (m[1].trim() !== 'REPOSITORY') bad.push(`the template ${m[0]}`);
  }
  for (const m of text.matchAll(/(['"`])(?:https?|wss?|ftp):(?:\/\/)?\1/gi)) bad.push(`the scheme ${m[0]}`);
  for (const m of text.matchAll(/\blocation\s*\.\s*(?:origin|host|hostname)\b/g)) bad.push(m[0]);
  return bad;
}
function style(text) {
  const bad = schemes(text, none);
  if (/@import/i.test(text)) bad.push('@import');
  for (const m of text.matchAll(/url\(\s*(['"]?)([^'")]*)\1\s*\)/gi)) if (!inside(m[2])) bad.push(`url(${m[2]})`);
  // Any other quoted string (comments apart: they have apostrophes): a font's name is "inside" by the same rule.
  for (const m of text.replace(/\/\*[\s\S]*?\*\//g, ' ').matchAll(/(['"])([^'"\n]*)\1/g)) {
    if (m[2] !== '' && !inside(m[2])) bad.push(`the string "${m[2]}"`);
  }
  return bad;
}
const ATTRIBUTES = new RegExp('\\b(src|href|action|poster|data|srcset|formaction|ping|manifest)\\s*=\\s*'
                              + '(?:"([^"]*)"|\'([^\']*)\'|([^\\s>]+))', 'gi');
function html(text) {
  const bad = [];
  for (const tag of text.matchAll(/<([A-Za-z][A-Za-z0-9]*)\b([^>]*)>/g)) {
    const name = tag[1].toLowerCase();
    for (const a of tag[2].matchAll(ATTRIBUTES)) {
      const attribute = a[1].toLowerCase();
      const value = trimmed(a[2] ?? a[3] ?? a[4]);
      const link = name === 'a' && attribute === 'href' && repository(value);
      const icon = name === 'link' && value === 'data:,';
      // A srcset is a list: each candidate is a URL and then its width or density.
      const urls = attribute === 'srcset' ? value.split(',').map((candidate) => candidate.trim().split(/\s+/)[0])
                                          : [value];
      if (!link && !icon && !urls.every(inside)) bad.push(`<${name} ${a[1]}="${value}">`);
    }
    for (const a of tag[2].matchAll(/\bstyle\s*=\s*(?:"([^"]*)"|'([^']*)')/gi)) {
      bad.push(...style(a[1] ?? a[2]).map((b) => `<${name} style>: ${b}`));
    }
  }
  for (const m of text.matchAll(/<script\b[^>]*>([\s\S]*?)<\/script>/gi)) bad.push(...script(m[1], none));
  for (const m of text.matchAll(/<style\b[^>]*>([\s\S]*?)<\/style>/gi)) bad.push(...style(m[1]));
  // Whatever the attribute scan does not know: no other absolute URL anywhere in the file.
  for (const url of schemes(text, repository)) if (!bad.some((b) => b.includes(url))) bad.push(url);
  return bad;
}
for (const f of files.filter((name) => /\.(html|js|css)$/.test(name))) {
  const text = readFileSync(join(site, f), 'utf8');
  let bad;
  if (f.endsWith('.html')) bad = html(text);
  else if (f.endsWith('.css')) bad = style(text);
  else if (f === 'fcmp-ui.js') bad = schemes(text, (url) => url.startsWith(NAMESPACES));
  else bad = script(text, f === 'main.js' ? repository : none);
  row(bad.length === 0, `urls.${f}`,
      bad.length === 0 ? 'no absolute or cross-origin URL is written in it' : bad.slice(0, 8).join('; '));
}

console.log('NOTE     measured now (the table for the top of this file):');
for (const line of now) console.log(`NOTE     ${line}`);
console.log(`${failed === 0 ? 'PASS' : 'FAIL'}     ${TEST}: ${passed} row(s) passed, ${failed} failed`);
process.exit(failed === 0 ? 0 : 1);
