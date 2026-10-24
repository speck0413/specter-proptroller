// A show script's effects started from the phone page by their names (SHOW-SCRIPT.md SS-R10): the Live tab offers
// a button per effect, Cancel and the remote trigger switch; a start is refused until the switch is on (except an
// effect in remote_free_effects), then starts the effect; the switch goes off when the page closes (SM-R2). Then
// start buttons that need a press held about a second, no STOP, and a profile's words.
//   node remote-script-test.mjs <screenshot dir> <url>      (CHROMIUM=/path/to/chrome)
import { chromium } from 'playwright-core';
const [S = '.', URL = 'http://127.0.0.1:18080/'] = process.argv.slice(2);
const ok = (c, msg) => { if (!c) { console.log('FAIL: ' + msg); process.exitCode = 1; } else console.log('ok: ' + msg); };
const rpc = async (op, body = {}) => (await fetch(URL + 'api/v1/rpc', { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ v: 1, op, ...body }) })).json();
const status = async () => Object.fromEntries((await rpc('state')).result.status.map((x) => [x.key, x.value]));
const SCRIPT = 'effect run:\n  wait for trig1\n  out1 = on\n  out2 = on\n  sleep 2\n  out1 = off\n  out2 = off\neffect refill:\n  wait for trig2\n  out2 = on\n  sleep 1\n  out2 = off\n';
await rpc('set_params', { values: [{ key: 'remote_free_effects', value: 'refill' }] });
const set = await rpc('set_script', { text: SCRIPT });
ok(set.ok && set.result.ok, 'the script is saved: ' + JSON.stringify(set.result?.errors || set.error || ''));
await new Promise((r) => setTimeout(r, 2500));  // the boot lockout
const browser = await chromium.launch({ executablePath: process.env.CHROMIUM || 'google-chrome', args: ['--no-sandbox'] });
const page = await browser.newPage({ viewport: { width: 412, height: 900 }, colorScheme: 'dark' });
const errors = [];
page.on('pageerror', (e) => errors.push(String(e)));
await page.goto(URL);
await page.waitForSelector('#conn.ok', { timeout: 5000 });
await page.waitForSelector('#actions button[data-key="run"]', { timeout: 5000 });
const keys = await page.$$eval('#actions button', (b) => b.map((x) => x.dataset.key));
ok(['run', 'refill', 'cancel'].every((k) => keys.includes(k)), 'the Live tab offers a button per effect and Cancel: ' + keys.join(', '));
ok(await page.isVisible('#remote-on'), 'the remote trigger switch is there');
await page.click('#actions button[data-key="run"]');
await page.waitForTimeout(500);
ok(!(await status()).pulsing, 'Start run without the switch: nothing starts');
await page.click('#actions button[data-key="refill"]');
await page.waitForTimeout(300);
ok((await status()).recovering === true, 'refill (in remote_free_effects) starts without the switch');
await page.waitForTimeout(1000);
await page.check('#remote-on');
await page.waitForTimeout(300);
ok((await status()).remote_trigger_enable === true, 'the switch turns remote_trigger_enable on');
await page.click('#actions button[data-key="run"]');
await page.waitForTimeout(500);
const st = await status();
ok(st.pulsing === true && st.run_count === 2, 'Start run starts the effect: ' + JSON.stringify(st));
await page.screenshot({ path: `${S}/remote-script.png` });
await page.close();
await new Promise((r) => setTimeout(r, 6500));  // the page's session ends (5 s without a heartbeat at most)
ok((await status()).remote_trigger_enable === false, 'the switch went off with the page');
// start buttons held (start_buttons = hold), no STOP (stop_button off), a profile's words for the button and a
// status line, and a time left as m:ss
await rpc('set_params', { values: [{ key: 'start_buttons', value: 'hold' }, { key: 'stop_button', value: false }, { key: 'remote_free_effects', value: 'run' }] });
const prof = await rpc('set_profile', { data: { aliases: [{ alias: 'run', control: 'trigger', label: 'Go' }, { alias: 'time_left', status: 'cooldown_remaining_ms', label: 'Time left' }] } });
ok(prof.ok, 'the profile is loaded: ' + JSON.stringify(prof.error || ''));
await rpc('set_script', { text: 'effect run:\n  wait for trig1\n  limit 1 run per 5 min\n  out1 = on\n  sleep 1\n  out1 = off\n' });
const p2 = await browser.newPage({ viewport: { width: 390, height: 900 } });
p2.on('pageerror', (e) => errors.push(String(e)));
await p2.goto(URL);
await p2.waitForSelector('#actions button.hold-start[data-key="run"]', { timeout: 5000 });
ok(!(await p2.isVisible('#stop-all')), 'stop_button off: no STOP on the page');
ok((await p2.textContent('#actions button[data-key="run"]')).trim() === 'Go', 'the profile names the button: Go');
const runs0 = (await status()).run_count;
await p2.click('#actions button[data-key="run"]');
await p2.waitForTimeout(600);
ok((await status()).run_count === runs0, 'held start: a click sends nothing');
const bb = await p2.locator('#actions button[data-key="run"]').boundingBox();
await p2.mouse.move(bb.x + bb.width / 2, bb.y + bb.height / 2);
await p2.mouse.down();
await p2.waitForTimeout(500);
const half = await p2.$eval('#actions button[data-key="run"] .fill', (f) => parseFloat(f.style.width));
await p2.mouse.up();
await p2.waitForTimeout(400);
ok(half > 20 && half < 90 && (await status()).run_count === runs0, `let go after half a second: nothing sent, the bar was at ${half} %`);
await p2.mouse.down();
await p2.waitForTimeout(400);
await rpc('set_params', { values: [{ key: 'ambient_volume', value: 12 }] });  // another phone saves: the page redraws
await p2.waitForTimeout(900);
await p2.mouse.up();
await p2.waitForTimeout(300);
ok((await status()).run_count === runs0 + 1, 'held about a second while another phone saved settings: the hold is kept, the effect starts');
await p2.waitForTimeout(500);
ok((await p2.$$eval('#actions button.hold-start', (b) => b.length)) === 1, 'the redraw that waited is done after the hold');
const lines = await p2.$$eval('#status-lines div', (d) => d.map((x) => x.textContent));
const i = lines.indexOf('Time left');
ok(i >= 0 && /^\d+:\d\d$/.test(lines[i + 1]), 'the time left as m:ss under the profile\'s label: ' + lines[i + 1]);
await p2.screenshot({ path: `${S}/remote-script-hold.png` });
await p2.close();
await rpc('set_profile', { data: { aliases: [] } });
await rpc('set_params', { values: [{ key: 'start_buttons', value: 'click' }, { key: 'stop_button', value: true }] });
await rpc('set_script', { text: '' });
await rpc('set_params', { values: [{ key: 'remote_free_effects', value: '' }] });
ok(errors.length === 0, 'no page errors: ' + errors.join(' | '));
await browser.close();
