// Scripts/web/scenario/screens.mjs: every screen opens and closes by a real press or key, judged by the Panel's own
// state (its screen, overlay and tab) and by the items that screen lists.
//
//   screens.panel                    the main panel: the slots, HISTORY, the transfer curve
//   screens.characteristics          CHARACTERISTICS opens the screen on its SIDE CHAIN tab
//   screens.characteristics.colour   the COLOUR tab, and SIDE CHAIN again
//   screens.characteristics.closes   Escape leaves it; opened again, a second press on CHARACTERISTICS leaves it
//   screens.settings                 the gear opens settings (FORMAT says WEB); Escape closes; the gear closes
//   screens.modebrowser              the Mode name opens the Mode browser with all 14 Modes; Escape closes
//   screens.presetbrowser            the preset name opens the preset browser; a press outside it closes
//   screens.nothing-edited           none of it posted a parameter record to the engine
import { OVERLAY, ROLE, SCREEN } from './driver.mjs';

export const page = 'continue';

const MODES = 14;                                     // Source/fcdsp/modes/Modes.def

export async function run({ u, row }) {
  const say = (s) => (s && s.a11y ? `screen ${s.a11y.screen}, overlay ${s.a11y.overlay}, tab ${s.a11y.scTab}, `
                                    + `${s.a11y.items.length} items` : 'no list');
  const is = (s, screen, overlay) => s.a11y.screen === screen && s.a11y.overlay === overlay;
  const has = (s, title, where) => u.find(s, title, where) !== null;
  await u.item('THRESHOLD', { role: ROLE.slider });   // the list is there, or the group ends here
  const records = (await u.look()).tap.n;

  const home = await u.until((s) => is(s, SCREEN.panel, OVERLAY.none) && has(s, 'THRESHOLD', { role: ROLE.slider })
                                    && has(s, /^History, last/) && has(s, /^Transfer curve/));
  row(home.ok, 'panel', say(home.s));

  await u.press('CHARACTERISTICS');
  const chars = await u.until((s) => is(s, SCREEN.characteristics, OVERLAY.none) && s.a11y.scTab === 0
                                     && has(s, /^Side-chain response/) && has(s, /^Control path/));
  row(chars.ok, 'characteristics', say(chars.s));

  await u.press('Colour', { parent: 'Side chain or colour' });
  const colour = await u.until((s) => s.a11y.screen === SCREEN.characteristics && s.a11y.scTab === 1
                                      && has(s, /^Colour transfer/) && !has(s, /^Side-chain response/));
  await u.press('Side chain', { parent: 'Side chain or colour' });
  const side = await u.until((s) => s.a11y.screen === SCREEN.characteristics && s.a11y.scTab === 0
                                    && has(s, /^Side-chain response/) && !has(s, /^Colour transfer/));
  row(colour.ok && side.ok, 'characteristics.colour', `COLOUR: ${say(colour.s)}; SIDE CHAIN: ${say(side.s)}`);

  await u.key('Escape');
  const left = await u.until((s) => is(s, SCREEN.panel, OVERLAY.none) && has(s, 'THRESHOLD', { role: ROLE.slider }));
  await u.press('CHARACTERISTICS');
  const again = await u.until((s) => is(s, SCREEN.characteristics, OVERLAY.none));
  await u.press('CHARACTERISTICS');
  const leftAgain = await u.until((s) => is(s, SCREEN.panel, OVERLAY.none));
  row(left.ok && again.ok && leftAgain.ok, 'characteristics.closes',
      `Escape: ${say(left.s)}; opened again: ${say(again.s)}; CHARACTERISTICS again: ${say(leftAgain.s)}`);

  await u.press('Settings');
  const settings = await u.until((s) => is(s, SCREEN.panel, OVERLAY.settings) && has(s, 'Copy report')
                                        && /^WEB/.test(u.value(s, 'Format and host')));
  await u.key('Escape');
  const closed = await u.until((s) => is(s, SCREEN.panel, OVERLAY.none));
  await u.press('Settings');
  const reopened = await u.until((s) => is(s, SCREEN.panel, OVERLAY.settings));
  await u.press('Settings');
  const gear = await u.until((s) => is(s, SCREEN.panel, OVERLAY.none));
  row(settings.ok && closed.ok && reopened.ok && gear.ok, 'settings',
      `the gear: ${say(settings.s)}, FORMAT "${settings.ok ? u.value(settings.s, 'Format and host') : '?'}"; `
      + `Escape: ${say(closed.s)}; the gear twice: ${say(reopened.s)}, then ${say(gear.s)}`);

  await u.press('Mode', { role: ROLE.combo });
  const rows = (s) => s.a11y.items.filter((i) => i.role === ROLE.row);
  const modes = await u.until((s) => is(s, SCREEN.panel, OVERLAY.modeBrowser) && rows(s).length === MODES
                                     && rows(s).filter((i) => i.checked).length === 1);
  await u.key('Escape');
  const modesClosed = await u.until((s) => is(s, SCREEN.panel, OVERLAY.none));
  row(modes.ok && modesClosed.ok, 'modebrowser',
      `${say(modes.s)}: ${modes.s && modes.s.a11y ? rows(modes.s).length : 0} Modes listed, `
      + `${modes.ok ? rows(modes.s).find((i) => i.checked).title : '?'} current; Escape: ${say(modesClosed.s)}`);

  await u.press('Preset', { role: ROLE.combo });
  const presets = await u.until((s) => is(s, SCREEN.panel, OVERLAY.presetBrowser) && rows(s).length > 0
                                       && has(s, 'Status') && has(s, 'Show'));
  await u.press('Footer');                            // below the browser: a press outside it
  const presetsClosed = await u.until((s) => is(s, SCREEN.panel, OVERLAY.none));
  row(presets.ok && presetsClosed.ok, 'presetbrowser',
      `${say(presets.s)}, status "${presets.ok ? u.value(presets.s, 'Status') : '?'}"; a press outside: `
      + `${say(presetsClosed.s)}`);

  const after = await u.look();
  row(after.tap.n === records && after.a11y.textEntry < 0, 'nothing-edited',
      `${after.tap.n - records} parameter records posted while the screens opened and closed`);
}
