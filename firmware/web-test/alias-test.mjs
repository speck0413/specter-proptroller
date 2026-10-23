// A profile's own words in the phone page (STATE-MACHINE.md §11, E27, E31): with the built-in effect
// on and the spec's example alias table loaded, the wiring view and the output cards read
// `REFILL (TRIG2)` / `VALVE (OUT1)`, Settings shows the alias labels and units, the status lines use
// the alias names, and the effect's buttons are there. The table is removed again at the end.
//   node alias-test.mjs <screenshot dir> <url>      (CHROMIUM=/path/to/chrome)
import { chromium } from 'playwright-core';
const [S = '.', URL = 'http://127.0.0.1:18080/'] = process.argv.slice(2);
const ok = (c, msg) => { if (!c) { console.log('FAIL: ' + msg); process.exitCode = 1; } else console.log('ok: ' + msg); };
const rpc = async (op, body = {}) => (await fetch(new globalThis.URL('api/v1/rpc', URL), { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ v: 1, op, ...body }) })).json();

const profile = {
  topic_prefix: 'myprop',
  aliases: [
    { alias: 'valve_time', key: 'out1_on_ms', label: 'Valve open time', unit: 'ms' },
    { alias: 'refill_s', key: 'out2_on_ms', label: 'Refill time', unit: 's', scale: 1000 },
    { alias: 'skip_after_output', key: 'out3_enabled', invert: true },
    { alias: 'allow_go', key: 'remote_trigger_enable', label: 'Allow go from the network' },
    { alias: 'go', control: 'trigger' },
    { alias: 'presses', status: 'run_count' },
    { alias: 'VALVE', terminal: 'OUT1', label: 'Valve' },
    { alias: 'REFILL', terminal: 'TRIG2', label: 'Refill button' },
  ],
};
await rpc('delete_show', { show: 1 });
ok((await rpc('set_params', { values: [{ key: 'effect', value: 'pulse_and_recover' }] })).ok, 'effect on');
const r = await rpc('set_profile', { data: profile });
ok(r.ok, 'profile loaded ' + JSON.stringify(r.error || ''));

const browser = await chromium.launch({ executablePath: process.env.CHROMIUM || 'google-chrome', args: ['--no-sandbox'] });
const page = await browser.newPage({ viewport: { width: 412, height: 900 }, colorScheme: 'dark' });
const errors = [];
page.on('pageerror', (e) => errors.push(String(e)));
await page.goto(URL);
await page.waitForSelector('#conn.ok', { timeout: 5000 });
await page.waitForTimeout(800);
const heads = await page.$$eval('#outputs .head span:first-child', (e) => e.map((x) => x.textContent));
ok(heads.includes('VALVE (OUT1)'), 'output card reads VALVE (OUT1): ' + heads.slice(0, 2).join(', '));
const status = await page.textContent('#status-lines');
ok(/presses/i.test(status) && !/run count/i.test(status), 'status line under its alias: presses');
const acts = await page.$$eval('#actions button', (e) => e.map((x) => x.textContent));
ok(acts.some((a) => /Trigger/.test(a)) && acts.some((a) => /Recover/.test(a)), 'effect buttons: ' + acts.join(', '));
ok(acts.includes('go (Trigger)'), 'a control alias names its button: go (Trigger)');
await page.screenshot({ path: `${S}/alias-live.png`, fullPage: false });
// the remote trigger switch beside the buttons, found through the profile, under the alias's label
await page.waitForSelector('#remote-on', { timeout: 3000 });
ok((await page.textContent('#remote-text')) === 'Allow go from the network', 'the switch reads the alias label');
await page.check('#remote-on');
await page.waitForFunction(() => /while this page stays connected/.test(document.querySelector('#actions-msg').textContent), null, { timeout: 3000 });
ok((await rpc('get_params')).result.values.find((v) => v.key === 'allow_go').value === true, 'switched on by its alias: allow_go true');
await page.evaluate(() => conn.ws.close());
await page.waitForFunction(() => /is off again/.test(document.querySelector('#actions-msg').textContent), null, { timeout: 10000 });
ok(!(await page.isChecked('#remote-on')), 'off again with the connection: ' + await page.textContent('#actions-msg'));

await page.click('button[data-tab="wiring"]');
await page.waitForTimeout(300);
const names = await page.$$eval('#wiring .wbox .a', (e) => e.map((x) => x.textContent));
ok(names.includes('REFILL (TRIG2)') && names.includes('VALVE (OUT1)'), 'wiring view: REFILL (TRIG2), VALVE (OUT1)');
const tags = await page.textContent('#wiring');
ok(/Refill button/.test(tags) && /Valve/.test(tags), 'the profile labels name what is connected');
await page.screenshot({ path: `${S}/alias-wiring.png`, fullPage: true });

await page.click('button[data-tab="settings"]');
await page.waitForSelector('#params [data-key="valve_time"]', { state: 'attached', timeout: 5000 });
await page.$$eval('#params details', (d) => d.forEach((x) => { x.open = true; }));
const labels = await page.textContent('#params');
ok(/Valve open time \(ms\)/.test(labels), 'Settings: the alias label and unit');
ok(/except where the name says \(ms\)/.test(await page.textContent('#p-units')), 'units line: a time in ms is typed in ms: ' + await page.textContent('#p-units'));
ok(await page.isVisible('#stop-all'), 'STOP shown on a board with shows and outputs');
ok(/Refill time \(s\)/.test(labels), 'Settings: a scaled alias in its own unit');
ok((await page.inputValue('[data-key="refill_s"]')) === '5', 'refill_s shows 5 (5500 ms in whole seconds)');
ok(!(await page.$('#params [data-key="out1_on_ms"]')), 'the generic key is listed once, under its alias');
await page.fill('[data-key="valve_time"]', '750');
await page.click('#p-save');
await page.waitForFunction(() => /saved/.test(document.getElementById('p-msg').textContent), null, { timeout: 5000 });
const back = await rpc('get_params');
ok(back.result.values.find((v) => v.key === 'valve_time').value === 750, 'saved by its alias: valve_time 750');
await page.screenshot({ path: `${S}/alias-settings.png`, fullPage: false });

ok(errors.length === 0, 'no page errors ' + errors.join(' | '));
await browser.close();
await rpc('set_profile', { data: {} });
await rpc('set_params', { values: [{ key: 'effect', value: 'none' }, { key: 'out1_on_ms', value: 1000 }] });
