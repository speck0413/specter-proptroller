// The Live tab at phone width (390 px) when things go wrong: a dead connection (the board stops answering
// while the socket stays open), STOP with no answer and with one, errors in the alert bar, a paused board.
import { chromium } from 'playwright-core';
const S = process.argv[2] || '.', URL = process.argv[3] || 'http://127.0.0.1:18080/';
const browser = await chromium.launch({ executablePath: process.env.CHROMIUM || 'google-chrome', args: ['--no-sandbox'] });
const page = await browser.newPage({ viewport: { width: 390, height: 844 }, deviceScaleFactor: 2, hasTouch: true, colorScheme: 'light' });
const errors = [];
page.on('pageerror', (e) => errors.push(String(e)));
page.on('dialog', (d) => { errors.push('dialog: ' + d.message()); d.dismiss(); });
const ok = (c, msg) => { if (!c) { console.log('FAIL: ' + msg); process.exitCode = 1; } else console.log('ok: ' + msg); };
const rpc = (body) => page.evaluate(async (b) => (await fetch('/api/v1/rpc', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ v: 1, ...b }) })).json(), body);
const text = (sel) => page.textContent(sel);
const noHScroll = async (what) => ok(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth), what + ': no sideways scroll at ' + page.viewportSize().width + ' px');

// the WebSocket goes through a relay the test can freeze: frames from the board stop arriving, the socket stays open
let frozen = false;
await page.routeWebSocket(/\/api\/v1\/ws/, (ws) => {
  const server = ws.connectToServer();
  ws.onMessage((m) => server.send(m));
  server.onMessage((m) => { if (!frozen) ws.send(m); });
});
await page.goto(URL);
await page.waitForSelector('#conn.ok', { timeout: 5000 });
await page.waitForFunction(() => document.querySelectorAll('#outputs .out').length === 8, null, { timeout: 5000 });
ok(true, 'connected');
const hd = (await page.request.get(URL)).headers();
ok(hd['x-frame-options'] === 'DENY' && /frame-ancestors 'none'/.test(hd['content-security-policy'] || ''), 'the page may not be framed by another site (X-Frame-Options, CSP frame-ancestors)');
await noHScroll('Live');
// every visible control at least 48 px high, fields with 16 px text (iOS zooms into smaller), on every tab
const small = async () => page.evaluate(() => [...document.querySelectorAll('button, input, select, textarea, label.check, .chip')]
  .filter((e) => e.offsetParent && getComputedStyle(e).visibility !== 'hidden' && !(e.type === 'checkbox' || e.type === 'radio' || e.type === 'range' || e.type === 'file') && !e.closest('#wiring') && !e.closest('label.check:not(:is(label))'))
  .filter((e) => e.getBoundingClientRect().height < 47.5 || (/^(INPUT|SELECT|TEXTAREA)$/.test(e.tagName) && parseFloat(getComputedStyle(e).fontSize) < 16))
  .map((e) => `${e.tagName.toLowerCase()}${e.id ? '#' + e.id : ''} "${(e.textContent || e.value || '').trim().slice(0, 16)}" ${Math.round(e.getBoundingClientRect().height)}px`));
