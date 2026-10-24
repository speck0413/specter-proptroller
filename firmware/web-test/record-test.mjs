// Record effect on the phone page (record.js + take.js) against a simulated board, at 390 px: pick TRIG1, OUT1
// (a slider) and RLY1, record, see the board follow live, Stop → the board does it back, Keep → the stored script
// has the new effect beside the one already there and passes the board's check, TRIG1 runs it. Also: the same
// name asks before it replaces, an output another effect uses is refused in plain words, STOP during the
// playback turns everything off. Cancel at every step (3-2-1, recording, playback, review), Escape, the
// "Discard this recording?" question only with a take, nothing saved.
//   node record-test.mjs <screenshot dir> <url>      (CHROMIUM=/path/to/chrome)
import { chromium } from 'playwright-core';
const [S = '.', URL = 'http://127.0.0.1:18080/'] = process.argv.slice(2);
const browser = await chromium.launch({ executablePath: process.env.CHROMIUM || 'google-chrome', args: ['--no-sandbox'] });
const page = await browser.newPage({ viewport: { width: 390, height: 844 }, deviceScaleFactor: 2, colorScheme: 'dark', hasTouch: false });
const errors = [];
page.on('pageerror', (e) => errors.push(String(e)));
page.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
const ok = (c, msg) => { if (!c) { console.log('FAIL: ' + msg); process.exitCode = 1; } else console.log('ok: ' + msg); };
const rpc = (req) => page.evaluate(async (r) => (await (await fetch('/api/v1/rpc', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ v: 1, ...r }) })).json()), req);
const level = async (ch) => (await page.evaluate(async () => (await (await fetch('/api/v1/state')).json()))).outputs.find((o) => o.channel === ch).level;
const waitLevel = async (ch, test, ms = 3000) => {
  const end = Date.now() + ms;
  let l;
  while (Date.now() < end) { l = await level(ch); if (test(l)) return l; await page.waitForTimeout(40); }
  return l;
};
const noHScroll = async (what) => ok(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth), `${what}: no sideways scrolling at 390 px`);
const slide = (target, v) => page.$eval(`#rx-controls .rxout[data-target="${target}"] input[type=range]`, (r, x) => { r.value = x; r.dispatchEvent(new Event('input', { bubbles: true })); }, v);
const holdDown = async (target) => {
  const b = await page.$(`#rx-controls .rxout[data-target="${target}"] .hold`);
  const box = await b.boundingBox();
  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
  await page.mouse.down();
};
const pick = async (outs, sounds = []) => {
  await page.$$eval('#rx-outs input, #rx-sounds input', (xs, want) => xs.forEach((x) => { const on = want.includes(x.value); if (x.checked !== on) x.click(); }), [...outs, ...sounds.map(String)]);
};

// a stored effect of the person's own: the recording must go beside it, never over it
const PORCH = '# my porch\neffect porch light:\n  wait for trig2\n  out2 = on\n  sleep 1\n  out2 = off\n';
await page.goto(URL);
await page.waitForSelector('#conn.ok', { timeout: 5000 });
ok((await rpc({ op: 'set_script', text: PORCH, sounds: {} })).result.ok, 'a stored script with one effect (porch light on TRIG2 / OUT2)');
await page.reload();
await page.waitForSelector('#conn.ok', { timeout: 5000 });
await page.click('#tabs button[data-tab="record"]');
ok(await page.isVisible('#rx-setup'), 'the Record effect tab is there');
ok(!(await page.$('#rec-start, #rec-tap')), 'the old Record tab (record_start / TAP) is gone');
const trig = await page.$$eval('#rx-trigger option', (o) => o.map((x) => x.value));
ok(['TRIG1', 'TRIG2', 'USER1', 'USER2', 'USER3'].every((t) => trig.includes(t)), 'starts on: TRIG1, TRIG2 and USER1-3 (USER buttons are plain triggers): ' + trig.join(', '));
await page.selectOption('#rx-trigger', 'TRIG1');
await pick(['out1', 'rly1']);
await page.uncheck('#rx-count');
ok(!(await page.isDisabled('#rx-start')), 'Start is ready with TRIG1, OUT1 and RLY1 picked');
await noHScroll('setup');
await page.screenshot({ path: `${S}/record-setup.png`, fullPage: true });

