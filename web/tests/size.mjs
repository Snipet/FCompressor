// web/tests/size.mjs: what the site holds, how large it is, and that it loads nothing from elsewhere (ADR-93, web
// Sprint D).
//
// FCMP_WEB_TEST name=web.size timeout=60 args={build}/site
//
//   node size.mjs <build>/site
//
// THE BUDGETS. Each file of the site with its size as last measured, raw and gzip -9, in bytes. A file may be at most
// 20 % larger than its entry, raw and gzip each. To re-measure (after the modules or the page change on purpose): run
// this test, and replace the table with the "measured now" lines it prints at the end.
//
// Measured at web Sprint D's base 7319ac7 plus card U-2's page, Emscripten 6.0.3, with the BASE's editor module (U-1's
// prototype): fcmp-ui.js and fcmp-ui.wasm are re-measured by the lead once U-1 is merged.
const MEASURED = {
  'built-from.txt': [73, 89],
  'demo.css': [2811, 1249],
  'fcmp-engine.wasm': [545704, 136515],
  'fcmp-ui.html': [2907, 1530],
  'fcmp-ui.js': [54618, 15827],
  'fcmp-ui.wasm': [1317406, 445224],
  'fcmp-worklet.js': [10548, 3556],
  'index.html': [2907, 1530],
  'licences/GPL-3.0.txt': [35149, 12091],
  'licences/JetBrainsMono-OFL.txt': [4399, 1969],
  'licences/THIRD-PARTY.txt': [62770, 9135],
  'loop.js': [7409, 2658],
  'main.js': [32425, 10751],
};
const HEADROOM = 1.2;
//
// The rows:
//   files            the site is exactly the table's files: nothing missing, nothing else (no source map, no test,
//                    no package.json, no file a build left behind)
//   size.<file>      not empty, and within its raw and its gzip budget
//   urls.<file>      every HTML, JavaScript and CSS file: each URL it loads is relative and stays inside the site, so
//                    the directory works from any path of any static host and nothing comes from another origin.
//                      HTML   src, href, action, poster, data, srcset, formaction, ping and manifest attributes. An
//                             <a href> to the project's repository is a link to follow, not a load, and the one
//                             absolute URL allowed; `data:,` is the empty icon.
//                      JS     every import names './<file>'; no string starts with '/' or '../'; no '<scheme>://'
//                             anywhere (comments included) but that repository link in main.js.
//                      CSS    no @import; every url() is relative; no '<scheme>://'.
//                    fcmp-ui.js is the toolchain's glue (it holds file-system paths as strings): only the scheme rule
//                    is applied to it, with XML's namespace names allowed (they are names, never fetched).
// Output: PASS/FAIL/NOTE lines, as the probes print them. Exit 0 pass, 1 fail, 2 usage.
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
  if (!(f in MEASURED)) continue;
  const [rawBudget, gzBudget] = MEASURED[f].map((m) => Math.ceil(m * HEADROOM));
  row(bytes.length > 0 && bytes.length <= rawBudget && gz <= gzBudget, `size.${f}`,
      `${bytes.length} bytes (budget ${rawBudget}), gzip ${gz} (budget ${gzBudget})`);
}
note(`the site: ${raw} bytes in ${files.length} files, gzip ${zipped}`);

// ---- the URLs -------------------------------------------------------------------------------------------------------
const SCHEME = /[A-Za-z][A-Za-z0-9+.-]*:\/\/[^\s"'`)<>]*/g;
// A URL a file may load: relative, inside the site.
const inside = (url) => url !== '' && !/^[A-Za-z][A-Za-z0-9+.-]*:/.test(url) && !url.startsWith('/')
                     && !url.startsWith('\\') && !url.split(/[?#]/)[0].split('/').includes('..');
function schemes(text, allowed) {
  return (text.match(SCHEME) || []).filter((url) => !allowed.some((prefix) => url.startsWith(prefix)));
}
function script(text, allowed) {
  const bad = schemes(text, allowed);
  const imports = /\b(?:import|export)\b[^'"`;()]*?\bfrom\s*(['"])([^'"]*)\1|\bimport\s*\(?\s*(['"])([^'"]*)\3/g;
  for (const m of text.matchAll(imports)) {
    const spec = m[2] ?? m[4];
    if (!spec.startsWith('./') || !inside(spec)) bad.push(`import '${spec}'`);
  }
  for (const m of text.matchAll(/(['"`])((?:\/|\.\.\/)[^'"`\s]+)/g)) bad.push(`the string ${m[1]}${m[2]}`);
  return bad;
}
function style(text) {
  const bad = schemes(text, []);
  if (/@import/.test(text)) bad.push('@import');
  for (const m of text.matchAll(/url\(\s*(['"]?)([^'")]*)\1\s*\)/g)) if (!inside(m[2])) bad.push(`url(${m[2]})`);
  return bad;
}
const ATTRIBUTES = new RegExp('\\b(src|href|action|poster|data|srcset|formaction|ping|manifest)\\s*=\\s*'
                              + '(?:"([^"]*)"|\'([^\']*)\'|([^\\s>]+))', 'gi');
function html(text) {
  const bad = [];
  for (const tag of text.matchAll(/<([A-Za-z][A-Za-z0-9]*)\b([^>]*)>/g)) {
    const name = tag[1].toLowerCase();
    for (const a of tag[2].matchAll(ATTRIBUTES)) {
      const value = a[2] ?? a[3] ?? a[4];
      const link = name === 'a' && a[1].toLowerCase() === 'href'
                && (value === REPOSITORY || value.startsWith(`${REPOSITORY}/`));
      const icon = name === 'link' && value === 'data:,';
      if (!link && !icon && !inside(value)) bad.push(`<${name} ${a[1]}="${value}">`);
    }
  }
  for (const m of text.matchAll(/<script\b[^>]*>([\s\S]*?)<\/script>/gi)) bad.push(...script(m[1], []));
  for (const m of text.matchAll(/<style\b[^>]*>([\s\S]*?)<\/style>/gi)) bad.push(...style(m[1]));
  // Whatever the attribute scan does not know: no other absolute URL anywhere in the file.
  for (const url of schemes(text, [REPOSITORY])) if (!bad.some((b) => b.includes(url))) bad.push(url);
  return bad;
}
for (const f of files.filter((name) => /\.(html|js|css)$/.test(name))) {
  const text = readFileSync(join(site, f), 'utf8');
  let bad;
  if (f.endsWith('.html')) bad = html(text);
  else if (f.endsWith('.css')) bad = style(text);
  else if (f === 'fcmp-ui.js') bad = schemes(text, [NAMESPACES]);
  else bad = script(text, f === 'main.js' ? [REPOSITORY] : []);
  row(bad.length === 0, `urls.${f}`,
      bad.length === 0 ? 'every URL it loads is relative and inside the site' : bad.slice(0, 8).join('; '));
}

console.log('NOTE     measured now (the table for the top of this file):');
for (const line of now) console.log(`NOTE     ${line}`);
console.log(`${failed === 0 ? 'PASS' : 'FAIL'}     ${TEST}: ${passed} row(s) passed, ${failed} failed`);
process.exit(failed === 0 ? 0 : 1);
