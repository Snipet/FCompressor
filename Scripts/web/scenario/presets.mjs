// Scripts/web/scenario/presets.mjs: the presets, on a page of its own (no preset of the user's yet). A load is judged
// by the engine: the editor posts one snapped record, and the engine runs that record's Mode (its crossfade done) and,
// in the Mode the page starts in, its THRESHOLD and RATIO. The names are read from the browser's own rows, never
// known here; what is saved, renamed and deleted is judged by the Panel's list.
//
//   presets.browser.click         a press on a row of another Mode loads it and the browser stays open
//   presets.browser.double-click  a double click on a row loads it and closes the browser
//   presets.browser.keys          the down arrow loads the next row, twice; Return closes the browser
//   presets.browser.wheel         the wheel scrolls the list, not the page, and loads nothing
//   presets.next-previous         the header's arrows step to the row before and back, the engine following both
//   presets.wrap                  PREVIOUS on the first preset goes to the last, NEXT returns: the engine runs what
//                                 the page started with
//   presets.modified              an edit marks the preset MODIFIED
//   presets.save-as               SAVE on a factory preset asks for a name; a typed name and Return make a preset of
//                                 the user's, current and not modified, and post nothing
//   presets.save                  SAVE on that preset, edited, saves over it at one press
//   presets.menu.save             SAVE's menu (Save, Save As...): Escape dismisses it and nothing happens; Save As...
//                                 opens the name field on the preset's name and says it is taken; typing replaces
//                                 it, Backspace takes a character away, and Escape leaves the browser open with
//                                 nothing saved
//   presets.menu.row              a row's menu (Load, Save As..., Rename..., Export... and Import... disabled,
//                                 Delete); a press outside dismisses it and goes no further; Rename... opens the
//                                 name field on the row's name, and Escape leaves the row as it was
//   presets.rename                RENAME with a typed name renames the row and the current preset
//   presets.delete                DELETE arms and says so; the second press deletes
//   presets.import-export         IMPORT and EXPORT are disabled, and the footer says why
import { OVERLAY, PID, ROLE } from './driver.mjs';
import { holds, near } from './engine.mjs';
import { num } from './report.mjs';

export const page = 'new';

const NAME = 'Scenario One';
const ROW_MENU = 'Load | Save As... | Rename... | Export... (disabled) | Import... (disabled) | Delete';
const RENAMED = 'Scenario Renamed';

