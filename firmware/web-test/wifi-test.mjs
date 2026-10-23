// Settings -> Wi-Fi in a browser against a simulated board: the board's address on the card, and the network
// field as an editable combo box (type a name, or pick one of the networks the board sees: op wifi_scan, the
// simulator's fixed list), with Rescan and its rate limit.
//   node wifi-test.mjs <screenshot dir> <url>      (CHROMIUM=/path/to/chrome)
import { chromium } from 'playwright-core';
const [S = '.', URL = 'http://127.0.0.1:18080/'] = process.argv.slice(2);
const browser = await chromium.launch({ executablePath: process.env.CHROMIUM || 'google-chrome', args: ['--no-sandbox'] });
const page = await browser.newPage({ viewport: { width: 390, height: 860 }, colorScheme: 'dark' });
const errors = [];
page.on('pageerror', (e) => errors.push(String(e)));
const ok = (c, msg) => { if (!c) { console.log('FAIL: ' + msg); process.exitCode = 1; } else console.log('ok: ' + msg); };
const SSID = '#params [data-key="wifi_ssid"]';

const rpc = (body) => fetch(URL.replace(/\/$/, '') + '/api/v1/rpc', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) }).then((r) => r.json());
const passSet = async () => (await rpc({ v: 1, op: 'get_params' })).result.values.find((v) => v.key === 'wifi_pass').set;
// the board was on another network, with its password
await rpc({ v: 1, op: 'set_params', values: [{ key: 'wifi_ssid', value: 'Old Net' }, { key: 'wifi_pass', value: 'old-secret-1' }] });
ok((await passSet()) === true, 'the board starts on "Old Net" with a stored password');
await page.goto(URL);
await page.waitForSelector('#conn.ok', { timeout: 5000 });
const caps = await page.evaluate(() => fetch('/api/v1/info').then((r) => r.json()).then((i) => i.capabilities));
ok(caps.includes('wifi_scan'), 'the simulated board says it can look for networks (capability wifi_scan)');
await page.click('button[data-tab="settings"]');
await page.waitForSelector(SSID, { state: 'attached', timeout: 5000 });
await page.$$eval('#params details', (d) => d.forEach((x) => { x.open = x.dataset.group === 'Wi-Fi'; }));
await page.locator('#wifi-address').scrollIntoViewIfNeeded();
ok(/This page is at http:\/\/127\.0\.0\.1:\d+\//.test(await page.textContent('#wifi-address')), 'the Wi-Fi card says where the board is: ' + (await page.textContent('#wifi-address')).slice(0, 60));
ok(/Device → Name/.test(await page.textContent('#wifi-address')), 'and that the name address follows the Name setting');

// the combo box: a text field (type any name) with a button that lists the networks
ok((await page.getAttribute(SSID, 'role')) === 'combobox' && (await page.getAttribute(SSID, 'aria-expanded')) === 'false', 'the network field is a combo box, closed');
ok(await page.isHidden('#wifi-list'), 'the list is closed at first');
await page.click('#wifi-pick');
ok(await page.isVisible('#wifi-list'), 'the ▾ button opens the list');
ok((await page.getAttribute(SSID, 'aria-expanded')) === 'true', 'aria-expanded follows');
ok(/Looking for networks/.test(await page.textContent('#wifi-list')), 'while the board scans the list says so');
await page.waitForSelector('#wifi-list .wifi-net', { timeout: 8000 });
const nets = await page.$$eval('#wifi-list .wifi-net', (b) => b.map((x) => [x.dataset.ssid, x.querySelector('.wifi-meta').textContent]));
ok(JSON.stringify(nets.map((n) => n[0])) === JSON.stringify(['Office', 'Workshop 2.4', 'Guest Wi-Fi', 'Neighbour']), 'each name once, strongest first, no hidden network: ' + nets.map((n) => n[0]).join(', '));
ok(/strong · password/.test(nets[0][1]) && /open/.test(nets[2][1]) && /very weak/.test(nets[3][1]), 'signal and password in words: ' + nets.map((n) => n[1]).join(' | '));
await page.screenshot({ path: `${S}/settings-wifi-list.png`, fullPage: false });

// pick one: the field takes it, the Save bar counts it, the password field is next
await page.click('#wifi-list .wifi-net[data-ssid="Workshop 2.4"]');
ok((await page.inputValue(SSID)) === 'Workshop 2.4', 'tapping a network fills the field');
ok(await page.isHidden('#wifi-list'), 'and closes the list');
ok((await page.textContent('#p-count')) === '1 change not saved', 'the Save bar counts the pick');
ok(/password of "Workshop 2.4"/.test(await page.textContent('#wifi-pick-hint')), 'the hint says to type its password next');
ok(await page.evaluate(() => document.activeElement?.dataset?.key === 'wifi_pass'), 'the Wi-Fi password field has the focus');

// typing a name of one's own still works (a hidden network, or one out of range now)
await page.fill(SSID, 'Back Lot');
ok((await page.inputValue(SSID)) === 'Back Lot' && (await page.textContent('#p-count')) === '1 change not saved', 'a typed name works too');

// an open network says to leave the password empty
await page.click('#wifi-pick');
await page.waitForSelector('#wifi-list .wifi-net', { timeout: 3000 });
await page.click('#wifi-list .wifi-net[data-ssid="Guest Wi-Fi"]');
ok(/open \(no password\)/.test(await page.textContent('#wifi-pick-hint')), 'an open network: leave the password empty');

// Rescan right after a scan: the board keeps scans 10 s apart and the page says when it works again
await page.click('#wifi-pick');
await page.click('#wifi-rescan');
await page.waitForFunction(() => /Rescan works again in \d+ s/.test(document.querySelector('#wifi-list .wifi-status')?.textContent || ''), null, { timeout: 3000 });
ok(true, 'Rescan too soon: ' + (await page.textContent('#wifi-list .wifi-status')));
ok((await page.$$('#wifi-list .wifi-net')).length === 4, 'the last list stays');
await page.keyboard.press('Escape');
ok(await page.isHidden('#wifi-list'), 'Escape closes the list');

// no sideways scrolling at phone width
ok(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth), 'no horizontal scroll at 390 px');

