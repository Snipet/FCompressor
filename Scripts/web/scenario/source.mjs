// Scripts/web/scenario/source.mjs: the two loops, their buttons, and a sample loop that does not come, on pages of
// its own. What plays is judged by three witnesses together (plays.mjs): the page's source line, fcmpPage.source() and
// the engine's input. Where a row needs the sample loop's file out of reach, the driver answers that one address in
// the server's place (u.loopFile(): the DevTools protocol stops the request in the browser): a 404, no connection, or
// no answer until the row gives one. The site and the server are never changed.
//
// A page as it is shipped:
//   source.sample   after START the sample loop plays: the page says so, source() is the file as one period at the
//                   context's rate, and one whole loop of the engine's input is the file's loop millisecond by
//                   millisecond, on into its second pass: the page plays the file at its own level, with no fade,
//                   and looped. The two input meters show the file's two sides. The file was asked of the server
//                   once, with no word about the cache
//   source.synth    SYNTH LOOP pressed: the synth loop plays, and one whole loop of the engine's input is the synth
//                   loop millisecond by millisecond, on into its second pass
//   source.back     SAMPLE LOOP pressed: the sample loop is back, the notice empty, and the file was not asked again
//   source.quick    SAMPLE LOOP and then SYNTH LOOP pressed under 30 ms apart, while a file at -70 dBFS plays: the
//                   source between the two never reaches the engine. The engine's input rises from the file's level
//                   as the synth loop does when it fades in, under -23 dBFS in its first two milliseconds (the sample
//                   loop begun part of the way down a fade would be at -14 dBFS and above there), and the synth loop
//                   plays afterwards
// The file answered 404:
//   source.lost     START still comes to PLAYING: the synth loop plays, the notice says the sample loop did not load,
//                   and SAMPLE LOOP stays enabled
//   source.again    a press while the file is still missing says LOADING, then that the source is unchanged, and the
//                   synth loop plays on; a press once the server answers plays the sample loop and clears the
//                   notice, and that request went round the browser's cache (Cache-Control: no-cache at the server).
//                   The three buttons are right after each
// The file held back while START loads:
//   source.kept     a file dropped while the page says LOADING is kept, and plays once the demo runs
//   source.fault    the engine says it stopped, and on another page the editor aborts: START ends at once on each
//                   (not when the loop's time is up), and the request for the file is given up
// The file answered 404, then a load held back:
//   source.ends     the audio context closed from outside while a load runs: the demo stops and says so, the notice
//                   is cleared, the three buttons are disabled, and the request is given up
// The file never answered:
//   source.slow     START says LOADING for the 15 s the page gives the file, and no more than 1.5 s longer; then it
//                   plays the synth loop with the notice of source.lost; the page has given the request up by then
//   source.turn     a later choice wins over a load that still runs, and the load says nothing and changes nothing
//                   when it ends: a file dropped during a load, and SYNTH LOOP pressed during one, each with a load
//                   that then fails and with one whose whole file then comes. The three buttons are right after
//                   each. Once the file has come, a press plays the sample loop with no new request. (A load that
//                   came whole leaves the loop loaded, so the fourth of the four is on a page of its own, where the
//                   file answered 404 at START.)
// The engine's stop and the editor's abort are what the page hears of each: a message `error` on the worklet's port,
// and Module.onAbort, the editor's half of the seam. The context is closed by its own close(). A user can do none of
// the three, so the script does them in the page; nothing else here is made in the page.
import { holds } from './engine.mjs';
import { NOTICE, QUIET_DB, RISE_MS, SAMPLE, SYNTH, buttons, quiet, rise, sampleLoop, synthLoop, tone,
         toneFile } from './plays.mjs';

export const page = 'own';

const SAMPLE_MS = 15000;                              // web/main.js: how long START waits for the file
// The page runs this long after that at most. Of it are the press itself, the synth loop that is then made (50 ms) and
// the look that sees the page run: 134 to 262 ms in twenty runs under load here, and 244 to 539 ms with the page's CPU
// slowed four and six times, on the software renderer too (a CI runner is slower than this machine). It was 3000, and
// a page that waited 17 s passed: at 1500 that page fails by half a second.
const LATE_MS = 1500;
const FAULT_MS = 5000;                                // "at once", against the 15 s of a load that is not given up
const QUIET_MS = 400;                                 // what a load must not say is watched for this long
const STOPPED = 'THE DEMO STOPPED: THE AUDIO CONTEXT CLOSED. RELOAD THE PAGE TO START IT AGAIN.';
const FAILED = (who) => `THE DEMO COULD NOT START: THE ${who} STOPPED (THE SCENARIO SAYS SO)`;
// What the page hears when its engine or its editor fails.
const ENGINE_STOPS = "fcmpPage.node().port.dispatchEvent(new MessageEvent('message', "
                     + "{ data: { fcmp: 'error', error: 'the scenario says so' } })), true";