await rpc({ op: 'add_example' });
for (const tab of ['live', 'wiring', 'shows', 'record', 'settings', 'script', 'firmware']) {
  await page.click(`#tabs button[data-tab="${tab}"]`);
  // the show editor and Link hold the densest controls
  if (tab === 'shows') { await page.waitForSelector('#show-list button:has-text("Edit")'); await page.click('#show-list button:has-text("Edit")'); await page.click('#link-open'); }
  if (tab === 'settings') await page.$$eval('#params details', (d) => d.forEach((x) => { x.open = true; }));
  await page.waitForTimeout(200);
  const bad = await small();
  ok(bad.length === 0, `${tab}: every control 48 px high, fields 16 px text` + (bad.length ? ': ' + bad.slice(0, 6).join(', ') : ''));
}
await page.evaluate(() => { closeEditor(); $('link').classList.add('hidden'); });
await rpc({ op: 'delete_show', show: 1 });
await page.click('#tabs button[data-tab="live"]');
// screen readers and keyboards: tabs are a tablist, sliders and On/Off/Auto have names, an input chip is held with Space
ok(await page.getAttribute('#tabs', 'role') === 'tablist' && await page.getAttribute('#tabs button.on', 'aria-selected') === 'true', 'tabs: role tablist, the open tab aria-selected');
ok(await page.$$eval('#outputs input[type=range]', (r) => r.every((x) => /level$/.test(x.getAttribute('aria-label') || ''))), 'every slider has a name ("OUT1 level")');
ok(await page.getAttribute('#out-RLY1 button:has-text("Auto")', 'aria-label') === 'RLY1: auto (the shows decide)', 'On / Off / Auto say which output');
ok(!(await page.$('#out-CC1 button')) && /role: trigger repeat/.test(await text('#out-CC1')), 'an output with a role has no On / Off / Auto: ' + (await text('#out-CC1 .role')));
await page.focus('#in-TRIG2');
await page.keyboard.down('Space');
await page.waitForFunction(() => document.querySelector('#in-TRIG2').classList.contains('active'), null, { timeout: 3000 });
await page.keyboard.up('Space');
await page.waitForFunction(() => !document.querySelector('#in-TRIG2').classList.contains('active'), null, { timeout: 3000 });
ok(true, 'an input chip is held with the Space key and let go when it is released');

// layout: no sideways scrolling on any tab (the show editor and Link open) at 360 px and at 200 % zoom of a
// 390 x 844 phone (195 x 422 CSS px), with a 32-character name; the part that stays on screen is small
await rpc({ op: 'add_example' });
await rpc({ op: 'set_params', values: [{ key: 'name', value: 'Haunted porch fog and lights 32c' }] });
await page.waitForFunction(() => /Haunted porch/.test(document.querySelector('#dev-name').textContent), null, { timeout: 3000 });
for (const [w, h] of [[360, 740], [195, 422]]) {
  await page.setViewportSize({ width: w, height: h });
  for (const tab of ['live', 'wiring', 'shows', 'script', 'record', 'settings', 'firmware']) {
    await page.click(`#tabs button[data-tab="${tab}"]`);
    if (tab === 'shows') { await page.click('#show-list button:has-text("Edit")'); await page.click('#link-open'); }
    await page.waitForTimeout(150);
    await noHScroll(`${tab} (${w} x ${h})`);
  }
  const stuck = await page.evaluate(() => { const t = document.querySelector('.stickytop'); return getComputedStyle(t).display === 'contents' ? document.querySelector('.top').offsetHeight : t.offsetHeight; });
  ok(stuck <= h * 0.25, `${w} x ${h}: what stays on screen at the top is ${stuck} px (at most a quarter)`);
  if (w === 195) await page.screenshot({ path: S + '/layout-zoom200.png' });
}
await page.evaluate(() => { closeEditor(); $('link').classList.add('hidden'); });
await rpc({ op: 'delete_show', show: 1 });
await rpc({ op: 'set_params', values: [{ key: 'name', value: 'Proptroller' }] });
await page.setViewportSize({ width: 390, height: 844 });
await page.click('#tabs button[data-tab="live"]');

// a Live error shows in the alert bar (not only in the log): a show run again during its cooldown
await rpc({ op: 'add_example' });
await page.waitForSelector('#live-shows button:has-text("Run")', { timeout: 3000 });
ok(await page.textContent('#live-shows .badge') === 'ready', 'a show waiting for its trigger reads "ready" (not "idle" twice)');
await page.click('#live-shows button:has-text("Run")');
await page.waitForFunction(() => document.querySelector('#live-shows .meta')?.textContent.startsWith('resting'), null, { timeout: 6000 });
await page.click('#live-shows button:has-text("Run")');
await page.waitForSelector('#alert:not([hidden])', { timeout: 3000 });
ok(/cooldown/.test(await text('#alert-text')), 'a refused Run shows in the alert bar: ' + await text('#alert-text'));
ok(await page.getAttribute('#alert', 'role') === 'alert', 'the alert bar is role=alert');
await page.screenshot({ path: S + '/live-alert.png' });
await page.click('#alert-close');