// a secured network picked, password left empty: the page says so and saves nothing
await page.click('#wifi-pick');
await page.waitForSelector('#wifi-list .wifi-net', { timeout: 3000 });
await page.click('#wifi-list .wifi-net[data-ssid="Workshop 2.4"]');
await page.click('#p-save');
await page.waitForFunction(() => /needs its password/.test(document.getElementById('p-msg').textContent), null, { timeout: 3000 });
ok(/Nothing saved/.test(await page.textContent('#p-msg')), 'a secured network with no password is refused on the page: ' + (await page.textContent('#p-msg')));
ok((await rpc({ v: 1, op: 'get_params' })).result.values.find((v) => v.key === 'wifi_ssid').value === 'Old Net', 'nothing was saved');

// an open network, password left empty (as the hint says): saved, and the old network's password is gone
await page.click('#wifi-pick');
await page.waitForSelector('#wifi-list .wifi-net', { timeout: 3000 });
await page.click('#wifi-list .wifi-net[data-ssid="Guest Wi-Fi"]');
await page.click('#p-save');
await page.waitForFunction(() => /^saved/.test(document.getElementById('p-msg').textContent), null, { timeout: 5000 });
ok(/http:\/\/proptroller\.local/.test(await page.textContent('#p-msg')), 'after saving: ' + (await page.textContent('#p-msg')).slice(0, 120));
ok((await passSet()) === false, 'the open network is saved with no password (the old one was not kept)');
await page.screenshot({ path: `${S}/settings-wifi-saved.png`, fullPage: false });

// the Live tab's address card (the hotspot is always on): the simulator is mode "sim", so the card is hidden; the
// board's state as a real board sends it is fed to the page's renderer
await page.click('button[data-tab="live"]');
ok(await page.isHidden('#net-card'), 'no address card on a simulated board');
const where = async (net) => page.evaluate((n) => { state.net = n; renderNetWhere(); return document.getElementById('net-card').hidden ? null : document.getElementById('net-where').textContent; }, net);
let w = await where({ mode: 'sta', ip: '192.168.1.23', ssid: 'Office', host: 'front-door.local', hotspot: 'Proptroller-AC2C' });
ok(/On your Wi-Fi "Office" this board is at http:\/\/front-door\.local\/ \(or http:\/\/192\.168\.1\.23\/\)/.test(w), 'on the hotspot the page gives the board\'s address on your Wi-Fi: ' + w);
ok(/own Wi-Fi "Proptroller-AC2C": http:\/\/192\.168\.4\.1\//.test(w), 'and the hotspot\'s address');
w = await where({ mode: 'ap', ip: '192.168.4.1', ssid: 'Proptroller-AC2C' });
ok(/not joined to a Wi-Fi network/.test(w) && /192\.168\.4\.1/.test(w), 'not on a network: says so: ' + w);
await page.screenshot({ path: `${S}/wifi-net-card.png` });
ok(errors.length === 0, 'no page errors ' + errors.join(' | '));
await browser.close();