// ---- record: the slider and the hold, the board follows live
await page.click('#rx-start');
await page.waitForFunction(() => /^Recording/.test(document.querySelector('#rx-status').textContent), null, { timeout: 3000 });
ok(await page.isVisible('#rx-controls .rxout[data-target="out1"] input[type=range]') && !(await page.$('#rx-controls .rxout[data-target="rly1"] input[type=range]')),
  'OUT1 has a level slider, RLY1 (on or off only) has none');
await page.waitForTimeout(300);
for (const v of [20, 40, 60]) { await slide('out1', v); await page.waitForTimeout(120); }
ok((await waitLevel('OUT1', (l) => l === 600)) === 600, 'the board follows the slider: OUT1 at 60 %');
await page.waitForFunction(() => /board: 60 %/.test(document.querySelector('.rxout[data-target="out1"] .board').textContent), null, { timeout: 3000 });
ok(true, 'the control shows what the board reports: ' + await page.textContent('.rxout[data-target="out1"] .board'));
await page.waitForTimeout(400);
await holdDown('rly1');
ok((await waitLevel('RLY1', (l) => l === 1000)) === 1000, 'held: RLY1 on at once');
await page.waitForTimeout(500);
await page.screenshot({ path: `${S}/record-live.png`, fullPage: true });
await page.mouse.up();
ok((await waitLevel('RLY1', (l) => l === 0)) === 0, 'let go: RLY1 off');
await page.waitForTimeout(300);
await slide('out1', 0);
ok((await waitLevel('OUT1', (l) => l === 0)) === 0, 'slider down: OUT1 off');
await noHScroll('recording');

// ---- Stop: the board does it back
await page.click('#rx-stop');
await page.waitForFunction(() => document.querySelector('#rx-title').textContent === 'The board does it back' && !document.querySelector('#rx-live').classList.contains('hidden'), null, { timeout: 3000 });
ok(true, 'Stop: ' + await page.textContent('#rx-status'));
ok((await waitLevel('OUT1', (l) => l > 0)) > 0, 'playback: OUT1 comes on at its recorded level');
ok((await waitLevel('RLY1', (l) => l === 1000, 4000)) === 1000, 'playback: RLY1 comes on');
await page.waitForSelector('#rx-review:not(.hidden)', { timeout: 6000 });
ok((await level('OUT1')) === 0 && (await level('RLY1')) === 0, 'after the playback everything is let go');
const code = await page.textContent('#rx-code');
ok(/effect .+:\n  wait for trig1\n/.test(code) && /rly1 = on/.test(code) && /rly1 = off/.test(code), 'the take as an effect script: wait for trig1, rly1 on / off');
ok(/out1 = (20|40|60)%|fade out1 to 60% over/.test(code) && /out1 = off/.test(code), 'the slider as levels (or a fade), off at the end:\n' + code);
await page.fill('#rx-name', 'test scare');
await noHScroll('review');
await page.screenshot({ path: `${S}/record-review.png`, fullPage: true });

// ---- Keep: into the stored script, beside the porch light
await page.click('#rx-keep');
await page.waitForFunction(() => /^Saved/.test(document.querySelector('#rx-msg').textContent), null, { timeout: 5000 });
ok(true, 'Keep: ' + await page.textContent('#rx-msg'));
const stored = (await rpc({ op: 'get_script' })).result;
ok(stored.text.startsWith(PORCH), 'the stored script keeps the porch light exactly as it was');
ok(/\neffect test scare:\n  wait for trig1\n/.test(stored.text), 'and has the recorded effect "test scare"');
ok(stored.check.ok && (await rpc({ op: 'check_script', text: stored.text, sounds: {} })).result.ok, 'the board\'s checker passes it');
await page.screenshot({ path: `${S}/record-saved.png`, fullPage: true });
// the Script tab shows it
await page.click('#tabs button[data-tab="script"]');
await page.waitForFunction(() => document.querySelector('#script-text').value.includes('effect test scare:'), null, { timeout: 3000 });
ok(true, 'the Script tab shows the recorded effect, ready to change');

// ---- TRIG1 runs it
await rpc({ op: 'sim_input', input: 'TRIG1', active: true, lease_ms: 1500 });
await page.waitForTimeout(250);
await rpc({ op: 'sim_input', input: 'TRIG1', active: false });
ok((await waitLevel('OUT1', (l) => l > 0, 3000)) > 0, 'TRIG1 pressed: the effect runs (OUT1 on)');
ok((await waitLevel('RLY1', (l) => l === 1000, 5000)) === 1000, 'and RLY1 comes on as recorded');
await waitLevel('RLY1', (l) => l === 0, 5000);
await page.waitForTimeout(1500);