// STOP answered: the status line says so
await page.click('#out-RLY2 button:has-text("On")');
await page.waitForFunction(() => document.querySelector('#out-RLY2 .v').textContent === 'ON', null, { timeout: 3000 });
// a slider follows the board: set OUT3 to 50 %, STOP, the slider goes back to 0
await page.$eval('#out-OUT3 input[type=range]', (r) => { r.value = 500; r.dispatchEvent(new Event('change')); });
await page.waitForFunction(() => document.querySelector('#out-OUT3 .v').textContent === '50%', null, { timeout: 3000 });
await page.click('#stop-all');
await page.waitForFunction(() => /^Stopped/.test(document.querySelector('#stop-line').textContent), null, { timeout: 3000 });
ok(true, 'STOP: ' + await text('#stop-line'));
await page.waitForFunction(() => document.querySelector('#out-OUT3 input[type=range]').value === '0', null, { timeout: 3000 });
ok(true, 'the OUT3 slider follows the board after STOP');
// contrast (light mode): text against what is behind it, at least 4.5:1
const contrast = (sel) => page.evaluate((q) => {
  const e = document.querySelector(q);
  const rgb = (c) => (c.match(/[\d.]+/g) || []).map(Number);
  const lum = ([r, g, b]) => { const f = (v) => { v /= 255; return v <= 0.03928 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4; }; return 0.2126 * f(r) + 0.7152 * f(g) + 0.0722 * f(b); };
  let n = e, bg = null;
  while (n && n.nodeType === 1) { const c = rgb(getComputedStyle(n).backgroundColor); if (c.length >= 3 && (c.length < 4 || c[3] > 0.5)) { bg = c; break; } n = n.parentElement; }
  bg = bg || [255, 255, 255];
  const a = lum(rgb(getComputedStyle(e).color)), b = lum(bg);
  return (Math.max(a, b) + 0.05) / (Math.min(a, b) + 0.05);
}, sel);
// more pairs: a primary button, messages, a held output's note
await page.evaluate(() => {
  const host = document.createElement('div'); host.id = 'cx'; host.className = 'card';
  host.innerHTML = '<button class="primary" id="cx-p">Save</button><div class="msg ok" id="cx-ok">saved</div><div class="msg err" id="cx-err">refused</div><span class="badge running" id="cx-run">running</span><div class="out"><div class="fault note" id="cx-note">held by this test</div></div>';
  document.querySelector('#tab-live').prepend(host);
});
const cxSel = ['#cx-p', '#cx-ok', '#cx-err', '#cx-run', '#cx-note'];
for (const q of ['#stop-all', '#tabs button.on', '#stop-line', '.hint', '#out-hint', '.out .head', ...cxSel]) {
  const c = await contrast(q);
  ok(c >= 4.5, `contrast ${q}: ${c.toFixed(1)}:1`);
}
await page.emulateMedia({ colorScheme: 'dark' });
for (const q of ['#stop-all', '#tabs button.on', '#stop-line', '.hint', ...cxSel]) {
  const c = await contrast(q);
  ok(c >= 4.5, `dark: contrast ${q}: ${c.toFixed(1)}:1`);
}
await page.evaluate(() => document.getElementById('cx').remove());
await page.emulateMedia({ colorScheme: 'light' });
// the page's own theme choice wins over the phone's (dark chosen while the phone is light)
await page.click('#tabs button[data-tab="settings"]');
await page.selectOption('#theme', 'dark');
ok(await page.evaluate(() => getComputedStyle(document.body).backgroundColor) === 'rgb(20, 22, 26)', 'Theme: Dark on a light phone');
await page.selectOption('#theme', '');
ok(await page.evaluate(() => getComputedStyle(document.body).backgroundColor) === 'rgb(242, 243, 245)', 'Theme: like the phone again');
await page.click('#tabs button[data-tab="live"]');
ok(await page.getAttribute('#stop-line', 'role') === 'status', 'the STOP line is role=status');

