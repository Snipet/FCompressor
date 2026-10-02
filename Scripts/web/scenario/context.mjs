// Scripts/web/scenario/context.mjs: a lost WebGL context, on a page of its own. A user cannot lose a context by hand:
// the browser's own WEBGL_lose_context extension does it, on the editor's canvas, and nothing else here is scripted.
//
// The pixel row (Module.fcmpSelftest(): one frame through the WebGL sink, read back and compared with SoftRaster's
// picture of the same frame) is exact only on a still frame, so it is taken before START, with the first-use hint
// pinned away (?nohint=1), once the Panel no longer asks for the full frame rate. It is judged by the renderer's
// class, as the page's own self-test judges it: on a GPU no sample differs by more than 2 of 255; on a software
// renderer (SwiftShader, llvmpipe, softpipe) none by more than 16 and at most 10 in 1000 by more than 2. A font atlas
// that was not uploaded again after the restore fails either rule by a wide margin.
//
//   context.still            before any loss: the still frame is SoftRaster's
//   context.still.restored   lost and restored before START: no frame can be read while it is lost, and afterwards
//                            the still frame is SoftRaster's again, from the same atlas
//   context.lost             lost while the demo plays: the editor says so without an error and draws nothing (the
//                            frames it would have drawn are counted as lost), while the replies and the audio go on
//                            and the page still says PLAYING
//   context.lost.edit        an edit made while it is lost reaches the engine
//   context.restored         restored: the editor draws again and counts no more lost frames
//   context.restored.edit    an edit afterwards reaches the engine
import { ROLE } from './driver.mjs';
import { holds, sayThreshold, threshold, thresholdIs } from './engine.mjs';

export const page = 'own';

const DEFAULT_DB = -18;                               // CLEAN's THRESHOLD
const SOFTWARE = /swiftshader|llvmpipe|softpipe|software/i;

// The WebGL renderer's name, from a canvas of its own that is given up at once.
const RENDERER = `(() => {
  const gl = document.createElement('canvas').getContext('webgl2');
  if (!gl) return '';
  const info = gl.getExtension('WEBGL_debug_renderer_info');
  const name = String(gl.getParameter(info ? info.UNMASKED_RENDERER_WEBGL : gl.RENDERER));
  const lose = gl.getExtension('WEBGL_lose_context');
  if (lose) lose.loseContext();
  return name;
})()`;
// The extension on the editor's own context (a canvas has one context: asking again gives the same one).
const LOSE = `(() => {
  const gl = document.getElementById('fcmp-canvas').getContext('webgl2');
  globalThis.fcmpScenarioLose = gl ? gl.getExtension('WEBGL_lose_context') : null;
  if (!globalThis.fcmpScenarioLose) return false;
  globalThis.fcmpScenarioLose.loseContext();
  return true;
})()`;
const RESTORE = '(() => { globalThis.fcmpScenarioLose.restoreContext(); return true; })()';

