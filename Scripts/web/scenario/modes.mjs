// Scripts/web/scenario/modes.mjs: a Mode change, on a page of its own. Judged in the three places of engine.mjs: the
// Mode item's name, the slot the editor posted, and the slot the engine runs once its crossfade is done.
//
// The two Modes (Source/fcdsp/modes): BUS G, slot 1 in Modes.def, is the stepped one: its RATIO has three positions,
// 2:1, 4:1 and 10:1, which the engine runs as the slopes 0.5, 0.75 and 0.9 (bus-g/BusGDesc.cpp kRatio). CLEAN, slot 0,
// is the continuous one: its RATIO is any slope from 0 to 1 (clean/CleanDesc.cpp).
//
//   modes.arrows       the latch's right arrow goes to the next Mode and the left one back: the engine runs each
//   modes.wheel        the wheel over the Mode name moves through the Modes, there and back; the page stays
//   modes.stepped      the browser: a press on BUS G chooses it and the browser stays open; Escape closes. A drag on
//                      RATIO then lands on a position: the engine runs the slope of 10:1
//   modes.continuous   the browser: a double click on CLEAN chooses it and closes. The same drag on RATIO lands
//                      between BUS G's positions, and the engine runs that slope
//
// The arrows have no item of their own in the list: they are the two ends of the Mode item (24 px each:
// Source/editor/Layout.h header::kModePrev and kModeNext).
import { OVERLAY, PID, ROLE } from './driver.mjs';
import { mode, near } from './engine.mjs';
import { num } from './report.mjs';

export const page = 'new';

const STEPPED = { name: 'BUS G', slot: 1, slopes: [0.5, 0.75, 0.9] };
const CONTINUOUS = { name: 'CLEAN', slot: 0 };
const ARROW_PX = 12;                                  // the middle of an arrow, from its end of the Mode item

export async function run({ u, row }) {
  const latch = await u.item('Mode', { role: ROLE.combo });
  const middle = latch.y + latch.h / 2;
  const place = (s) => Number((/(\d+) OF \d+$/.exec(u.find(s, 'Mode', { role: ROLE.combo }).description) || [0, 0])[1]);
  // The engine runs the Mode the Panel names: the newest record's slot, no crossfade left.
  const runs = (s) => {
    const m = mode(u, s);
    return m.posted !== null && m.engine === m.posted && m.fade === 1;
  };
  const say = (s) => {
    const m = mode(u, s);
    return `${m.name} (${place(s)} in the order), the editor posted slot ${m.posted}, the engine runs slot ${m.engine}`;
  };
  const ratio = (s) => ({ panel: u.find(s, 'RATIO', { role: ROLE.slider }), posted: s.tap.last.v[PID.ratio],
                          engine: s.tap.reply.slope });
  const sayRatio = (r) => `the Panel "${r.panel.value}" (${r.panel.v}), the editor posted ${num(r.posted, 4)}, the `
                          + `engine runs the slope ${num(r.engine, 4)}`;

  const s0 = await u.look();
  const home = { name: mode(u, s0).name, slot: s0.tap.reply.modeSlot, place: place(s0) };

  // ---- the arrows ----
  await u.clickAt(latch.x + latch.w - ARROW_PX, middle);
  const next = await u.until((s) => runs(s) && mode(u, s).name !== home.name && mode(u, s).engine !== home.slot);
  await u.clickAt(latch.x + ARROW_PX, middle);
  const back = await u.until((s) => runs(s) && mode(u, s).name === home.name && mode(u, s).engine === home.slot);
  row(home.name === CONTINUOUS.name && home.slot === CONTINUOUS.slot && next.ok && back.ok
      && place(next.s) === home.place + 1, 'arrows',
      `from ${home.name} (${home.place} in the order, slot ${home.slot}): the right arrow: ${say(next.s)}; the left `
      + `arrow: ${say(back.s)}`);

  // ---- the wheel ----
  await u.wheelAt(...u.centre(latch), 120);
  const wheeled = await u.until((s) => runs(s) && place(s) < home.place);
  await u.wheelAt(...u.centre(latch), -120);
  const returned = await u.until((s) => runs(s) && wheeled.ok && place(s) > place(wheeled.s));
  row(wheeled.ok && returned.ok && returned.s.scrollY === 0, 'wheel',
      `the wheel down: ${say(wheeled.s)}; up: ${say(returned.s)}; the page scrolled ${returned.s.scrollY} px`);

  // ---- the browser, and a drag on RATIO in each of the two Modes ----
  const browse = async () => {
    await u.press('Mode', { role: ROLE.combo });
    await u.until((s) => s.a11y.overlay === OVERLAY.modeBrowser);
    return u.item(STEPPED.name, { role: ROLE.row });
  };
  const is = (s, which) => runs(s) && mode(u, s).name === which.name && mode(u, s).engine === which.slot;
  const closed = (s) => s.a11y.overlay === OVERLAY.none;
  const drag = async () => {
    const slot = await u.item('RATIO', { role: ROLE.slider });
    const before = (await u.look()).tap.n;
    await u.dragBy(...u.centre(slot), 40, 0);
    return u.until((s) => s.tap.n > before && near(ratio(s).engine, ratio(s).posted, 1e-4)
                          && ratio(s).engine > 0.75 + 0.01);
  };

  await u.clickAt(...u.centre(await browse()));
  const chosen = await u.until((s) => is(s, STEPPED) && s.a11y.overlay === OVERLAY.modeBrowser
                                      && u.find(s, STEPPED.name, { role: ROLE.row }).checked === 1);
  await u.key('Escape');
  const escaped = await u.until(closed);
  const stepped = await drag();
  const r1 = ratio(stepped.s);
  row(chosen.ok && escaped.ok && stepped.ok && near(r1.engine, STEPPED.slopes[2], 1e-4) && Number.isInteger(r1.panel.v),
      'stepped', `a press on ${STEPPED.name}: ${say(chosen.s)}, the browser is `
      + `${chosen.ok ? 'still open' : 'NOT as it should be'}; RATIO dragged 40 px: ${sayRatio(r1)}`);

  await browse();
  await u.doubleClickAt(...u.centre(await u.item(CONTINUOUS.name, { role: ROLE.row })));
  const chosenBack = await u.until((s) => is(s, CONTINUOUS) && closed(s));
  const continuous = await drag();
  const r2 = ratio(continuous.s);
  row(chosenBack.ok && continuous.ok && STEPPED.slopes.every((step) => !near(r2.engine, step, 0.01))
      && !Number.isInteger(r2.panel.v), 'continuous',
      `a double click on ${CONTINUOUS.name}: ${say(chosenBack.s)}, the browser is `
      + `${chosenBack.ok ? 'closed' : 'NOT as it should be'}; RATIO dragged 40 px: ${sayRatio(r2)}`);
}