// the board goes silent: within about 8 s the dot turns red, the header says how old the values are, and they are greyed
await page.click('#out-RLY2 button:has-text("On")');
await page.waitForFunction(() => document.querySelector('#out-RLY2 .v').textContent === 'ON', null, { timeout: 3000 });
frozen = true;
await page.route('**/api/v1/rpc', () => {});  // plain HTTP hangs too
const t0 = Date.now();
await page.waitForFunction(() => !document.querySelector('#conn').classList.contains('ok') && document.body.classList.contains('stale'), null, { timeout: 10000 });
ok(Date.now() - t0 < 9500, `dead connection seen after ${((Date.now() - t0) / 1000).toFixed(1)} s`);
await page.waitForFunction(() => /last update \d+ s ago/.test(document.querySelector('#dev-sub').textContent), null, { timeout: 3000 });
ok(true, 'header: ' + await text('#dev-sub'));
ok(await page.evaluate(() => getComputedStyle(document.querySelector('.out')).opacity) < 0.6, 'old values are greyed');
// STOP with no answer says so, within a few seconds
const s0 = Date.now();
await page.click('#stop-all');
ok((await text('#stop-line')).startsWith('Stopping'), 'STOP: "Stopping…" at once');
await page.waitForFunction(() => /^STOP not confirmed/.test(document.querySelector('#stop-line').textContent), null, { timeout: 8000 });
ok(Date.now() - s0 < 7000, `STOP not confirmed after ${((Date.now() - s0) / 1000).toFixed(1)} s: ` + await text('#stop-line'));
ok(!(await page.$('#conn.ok')) && await page.evaluate(() => document.body.classList.contains('stale')), 'still not connected while the board is silent (an open socket alone is not a connection)');
await page.screenshot({ path: S + '/live-stale-stop.png' });
// the board answers again: the page reconnects by itself and STOP is confirmed
frozen = false;
await page.unroute('**/api/v1/rpc');
await page.waitForSelector('#conn.ok', { timeout: 12000 });
await page.waitForFunction(() => !document.body.classList.contains('stale'), null, { timeout: 3000 });
ok(true, 'reconnected: ' + await text('#dev-sub'));
await page.click('#stop-all');
await page.waitForFunction(() => /^Stopped/.test(document.querySelector('#stop-line').textContent), null, { timeout: 3000 });
ok(true, 'STOP confirmed after the reconnect');

// a paused board: the banner says so and Resume (no password asked) lets the shows start again
await rpc({ op: 'pause', on: true });
await page.waitForSelector('#banner:not(.hidden) #resume', { timeout: 3000 });
ok(/PAUSED/.test(await text('#banner-text')), 'paused: ' + await text('#banner-text'));
await page.screenshot({ path: S + '/live-paused.png' });
await page.click('#resume');
await page.waitForSelector('#banner.hidden', { state: 'attached', timeout: 3000 });
ok(!(await rpc({ op: 'state' })).result.paused, 'Resume: the board is no longer paused');

// too many pages (S6): the board takes 4; the 5th says so, shows the state over HTTP and STOP still works
const ctx = await browser.newContext({ viewport: { width: 390, height: 844 } });
const extra = [];
for (let k = 0; k < 3; k++) { const p = await ctx.newPage(); await p.goto(URL); await p.waitForSelector('#conn.ok', { timeout: 5000 }); extra.push(p); }
const fifth = await ctx.newPage();
fifth.on('pageerror', (e) => errors.push('5th page: ' + String(e)));
await fifth.goto(URL);
await fifth.waitForFunction(() => /too many pages are open/.test(document.querySelector('#dev-sub').textContent), null, { timeout: 15000 });
ok(true, '5th page: ' + await fifth.textContent('#dev-sub'));
await fifth.waitForFunction(() => document.querySelectorAll('#outputs .out').length === 8 && !document.body.classList.contains('stale'), null, { timeout: 5000 });
ok(true, '5th page shows the outputs (state over HTTP)');
await fifth.screenshot({ path: S + '/live-too-many.png' });
await fifth.click('#stop-all');
await fifth.waitForFunction(() => /^Stopped/.test(document.querySelector('#stop-line').textContent), null, { timeout: 5000 });
ok(true, '5th page: STOP confirmed over HTTP');
await extra.pop().close();
await fifth.waitForSelector('#conn.ok', { timeout: 12000 });
ok(true, 'a page closed elsewhere: the 5th connects');
// a page in the background lets go of its place after a while, and comes back when shown
await fifth.evaluate(() => { conn.hiddenMs = 300; Object.defineProperty(document, 'hidden', { configurable: true, get: () => true }); document.dispatchEvent(new Event('visibilitychange')); });
await fifth.waitForFunction(() => /in the background/.test(document.querySelector('#dev-sub').textContent) && !conn.ws, null, { timeout: 3000 });
ok(true, 'hidden page let go of its place: ' + await fifth.textContent('#dev-sub'));
await fifth.evaluate(() => { Object.defineProperty(document, 'hidden', { configurable: true, get: () => false }); document.dispatchEvent(new Event('visibilitychange')); });
await fifth.waitForSelector('#conn.ok', { timeout: 5000 });
ok(true, 'shown again: reconnected');
await ctx.close();

