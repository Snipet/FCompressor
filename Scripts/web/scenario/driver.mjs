// Scripts/web/scenario/driver.mjs: what the scripted user (Scripts/web/scenario.mjs) needs of a browser, over the
// project's one DevTools library, Scripts/web/cdp.mjs. This is the only file of the scenario that imports that library:
// what the scenario uses of it is named under "The library" below, and what it lacks is added here.
//
// The rules every group relies on:
// - Input is real: mouse, key, wheel and drag events through the browser's own input pipeline (Input.dispatch*). No
//   event is made in the page, and no function of the editor is called to act for the user.
// - A control is found by its title in the Panel's own accessibility list (Module.fcmpA11y()), and pressed where that
//   list says it is. A group never knows a coordinate of the layout.
// - Nothing waits a fixed time for an outcome. until() polls the page (one look: the list, Module.fcmpStatus(), the
//   page's own state, the port tap) until a condition holds, and gives up after a bound; the row then fails with what
//   was last seen. The sleeps left are the user's own pace (a button held for 40 ms, 16 ms between the moves of a
//   drag), never a wait for the page.
// - What is not timing of the page is not left to timing of the driver: the two presses of a double click carry their
//   own timestamps (90 ms apart), so the editor counts them as one however long the browser took to answer the first.
//
// The library (cdp.mjs, as the lead phase's base has it; card L-R keeps these names), and all the scenario uses of it:
//   serve(dir) -> { base, kill() }          the site on 127.0.0.1
//   chrome({ width, height, profile, extra }) -> { page(null), send(method, params), kill(), pid }
//                                           headless and muted, the profile given; `extra` are further switches,
//                                           after the library's own (of two that say the same, the last wins: the
//                                           GPU flag is overridden so)
//   a page: s(method, params), ev(expression), consoleLines, targetId, metrics(w, h, 1), go(url, 0), at(x, y),
//           move(x, y), wheel(x, y, deltaY), key(key, { modifiers, settle }), type(text), menu(), and the port tap:
//           tap(), tapRead() -> { n, last: { v, snap }, reply: { flags, publish, latency, frameLatency, rate,
//           modeSlot, fade, inPeak, outPeak, blockMaxGr, thrDb, slope }, replies }
//   sleep, cleanUp, PID
//   where the library has them (the base's has not): a browser's gone(), '' while Chrome lives, and its close()
// The presses, the drags, the double click, START and the pictures are made here, not with the library's own: they
// carry timestamps, wait on conditions and write where the scenario is told to.
// Requests to that card (the handoff lists them): the Chrome to run comes from --chrome, which the base's library
// cannot be told (it is passed as the option `chrome` and as $CHROME); the base's library makes a directory `png`
// beside itself as it loads (taken away again below while it is empty).
import { mkdirSync, mkdtempSync, rmdirSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';

import * as cdp from '../cdp.mjs';

if (typeof cdp.PNG === 'string') {
  try {
    rmdirSync(cdp.PNG);
  } catch { /* not there, or it holds something: not the scenario's to remove */ }
}

export const sleep = cdp.sleep;
export const PID = cdp.PID;                           // a Params record's values, by name (fcdsp/params/Pid.h)

// funkgui::A11yRole, as Module.fcmpA11y() numbers an item's role.
export const ROLE = { slider: 0, toggle: 1, button: 2, radioGroup: 3, radio: 4, combo: 5, row: 6, text: 7, image: 8,
                      bar: 9 };
// fcmp::ui::Screen and Overlay (Source/editor/Panel.h), as the hook's `screen` and `overlay`.
export const SCREEN = { panel: 0, characteristics: 1 };
export const OVERLAY = { none: 0, modeBrowser: 1, presetBrowser: 2, settings: 3 };
// A reply's flags (Source/web/engine/WebProtocol.h ReplyFlag) and a frame's (fcdsp/telemetry/UiFrame.h UiFlag).
export const REPLY = { frame: 1 << 2, gated: 1 << 3, configured: 1 << 4, attached: 1 << 5 };
export const UI = { bypassed: 1 << 0, fading: 1 << 5 };
// Input.dispatch*'s modifiers.
export const MOD = { alt: 1, ctrl: 2, meta: 4, shift: 8 };

// The pointer's pace, in seconds of the events' own timestamps (funkgui::web::ClickCounter has the editor's limits).
const DOUBLE_S = 0.09;                                // between the two presses of a double click (the limit: 0.4)
const SINGLE_S = 0.5;                                 // at least this between two presses that are not a pair
const HELD_S = 0.04;                                  // a click holds the button this long (a long press: 0.3)
const NEAR_PX = 10;                                   // two presses nearer than this are at one place (the limit: 8)
// How long until() looks for an outcome before the row fails, unless the caller says: far longer than any outcome
// takes on a fast machine (tens of milliseconds), for a software renderer on a loaded one.
const BOUND_MS = 6000;
const OUTCOME_MS = 10000;                             // a wait with a bound up to this is a wait for an outcome

// One look at the page, as a JSON text: evaluated in the page, so it must stand alone.
const LOOK = `JSON.stringify((() => {
  const M = globalThis.Module;
  const page = globalThis.fcmpPage;
  const has = (name) => !!M && typeof M[name] === 'function';
  const text = (id) => { const e = document.getElementById(id); return e ? e.textContent : ''; };
  const context = page ? page.context() : null;
  const start = document.getElementById('fcmp-start');
  return { a11y: has('fcmpA11y') ? JSON.parse(M.fcmpA11y()) : null,
           status: has('fcmpStatus') ? JSON.parse(M.fcmpStatus()) : null,
           state: page ? page.state() : '', context: context ? context.state : '',
           rate: context ? context.sampleRate : 0,
           says: text('fcmp-status'), notice: text('fcmp-notice'), source: text('fcmp-source-name'),
           button: start && !start.hidden ? start.textContent : '',
           away: document.getElementById('fcmp-overlay').classList.contains('away'),
           hidden: document.visibilityState === 'hidden', scrollY: window.scrollY, zoomPref: (() => {
             try { return localStorage.getItem('FCompressor.uiZoom'); } catch { return null; } })() };
})())`;

// The editor's status and the worklet's counters at one instant: the status is read and the worklet asked in one task
// of the page, and a port delivers in order, so every record the status counts as posted has reached the worklet when
// it answers. posted === records is then exact, and a difference is a record that went missing. A page that does not
// play has no worklet to ask (the page's own question would wait 10 s for no answer): that is an error at once.
const LEDGER = `(async () => {
  if (fcmpPage.state() !== 'running') {
    return JSON.stringify({ stopped: 'the demo does not play: the page is ' + fcmpPage.state() + ' and says "'
                                     + document.getElementById('fcmp-status').textContent + '"' });
  }
  const status = JSON.parse(Module.fcmpStatus());
  const worklet = await fcmpPage.stats();
  return JSON.stringify({ status, worklet });
})()`;

// Whether a reply (the port tap's) carries a frame the engine has published: before the first one its values are 0.
const published = (reply) => reply !== null && (reply.flags & REPLY.frame) !== 0 && reply.publish > 0;

// A box of the page in client px, by element id.
const boxOf = (id) => `JSON.stringify((() => {
  const r = document.getElementById(${JSON.stringify(id)}).getBoundingClientRect();
  return { x: r.left, y: r.top, w: r.width, h: r.height };
})())`;

// Whether an AudioContext renders in this browser: one made on a blank page runs and its clock moves within 3 s.
const RENDERS = `(async () => {
  const context = new AudioContext();
  const t0 = performance.now();
  const renders = () => context.state === 'running' && context.currentTime > 0;
  while (!renders() && performance.now() - t0 < 3000) await new Promise((r) => setTimeout(r, 50));
  const ok = renders();
  await context.close().catch(() => {});
  return ok;
})()`;
const NULL_SINK = '--disable-audio-output';

// The server, one Chrome with a throwaway profile, and the scripted user's page. `flags` are further switches for
// Chrome (a software renderer, no audio device); `chromePath` is --chrome; `out` is --out.
//
// The rows need a context that renders. On a machine with no audio device (a CI runner) Chrome may give one that
// never does: then that Chrome is given up and another started with the browser's own null sink (NULL_SINK), which
// renders at the same pace into nothing, and `audio` says so. Chrome is muted either way.
export async function launch({ dir, out, chromePath = '', flags = [], width = 1280, height = 800 }) {
  // One scratch directory, under --out and gone when the run ends: Chrome's profiles, and the files the user drops.
  mkdirSync(out, { recursive: true });
  const scratch = mkdtempSync(join(out, 'scenario-'));
  const removeScratch = () => {
    try {
      rmSync(scratch, { recursive: true, force: true, maxRetries: 10, retryDelay: 100 });
    } catch { /* a file Chrome still held */ }
  };
  process.on('exit', removeScratch);                  // after the library's own handler, which kills what it started
  const server = await cdp.serve(dir);
  let browser = null;
  let audio = '';
  // Whether that Chrome still runs: the library says so, or its process does.
  const alive = () => {
    if (typeof browser.gone === 'function') return browser.gone() === '';
    try {
      process.kill(browser.pid, 0);
      return true;
    } catch {
      return false;
    }
  };
  // The orderly end of a Chrome: asked to close, it ends its own helper processes; killed if it has not within 2 s.
  const end = async () => {
    if (browser === null) return;
    if (typeof browser.close === 'function') {
      await browser.close();                          // the library's own orderly end, where it has one
    } else {
      browser.send('Browser.close').catch(() => {});
      for (const t0 = Date.now(); alive() && Date.now() - t0 < 2000;) await sleep(50);
      browser.kill();
    }
  };
  const close = async () => {
    await end();
    server.kill();
    cdp.cleanUp();
    removeScratch();
  };
  const start = async (name, extra) => {
    const profile = join(scratch, name);
    mkdirSync(profile);
    browser = await cdp.chrome({ width, height, profile, extra, chrome: chromePath || undefined });
    const probe = await browser.page(null);
    const renders = await probe.ev(RENDERS);
    await browser.send('Target.closeTarget', { targetId: probe.targetId }).catch(() => {});
    return renders;
  };
  try {
    if (await start('profile', flags)) {
      audio = flags.includes(NULL_SINK) ? `a context renders into the browser's null sink (${NULL_SINK}, as asked)`
                                        : "a context renders through the machine's audio device (Chrome is muted)";
    } else if (flags.includes(NULL_SINK)) {
      throw new Error(`no AudioContext renders in this browser, with ${NULL_SINK}`);
    } else {
      await end();
      if (!await start('profile-null-sink', [...flags, NULL_SINK])) {
        throw new Error(`no AudioContext renders in this browser, with ${NULL_SINK} or without`);
      }
      audio = `a context renders into the browser's null sink (${NULL_SINK}): none did through an audio device of `
              + 'this machine';
    }
  } catch (error) {
    await close();
    throw error;
  }
  return { base: server.base, browser, scratch, audio, alive, close };
}

// The scripted user on one tab: the library's page, and what the groups ask of it.
export async function user(browser, base) {
  const p = await browser.page(null);
  const u = { p, base, browser, width: 0, height: 0 };
  let tapped = false;                                 // the port tap is in this document

  // ---- looking ------------------------------------------------------------------------------------------------------
  u.look = async () => {
    const s = JSON.parse(await p.ev(LOOK));
    s.tap = tapped ? await p.tapRead() : null;
    return s;
  };
  // Looks until `test(look)` holds or `ms` have passed: { ok, s (the last look), ms }. A look that cannot be taken (the
  // page is between two documents) or a test that throws (the look has no such item yet) does not hold.
  u.until = async (test, ms = BOUND_MS) => {
    const t0 = Date.now();
    let s = null;
    for (;;) {
      try {
        s = await u.look();
        if (test(s)) {
          const took = Date.now() - t0;
          if (ms <= OUTCOME_MS && took / ms > u.slowest.ms / u.slowest.bound) u.slowest = { ms: took, bound: ms };
          return { ok: true, s, ms: took };
        }
      } catch { /* the next look */ }
      if (Date.now() - t0 >= ms) return { ok: false, s, ms: Date.now() - t0 };
      await sleep(40);
    }
  };
  // The outcome that came nearest to its bound, of those that came: how much room the bounds leave on this machine.
  // Not counted: the longer waits, for a page to boot and for the Panel to come to rest, which take seconds by design.
  u.slowest = { ms: 0, bound: BOUND_MS };
  // As until(), and every item of the list lies where it lay in the look before: for a look whose items are then
  // pressed where it says they are (a screen that is opening lists them, then moves them).
  u.steady = (test, ms = BOUND_MS) => {
    let before = '';
    return u.until((s) => {
      const now = JSON.stringify(u.list(s).items.map((i) => [i.id, i.x, i.y, i.w, i.h]));
      const same = now === before;
      before = now;
      return same && test(s);
    }, ms);
  };
  // The look's list, or an error that says there is none: a group that cannot do without it ends there.
  u.list = (s) => {
    if (!s || !s.a11y) {
      throw new Error('the module gives no Module.fcmpA11y(): the Panel has no list to find a control in');
    }
    return s.a11y;
  };
  // The item of a look with this title (a text or a pattern): { parent: the title of its group, role, nth }.
  u.find = (s, title, { parent = null, role = null, nth = 0 } = {}) => {
    const items = u.list(s).items;
    const group = parent === null ? null : items.find((i) => i.title === parent);
    if (parent !== null && !group) return null;
    const hits = items.filter((i) => (title instanceof RegExp ? title.test(i.title) : i.title === title)
                                     && (group === null || i.parent === group.id)
                                     && (role === null || i.role === role));
    return hits[nth] || null;
  };
  // The item, once the list has it at the same place in two looks (a screen that is opening has it, then moves it).
  u.item = async (title, where = {}, ms = BOUND_MS) => {
    let before = null;
    const r = await u.until((s) => {
      if (!s.a11y) return true;                        // no list at all: known at the first look, said below
      const it = u.find(s, title, where);
      const still = it !== null && before !== null && ['x', 'y', 'w', 'h'].every((k) => it[k] === before[k]);
      before = it;
      return still;
    }, ms);
    if (r.s) u.list(r.s);                              // throws: no list at all
    if (!r.ok) throw new Error(`the Panel lists no "${title}"${where.parent ? ` in "${where.parent}"` : ''}`);
    return u.find(r.s, title, where);
  };
  u.centre = (it) => [it.x + it.w / 2, it.y + it.h / 2];
  u.value = (s, title, where = {}) => {
    const it = u.find(s, title, where);
    return it ? it.value : null;
  };
  u.ledger = async () => {
    const l = JSON.parse(await p.ev(LEDGER));
    if (l.stopped) throw new Error(l.stopped);
    return l;
  };
  // The ledger, read until `test(ledger)` holds or `ms` have passed: { ok, l (the last one) }.
  u.untilLedger = async (test, ms = BOUND_MS) => {
    const t0 = Date.now();
    for (;;) {
      const l = await u.ledger();
      if (test(l)) return { ok: true, l };
      if (Date.now() - t0 >= ms) return { ok: false, l };
      await sleep(40);
    }
  };

  // ---- the pointer and the keys -------------------------------------------------------------------------------------
  // Every press and release carries its own timestamp (the page reads it as the event's timeStamp), and the editor
  // counts clicks from those: two presses at one place less than 0.4 s apart are a double click, which resets a
  // value. So a press that is not meant as the second of a pair is never stamped less than SINGLE_S after the press
  // before it at that place (press() waits out the difference, as a hand would), and the two of a pair are stamped
  // DOUBLE_S apart however long the browser took over the first.
  let last = { at: 0, x: -1e9, y: -1e9 };              // the last press: its stamp (s) and its place (client px)
  const mouse = (type, cx, cy, more) => p.s('Input.dispatchMouseEvent', { type, x: cx, y: cy, button: 'none',
                                                                         buttons: 0, pointerType: 'mouse', ...more });
  const press = async (cx, cy, { button = 'left', pair = false } = {}) => {
    await mouse('mouseMoved', cx, cy);
    const near = Math.abs(cx - last.x) < NEAR_PX && Math.abs(cy - last.y) < NEAR_PX;
    let at = Date.now() / 1000;
    if (pair) {
      at = last.at + DOUBLE_S;
    } else if (near && at - last.at < SINGLE_S) {
      await sleep((last.at + SINGLE_S - at) * 1000);
      at = Math.max(Date.now() / 1000, last.at + SINGLE_S);
    }
    last = { at, x: cx, y: cy };
    await mouse('mousePressed', cx, cy, { button, buttons: button === 'right' ? 2 : 1, clickCount: pair ? 2 : 1,
                                         timestamp: at });
  };
  const release = (cx, cy, { button = 'left', pair = false, held = HELD_S } = {}) =>
    mouse('mouseReleased', cx, cy, { button, clickCount: pair ? 2 : 1, timestamp: last.at + held });
  const click = async (cx, cy, how = {}) => {
    await press(cx, cy, how);
    await sleep(HELD_S * 1000);
    await release(cx, cy, how);
  };
  // (x, y) are the Panel's logical px, as the list gives them; the library maps them through the canvas's box as it
  // is now (the zoom may have changed it).
  u.clickAt = async (x, y, { button = 'left' } = {}) => click(...(await p.at(x, y)), { button });
  u.press = async (title, where = {}) => {
    const it = await u.item(title, where);
    await u.clickAt(...u.centre(it), { button: where.button });
    return it;
  };
  u.hover = async (title, where = {}) => {
    const it = await u.item(title, where);
    await p.move(...u.centre(it));
    return it;
  };
  u.doubleClickAt = async (x, y) => {
    const [cx, cy] = await p.at(x, y);
    await click(cx, cy);
    await sleep((DOUBLE_S - HELD_S) * 1000);
    await click(cx, cy, { pair: true });
  };
  // A drag from (x, y) by (dx, dy) in `steps` moves 16 ms apart; the button comes up at the end.
  u.dragBy = async (x, y, dx, dy, steps = 20) => {
    const [x0, y0] = await p.at(x, y);
    const [x1, y1] = await p.at(x + dx, y + dy);
    await press(x0, y0);
    for (let i = 1; i <= steps; i += 1) {
      await mouse('mouseMoved', x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps, { button: 'left', buttons: 1 });
      await sleep(16);
    }
    await release(x1, y1, { held: Math.max(HELD_S, Date.now() / 1000 - last.at) });
  };
  u.wheelAt = (x, y, deltaY) => p.wheel(x, y, deltaY);
  u.key = (key, modifiers = 0) => p.key(key, { modifiers, settle: 30 });
  u.type = (text) => p.type(text);
  // A press on an element of the page itself (START, BUILT-IN LOOP), or on a client point (a row of a DOM menu).
  u.clickClient = (cx, cy) => click(cx, cy);
  u.pressElement = async (id) => {
    const b = JSON.parse(await p.ev(boxOf(id)));
    await click(b.x + b.w / 2, b.y + b.h / 2);
  };
  // Whether this browser's command key is Meta (Apple's) or Control: the page's own rule (funkgui::web, WebInput.h).
  u.commandKey = async () => (await p.ev('/mac|iphone|ipad/i.test(navigator.userAgentData '
                                         + '? navigator.userAgentData.platform : navigator.platform)')
    ? MOD.meta : MOD.ctrl);

  // ---- FunkGui's DOM menu -------------------------------------------------------------------------------------------
  u.menu = () => p.menu();
  u.menuOpen = async (ms = BOUND_MS) => {
    const t0 = Date.now();
    for (;;) {
      const m = await p.menu();
      if (m !== null && m.items.length > 0) return m;
      if (Date.now() - t0 >= ms) return null;
      await sleep(40);
    }
  };
  u.menuGone = async (ms = BOUND_MS) => {
    const t0 = Date.now();
    for (;;) {
      if (await p.menu() === null) return true;
      if (Date.now() - t0 >= ms) return false;
      await sleep(40);
    }
  };
  u.menuTexts = (m) => m.items.filter((i) => i.role !== 'separator')
    .map((i) => `${i.text}${i.disabled ? ' (disabled)' : ''}`).join(' | ');
  u.menuPress = async (m, text) => {
    const it = m.items.find((i) => (text instanceof RegExp ? text.test(i.text) : i.text === text));
    if (!it) throw new Error(`the menu has no "${text}": ${u.menuTexts(m)}`);
    await u.clickClient(it.x, it.y);
    return it;
  };

  // ---- the page -----------------------------------------------------------------------------------------------------
  // A new document at `query` in a window of this size, its storage cleared first when `clear` (the preferences are
  // the origin's, and survive a load). Returns once the page has booted and shows START, and the editor has drawn.
  u.load = async ({ query = '', width = 1280, height = 800, clear = true } = {}) => {
    tapped = false;
    await p.metrics(width, height, 1);
    u.width = width;
    u.height = height;
    if (clear) await p.s('Storage.clearDataForOrigin', { origin: base, storageTypes: 'local_storage' });
    await p.go(`${base}/${query}`, 0);
    return u.until((s) => s.state === 'idle' && s.button === 'START' && s.status !== null && s.status.frames >= 2,
                   30000);
  };
  // A press on START: { ok, s, ms, why }. A press that does nothing is known at once (the page leaves `idle` inside
  // the click); a start that began has 20 s.
  u.start = async () => {
    const t0 = Date.now();
    await u.pressElement('fcmp-start');
    const left = await u.until((s) => s.state !== 'idle', 2000);
    if (!left.ok) return { ...left, why: 'the press on START did nothing: the page is still idle' };
    const r = await u.until((s) => s.state !== 'loading', 20000);
    if (!r.ok || r.s.state !== 'running') {
      return { ...r, ok: false,
               why: `the page is ${r.s ? `${r.s.state || 'not booted'} and says "${r.s.says}"` : 'not answering'}` };
    }
    await p.tap();
    tapped = true;
    // The first replies may carry a frame the engine has not published yet (its values are all 0): a group reads the
    // engine's values from its first look on, so START is over only when a published frame has come back.
    const fed = await u.until((s) => s.context === 'running' && s.away && published(s.tap.reply), 10000);
    return { ...fed, ms: Date.now() - t0,
             why: fed.ok ? '' : `the page runs, but the context is ${fed.s ? fed.s.context : '?'} and `
                                + `${fed.s && fed.s.tap && fed.s.tap.replies > 0 ? 'no reply carries a published frame'
                                                                                 : 'no reply came'}` };
  };
  u.tapped = () => tapped;
  // What load() returned, or an error that says the page did not come up: for a group that goes no further then.
  u.up = (loaded) => {
    if (!loaded.ok) {
      const s = loaded.s;
      throw new Error(`the page did not come up: ${s ? `it is ${s.state || 'not booted'} and says "${s.says}"`
                                                      : 'it does not answer'}`);
    }
    return loaded;
  };

  // ---- hidden and shown ---------------------------------------------------------------------------------------------
  // Another tab in front hides this one, as a user hides it (Page.setWebLifecycleState would leave it hidden for
  // good). How the document is hidden is the driver's business, and only a hidden document is judged: should the
  // new tab not have hidden it within 2 s (seen once in some forty runs, on a desktop where other browsers ran, and
  // in none of 3,000 hides afterwards), its window is minimised instead. Answers how it was hidden; an error when it
  // is not.
  let other = null;
  let minimised = null;                               // the window's id, while it is minimised
  const hiddenSoon = async () => (await u.until((s) => s.hidden === true, 2000)).ok;
  u.hide = async () => {
    const made = await browser.send('Target.createTarget', { url: 'about:blank' });
    other = made.targetId;
    await browser.send('Target.activateTarget', { targetId: other });
    if (await hiddenSoon()) return 'another tab is in front';
    // Why not is worth a line: the new tab may have gone to another window than this page's.
    const { windowId } = await browser.send('Browser.getWindowForTarget', { targetId: p.targetId });
    const others = await browser.send('Browser.getWindowForTarget', { targetId: other }).catch(() => ({}));
    const why = `the new tab did not hide it: that tab is in ${others.windowId === windowId ? 'the same window'
                                                                                           : 'another window'}`;
    await browser.send('Browser.setWindowBounds', { windowId, bounds: { windowState: 'minimized' } });
    minimised = windowId;
    if (await hiddenSoon()) return `its window is minimised (${why})`;
    throw new Error(`the document could not be hidden, by another tab in front or by minimising its window (${why})`);
  };
  u.show = async () => {
    if (minimised !== null) {
      await browser.send('Browser.setWindowBounds', { windowId: minimised, bounds: { windowState: 'normal' } })
        .catch(() => {});
    }
    minimised = null;
    await browser.send('Target.activateTarget', { targetId: p.targetId });
    await p.s('Page.bringToFront');
    if (other !== null) await browser.send('Target.closeTarget', { targetId: other }).catch(() => {});
    other = null;
  };
  // Whether the user's tab is still there (a browser may take a tab away: it discards one under memory pressure).
  u.there = async () => (await browser.send('Target.getTargets')).targetInfos.some((t) => t.targetId === p.targetId);

  // ---- files and pictures -------------------------------------------------------------------------------------------
  // A file dropped on the page at client (x, y): the browser's own drag events, with the file's path.
  u.drop = async (file, x = 400, y = 300) => {
    const data = { items: [], files: [file], dragOperationsMask: 1 };
    for (const type of ['dragEnter', 'dragOver', 'drop']) await p.s('Input.dispatchDragEvent', { type, x, y, data });
  };
  // The canvas as a PNG at `file`: the file's size in bytes, and the canvas's box in CSS px.
  u.shot = async (file) => {
    const b = JSON.parse(await p.ev(boxOf('fcmp-canvas')));
    const scroll = JSON.parse(await p.ev('JSON.stringify([scrollX, scrollY])'));
    const r = await p.s('Page.captureScreenshot', { format: 'png', captureBeyondViewport: true,
      clip: { x: b.x + scroll[0], y: b.y + scroll[1], width: b.w, height: b.h, scale: 1 } });
    mkdirSync(dirname(file), { recursive: true });
    writeFileSync(file, Buffer.from(r.data, 'base64'));
    return { bytes: statSync(file).size, width: b.w, height: b.h };
  };

  // ---- what the console said ----------------------------------------------------------------------------------------
  // Uncaught errors and error-level lines of this page. A renderer's own warnings (SwiftShader logs one on the plain
  // page) are not errors.
  u.errors = () => p.consoleLines.filter((line) => /^(EXCEPTION|console\.(error|assert)|log\.error):/.test(line));
  u.consoleLines = () => p.consoleLines.length;
  return u;
}

// A 16-bit PCM WAV of a sine at `hz` and `db` dBFS, written to `file`.
export function writeTone(file, { seconds = 2, hz = 1000, db = -12, channels = 2, rate = 48000 } = {}) {
  const frames = Math.round(seconds * rate);
  const data = Buffer.alloc(frames * channels * 2);
  const amplitude = 10 ** (db / 20) * 32767;
  for (let i = 0; i < frames; i += 1) {
    const sample = Math.round(amplitude * Math.sin(2 * Math.PI * hz * i / rate));
    for (let c = 0; c < channels; c += 1) data.writeInt16LE(sample, (i * channels + c) * 2);
  }
  const head = Buffer.alloc(44);
  head.write('RIFF', 0);
  head.writeUInt32LE(36 + data.length, 4);
  head.write('WAVEfmt ', 8);
  head.writeUInt32LE(16, 16);
  head.writeUInt16LE(1, 20);
  head.writeUInt16LE(channels, 22);
  head.writeUInt32LE(rate, 24);
  head.writeUInt32LE(rate * channels * 2, 28);
  head.writeUInt16LE(channels * 2, 32);
  head.writeUInt16LE(16, 34);
  head.write('data', 36);
  head.writeUInt32LE(data.length, 40);
  mkdirSync(dirname(file), { recursive: true });
  writeFileSync(file, Buffer.concat([head, data]));
  return file;
}
