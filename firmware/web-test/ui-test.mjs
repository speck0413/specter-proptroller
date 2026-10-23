import { chromium } from 'playwright-core';
const S = process.argv[2] || '.', URL = process.argv[3] || 'http://127.0.0.1:18080/';
// Chromium: set CHROMIUM to its path (default: google-chrome on PATH)
const browser = await chromium.launch({ executablePath: process.env.CHROMIUM || 'google-chrome', args: ['--no-sandbox'] });
const page = await browser.newPage({ viewport: { width: 412, height: 900 }, deviceScaleFactor: 1, colorScheme: 'dark' });
const errors = [];
page.on('pageerror', (e) => errors.push(String(e)));
page.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
const ok = (c, msg) => { if (!c) { console.log('FAIL: ' + msg); process.exitCode = 1; } else console.log('ok: ' + msg); };
await page.goto(URL);
await page.waitForSelector('#conn.ok', { timeout: 5000 });
ok(true, 'connected (WebSocket)');
await page.waitForFunction(() => document.querySelectorAll('#outputs .out').length === 8, null, { timeout: 5000 });
ok(true, '8 output cards');
// a new board has no shows: nothing runs from the inputs until one is added
await page.waitForFunction(() => document.querySelector('#live-shows').textContent.startsWith('No shows yet'), null, { timeout: 3000 });
ok(true, 'a fresh board has no shows: ' + (await page.textContent('#live-shows')));
await page.click('#tabs button[data-tab="shows"]');
await page.click('#add-example');
await page.waitForFunction(() => document.querySelector('#shows-msg').textContent.startsWith('added'), null, { timeout: 3000 });
ok(true, 'Shows: ' + (await page.textContent('#shows-msg')));
await page.click('#tabs button[data-tab="live"]');
await page.waitForSelector('#live-shows button:has-text("Run")', { timeout: 3000 });
// run the example show from the live list
await page.click('#live-shows button:has-text("Run")');
await page.waitForFunction(() => document.querySelector('#out-OUT1 .v').textContent === '100%', null, { timeout: 3000 });
ok(true, 'Run: OUT1 at 100%');
await page.screenshot({ path: S + '/ui-running.png', fullPage: true });
await page.waitForFunction(() => document.querySelector('#out-OUT1 .v').textContent === '0%', null, { timeout: 5000 });
ok(true, 'show ended: OUT1 off');
// manual output test
await page.click('#out-RLY2 button:has-text("On")');
await page.waitForFunction(() => document.querySelector('#out-RLY2 .v').textContent === 'ON' && document.querySelector('#out-RLY2').classList.contains('manual'), null, { timeout: 3000 });
ok(true, 'manual RLY2 on');
await page.click('#stop-all');
await page.waitForFunction(() => document.querySelector('#out-RLY2 .v').textContent === 'off', null, { timeout: 3000 });
ok(true, 'STOP releases it');
// simulator input chip: press and hold TRIG1 (momentary), the example show starts
await page.waitForTimeout(3200);
const chip = await page.$('#in-TRIG1');
await chip.scrollIntoViewIfNeeded();
const cb = await chip.boundingBox();
await page.mouse.move(cb.x + cb.width / 2, cb.y + cb.height / 2);
await page.mouse.down();
await page.waitForFunction(() => document.querySelector('#in-TRIG1').classList.contains('active'), null, { timeout: 3000 });
ok(true, 'holding TRIG1 closes it');
await page.waitForFunction(() => document.querySelector('#out-OUT1 .v').textContent === '100%', null, { timeout: 3000 });
ok(true, 'TRIG1 held: the show starts');
await page.mouse.up();
await page.waitForFunction(() => !document.querySelector('#in-TRIG1').classList.contains('active'), null, { timeout: 3000 });
ok(true, 'release opens it again (momentary, not a toggle)');
// a held input is released when the page loses focus
const t2 = await page.$('#in-TRIG2');
await t2.scrollIntoViewIfNeeded();
const c2 = await t2.boundingBox();
await page.mouse.move(c2.x + c2.width / 2, c2.y + c2.height / 2);
await page.mouse.down();
await page.waitForFunction(() => document.querySelector('#in-TRIG2').classList.contains('active'), null, { timeout: 3000 });
await page.evaluate(() => window.dispatchEvent(new Event('blur')));
await page.waitForFunction(() => !document.querySelector('#in-TRIG2').classList.contains('active'), null, { timeout: 3000 });
await page.mouse.up();
ok(true, 'blur releases a held input');
// wiring tab
await page.click('#tabs button[data-tab="wiring"]');
await page.waitForFunction(() => document.querySelectorAll('#wiring .wbox').length >= 15, null, { timeout: 3000 });
ok(true, 'wiring view: ' + (await page.$$eval('#wiring .wbox', (b) => b.length)) + ' terminal boxes');
await page.waitForTimeout(5500); // the example show's run + 3 s cooldown
const w1 = await page.$('#wb-TRIG1');
await w1.scrollIntoViewIfNeeded();
const wb = await w1.boundingBox();
await page.mouse.move(wb.x + wb.width / 2, wb.y + wb.height / 2);
await page.mouse.down();
await page.waitForFunction(() => document.querySelector('#wb-TRIG1').textContent.includes('closed'), null, { timeout: 3000 });
await page.waitForFunction(() => document.querySelector('#wb-OUT1').textContent.includes('Example show: door knock'), null, { timeout: 3000 });
ok(true, 'wiring: TRIG1 closed, OUT1 driven by the show');
await page.waitForFunction(() => document.querySelector('#wb-CC1').textContent.includes('trigger repeat'), null, { timeout: 3000 });
ok(true, 'wiring: CC1 shows its role');
await page.screenshot({ path: S + '/wiring-generic.png', fullPage: true });
await page.mouse.up();
await page.waitForFunction(() => document.querySelector('#evlog').textContent.includes('TRIG1 closed') && document.querySelector('#evlog').textContent.includes('OUT1 on'), null, { timeout: 3000 });
ok(true, 'event log has the input edge and the output change');
// shows tab: edit and save
await page.click('#tabs button[data-tab="shows"]');
await page.click('#show-list button:has-text("Edit")');
await page.waitForSelector('#editor:not(.hidden)');
await page.fill('#ed-name', 'Door knock v2');
await page.click('.track:has-text("OUT1") button:has-text("+ cue")');
ok((await page.textContent('.track:has-text("OUT1") .cue label')).startsWith('at (s)'), 'cue times in seconds');
await page.selectOption('#ed-new-target', 'RLY1');
await page.click('#ed-add-track');
await page.click('#ed-save');
await page.waitForFunction(() => document.querySelector('#ed-msg').textContent.startsWith('saved'), null, { timeout: 3000 });
ok(true, 'edited show saved');
await page.waitForFunction(() => [...document.querySelectorAll('#show-list .name')].some((e) => e.textContent === 'Door knock v2'), null, { timeout: 3000 });
ok(true, 'list shows the new name');
await page.screenshot({ path: S + '/ui-editor.png', fullPage: true });
// an invalid edit shows the reason
await page.click('.track:has-text("RLY1") .cue select');
await page.selectOption('.track:has-text("RLY1") .cue select', 'on');
await page.click('details summary');
await page.evaluate(() => { const t = document.querySelector('#ed-json'); const j = JSON.parse(t.value); j.tracks.push({ target: 'SOUND', cues: [{ at: 0, do: 'volume', volume: 99 }] }); t.value = JSON.stringify(j); });
await page.click('#ed-json-apply');
await page.click('#ed-save');
await page.waitForFunction(() => document.querySelector('#ed-msg').classList.contains('err'), null, { timeout: 3000 });
ok(/volume \d+ is too loud; the player goes from 0 to 30/.test(await page.textContent('#ed-msg')), 'invalid volume refused: ' + (await page.textContent('#ed-msg')));
ok(!(await page.$$eval('#ed-new-target option', (o) => o.map((x) => x.value))).includes('CC1'), 'role outputs are not offered as show tracks');
// at phone width (390 px) every tab is on screen without scrolling
await page.setViewportSize({ width: 390, height: 900 });
const tabRight = await page.$$eval('#tabs button', (b) => b.filter((x) => x.offsetParent).map((x) => x.getBoundingClientRect().right));
ok(tabRight.length >= 7 && tabRight.every((r) => r <= 390), 'every tab fits at 390 px: ' + JSON.stringify(tabRight.map(Math.round)));
await page.click('#tabs button[data-tab="record"]');
ok(await page.isVisible('#rx-setup') && !(await page.$('#rec-start')), 'the Record effect tab (record-test.mjs records with it); the old Record tab is gone');
// settings
await page.click('#tabs button[data-tab="settings"]');
await page.waitForSelector('#params [data-key="trig_release_ms"]', { state: 'attached' });
await page.click('#params details[data-group="Inputs"] summary');
await page.fill('#params [data-key="trig_release_ms"]', '0.5');
ok((await page.textContent('#p-count')) === '1 change not saved', 'the Save bar counts the change');
await page.click('#p-save');
await page.waitForFunction(() => document.querySelector('#p-msg').classList.contains('ok'), null, { timeout: 3000 });
ok(true, 'settings saved');
await page.screenshot({ path: S + '/ui-settings.png', fullPage: true });
ok(errors.length === 0, 'no page errors ' + JSON.stringify(errors));
await browser.close();