// the built-in effect with Recover in hold mode: a press-and-hold button; another phone saving a setting while it
// is held redraws the buttons, and the hold is let go (it never stays down unseen)
await rpc({ op: 'delete_show', show: 1 });  // the effect owns OUT1 and OUT2: the example show must go
await rpc({ op: 'set_params', values: [{ key: 'effect', value: 'pulse_and_recover' }, { key: 'recover_mode', value: 'hold' }] });
await rpc({ op: 'reboot' });
await page.waitForTimeout(2500);
await page.reload();
const rec = await page.waitForSelector('#actions button:has-text("Recover (hold)")', { timeout: 8000 });
ok(true, 'Recover in hold mode reads "Recover (hold)"');
// the effect's status in plain words, its buttons by state
const lines = await text('#status-lines');
ok(/Cooldown remaining0\.0 s|Cooldown remaining\d/.test(lines) && !/ ms|false|true/.test(lines), 'status in plain words (seconds, yes / no): ' + lines.replace(/\s+/g, ' ').slice(0, 160));
const acts = await page.$$eval('#actions button', (b) => b.filter((x) => !x.hidden).map((x) => x.textContent));
ok(acts.at(-1) === 'Save now' && !acts.includes('Cancel queued run'), 'Save now last, Cancel only while a run is queued: ' + acts.join(', '));
ok(await page.evaluate(() => fmtMs(119600)) === '2m 0s', 'fmtMs(119600) is "2m 0s"');
await page.waitForTimeout(3000);  // the effect's power-on wait
const rb = await rec.boundingBox();
await page.mouse.move(rb.x + rb.width / 2, rb.y + rb.height / 2);
await page.mouse.down();
await page.waitForFunction(() => document.querySelector('#out-OUT2 .v').textContent !== '0%', null, { timeout: 4000 });
ok(true, 'held: OUT2 runs the recovery');
await rpc({ op: 'set_params', values: [{ key: 'trig_release_ms', value: 300 }] });
await page.waitForFunction(() => document.querySelector('#out-OUT2 .v').textContent === '0%', null, { timeout: 4000 });
ok(true, 'another phone saved a setting mid-hold: the redrawn button let go and OUT2 stopped');
await page.mouse.up();
// the remote trigger enable sits next to Trigger; Trigger refused says why on the page
await page.waitForTimeout(1500);
await page.click('#actions button:text-is("Trigger")');
await page.waitForFunction(() => /not enabled/.test(document.querySelector('#actions-msg').textContent), null, { timeout: 3000 });
ok(true, 'Trigger refused, shown under the buttons: ' + await text('#actions-msg'));
await page.check('#remote-on');
await page.waitForFunction(() => /while this page stays connected/.test(document.querySelector('#actions-msg').textContent), null, { timeout: 3000 });
ok((await rpc({ op: 'get_params' })).result.values.find((v) => v.key === 'remote_trigger_enable').value === true, 'Remote trigger on from the Live tab');
await page.screenshot({ path: S + '/live-remote.png' });
// the connection drops: the board turns it off with the session; the page says so and offers it again
await page.evaluate(() => conn.ws.close());
await page.waitForFunction(() => /is off again/.test(document.querySelector('#actions-msg').textContent), null, { timeout: 10000 });
ok(!(await page.isChecked('#remote-on')), 'after a dropped connection: ' + await text('#actions-msg'));

ok(errors.length === 0, 'no page errors or dialogs ' + JSON.stringify(errors));
await browser.close();