const EDITOR_ABORTS = "Module.onAbort('the scenario says so'), true";
const CLOSE_CONTEXT = 'fcmpPage.context().close().then(() => true)';

// source.quick judges two presses that the page took this far apart by its audio context's clock, in ms: at least
// three render quanta of 128 frames (nearer than that, a source that began between them would have played too
// briefly to be told from none), and under the 30 ms a source takes to give way. The driver asks for 12 ms between
// them, and for more or less when the page took them otherwise: six tries at most.
const QUICK = { least: 7, under: 29.5, ask: 12, step: 6, tries: 6 };

const kindOf = (s) => (s && s.plays ? s.plays.kind : 'none');
const noButton = (s) => !!s && !s.can.loop && !s.can.synth && !s.can.open;
// The three buttons while each source plays: OPEN always, and a loop's button unless that loop is what plays.
const samplePlays = (s) => !!s && !s.can.loop && s.can.synth && s.can.open;
const synthPlays = (s) => !!s && s.can.loop && !s.can.synth && s.can.open;
const filePlays = (s) => !!s && s.can.loop && s.can.synth && s.can.open;
const given = (requests) => requests.length === 1 && requests[0].gone;
const givenSaid = (requests) => (given(requests) ? 'was given up' : 'was NOT given up');

// SAMPLE LOOP and then SYNTH LOOP pressed in quick succession while the quiet file `hush` plays: { ok, text }.
async function quick(u, hush) {
  const others = [];                                  // how far apart the tries that were not judged came
  let ask = QUICK.ask;
  for (let n = 0; n < QUICK.tries; n += 1) {
    await u.drop(hush.file);
    const named = await u.until((s) => s.source === hush.line && s.notice === '', 8000);
    const before = await quiet(u, hush);
    if (!named.ok || !before.ok) return { ok: false, text: `the quiet file does NOT play: ${before.text}` };
    await u.hear(0, RISE_MS);
    const apart = await u.pressTwo('fcmp-loop', 'fcmp-synth', ask);
    // The page's 30 ms and the first of the next source are in the columns kept: a listening, not a wait.
    const came = await u.pace((s) => s.state !== 'running' || s.tap.heard.n >= RISE_MS, RISE_MS + 5000);
    const levels = await u.levels();
    const whole = came.ok && came.s.state === 'running' && came.s.tap.heard.lost === 0;
    if (whole && apart >= QUICK.least && apart < QUICK.under) {
      const rose = rise(levels);
      const after = await synthLoop(u);
      return { ok: rose.ok && after.ok && after.s.notice === '',
               text: `SAMPLE LOOP and then SYNTH LOOP pressed ${apart.toFixed(1)} ms apart by the audio context's `
                     + `clock, while ${before.text}: ${rose.text}. Then ${after.text}; the notice is `
                     + `"${after.s.notice}"${others.length > 0 ? ` (not judged: the tries that came `
                                                                 + `${others.join(', ')} ms apart)` : ''}` };
    }
    others.push(Number.isFinite(apart) ? apart.toFixed(1) : 'not taken');
    ask = apart < QUICK.least ? ask + QUICK.step : Math.max(0, ask - QUICK.step);
  }
  return { ok: false, text: `no two presses came ${QUICK.least} to ${QUICK.under} ms apart by the audio context's `
                            + `clock in ${QUICK.tries} tries (they came ${others.join(', ')} ms apart): nothing to `
                            + 'judge' };
}