// ---- the same name again: asked before it replaces
await page.click('#tabs button[data-tab="record"]');
await page.click('#rx-again');
await pick(['rly1']);
await page.click('#rx-start');
await page.waitForFunction(() => /^Recording/.test(document.querySelector('#rx-status').textContent), null, { timeout: 3000 });
await holdDown('rly1'); await page.waitForTimeout(300); await page.mouse.up();
await page.click('#rx-stop');
await page.waitForSelector('#rx-review:not(.hidden)', { timeout: 6000 });
await page.fill('#rx-name', 'Test  Scare');
await page.click('#rx-keep');
await page.waitForSelector('#rx-review .ask', { timeout: 3000 });
ok(/already has an effect called “test scare”\. Replace it\?/.test(await page.textContent('#rx-review .ask')), 'same name (any case or spacing): asks first: ' + await page.textContent('#rx-review .ask'));
await page.screenshot({ path: `${S}/record-replace.png`, fullPage: false });
await page.click('#rx-review .ask button:has-text("Replace")');
await page.waitForFunction(() => /^Saved/.test(document.querySelector('#rx-msg').textContent), null, { timeout: 5000 });
const again = (await rpc({ op: 'get_script' })).result.text;
ok(again.startsWith(PORCH) && (again.match(/^effect /gm) || []).length === 2 && /effect Test Scare:/.test(again) && !/out1/.test(again.split('effect Test Scare:')[1]),
  'replaced in place: still two effects, the new one has only RLY1');

// ---- an output another effect uses: refused in plain words
await page.click('#rx-again');
await pick(['out2']);
await page.click('#rx-start');
await page.waitForFunction(() => /^Recording/.test(document.querySelector('#rx-status').textContent), null, { timeout: 3000 });
await holdDown('out2'); await page.waitForTimeout(300); await page.mouse.up();
await page.click('#rx-stop');
await page.waitForSelector('#rx-review:not(.hidden)', { timeout: 6000 });
await page.fill('#rx-name', 'fog');
await page.click('#rx-keep');
await page.waitForFunction(() => /already used by the effect “porch light”/.test(document.querySelector('#rx-msg').textContent), null, { timeout: 5000 });
ok(true, 'an output another effect uses: ' + await page.textContent('#rx-msg'));
ok((await rpc({ op: 'get_script' })).result.text === again, 'nothing was saved');

// ---- STOP during the playback: everything off, nothing comes back on
await page.click('#rx-again');
await pick(['rly2']);
await page.click('#rx-start');
await page.waitForFunction(() => /^Recording/.test(document.querySelector('#rx-status').textContent), null, { timeout: 3000 });
await page.waitForTimeout(800);
await holdDown('rly2'); await page.waitForTimeout(1200); await page.mouse.up();
await page.click('#rx-stop');
await page.waitForFunction(() => document.querySelector('#rx-title').textContent === 'The board does it back', null, { timeout: 3000 });
await waitLevel('RLY2', (l) => l === 1000, 3000);
await page.click('#stop-all');
ok((await waitLevel('RLY2', (l) => l === 0)) === 0, 'STOP during the playback: RLY2 off');
await page.waitForTimeout(1500);
ok((await level('RLY2')) === 0, 'and the playback turns nothing back on');
ok(await page.isVisible('#rx-review') && /STOP/.test(await page.textContent('#rx-msg')), 'the take is kept for review: ' + await page.textContent('#rx-msg'));