export async function run({ u, row }) {
  const preset = (s) => u.find(s, 'Preset', { role: ROLE.combo });
  const rows = (s) => s.a11y.items.filter((i) => i.role === ROLE.row);
  const names = (s) => rows(s).map((i) => i.title);
  const browser = (s) => s.a11y.overlay === OVERLAY.presetBrowser;
  const field = (s) => u.find(s, /^New (preset )?name$/);
  const modified = (s) => /, modified$/.test(preset(s).value);
  const modeOf = (item) => item.description.split(', ')[1];      // a row: "<category>, <Mode>, factory|user"
  // What the engine runs, as a text, and whether it runs the newest record.
  const engine = (s) => `${s.tap.reply.modeSlot}/${num(s.tap.reply.thrDb, 3)}/${num(s.tap.reply.slope, 4)}`;
  const runsRecord = (s) => {
    const r = s.tap.last;
    if (r === null || r.snap !== 1 || s.tap.reply.modeSlot !== r.v[PID.mode] || s.tap.reply.fade !== 1) return false;
    return r.v[PID.mode] !== first.slot
           || (near(s.tap.reply.thrDb, r.v[PID.thr]) && near(s.tap.reply.slope, r.v[PID.ratio]));
  };
  const say = (s) => `"${preset(s).value}" (${preset(s).description}), Mode `
                     + `${u.value(s, 'Mode', { role: ROLE.combo })}, the engine runs slot/threshold/slope `
                     + `${engine(s)}, ${s.tap.n} records`;
  // Loaded: the Panel names the preset, one more record than `before` was posted, snapped, and the engine runs it.
  const loaded = (name, before) => (s) => preset(s).value === name && s.tap.n === before + 1 && runsRecord(s);
  const open = async () => {
    await u.press('Preset', { role: ROLE.combo });
    return u.until((s) => browser(s) && rows(s).length > 1);
  };

  const s0 = await u.look();
  const first = { name: preset(s0).value, slot: s0.tap.reply.modeSlot, engine: engine(s0),
                  mode: u.value(s0, 'Mode', { role: ROLE.combo }) };

  // ---- the browser ----
  const opened = await open();
  const list = rows(opened.s);
  const current = list.findIndex((i) => i.checked === 1);
  const far = list.find((i) => modeOf(i) !== first.mode);
  let n = opened.s.tap.n;
  await u.clickAt(...u.centre(far));
  const clicked = await u.until((s) => loaded(far.title, n)(s) && browser(s)
                                       && u.value(s, 'Mode', { role: ROLE.combo }) === modeOf(far)
                                       && s.tap.reply.modeSlot !== first.slot
                                       && rows(s).find((i) => i.checked).title === far.title);
  row(opened.ok && current === 0 && list[0].title === first.name && clicked.ok, 'browser.click',
      `${list.length} rows, "${list[current] ? list[current].title : '?'}" current; a press on "${far.title}" `
      + `(${far.description}): ${say(clicked.s)}; the browser is ${browser(clicked.s) ? 'still open' : 'CLOSED'}`);

  n = clicked.s.tap.n;
  await u.doubleClickAt(...u.centre(list[1]));
  // Two presses, two loads of the same row: the engine runs it and the browser is closed.
  const doubled = await u.until((s) => preset(s).value === list[1].title && s.tap.n > n && runsRecord(s)
                                       && s.a11y.overlay === OVERLAY.none
                                       && u.value(s, 'Mode', { role: ROLE.combo }) === modeOf(list[1]));
  row(doubled.ok, 'browser.double-click',
      `a double click on "${list[1].title}": ${say(doubled.s)}; the browser is `
      + `${doubled.s.a11y.overlay === OVERLAY.none ? 'closed' : 'STILL OPEN'}`);

  await open();
  n = (await u.look()).tap.n;
  await u.key('ArrowDown');
  const down1 = await u.until(loaded(list[2].title, n));
  await u.key('ArrowDown');
  const down2 = await u.until(loaded(list[3].title, n + 1));
  await u.key('Enter');
  const entered = await u.until((s) => s.a11y.overlay === OVERLAY.none && preset(s).value === list[3].title);
  row(down1.ok && down2.ok && entered.ok, 'browser.keys',
      `the down arrow: ${say(down1.s)}; again: ${say(down2.s)}; Return: the browser is `
      + `${entered.ok ? 'closed' : 'STILL OPEN'}`);

  const reopened = await open();
  const top = names(reopened.s)[0];
  n = reopened.s.tap.n;
  await u.wheelAt(...u.centre(rows(reopened.s)[4]), 240);
  const scrolled = await u.until((s) => browser(s) && names(s)[0] !== top);
  row(scrolled.ok && scrolled.s.scrollY === 0 && scrolled.s.tap.n === n && preset(scrolled.s).value === list[3].title,
      'browser.wheel', `the first row was "${top}" and is "${names(scrolled.s)[0]}"; the page scrolled `
      + `${scrolled.s.scrollY} px; ${scrolled.s.tap.n - n} records posted`);
  await u.key('Escape');
  await u.until((s) => s.a11y.overlay === OVERLAY.none);

  // ---- the header's arrows ----
  n = (await u.look()).tap.n;
  const before = engine(entered.s);
  await u.press('Previous preset');
  const stepped = await u.until(loaded(list[2].title, n));
  await u.press('Next preset');
  const steppedBack = await u.until(loaded(list[3].title, n + 1));
  row(stepped.ok && steppedBack.ok && engine(stepped.s) !== before && engine(steppedBack.s) === before
      && engine(stepped.s) === engine(down1.s), 'next-previous',
      `from "${list[3].title}" (${before}): PREVIOUS: ${say(stepped.s)}; NEXT: ${say(steppedBack.s)}`);

  for (let i = 3; i > 0; i -= 1) {
    await u.press('Previous preset');
    await u.until(loaded(list[i - 1].title, n + 2 + (3 - i)));
  }
  const atFirst = await u.look();
  n = atFirst.tap.n;
  await u.press('Previous preset');
  const wrapped = await u.until((s) => !names(opened.s).slice(0, 4).includes(preset(s).value) && s.tap.n === n + 1
                                       && runsRecord(s));
  await u.press('Next preset');
  const home = await u.until((s) => loaded(first.name, n + 1)(s) && engine(s) === first.engine);
  row(preset(atFirst).value === first.name && wrapped.ok && home.ok, 'wrap',
      `on "${preset(atFirst).value}", PREVIOUS: ${say(wrapped.s)}; NEXT: ${say(home.s)} (the page started with `
      + `${first.engine})`);

  // ---- MODIFIED, SAVE AS, SAVE ----
  const slot = await u.item('THRESHOLD', { role: ROLE.slider });
  await u.dragBy(...u.centre(slot), -30, 0);
  const marked = await u.until((s) => modified(s) && near(s.tap.reply.thrDb, s.tap.last.v[PID.thr])
                                      && s.tap.last.snap === 0);
  row(marked.ok && !modified(home.s), 'modified', `after a drag on THRESHOLD: ${say(marked.s)}`);

  n = marked.s.tap.n;
  await u.press('Save preset');
  const asking = await u.until((s) => browser(s) && field(s) !== null);
  await u.type(NAME);
  const typed = await u.until((s) => field(s) !== null && field(s).value === NAME);
  await u.key('Enter');
  const saved = await u.until((s) => s.a11y.overlay === OVERLAY.none && preset(s).value === NAME
                                     && /(^|, )user$/.test(preset(s).description));
  row(asking.ok && typed.ok && saved.ok && saved.s.tap.n === n, 'save-as',
      `SAVE on "${first.name}, modified": the field holds "${asking.ok ? field(asking.s).value : '?'}" `
      + `("${u.value(asking.s, 'Status')}"); typed: "${typed.ok ? field(typed.s).value : '?'}"; Return: `
      + `${say(saved.s)}`);

  await u.dragBy(...u.centre(slot), 20, 0);
  const edited = await u.until((s) => modified(s) && preset(s).value === `${NAME}, modified`);
  n = edited.s.tap.n;
  await u.press('Save preset');
  const over = await u.until((s) => preset(s).value === NAME);
  const quiet = await holds(u, (s) => s.a11y.overlay === OVERLAY.none && preset(s).value === NAME && s.tap.n === n);
  row(edited.ok && over.ok && quiet.ok, 'save',
      `edited: "${preset(edited.s).value}"; SAVE: ${say(quiet.s)}; no browser, nothing posted`);

  // ---- the menus ----
  await u.press('Save preset', { button: 'right' });
  const dismissed = await u.menuOpen();
  await u.key('Escape');
  const escaped = dismissed !== null && await u.menuGone();
  const untouched = await holds(u, (s) => s.a11y.overlay === OVERLAY.none && preset(s).value === NAME && s.tap.n === n);
  await u.press('Save preset', { button: 'right' });
  const saveMenu = await u.menuOpen();
  if (saveMenu !== null) await u.menuPress(saveMenu, 'Save As...');
  const again = await u.until((s) => browser(s) && field(s) !== null && field(s).value === NAME
                                     && /^TAKEN/.test(u.value(s, 'Status')));
  await u.type(`${NAME}x`);                            // the field's text is selected: typing replaces it
  const free = await u.until((s) => field(s) !== null && field(s).value === `${NAME}x`
                                    && !/^TAKEN/.test(u.value(s, 'Status')));
  await u.key('Backspace');
  const taken = await u.until((s) => field(s) !== null && field(s).value === NAME
                                     && /^TAKEN/.test(u.value(s, 'Status')));
  await u.key('Escape');
  const kept = await u.until((s) => browser(s) && field(s) === null);
  const mine = (s) => u.find(s, 'User', { parent: 'Show' }).description;
  row(escaped && untouched.ok && saveMenu !== null && u.menuTexts(saveMenu) === 'Save | Save As...' && again.ok
      && free.ok && taken.ok && kept.ok && mine(kept.s) === '1 preset', 'menu.save',
      `a right-click on SAVE: "${saveMenu ? u.menuTexts(saveMenu) : 'no menu'}"; Escape: the menu is `
      + `${escaped ? 'gone' : 'STILL THERE'} and ${untouched.ok ? 'nothing happened' : 'SOMETHING CHANGED'}; Save `
      + `As...: the field holds `
      + `"${again.ok ? field(again.s).value : '?'}" ("${u.value(again.s, 'Status')}"); typed "${NAME}x": `
      + `"${u.value(free.s, 'Status')}"; Backspace: "${taken.ok ? field(taken.s).value : '?'}" `
      + `("${u.value(taken.s, 'Status')}"); Escape: the browser is `
      + `${kept.ok ? 'open, the field gone' : 'NOT as it was'}, yours: ${mine(kept.s)}`);

  await u.press('User', { parent: 'Show' });
  const shown = await u.until((s) => names(s).length === 1 && names(s)[0] === NAME);
  const own = await u.item(NAME, { role: ROLE.row });
  await u.clickAt(...u.centre(own), { button: 'right' });
  const rowMenu = await u.menuOpen();
  await u.press('Next preset');
  const gone = await u.menuGone();
  const unmoved = await holds(u, (s) => preset(s).value === NAME && browser(s));
  await u.clickAt(...u.centre(own), { button: 'right' });
  const rowMenuAgain = await u.menuOpen();
  if (rowMenuAgain !== null) await u.menuPress(rowMenuAgain, 'Rename...');
  const asked = await u.until((s) => field(s) !== null && field(s).value === NAME);
  await u.key('Escape');
  const left = await u.until((s) => browser(s) && field(s) === null && names(s).length === 1 && names(s)[0] === NAME);
  row(shown.ok && rowMenu !== null && gone && unmoved.ok && rowMenuAgain !== null && asked.ok && left.ok
      && u.menuTexts(rowMenu) === ROW_MENU,
      'menu.row', `USER shows ${JSON.stringify(names(shown.s))}; a right-click on the row: `
      + `"${rowMenu ? u.menuTexts(rowMenu) : 'no menu'}"; a press on NEXT PRESET: the menu is `
      + `${gone ? 'gone' : 'STILL THERE'} and the preset is "${preset(unmoved.s).value}"; Rename...: the field `
      + `${asked.ok ? `holds "${field(asked.s).value}"` : 'did NOT open'}; Escape: the rows `
      + `${JSON.stringify(names(left.s))}`);

  // ---- RENAME, DELETE ----
  await u.press('Rename', { role: ROLE.button });
  const renaming = await u.until((s) => field(s) !== null && field(s).value === NAME);
  await u.type(RENAMED);
  await u.key('Enter');
  const renamed = await u.until((s) => field(s) === null && names(s).length === 1 && names(s)[0] === RENAMED
                                       && preset(s).value === RENAMED);
  row(renaming.ok && renamed.ok, 'rename',
      `RENAME and "${RENAMED}": the rows ${JSON.stringify(names(renamed.s))}, the preset "${preset(renamed.s).value}", `
      + `the status "${u.value(renamed.s, 'Status')}"`);

  await u.press('Delete', { role: ROLE.button });
  const armed = await u.until((s) => u.find(s, 'Confirm delete') !== null && /AGAIN/.test(u.value(s, 'Status')));
  await u.press('Confirm delete');
  const deleted = await u.until((s) => names(s).length === 0 && mine(s) === '0 presets'
                                       && u.find(s, 'Delete', { role: ROLE.button }).enabled === 0);
  row(armed.ok && names(armed.s).length === 1 && deleted.ok, 'delete',
      `DELETE: "${u.value(armed.s, 'Status')}" (${names(armed.s).length} row); again: `
      + `${names(deleted.s).length} rows, yours: ${mine(deleted.s)}, the status "${u.value(deleted.s, 'Status')}"`);

  // ---- IMPORT, EXPORT ----
  await u.press('All', { parent: 'Show' });
  await u.until((s) => names(s).length > 1);
  const cells = {};
  for (const title of ['Import', 'Export']) {
    const cell = await u.hover(title, { role: ROLE.button });
    const told = await u.until((s) => /NOT AVAILABLE HERE/.test(u.value(s, 'Footer')));
    cells[title] = { enabled: cell.enabled, footer: u.value(told.s, 'Footer'), ok: told.ok };
  }
  n = (await u.look()).tap.n;
  await u.press('Import', { role: ROLE.button });
  const nothing = await holds(u, (s) => browser(s) && field(s) === null && s.tap.n === n);
  row(cells.Import.enabled === 0 && cells.Export.enabled === 0 && cells.Import.ok && cells.Export.ok
      && /^IMPORTING/.test(cells.Import.footer) && /^EXPORTING/.test(cells.Export.footer) && nothing.ok,
      'import-export', `IMPORT is ${cells.Import.enabled ? 'ENABLED' : 'disabled'}: "${cells.Import.footer}"; EXPORT `
      + `is ${cells.Export.enabled ? 'ENABLED' : 'disabled'}: "${cells.Export.footer}"; a press on IMPORT `
      + `${nothing.ok ? 'does nothing' : 'DOES SOMETHING'}`);
  await u.key('Escape');
  await u.until((s) => s.a11y.overlay === OVERLAY.none);
}