export async function run({ u, row, scratch, leave }) {
  const wav = await u.loopFile();
  const choice = toneFile(scratch, 'scenario-choice');
  const kept = toneFile(scratch, 'scenario-kept');
  const hush = toneFile(scratch, 'scenario-quiet', QUIET_DB);
  const loading = () => u.until((s) => s.notice === NOTICE.loading && wav.waiting() === 1);
  // A new page with the file answered as `answer` from its first request on.
  const open = async (answer) => {
    wav.answer = answer;
    u.up(await u.load());
  };
  try {
    // ---- a page as it is shipped ----
    {
      await open('pass');
      const before = u.askedForLoop().length;
      const started = await u.start();
      const first = await sampleLoop(u, { whole: true });
      const s = first.s;
      const asked = u.askedForLoop().slice(before);
      row(started.ok && first.ok && s.notice === '' && samplePlays(s) && asked.length === 1
          && asked[0].cache === '', 'sample',
          started.ok ? `after START ${first.text}; the notice is "${s.notice}"; ${buttons(s)}; the server was asked `
                       + `for the file ${asked.length} time(s)`
                       + `${asked.length > 0 ? `, Cache-Control "${asked[0].cache}"` : ''}`
                     : started.why);
      if (started.ok) {
        await u.pressElement('fcmp-synth');
        const turned = await u.until((x) => x.source === SYNTH.line && x.notice === '');
        const whole = await synthLoop(u, { whole: true });
        const y = whole.s;
        row(kindOf(s) === 'sample' && turned.ok && whole.ok && y.notice === '' && synthPlays(y), 'synth',
            `SYNTH LOOP pressed while the ${kindOf(s)} source played: ${whole.text}; the notice is "${y.notice}"; `
            + `${buttons(y)}`);

        await u.pressElement('fcmp-loop');
        const returned = await u.until((x) => x.source === SAMPLE.line && x.notice === '');
        const back = await sampleLoop(u);
        const z = back.s;
        const inAll = u.askedForLoop().length - before;
        row(kindOf(y) === 'synth' && returned.ok && back.ok && z.notice === '' && samplePlays(z) && inAll === 1,
            'back',
            `SAMPLE LOOP pressed while the ${kindOf(y)} source played: ${back.text}; the notice is "${z.notice}"; `
            + `${buttons(z)}; the server was asked for the file ${inAll} time(s) in all`);

        const swift = await quick(u, hush);
        row(swift.ok, 'quick', swift.text);
      }
      await leave();
    }

    // ---- the file is not there ----
    {
      await open('missing');
      const before = wav.requests.length;
      const started = await u.start();
      const lost = await synthLoop(u);
      const s = lost.s;
      const asked = wav.requests.length - before;
      row(started.ok && s.says === 'PLAYING' && lost.ok && s.notice === NOTICE.lost && synthPlays(s) && asked === 1,
          'lost',
          started.ok ? `the file answered 404 (asked ${asked} time(s)): the page says "${s.says}"; ${lost.text}; the `
                       + `notice is "${s.notice}"; ${buttons(s)}`
                     : `the file answered 404: ${started.why}`);
      if (started.ok) {
        // Still missing. Held first, so that what the page says while it loads is seen.
        wav.answer = 'hold';
        await u.pressElement('fcmp-loop');
        const waits = await loading();
        await wav.release('missing');
        const told = await u.until((x) => x.notice === NOTICE.unchanged);
        const stays = await synthLoop(u);
        const a = stays.s;
        // The server answers again.
        const served = u.askedForLoop().length;
        await u.pressElement('fcmp-loop');
        const waitsAgain = await loading();
        await wav.release('pass');
        const cleared = await u.until((x) => x.source === SAMPLE.line && x.notice === '', 8000);
        const back = await sampleLoop(u);
        const b = back.s;
        const retry = u.askedForLoop().slice(served);
        const round = retry.length === 1 && /\bno-cache\b/.test(retry[0].cache);
        row(waits.ok && told.ok && stays.ok && a.notice === NOTICE.unchanged && synthPlays(a) && waitsAgain.ok
            && cleared.ok && back.ok && b.notice === '' && samplePlays(b) && round, 'again',
            `a press with the file still missing: "${waits.s.notice}", then "${told.s.notice}"; ${stays.text}; `
            + `${buttons(a)}. A press once the server answers: "${waitsAgain.s.notice}", then "${b.notice}"; `
            + `${back.text}; ${buttons(b)}; the server was asked ${retry.length} time(s)`
            + `${retry.length > 0 ? `, Cache-Control "${retry[0].cache}"` : ''}`);
      }
      await leave();
    }

    // ---- a file dropped while START loads ----
    {
      await open('hold');
      const t0 = Date.now();
      const began = await u.begin();
      const waits = await u.until((s) => s.state === 'loading' && s.says === 'LOADING' && wav.waiting() === 1);
      await u.drop(kept.file);
      const keeps = await holds(u, (s) => s.state === 'loading' && s.plays === null && s.notice === '', QUIET_MS);
      const heldFor = Date.now() - t0;
      await wav.release('pass');
      const started = began.ok ? await u.started(t0) : began;
      const named = started.ok ? await u.until((s) => s.source === kept.line && s.notice === '', 8000) : started;
      const plays = await tone(u, kept);
      const s = plays.s;
      row(waits.ok && keeps.ok && started.ok && named.ok && plays.ok && s.notice === '' && filePlays(s), 'kept',
          started.ok ? `a file dropped while the page said "${waits.s.says}" (the sample loop's file held back for `
                       + `${heldFor} ms): the page went on loading with the notice "${keeps.s.notice}"; once the demo `
                       + `ran: ${plays.text}; the notice is "${s.notice}"; ${buttons(s)}`
                     : started.why);
      await leave();
    }

    // ---- the engine or the editor fails while START loads ----
    {
      const fault = async (who, how) => {
        await open('hold');
        const began = await u.begin();
        // The node is there: START has its engine and waits for the file alone.
        const waits = await u.until((s) => s.state === 'loading' && s.node && wav.waiting() === 1);
        const t0 = Date.now();
        if (waits.ok) await u.p.ev(how);
        const ended = await u.until((s) => s.state !== 'loading', FAULT_MS);
        const ms = Date.now() - t0;
        const requests = await wav.release('pass');
        const e = ended.s;
        return { ok: began.ok && waits.ok && ended.ok && e.state === 'failed' && e.says === FAILED(who)
                     && e.notice === '' && noButton(e) && given(requests),
                 text: `${ms} ms later the page is ${e.state} and says "${e.says}", ${buttons(e)}, and its request `
                       + `for the file ${givenSaid(requests)}` };
      };
      const engine = await fault('ENGINE', ENGINE_STOPS);
      const editor = await fault('EDITOR', EDITOR_ABORTS);
      row(engine.ok && editor.ok, 'fault',
          `while START waited for the file alone (held back; the page gives it ${SAMPLE_MS} ms): the engine says it `
          + `stopped: ${engine.text}. The editor aborts: ${editor.text}`);
    }

    // ---- the demo ends while a load runs ----
    {
      await open('missing');
      const started = await u.start();
      if (!started.ok) {
        row(false, 'ends', `the file answered 404: ${started.why}`);
      } else {
        wav.answer = 'hold';
        await u.pressElement('fcmp-loop');
        const waits = await loading();
        await leave();                                // this page's ledger while its demo still plays: the row ends it
        await u.p.ev(CLOSE_CONTEXT);
        const ended = await u.until((s) => s.state !== 'running');
        const requests = await wav.release('pass');
        const e = ended.s;
        row(waits.ok && ended.ok && e.state === 'stopped' && e.says === STOPPED && e.notice === '' && noButton(e)
            && given(requests), 'ends',
            `the audio context closed from outside while the notice said "${waits.s.notice}": the page is ${e.state} `
            + `and says "${e.says}"; the notice is "${e.notice}"; ${buttons(e)}; the request for the file `
            + `${givenSaid(requests)}`);
      }
    }

    // ---- the file never answers ----
    {
      await open('hold');
      const t0 = Date.now();
      const began = await u.begin();
      const waits = await u.until((s) => s.state === 'loading' && s.says === 'LOADING' && s.button === ''
                                         && wav.waiting() === 1);
      const started = began.ok ? await u.started(t0) : began;
      const requests = await wav.release('pass');
      const synth = await synthLoop(u);
      const s = synth.s;
      const onTime = started.ran >= SAMPLE_MS && started.ran <= SAMPLE_MS + LATE_MS;
      row(waits.ok && started.ok && onTime && synth.ok && s.notice === NOTICE.lost && synthPlays(s)
          && given(requests), 'slow',
          started.ok ? `the file never answered: the page said "${waits.s.says}" and ran ${started.ran} ms after the `
                       + `press (${SAMPLE_MS} to ${SAMPLE_MS + LATE_MS}); ${synth.text}; the notice is "${s.notice}"; `
                       + `${buttons(s)}; the request for the file ${givenSaid(requests)}`
                     : `the file never answered: ${started.why}`);

      if (!started.ok) return;
      // A later choice wins over a load that still runs. What a load that ended must not say or change is watched
      // for QUIET_MS after its answer.
      const fileOn = (x) => x.source === choice.line && x.notice === '' && kindOf(x) === 'file' && filePlays(x);
      const synthOn = (x) => x.source === SYNTH.line && x.notice === '' && kindOf(x) === 'synth' && synthPlays(x);
      // A file chosen while a load runs, and the load then fails.
      wav.answer = 'hold';
      await u.pressElement('fcmp-loop');
      const first = await loading();
      await u.drop(choice.file);
      const chosen = await u.until((x) => x.source === choice.line && x.notice === '', 8000);
      const file = await tone(u, choice);
      await wav.release('fail');
      const fileStays = await holds(u, fileOn, QUIET_MS);
      // The other loop chosen while a load runs, and the load then fails.
      await u.pressElement('fcmp-loop');
      const second = await loading();
      await u.pressElement('fcmp-synth');
      const other = await u.until((x) => x.source === SYNTH.line && x.notice === '');
      await wav.release('fail');
      const synthStays = await holds(u, synthOn, QUIET_MS);
      // The other loop chosen while a load runs, and the whole file then comes. The file plays first: SYNTH LOOP
      // cannot be pressed while the synth loop plays.
      await u.drop(choice.file);
      const fileAgain = await u.until((x) => x.source === choice.line && x.notice === '', 8000);
      await u.pressElement('fcmp-loop');
      const third = await loading();
      await u.pressElement('fcmp-synth');
      const otherAgain = await u.until((x) => x.source === SYNTH.line && x.notice === '');
      const before = otherAgain.s.loaded;
      await wav.release('pass');
      const came = await u.until((x) => x.loaded > before, 8000);
      const stays = await synthLoop(u);
      const y = stays.s;
      // The loop is there: it plays at a press, and the page asks for nothing.
      const asked = wav.requests.length;
      await u.pressElement('fcmp-loop');
      const atOnce = await u.until((x) => x.source === SAMPLE.line && x.notice === '');
      const sample = await sampleLoop(u);
      const z = sample.s;
      const more = wav.requests.length - asked;
      await leave();

      // A file chosen while a load runs, and the whole file of the loop then comes: on a page where it is not loaded.
      await open('missing');
      const again = await u.start();
      let fourth = { ok: false, text: again.ok ? '' : again.why };
      if (again.ok) {
        wav.answer = 'hold';
        await u.pressElement('fcmp-loop');
        const waits = await loading();
        await u.drop(choice.file);
        const named = await u.until((x) => x.source === choice.line && x.notice === '', 8000);
        const plays = await tone(u, choice);
        const loaded = plays.s.loaded;
        await wav.release('pass');
        const whole = await u.until((x) => x.loaded > loaded, 8000);
        const on = await holds(u, fileOn, QUIET_MS);
        fourth = { ok: waits.ok && named.ok && plays.ok && whole.ok && on.ok,
                   text: `a file dropped while the notice said "${waits.s.notice}": ${plays.text}; the whole file of `
                         + `the loop came ${whole.ok ? `${whole.ms} ms after it was let through` : 'NOT'}, and `
                         + `${QUIET_MS} ms later the page says "${on.s.source}" with the notice "${on.s.notice}", `
                         + `source() is ${kindOf(on.s)}; ${buttons(on.s)}` };
      }
      row(first.ok && chosen.ok && file.ok && fileStays.ok && second.ok && other.ok && synthStays.ok && fileAgain.ok
          && third.ok && otherAgain.ok && came.ok && stays.ok && y.notice === '' && synthPlays(y) && atOnce.ok
          && sample.ok && samplePlays(z) && more === 0 && fourth.ok, 'turn',
          `a file dropped while the notice said "${first.s.notice}": ${file.text}; the load then failed, and `
          + `${QUIET_MS} ms later the page says "${fileStays.s.source}" with the notice "${fileStays.s.notice}"; `
          + `${buttons(fileStays.s)}. SYNTH LOOP pressed while the notice said "${second.s.notice}", and the load then `
          + `failed: ${QUIET_MS} ms later the page says "${synthStays.s.source}" with the notice `
          + `"${synthStays.s.notice}", source() is ${kindOf(synthStays.s)}; ${buttons(synthStays.s)}. SYNTH LOOP `
          + `pressed while the notice said "${third.s.notice}" (the file played again), and the whole file of the loop `
          + `came ${came.ok ? `${came.ms} ms after it was let through` : 'NOT'}: ${stays.text}; the notice is `
          + `"${y.notice}"; ${buttons(y)}. SAMPLE LOOP pressed then: ${sample.text}, with ${more} new request(s); `
          + `${buttons(z)}. On a page where the file answered 404 at START: ${fourth.text}`);
    }
  } finally {
    await wav.end();
  }
}
