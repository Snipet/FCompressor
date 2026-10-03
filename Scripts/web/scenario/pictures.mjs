// Scripts/web/scenario/pictures.mjs (--png only): PNGs of the canvas while the demo plays, into <out>/png: each of
// the six views in one stepped Mode (BUS G) and one continuous Mode (CLEAN) in GRAPHITE, and the panel in PAPER.
// Fourteen files, <mode>.<view>.<theme>.png, never committed. Every view is reached by presses, as a user reaches it.
//
//   pictures.bus-g, pictures.clean   seven pictures each: the Panel was on the screen the file is named after, in
//                                    that Mode and theme, when each was taken; each file is a PNG of the canvas's
//                                    size and not an empty one
//
// A picture is taken 0.6 s after its screen opened, the pointer moved away first, and the first of a Mode 1.5 s after
// the Mode changed: the times are for the screen's transition and the Mode change's own animation, and nothing is
// judged by them.
import { readFileSync } from 'node:fs';
import { join } from 'node:path';

import { OVERLAY, ROLE, SCREEN, sleep } from './driver.mjs';
import { mode } from './engine.mjs';

export const page = 'new';

const MODES = [{ key: 'bus-g', name: 'BUS G' }, { key: 'clean', name: 'CLEAN' }];
const SETTLE_MS = 600;
const MODE_SETTLE_MS = 1500;
const SMALLEST = 8000;                                // bytes: a canvas of one colour makes a PNG well under this

export async function run({ u, row, out }) {
  const at = (s, screen, overlay, tab = null) => s.a11y.screen === screen && s.a11y.overlay === overlay
                                                 && (tab === null || s.a11y.scTab === tab);
  const theme = (s) => u.value(s, 'Theme');
  const taken = [];
  // The picture `name`, once `test` holds; what was wrong with it otherwise.
  const shot = async (key, name, test) => {
    const there = await u.until(test);
    await u.hover('Footer');
    await sleep(SETTLE_MS);
    const file = join(out, 'png', `${key}.${name}.png`);
    const canvas = await u.shot(file);
    const png = readFileSync(file);
    const isPng = png.subarray(1, 4).toString('latin1') === 'PNG';
    const size = isPng ? `${png.readUInt32BE(16)} x ${png.readUInt32BE(20)}` : 'not a PNG';
    const fine = there.ok && isPng && canvas.bytes >= SMALLEST
                 && size === `${Math.round(canvas.width)} x ${Math.round(canvas.height)}`;
    taken.push({ fine, text: `${key}.${name}.png ${size}, ${canvas.bytes} bytes`
                             + `${there.ok ? '' : ' (NOT that screen)'}` });
  };

  for (const m of MODES) {
    taken.length = 0;
    await u.press('Mode', { role: ROLE.combo });
    await u.until((s) => s.a11y.overlay === OVERLAY.modeBrowser);
    await u.doubleClickAt(...u.centre(await u.item(m.name, { role: ROLE.row })));
    const chosen = await u.until((s) => mode(u, s).name === m.name && mode(u, s).fade === 1
                                        && s.a11y.overlay === OVERLAY.none);
    const graphite = (s) => /^Graphite/.test(theme(s)) && mode(u, s).name === m.name;
    await sleep(MODE_SETTLE_MS);

    await shot(m.key, 'panel.graphite', (s) => at(s, SCREEN.panel, OVERLAY.none) && graphite(s));
    await u.press('Paper theme', { parent: 'Theme' });
    await shot(m.key, 'panel.paper', (s) => at(s, SCREEN.panel, OVERLAY.none) && /^Paper/.test(theme(s)));
    await u.press('Graphite theme', { parent: 'Theme' });
    await u.until(graphite);

    await u.press('CHARACTERISTICS');
    await shot(m.key, 'chars.sidechain.graphite', (s) => at(s, SCREEN.characteristics, OVERLAY.none, 0) && graphite(s));
    await u.press('Colour', { parent: 'Side chain or colour' });
    await shot(m.key, 'chars.colour.graphite', (s) => at(s, SCREEN.characteristics, OVERLAY.none, 1) && graphite(s));
    await u.press('Side chain', { parent: 'Side chain or colour' });
    await u.until((s) => at(s, SCREEN.characteristics, OVERLAY.none, 0));
    await u.press('CHARACTERISTICS');
    await u.until((s) => at(s, SCREEN.panel, OVERLAY.none));

    await u.press('Mode', { role: ROLE.combo });
    await shot(m.key, 'modebrowser.graphite', (s) => at(s, SCREEN.panel, OVERLAY.modeBrowser) && graphite(s));
    await u.key('Escape');
    await u.until((s) => at(s, SCREEN.panel, OVERLAY.none));
    await u.press('Preset', { role: ROLE.combo });
    await shot(m.key, 'presetbrowser.graphite', (s) => at(s, SCREEN.panel, OVERLAY.presetBrowser) && graphite(s));
    await u.key('Escape');
    await u.until((s) => at(s, SCREEN.panel, OVERLAY.none));
    await u.press('Settings');
    await shot(m.key, 'settings.graphite', (s) => at(s, SCREEN.panel, OVERLAY.settings) && graphite(s));
    await u.key('Escape');
    const home = await u.until((s) => at(s, SCREEN.panel, OVERLAY.none));

    row(chosen.ok && home.ok && taken.length === 7 && taken.every((p) => p.fine), m.key,
        `${m.name}: ${taken.map((p) => p.text).join('; ')}`);
  }
}