// ---- the 3-2-1, a sound, and Add more on top: the earlier pass plays on the board while the next is recorded
await page.click('#rx-again');
await pick(['rly1'], [1]);
await page.check('#rx-count');
await page.click('#rx-start');
await page.waitForFunction(() => document.querySelector('#rx-status').textContent === '3', null, { timeout: 2000 });
ok(true, 'the count-in shows 3, 2, 1');
await page.waitForFunction(() => /^Recording/.test(document.querySelector('#rx-status').textContent), null, { timeout: 4000 });
await page.waitForTimeout(300);
await page.click('#rx-controls .snd[data-sound="1"]');
await page.waitForTimeout(200);
await holdDown('rly1'); await page.waitForTimeout(600); await page.mouse.up();
await page.click('#rx-stop');
await page.waitForSelector('#rx-review:not(.hidden)', { timeout: 8000 });
ok(/audio_single = 1/.test(await page.textContent('#rx-code')), 'a sound button becomes audio_single = 1');
await page.click('#rx-more');
await pick(['out1']);
await page.uncheck('#rx-count');
await page.click('#rx-start');
await page.waitForFunction(() => /^Recording/.test(document.querySelector('#rx-status').textContent), null, { timeout: 3000 });
await slide('out1', 50);
ok((await waitLevel('RLY1', (l) => l === 1000, 3000)) === 1000, 'Add more on top: the earlier pass (RLY1) plays on the board while the new one is recorded');
await page.waitForTimeout(800);
await slide('out1', 0);
await page.click('#rx-stop');
await page.waitForSelector('#rx-review:not(.hidden)', { timeout: 8000 });
const both = await page.textContent('#rx-code');
ok((both.match(/^effect /gm) || []).length === 1 && /rly1 = on/.test(both) && /out1 = 50%/.test(both) && /audio_single = 1/.test(both), 'both passes in one effect:\n' + both);
// the guide's worked example and its tweaked version pass the board's checker
const EXAMPLE = 'effect porch scare:\n  wait for trig1\n  out1 = 30%\n  wait 1.6\n  out1 = 40%\n  fade out1 to 100% over 0.7\n  wait 0.2\n  audio_single = 2\n  wait 0.1\n  rly1 = on\n  wait 0.5\n  rly1 = off\n  wait 1.6\n  out1 = off\n';
const TWEAKED = EXAMPLE.replace('wait 1.6', 'wait 4').replace('out1 = 30%', 'out1 = 15%') + '  wait 20\n';
for (const [n, t] of [['the worked example', EXAMPLE], ['the tweaked example', TWEAKED]]) {
  const c = (await rpc({ op: 'check_script', text: t, sounds: {} })).result;
  ok(c.ok && !c.warnings.length, `RECORD-EFFECT.md: ${n} passes the checker` + (c.ok ? '' : ': ' + JSON.stringify(c.errors)));
}

// ---- the page loses focus while recording: everything is let go, the take ends there and is kept
await page.click('#rx-again');
await pick(['rly1']);
await page.click('#rx-start');
await page.waitForFunction(() => /^Recording/.test(document.querySelector('#rx-status').textContent), null, { timeout: 3000 });
await holdDown('rly1');
await waitLevel('RLY1', (l) => l === 1000);
await page.evaluate(() => window.dispatchEvent(new Event('blur')));
ok((await waitLevel('RLY1', (l) => l === 0)) === 0, 'focus lost while holding: RLY1 off');
await page.mouse.up();
ok(await page.isVisible('#rx-review') && /lost focus/.test(await page.textContent('#rx-msg')) && /rly1 = on/.test(await page.textContent('#rx-code')),
  'the take ends there and is kept: ' + await page.textContent('#rx-msg'));

// ---- the connection drops while recording: the board lets go of this page's outputs, the page says so
await page.click('#rx-again');
await pick(['out3']);
await page.click('#rx-start');
await page.waitForFunction(() => /^Recording/.test(document.querySelector('#rx-status').textContent), null, { timeout: 3000 });
await slide('out3', 50);
await waitLevel('OUT3', (l) => l === 500);
await page.evaluate(() => conn.ws.close());
await page.waitForFunction(() => /connection to the board dropped/.test(document.querySelector('#rx-msg').textContent), null, { timeout: 5000 });
ok((await waitLevel('OUT3', (l) => l === 0)) === 0, 'connection dropped: OUT3 off (the board ends what the page held)');
ok(true, 'and the page says so: ' + await page.textContent('#rx-msg'));
await page.waitForSelector('#conn.ok', { timeout: 8000 });

// ---- Cancel: at every step; it stops, lets go of everything and throws the take away (asked only with a take)
const askShown = () => page.isVisible('#rx-ask');
await page.click('#tabs button[data-tab="record"]');
if (await page.isVisible('#rx-review')) await page.click('#rx-again');
const before = (await rpc({ op: 'get_script' })).result.text;
ok(await page.isVisible('#rx-cancel-setup'), 'Cancel on the first screen');
// during the 3-2-1: nothing recorded, so no question
await pick(['rly1']);
await page.check('#rx-count');
await page.click('#rx-start');
await page.waitForFunction(() => document.querySelector('#rx-status').textContent === '3', null, { timeout: 2000 });
ok(await page.isVisible('#rx-cancel-live'), 'Cancel during the 3-2-1');
await page.click('#rx-cancel-live');
ok(await page.isVisible('#rx-setup') && !(await askShown()) && /^Cancelled/.test(await page.textContent('#rx-setup-msg')),
  'cancel during the 3-2-1: back to the start, nothing asked: ' + await page.textContent('#rx-setup-msg'));
