// The Settings page in a browser against a simulated board: change values, save, read back after a
// page reload and after a restart; an out-of-range value is refused with a message that says what
// to do; a secret is never shown.
//   node settings-test.mjs <screenshot dir> <url>      (CHROMIUM=/path/to/chrome)
import { chromium } from 'playwright-core';
const [S = '.', URL = 'http://127.0.0.1:18080/'] = process.argv.slice(2);
const browser = await chromium.launch({ executablePath: process.env.CHROMIUM || 'google-chrome', args: ['--no-sandbox'] });
const page = await browser.newPage({ viewport: { width: 412, height: 900 }, colorScheme: 'dark' });
const errors = [];
page.on('pageerror', (e) => errors.push(String(e)));
const ok = (c, msg) => { if (!c) { console.log('FAIL: ' + msg); process.exitCode = 1; } else console.log('ok: ' + msg); };
const open = async () => {
  await page.goto(URL);
  await page.waitForSelector('#conn.ok', { timeout: 5000 });
  await page.click('button[data-tab="settings"]');
  await page.waitForSelector('#params [data-key="trig_release_ms"]', { state: 'attached', timeout: 5000 });
  await page.$$eval('#params details', (d) => d.forEach((x) => { x.open = true; }));
};
const rpc = (body) => fetch(URL.replace(/\/$/, '') + '/api/v1/rpc', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) }).then((r) => r.json());
await open();
const n = await page.$$eval('#params [data-key]', (e) => e.length);
// a choice shows the board's own words for its options (from the setting's help), the value stays the same
const kinds = await page.$$eval('[data-key="trig1_input"] option', (o) => o.map((x) => [x.value, x.textContent]));
ok(JSON.stringify(kinds) === JSON.stringify([['switch', 'Switch, button or relay contact (closes to ground)'], ['sensor_high', 'Powered sensor (its signal goes high)']]), 'TRIG1 input type in plain words: ' + JSON.stringify(kinds));
ok(n > 40, `${n} settings on the page`);
ok((await page.textContent('#params')).includes('Trigger re-arm after release (s)'), 'times in seconds: "Trigger re-arm after release (s)"');
// change three kinds at once: a number, a checkbox, a choice, plus a secret
await page.fill('[data-key="trig_debounce_ms"]', '0.08');
await page.uncheck('[data-key="ambient"]');
await page.selectOption('[data-key="cc1_role"]', 'none');
await page.fill('[data-key="wifi_pass"]', 'Pa55word!');
ok((await page.textContent('#p-count')) === '4 changes not saved', 'the Save bar counts the changes');
await page.locator('[data-key="trig_debounce_ms"]').scrollIntoViewIfNeeded();
await page.screenshot({ path: `${S}/settings-savebar.png`, fullPage: false });
await page.click('#p-save');
await page.waitForFunction(() => /saved/.test(document.getElementById('p-msg').textContent), null, { timeout: 5000 });
ok(true, 'saved: ' + (await page.textContent('#p-msg')));
ok((await page.inputValue('[data-key="wifi_pass"]')) === '', 'the password field is empty again after saving');
ok((await page.getAttribute('[data-key="wifi_pass"]', 'placeholder')).startsWith('set (not shown)') && (await page.getAttribute('[data-key="pin"]', 'placeholder')) === 'not set', 'a secret says set / not set, never its value');
await page.screenshot({ path: `${S}/settings-saved.png`, fullPage: false });
// reload: the board's values
await open();
ok((await page.inputValue('[data-key="trig_debounce_ms"]')) === '0.08', 'debounce 0.08 s after a reload');
ok(!(await page.isChecked('[data-key="ambient"]')), 'ambient off after a reload');
ok((await page.inputValue('[data-key="cc1_role"]')) === 'none', 'CC1 role none after a reload');
ok(!(await page.content()).includes('Pa55word!'), 'the password is nowhere on the page');
// out of range: refused, nothing changes, the message says what to do
await page.fill('[data-key="out2_soft_start_ms"]', '6');
await page.click('#p-save');
await page.waitForFunction(() => document.getElementById('p-msg').className.includes('err'), null, { timeout: 5000 });
const m = await page.textContent('#p-msg');
ok(/"OUT2 soft start" goes from 0 to 2 s, not 6: nothing saved/.test(m), 'refused on the phone, in seconds: ' + m);
await page.screenshot({ path: `${S}/settings-refused.png`, fullPage: false });
// a number field left empty is refused on the page (it is not saved as 0 = no limit)
await rpc({ v: 1, id: 8, op: 'set_params', values: [{ key: 'rly1_max_on_ms', value: 60000 }] });
await open();
await page.fill('[data-key="out2_soft_start_ms"]', '');
await page.fill('[data-key="rly1_max_on_ms"]', '');
await page.click('#p-save');
await page.waitForFunction(() => /is empty: enter a number/.test(document.getElementById('p-msg').textContent), null, { timeout: 3000 });
ok(/nothing saved/.test(await page.textContent('#p-msg')), 'an empty number is refused: ' + await page.textContent('#p-msg'));
ok((await rpc({ v: 1, id: 9, op: 'get_params' })).result.values.find((v) => v.key === 'rly1_max_on_ms').value === 60000, 'RLY1 max on time unchanged (not 0 = no limit)');
// restart: still there
await page.evaluate(() => { window.confirm = () => true; });
await page.click('#p-reboot');
await page.waitForTimeout(2500);
await open();
ok((await page.inputValue('[data-key="trig_debounce_ms"]')) === '0.08', 'debounce 0.08 s after a restart');
ok((await page.inputValue('[data-key="out2_soft_start_ms"]')) !== '6', 'the refused value was not stored');
// another device saves a setting while this page has a half-typed edit: the edit stays, a message offers a reload
await open();
await page.fill('[data-key="trig_debounce_ms"]', '0.095');
await page.click('#params h2'); // the field is no longer focused, the edit not saved
ok((await rpc({ v: 1, id: 1, op: 'set_params', values: [{ key: 'trig_release_ms', value: 333 }] })).ok, 'another device saves a setting');
await page.waitForFunction(() => /another device/.test(document.getElementById('p-msg').textContent), null, { timeout: 5000 });
ok((await page.inputValue('[data-key="trig_debounce_ms"]')) === '0.095', 'the half-typed value stays: ' + (await page.textContent('#p-msg')));
await page.click('#p-msg button');
await page.waitForFunction(() => document.querySelector('[data-key="trig_release_ms"]').value === '0.333', null, { timeout: 5000 });
ok((await page.inputValue('[data-key="trig_debounce_ms"]')) === '0.08', 'Reload shows the board\'s values');
// the connection drops and comes back while an edit is not saved: the edit stays
await page.fill('[data-key="trig_debounce_ms"]', '0.077');
await page.click('#params h2');
await page.evaluate(() => conn.ws.close());
await page.waitForFunction(() => !document.querySelector('#conn').classList.contains('ok'), null, { timeout: 3000 });
await page.waitForSelector('#conn.ok', { timeout: 8000 });
await page.waitForFunction(() => /Reconnected/.test(document.getElementById('p-msg').textContent), null, { timeout: 5000 });
ok((await page.inputValue('[data-key="trig_debounce_ms"]')) === '0.077', 'a reconnect keeps the unsaved edit: ' + (await page.textContent('#p-msg')));
await page.click('#p-msg button');
await page.waitForFunction(() => document.querySelector('[data-key="trig_debounce_ms"]').value === '0.08', null, { timeout: 5000 });
// with no edits the form follows another device at once
await rpc({ v: 1, id: 2, op: 'set_params', values: [{ key: 'trig_release_ms', value: 444 }] });
await page.waitForFunction(() => document.querySelector('[data-key="trig_release_ms"]').value === '0.444', null, { timeout: 5000 });
ok(true, 'no edits: the form follows another device');
// a name given in Settings shows everywhere beside the board name: "Fog machine (RLY1)"
await page.fill('[data-key="rly1_name"]', 'Fog machine');
await page.fill('[data-key="trig2_name"]', 'Doorbell');
await page.click('#p-save');
await page.waitForFunction(() => /saved 2 settings/.test(document.getElementById('p-msg').textContent), null, { timeout: 5000 });
await page.click('button[data-tab="live"]');
await page.waitForFunction(() => document.querySelector('#out-RLY1 .head span')?.textContent === 'Fog machine (RLY1)', null, { timeout: 5000 });
ok((await page.textContent('#in-TRIG2')).startsWith('Doorbell (TRIG2)'), 'Live: "Fog machine (RLY1)", "Doorbell (TRIG2)"');
await page.click('button[data-tab="record"]');
ok((await page.$$eval('#rx-outs label', (o) => o.map((x) => x.textContent))).includes('Fog machine (RLY1)'), 'Record effect: the outputs say "Fog machine (RLY1)"');
ok((await page.$$eval('#rx-trigger option', (o) => o.map((x) => x.textContent))).includes('Doorbell (TRIG2)'), 'Record effect: the trigger list says "Doorbell (TRIG2)"');
await page.click('button[data-tab="shows"]');
await page.click('#new-show');
ok((await page.$$eval('#ed-triggers select', (s) => s.flatMap((x) => [...x.options].map((o) => o.textContent)))).includes('Doorbell (TRIG2)'), 'editor: the trigger input list says "Doorbell (TRIG2)"');
ok((await page.$$eval('#ed-new-target option', (o) => o.map((x) => x.textContent))).includes('Fog machine (RLY1)'), 'editor: the new track list says "Fog machine (RLY1)"');
await page.screenshot({ path: `${S}/settings-names-editor.png`, fullPage: false });
await page.click('#ed-cancel');
await page.click('button[data-tab="settings"]');
// a new Wi-Fi network: the page says how to find the board again after the restart
await page.fill('[data-key="wifi_ssid"]', 'HauntNet');
await page.click('#p-save');
await page.waitForFunction(() => /joins "HauntNet" and keeps its own Wi-Fi on/.test(document.getElementById('p-msg').textContent), null, { timeout: 5000 });
ok(/\.local/.test(await page.textContent('#p-msg')), 'Wi-Fi change: ' + (await page.textContent('#p-msg')).slice(0, 140));
await rpc({ v: 1, id: 12, op: 'set_params', values: [{ key: 'wifi_ssid', value: '' }] });
await page.waitForFunction(() => document.querySelector('[data-key="wifi_ssid"]').value === '', null, { timeout: 5000 });
// the page lock turned on from this page: its next save works (the page keeps the password it just set)
await page.fill('[data-key="pin"]', 'spooky1');
await page.check('[data-key="page_lock"]');
await page.click('#p-save');
await page.waitForFunction(() => /saved/.test(document.getElementById('p-msg').textContent), null, { timeout: 5000 });
await page.waitForSelector('#unlock:not([hidden])', { timeout: 5000 });
ok((await page.textContent('#unlock')) === 'Unlocked', 'lock on: the header says Unlocked');
await page.fill('[data-key="trig_debounce_ms"]', '0.06');
await page.click('#p-save');
await page.waitForFunction(() => /saved 1 setting/.test(document.getElementById('p-msg').textContent), null, { timeout: 5000 });
ok(true, 'the next save after turning the lock on works');
// forget the password: a save is refused with the board's words and the Unlock box opens
await page.click('#unlock');
await page.fill('[data-key="trig_debounce_ms"]', '0.061');
await page.click('#p-save');
await page.waitForFunction(() => /page is locked/.test(document.getElementById('p-msg').textContent), null, { timeout: 5000 });
ok(await page.isVisible('#unlock-box') && /^the page is locked: enter the page password: tap Unlock at the top/.test(await page.textContent('#p-msg')), 'locked: ' + await page.textContent('#p-msg'));
await page.screenshot({ path: `${S}/settings-locked.png`, fullPage: false });
await page.fill('#unlock-pin', 'spooky1');
await page.click('#unlock-go');
await page.click('#p-save');
await page.waitForFunction(() => /saved 1 setting/.test(document.getElementById('p-msg').textContent), null, { timeout: 5000 });
ok(true, 'unlocked from the header: saved');
// STOP needs no password
await page.click('#unlock');
await page.click('#stop-all');
await page.waitForFunction(() => /^Stopped/.test(document.querySelector('#stop-line').textContent), null, { timeout: 3000 });
ok(await page.isHidden('#unlock-box'), 'STOP with the lock on and no password: stopped, nothing asked');
ok(errors.length === 0, 'no page errors ' + errors.join(' | '));
await browser.close();
