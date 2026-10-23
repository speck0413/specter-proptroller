// The Firmware tab in a browser against a simulated board (`.pio/build/sim/program --speed 10`, so the 30 s
// self-check takes 3 s): files checked in the page before sending (another board's image, too big), a wrong
// SHA-256 answered by the board, a refusal while a show runs with the "stop all" button, an update that
// restarts the board into the new firmware (pending, then valid), and one whose self-check fails (rolled
// back), sent with the page lock on.
//   node update-test.mjs <screenshot dir> <url>      (CHROMIUM=/path/to/chrome; PTCLIENT=path to tools/ptclient.py)
import { chromium } from 'playwright-core';
import { execFileSync } from 'node:child_process';
import { readFileSync, mkdtempSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
const [S = '.', URL = 'http://127.0.0.1:18080/'] = process.argv.slice(2);
const PTCLIENT = process.env.PTCLIENT || new globalThis.URL('../arduino/tools/ptclient.py', import.meta.url).pathname;
const dir = mkdtempSync(join(tmpdir(), 'pt-update-'));
const image = (name, ...args) => { const p = join(dir, name); execFileSync('python3', [PTCLIENT, 'image', p, ...args]); return p; };
const good = image('good.bin', '--version', '0.2.0', '--build', 'web0001');
const failing = image('failing.bin', '--version', '0.3.0', '--build', 'bad0001', '--fail-self-check');

const browser = await chromium.launch({ executablePath: process.env.CHROMIUM || 'google-chrome', args: ['--no-sandbox'] });
const page = await browser.newPage({ viewport: { width: 412, height: 900 }, colorScheme: 'dark' });
const errors = [];
page.on('pageerror', (e) => errors.push(String(e)));
let pin = null;
page.on('dialog', (d) => (d.type() === 'prompt' ? d.accept(pin ?? '') : d.accept()));
const ok = (c, msg) => { if (!c) { console.log('FAIL: ' + msg); process.exitCode = 1; } else console.log('ok: ' + msg); };
const rpc = (env) => page.evaluate(async (e) => (await fetch('/api/v1/rpc', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(e) })).json(), env);
const text = (id) => page.textContent('#' + id);
const waitMsg = (re, ms = 20000) => page.waitForFunction((r) => new RegExp(r).test(document.getElementById('fw-msg').textContent), re.source, { timeout: ms });

await page.goto(URL);
await page.waitForSelector('#conn.ok', { timeout: 5000 });
ok(await page.isVisible('button[data-tab="firmware"]'), 'the Firmware tab is there (update capability)');
await page.click('button[data-tab="firmware"]');
ok(/sim/.test(await text('fw-info')) && /app0/.test(await text('fw-info')), 'board, slot shown: ' + (await text('fw-info')).replace(/\s+/g, ' '));

// another board's image, dropped: refused in the page, nothing sent
await page.evaluate(() => {
  const b = new Uint8Array(4096); b[0] = 0xE9; b[32] = 0x32; b[33] = 0x54; b[34] = 0xcd; b[35] = 0xab;
  const dt = new DataTransfer(); dt.items.add(new File([b], 'esp32.bin'));
  document.getElementById('fw-drop').dispatchEvent(new DragEvent('drop', { dataTransfer: dt, bubbles: true, cancelable: true }));
});
await page.waitForFunction(() => /ESP32/.test(document.getElementById('fw-file-info').textContent));
ok(/not for this board \(sim\)/.test(await text('fw-file-info')), 'dropped ESP32 image: ' + await text('fw-file-info'));
ok(await page.isDisabled('#fw-send'), 'Update stays disabled');
await page.screenshot({ path: `${S}/update-wrong-board.png` });

// too big
await page.setInputFiles('#fw-file', { name: 'big.bin', mimeType: 'application/octet-stream', buffer: Buffer.alloc(1966081) });
await page.waitForFunction(() => /at most/.test(document.getElementById('fw-file-info').textContent));
ok(/1,966,081 bytes; this board takes at most 1,966,080/.test(await text('fw-file-info')), 'too big: ' + await text('fw-file-info'));

// a wrong SHA-256 (changed on the way): the board says so, the running firmware is untouched
await page.setInputFiles('#fw-file', good);
await page.waitForFunction(() => /simulator firmware/.test(document.getElementById('fw-file-info').textContent));
ok(/0\.2\.0 \(web0001\)/.test(await text('fw-file-info')), 'file read: ' + await text('fw-file-info'));
await page.route('**/api/v1/update', (r) => r.continue({ headers: { ...r.request().headers(), 'x-proptroller-sha256': '0'.repeat(64) } }));
await page.click('#fw-send');
await waitMsg(/SHA-256 does not match/);
ok(true, 'bad SHA-256: ' + await text('fw-msg'));
await page.unroute('**/api/v1/update');

// refused while a show runs; "stop all" stops it and sends again; restart, pending, valid
let r = await rpc({ op: 'put_show', data: { id: 5, name: 'Long', tracks: [{ target: 'OUT2', cues: [{ at: 0, do: 'on' }, { at: 600000, do: 'off' }] }] } });
ok(r.ok, 'a long show stored');
await rpc({ op: 'run', show: 5 });
await page.click('#fw-send');
await waitMsg(/not updated: a show is running: stop it first/);
ok(await page.isVisible('#fw-stop'), 'refused while a show runs, "stop all" offered: ' + await text('fw-msg'));
await page.screenshot({ path: `${S}/update-refused.png` });
await page.click('#fw-stop');
await waitMsg(/checking itself|updated: now/);
ok(true, 'after the restart: ' + await text('fw-msg'));
await waitMsg(/updated: now 0\.2\.0 \(web0001\), valid/, 30000);
ok(true, await text('fw-msg'));
ok(/app1/.test(await text('fw-info')) && /web0001/.test(await text('fw-info')), 'info shows the new firmware in app1');
await page.screenshot({ path: `${S}/update-done.png` });

// page lock on: the password is asked; the new firmware fails its self-check and the board goes back
r = await rpc({ op: 'set_params', values: [{ key: 'pin', value: '258025' }, { key: 'page_lock', value: true }] });
ok(r.ok, 'page lock on');
await page.waitForSelector('#unlock:not([hidden])', { timeout: 5000 });
await page.setInputFiles('#fw-file', failing);
await page.waitForFunction(() => /bad0001/.test(document.getElementById('fw-file-info').textContent));
// locked: Update asks for the password in the masked box at the top (no prompt() showing it)
await page.click('#fw-send');
await waitMsg(/the page is locked/);
ok(await page.isVisible('#unlock-box') && (await page.getAttribute('#unlock-pin', 'type')) === 'password', 'locked: ' + await text('fw-msg'));
await page.fill('#unlock-pin', '258025');
await page.click('#unlock-go');
await page.click('#fw-send');
await waitMsg(/failed its self-check: the board went back to 0\.2\.0 \(web0001\)/, 40000);
ok(true, 'rolled back: ' + await text('fw-msg'));
ok(/went back/.test(await text('fw-info')), 'info says the last update failed');
await page.screenshot({ path: `${S}/update-rolled-back.png` });

ok(errors.length === 0, 'no page errors ' + errors.join(' | '));
await browser.close();
