// Scripts/web/scenario/zoom.mjs: the editor's zoom steps and its preference, on pages of its own in a 1920 x 1080
// window (in the scenario's 1280 x 800 only 100 % fits). The steps are 100, 125, 150 and 175 %, the default 125
// (Source/editor/Layout.h footer::kZoomSteps, kDefaultZoomPercent); which of them fit a window is the host's to say,
// and the footer's cells say it: a cell that does not fit is disabled. The rows hold the cells to what happens.
//
//   zoom.default   no preference: the page shows the default step (or the largest that fits below it), the canvas has
//                  that size, and at least the first two steps fit this window
//   zoom.steps     a press on each cell that fits: the host's zoom, the canvas's box and its buffer are that step's,
//                  the preference is stored, and the page gets no horizontal scroll bar; a press on a cell that does
//                  not fit changes nothing
//   zoom.reload    a new document, the storage kept: the page comes up at the chosen step
//   zoom.refit     the window made 1280 x 800: the zoom falls to 100 % and the cells above it go off, while the stored
//                  preference stays; made large again: the chosen step is back
import { ROLE } from './driver.mjs';
import { holds } from './engine.mjs';

export const page = 'own';

const STEPS = [100, 125, 150, 175];
const DEFAULT = 125;
const LARGE = { width: 1920, height: 1080 };
const SMALL = { width: 1280, height: 800 };
const PANEL = { w: 960, h: 640 };                     // the Panel's logical size (Layout.h kW, kH)

const CANVAS = `JSON.stringify((() => {
  const c = document.getElementById('fcmp-canvas');
  const r = c.getBoundingClientRect();
  return { w: r.width, h: r.height, bufferW: c.width, bufferH: c.height, ratio: devicePixelRatio,
           inner: innerWidth, scroll: document.documentElement.scrollWidth };
})())`;

export async function run({ u, row, leave }) {
  const cell = (s, step) => u.find(s, `ZOOM ${step} %`, { parent: 'Zoom', role: ROLE.radio });
  const fits = (s) => STEPS.filter((step) => cell(s, step).enabled === 1);
  const canvas = async () => JSON.parse(await u.p.ev(CANVAS));
  // The host shows `step`: its status, the checked cell, and the canvas's box and buffer.
  const shows = async (step, ms = 5000) => {
    const r = await u.until((s) => s.status.zoom === step && cell(s, step).checked === 1, ms);
    const c = await canvas();
    const ok = r.ok && c.w === PANEL.w * step / 100 && c.h === PANEL.h * step / 100
               && c.bufferW === Math.round(c.w * c.ratio) && c.bufferH === Math.round(c.h * c.ratio);
    return { ok, s: r.s, c, text: `zoom ${r.s.status.zoom}, the canvas ${c.w} x ${c.h} (buffer ${c.bufferW} x `
                                  + `${c.bufferH}), stored ${JSON.stringify(r.s.zoomPref)}` };
  };

  // ---- no preference ----
  u.list(u.up(await u.load(LARGE)).s);                // the cells are the list's
  let cells = '';                                     // the host fits the window in its first frames: two looks agree
  const loaded = await u.until((s) => {
    const same = fits(s).join() === cells;
    cells = fits(s).join();
    return same && s.status.frames >= 5;
  });
  const fit = loaded.ok ? fits(loaded.s) : [];
  const first = Math.max(...fit.filter((step) => step <= DEFAULT));
  const atLoad = await shows(first);
  row(loaded.ok && atLoad.ok && fit.includes(100) && fit.includes(DEFAULT) && atLoad.s.zoomPref === null, 'default',
      `${LARGE.width} x ${LARGE.height}, nothing stored: the steps ${fit.join(', ')} fit; ${atLoad.text}`);

  const started = await u.start();
  if (!row(started.ok, 'start', started.ok ? `the demo plays ${started.ms} ms after START` : started.why)) return;

  // ---- each step ----
  const tried = [];
  let allOk = true;
  for (const step of [...fit.filter((s) => s !== first), first]) {     // the step shown at load is pressed last
    await u.press(`ZOOM ${step} %`, { parent: 'Zoom', role: ROLE.radio });
    const shown = await shows(step);
    const fine = shown.ok && shown.s.zoomPref === String(step) && shown.c.scroll <= shown.c.inner;
    allOk = allOk && fine;
    tried.push(`${step}: ${shown.text}${fine ? '' : ' (NOT as pressed)'}`);
  }
  const off = STEPS.filter((step) => !fit.includes(step));
  let offText = 'every step fits';
  if (off.length > 0) {
    await u.press(`ZOOM ${off[0]} %`, { parent: 'Zoom', role: ROLE.radio });
    const same = await holds(u, (s) => s.status.zoom === first && s.zoomPref === String(first));
    allOk = allOk && same.ok;
    offText = `a press on ${off[0]} (off): zoom ${same.s.status.zoom}, stored ${JSON.stringify(same.s.zoomPref)}`;
  }
  row(allOk && tried.length >= 2, 'steps', `${tried.join('; ')}; ${offText}`);

  // ---- the preference, across a reload ----
  const chosen = Math.max(...fit.filter((step) => step !== DEFAULT));   // not what a page with no preference shows
  await u.press(`ZOOM ${chosen} %`, { parent: 'Zoom', role: ROLE.radio });
  const picked = await shows(chosen);
  await leave();
  const again = u.up(await u.load({ ...LARGE, clear: false }));
  const kept = await shows(chosen);
  row(picked.ok && picked.s.zoomPref === String(chosen) && again.ok && kept.ok && kept.s.zoomPref === String(chosen),
      'reload', `chosen: ${picked.text}; after a reload: ${kept.text}`);

  // ---- a smaller window, and the large one again ----
  await u.p.metrics(SMALL.width, SMALL.height, 1);
  await u.until((s) => s.status.zoom === 100 && fits(s).length === 1);
  const small = await shows(100);
  const smallFit = fits(small.s);
  await u.p.metrics(LARGE.width, LARGE.height, 1);
  const large = await shows(chosen);
  row(small.ok && smallFit.length === 1 && small.s.zoomPref === String(chosen) && large.ok, 'refit',
      `${SMALL.width} x ${SMALL.height}: ${small.text}, the steps ${smallFit.join(', ')} fit; ${LARGE.width} x `
      + `${LARGE.height} again: ${large.text}`);
}