await page.waitForTimeout(3 * 800 + 300);
ok(await page.isVisible('#rx-setup') && (await level('RLY1')) === 0, 'and the recording never starts later');
// while recording, with RLY1 held and OUT1's slider up: Escape lets go of both before anything is asked
await pick(['out1', 'rly1']);
await page.uncheck('#rx-count');
await page.click('#rx-start');
await page.waitForFunction(() => /^Recording/.test(document.querySelector('#rx-status').textContent), null, { timeout: 3000 });
await slide('out1', 50);
await waitLevel('OUT1', (l) => l === 500);
await holdDown('rly1');
ok((await waitLevel('RLY1', (l) => l === 1000)) === 1000, 'recording: RLY1 held, OUT1 at 50 %');
ok(await page.isVisible('#rx-cancel-live'), 'Cancel while recording');
await page.keyboard.press('Escape');
ok((await waitLevel('RLY1', (l) => l === 0)) === 0 && (await waitLevel('OUT1', (l) => l === 0)) === 0, 'Escape while recording: RLY1 and OUT1 off at once, before any question');
await page.mouse.up();
ok(await askShown() && (await page.textContent('#rx-ask-q')) === 'Discard this recording?', 'a take was recorded: "Discard this recording?"');
ok(await page.evaluate(() => document.activeElement?.id) === 'rx-keep-take', 'the focus is on No, go back (Enter or Escape never throws the take away)');
await page.screenshot({ path: `${S}/record-cancel-ask.png`, fullPage: true });
await page.keyboard.press('Escape');
ok(!(await askShown()) && await page.isVisible('#rx-review') && /rly1 = on/.test(await page.textContent('#rx-code')), 'Escape on the question keeps the take for review');
await page.waitForTimeout(600);
ok((await level('RLY1')) === 0 && (await level('OUT1')) === 0, 'and nothing comes back on (no playback)');
// on the review screen: Cancel, Discard → nothing saved, back to the start
ok(await page.isVisible('#rx-cancel-review'), 'Cancel on the review screen');
await page.click('#rx-cancel-review');
ok(await askShown(), 'Cancel on review asks first');
await page.click('#rx-discard');
ok(await page.isVisible('#rx-setup') && /^Cancelled/.test(await page.textContent('#rx-setup-msg')) && (await page.evaluate(() => document.activeElement?.id)) === 'rx-start',
  'Discard: back to the start, the focus on Start: ' + await page.textContent('#rx-setup-msg'));
ok((await rpc({ op: 'get_script' })).result.text === before, 'nothing was saved on the board');
// during the playback: everything off, asked, Discard
await pick(['rly2']);
await page.click('#rx-start');
await page.waitForFunction(() => /^Recording/.test(document.querySelector('#rx-status').textContent), null, { timeout: 3000 });
await holdDown('rly2'); await page.waitForTimeout(1200); await page.mouse.up();
await page.click('#rx-stop');
await page.waitForFunction(() => document.querySelector('#rx-title').textContent === 'The board does it back', null, { timeout: 3000 });
await waitLevel('RLY2', (l) => l === 1000, 3000);
await page.click('#rx-cancel-live');
ok((await waitLevel('RLY2', (l) => l === 0)) === 0 && await askShown(), 'Cancel during the playback: RLY2 off, then asked');
await page.click('#rx-discard');
await page.waitForTimeout(1500);
ok((await level('RLY2')) === 0 && (await rpc({ op: 'get_script' })).result.text === before, 'the playback turns nothing back on, nothing saved');
// the first screen with nothing recorded: Cancel goes back to the Live tab
await page.click('#rx-cancel-setup');
ok(await page.isVisible('#tab-live') && !(await page.isVisible('#tab-record')), 'Cancel on the first screen: back to the Live tab');
await noHScroll('after cancel');

ok(errors.length === 0, 'no page errors' + (errors.length ? ': ' + errors.join(' | ') : ''));
await browser.close();
