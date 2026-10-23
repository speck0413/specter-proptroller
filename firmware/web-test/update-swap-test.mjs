// The Firmware tab against a simulated board whose bootloader installs the update at the restart, as the Pico W /
// Pico 2 W bootloader swaps it in (`.pio/build/sim/program --speed 10 --swap-ms 20000`: off the network for 2 s,
// info.update.install_max_s 180): the page says the board is installing the update and how long it may take,
// keeps waiting while the board is away, then sees the new firmware pending and valid; a failing image is
// swapped back the same way.
//   node update-swap-test.mjs <screenshot dir> <url>      (CHROMIUM=/path/to/chrome; PTCLIENT=path to tools/ptclient.py)
import { chromium } from 'playwright-core';
import { execFileSync } from 'node:child_process';
import { mkdtempSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
const [S = '.', URL = 'http://127.0.0.1:18080/'] = process.argv.slice(2);
const PTCLIENT = process.env.PTCLIENT || new globalThis.URL('../arduino/tools/ptclient.py', import.meta.url).pathname;
const dir = mkdtempSync(join(tmpdir(), 'pt-swap-'));
const image = (name, ...args) => { const p = join(dir, name); execFileSync('python3', [PTCLIENT, 'image', p, ...args]); return p; };
const good = image('good.bin', '--version', '0.2.0', '--build', 'swp0001');
const failing = image('failing.bin', '--version', '0.3.0', '--build', 'swp0bad', '--fail-self-check');

const browser = await chromium.launch({ executablePath: process.env.CHROMIUM || 'google-chrome', args: ['--no-sandbox'] });
const page = await browser.newPage({ viewport: { width: 412, height: 900 }, colorScheme: 'dark' });
const errors = [];
page.on('pageerror', (e) => errors.push(String(e)));
page.on('dialog', (d) => d.accept());
const ok = (c, msg) => { if (!c) { console.log('FAIL: ' + msg); process.exitCode = 1; } else console.log('ok: ' + msg); };
const text = (id) => page.textContent('#' + id);
const waitMsg = (re, ms = 20000) => page.waitForFunction((r) => new RegExp(r).test(document.getElementById('fw-msg').textContent), re.source, { timeout: ms });

await page.goto(URL);
await page.waitForSelector('#conn.ok', { timeout: 5000 });
const info = await page.evaluate(async () => (await fetch('/api/v1/info')).json());
ok(info.update?.install_max_s === 180, 'info.update.install_max_s: ' + info.update?.install_max_s);
await page.click('button[data-tab="firmware"]');

await page.setInputFiles('#fw-file', good);
await page.waitForFunction(() => /swp0001/.test(document.getElementById('fw-file-info').textContent));
await page.click('#fw-send');
await waitMsg(/installing the update, this can take up to 3 minutes/, 10000);
ok(true, 'while the board installs it: ' + await text('fw-msg'));
await page.screenshot({ path: `${S}/update-swap-installing.png` });
await waitMsg(/updated: now 0\.2\.0 \(swp0001\), valid/, 30000);
ok(true, await text('fw-msg'));

await page.setInputFiles('#fw-file', failing);
await page.waitForFunction(() => /swp0bad/.test(document.getElementById('fw-file-info').textContent));
await page.click('#fw-send');
await waitMsg(/failed its self-check: the board went back to 0\.2\.0 \(swp0001\)/, 40000);
ok(true, 'swapped back: ' + await text('fw-msg'));

ok(errors.length === 0, 'no page errors ' + errors.join(' | '));
await browser.close();
