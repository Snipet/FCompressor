// Scripts/web/scenario/edits.mjs: undo and redo, A|B and its menu, on a page of its own (the history starts empty).
// Each row is judged by THRESHOLD in the engine (engine.mjs) and by the Panel's own buttons.
//
//   edits.undo.chord     UNDO is off on a new page and on after a drag, and its footer line names this platform's
//                        chord; Z with the other modifier does nothing; the chord moves the engine back
//   edits.redo.chord     Shift and the chord move the engine forward again
//   edits.undo.buttons   the UNDO and REDO buttons do the same
//   edits.ab.switch      B starts as A; an edit on B stays B's: A and B each bring their own value to the engine
//   edits.ab.menu        a right-click on a letter opens the menu (COPY the current one TO the other); Escape
//                        dismisses it and nothing changes
//   edits.ab.menu.outside   a press outside the menu, on NEXT PRESET, dismisses it and goes no further: the preset
//                        is not stepped and nothing is posted
//   edits.ab.menu.keys   the down arrow and Return choose: A then runs what B had
//   edits.ab.menu.click  a press on the item chooses: B then runs what A had
import { MOD, ROLE } from './driver.mjs';
import { holds, near, sayThreshold, threshold, thresholdIs } from './engine.mjs';

export const page = 'new';

const DEFAULT_DB = -18;                               // CLEAN's THRESHOLD

export async function run({ u, row }) {
  const slot = await u.item('THRESHOLD', { role: ROLE.slider });
  const [x, y] = u.centre(slot);
  const command = await u.commandKey();
  const other = command === MOD.meta ? MOD.ctrl : MOD.meta;
  const chord = command === MOD.meta ? 'CMD-Z' : 'CTRL-Z';
  const can = (s, title) => u.find(s, title, { role: ROLE.button }).enabled === 1;
  const say = (s) => `${sayThreshold(threshold(u, s))}; UNDO ${can(s, 'Undo') ? 'on' : 'off'}, REDO `
                     + `${can(s, 'Redo') ? 'on' : 'off'}`;
  const agrees = (s) => {
    const t = threshold(u, s);
    return near(t.panel, t.posted) && near(t.panel, t.engine);
  };

  // ---- undo and redo ----
  const fresh = await u.look();
  await u.dragBy(x, y, -40, 0);
  const dragged = await u.until((s) => agrees(s) && threshold(u, s).panel < DEFAULT_DB - 5 && can(s, 'Undo'));
  const edited = threshold(u, dragged.s).panel;
  await u.hover('Undo');
  const named = await u.until((s) => u.value(s, 'Footer').includes(chord));
  await u.key('z', other);
  const ignored = await holds(u, (s) => thresholdIs(u, s, edited) && can(s, 'Undo') && !can(s, 'Redo'));
  await u.key('z', command);
  const undone = await u.until((s) => thresholdIs(u, s, DEFAULT_DB) && !can(s, 'Undo') && can(s, 'Redo'));
  row(!can(fresh, 'Undo') && !can(fresh, 'Redo') && dragged.ok && named.ok && ignored.ok && undone.ok, 'undo.chord',
      `a new page: UNDO ${can(fresh, 'Undo') ? 'ON' : 'off'}; after a drag: ${say(dragged.s)}, the footer over UNDO `
      + `"${u.value(named.s, 'Footer')}"; Z with the other modifier: ${ignored.ok ? 'nothing' : say(ignored.s)}; `
      + `${chord}: ${say(undone.s)}`);

  await u.key('z', command | MOD.shift);
  const redone = await u.until((s) => thresholdIs(u, s, edited) && can(s, 'Undo') && !can(s, 'Redo'));
  row(redone.ok, 'redo.chord', `SHIFT-${chord}: ${say(redone.s)}`);

  await u.press('Undo');
  const byButton = await u.until((s) => thresholdIs(u, s, DEFAULT_DB) && !can(s, 'Undo') && can(s, 'Redo'));
  await u.press('Redo');
  const againByButton = await u.until((s) => thresholdIs(u, s, edited) && can(s, 'Undo') && !can(s, 'Redo'));
  row(byButton.ok && againByButton.ok, 'undo.buttons', `UNDO: ${say(byButton.s)}; REDO: ${say(againByButton.s)}`);

  // ---- A|B ----
  const LETTER = { parent: 'Compare', role: ROLE.radio };
  const on = (s, name) => u.value(s, 'Compare') === name;
  const go = async (name, want) => {
    await u.press(name, LETTER);
    return u.until((s) => on(s, name) && thresholdIs(u, s, want));
  };
  const a = edited;
  const toB = await go('B', a);                        // B starts as a copy of A
  await u.dragBy(x, y, 50, 0);
  const editedB = await u.until((s) => agrees(s) && threshold(u, s).panel > a + 5 && on(s, 'B'));
  const b = threshold(u, editedB.s).panel;
  const backToA = await go('A', a);
  const backToB = await go('B', b);
  row(toB.ok && editedB.ok && backToA.ok && backToB.ok, 'ab.switch',
      `B at first: ${say(toB.s)}; edited on B: ${b} dB; A again: ${say(backToA.s)}; B again: ${say(backToB.s)}`);

  // The menu, with B current: it offers to copy B to A.
  const records = threshold(u, backToB.s).records;
  const openMenu = async (name) => {
    await u.press(name, { ...LETTER, button: 'right' });
    return u.menuOpen();
  };
  const unchanged = (s) => on(s, 'B') && thresholdIs(u, s, b) && threshold(u, s).records === records;
  const menu = await openMenu('A');
  await u.key('Escape');
  const escaped = await u.menuGone();
  const still = await holds(u, unchanged);
  row(menu !== null && /^Copy B to A$/.test(u.menuTexts(menu)) && escaped && still.ok, 'ab.menu',
      `a right-click on A: ${menu ? `"${u.menuTexts(menu)}"` : 'no menu'}; Escape: the menu is `
      + `${escaped ? 'gone' : 'STILL THERE'}, ${say(still.s)}`);

  const preset = u.value(still.s, 'Preset');
  const second = await openMenu('B');
  await u.press('Next preset');
  const dismissed = await u.menuGone();
  const further = await holds(u, (s) => unchanged(s) && u.value(s, 'Preset') === preset);
  row(second !== null && dismissed && further.ok, 'ab.menu.outside',
      `a press on NEXT PRESET with the menu open: the menu is ${dismissed ? 'gone' : 'STILL THERE'}, the preset is `
      + `"${u.value(further.s, 'Preset')}" (it was "${preset}"), ${threshold(u, further.s).records - records} records `
      + 'posted');

  const third = await openMenu('A');
  await u.key('ArrowDown');
  await u.key('Enter');
  const chosen = await u.menuGone();
  const aHasB = await go('A', b);
  row(third !== null && chosen && aHasB.ok, 'ab.menu.keys',
      `the down arrow and Return on "${third ? u.menuTexts(third) : ''}": on A, ${say(aHasB.s)} (B had ${b})`);

  await u.dragBy(x, y, -30, 0);                        // A differs from B again
  const editedA = await u.until((s) => agrees(s) && threshold(u, s).panel < b - 5 && on(s, 'A'));
  const a2 = threshold(u, editedA.s).panel;
  const fourth = await openMenu('A');
  const item = fourth === null ? null : await u.menuPress(fourth, 'Copy A to B');
  const clicked = await u.menuGone();
  const bHasA = await go('B', a2);
  row(editedA.ok && item !== null && clicked && bHasA.ok, 'ab.menu.click',
      `a press on "${item ? item.text : ''}" with A at ${a2}: on B, ${say(bHasA.s)}`);
}