export async function run({ u, row, note }) {
  const selftest = async () => JSON.parse(await u.p.ev('Module.fcmpSelftest()'));
  const still = (ms = 15000) => u.until((s) => s.status.ok === 1 && s.a11y.fullRate === 0, ms);

  const loaded = u.up(await u.load({ query: '?nohint=1' }));
  u.list(loaded.s);                                   // whether the Panel is at rest is the list's to say
  const software = SOFTWARE.test(await u.p.ev(RENDERER));
  const exact = (p) => p.frames === 1 && p.samples > 0
                       && (software ? p.largest <= 16 && p.over2 * 1000 <= p.samples * 10 : p.over2 === 0);
  const say = (p) => `${p.frames} frame read back, ${p.samples} samples: the largest difference ${p.largest} of 255, `
                     + `${p.over2} over 2`;
  note('renderer', software ? 'a software renderer: none over 16 and at most 10 in 1000 over 2'
                            : 'a GPU: none over 2');

  // ---- the still frame, before START ----
  const rested = await still();
  const first = await selftest();
  row(loaded.ok && rested.ok && exact(first.pixels), 'still',
      `before START, the Panel at rest (${rested.ms} ms): ${say(first.pixels)}; atlas ${first.atlasHash}`);

  const could = await u.p.ev(LOSE);
  const gone = await u.until((s) => s.status.ok === 0 && s.status.error === '');
  const blind = await selftest();
  await u.p.ev(RESTORE);
  const drawing = await u.until((s) => s.status.ok === 1 && s.status.frames > gone.s.status.frames);
  const restedAgain = await still();
  const second = await selftest();
  row(could && gone.ok && blind.pixels.frames === 0 && drawing.ok && restedAgain.ok && exact(second.pixels)
      && second.atlasHash === first.atlasHash && second.pixels.samples === first.pixels.samples, 'still.restored',
      `lost: the status ok ${gone.s.status.ok}, error "${gone.s.status.error}", ${blind.pixels.frames} frames read `
      + `back; restored: ${say(second.pixels)}; atlas ${second.atlasHash}`);

  // ---- while the demo plays ----
  const started = await u.start();
  if (!row(started.ok, 'start', started.ok ? `the demo plays ${started.ms} ms after START` : started.why)) return;
  const slot = await u.item('THRESHOLD', { role: ROLE.slider });

  await u.p.ev(LOSE);
  const lost = await u.until((s) => s.status.ok === 0 && s.status.error === '');
  const l0 = await u.ledger();
  const counted = await u.until((s) => s.status.lost >= lost.s.status.lost + 5
                                       && s.status.replies >= lost.s.status.replies + 5);
  const undrawn = await holds(u, (s) => s.status.ok === 0 && s.status.frames === lost.s.status.frames
                                        && s.says === 'PLAYING' && s.state === 'running' && s.context === 'running');
  const l1 = await u.ledger();
  row(lost.ok && counted.ok && undrawn.ok && l1.worklet.quanta > l0.worklet.quanta && l1.worklet.ok, 'lost',
      `the status: ok ${undrawn.s.status.ok}, error "${undrawn.s.status.error}"; `
      + `${undrawn.s.status.frames - lost.s.status.frames} frames drawn, ${undrawn.s.status.lost - lost.s.status.lost} `
      + `counted as lost, ${undrawn.s.status.replies - lost.s.status.replies} replies, `
      + `${l1.worklet.quanta - l0.worklet.quanta} quanta meanwhile; the page says "${undrawn.s.says}"`);

  await u.dragBy(...u.centre(slot), -40, 0);
  const edited = await u.until((s) => threshold(u, s).panel < DEFAULT_DB - 5
                                      && thresholdIs(u, s, threshold(u, s).panel) && s.status.ok === 0);
  row(edited.ok, 'lost.edit', `a drag on THRESHOLD while lost: ${sayThreshold(threshold(u, edited.s))}`);

  await u.p.ev(RESTORE);
  const back = await u.until((s) => s.status.ok === 1 && s.status.frames >= edited.s.status.frames + 10);
  const steady = await holds(u, (s) => s.status.ok === 1 && s.status.lost === back.s.status.lost);
  const more = await u.until((s) => s.status.frames > steady.s.status.frames
                                    && s.status.replies > steady.s.status.replies);
  row(back.ok && steady.ok && more.ok, 'restored',
      `the status: ok ${more.s.status.ok}; ${more.s.status.frames - edited.s.status.frames} frames drawn since, `
      + `${more.s.status.lost - back.s.status.lost} more counted as lost`);

  await u.doubleClickAt(...u.centre(slot));
  const reset = await u.until((s) => thresholdIs(u, s, DEFAULT_DB));
  row(reset.ok, 'restored.edit', `a double click on THRESHOLD: ${sayThreshold(threshold(u, reset.s))}`);
}
