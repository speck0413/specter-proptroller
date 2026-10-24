// The Script tab in a browser against a simulated board: a script with mistakes is checked (every
// message, a tap jumps to its line), nothing is saved; the fixed script is saved and runs from a held
// input (the Live tab shows the effect and its line); it is still there after a reload and a restart.
//   node script-test.mjs <screenshot dir> <url>      (CHROMIUM=/path/to/chrome)
import { chromium } from 'playwright-core';
const [S = '.', URL = 'http://127.0.0.1:18080/'] = process.argv.slice(2);
const browser = await chromium.launch({ executablePath: process.env.CHROMIUM || 'google-chrome', args: ['--no-sandbox'] });
const page = await browser.newPage({ viewport: { width: 412, height: 900 }, colorScheme: 'dark' });
const errors = [];
page.on('pageerror', (e) => errors.push(String(e)));
const ok = (c, msg) => { if (!c) { console.log('FAIL: ' + msg); process.exitCode = 1; } else console.log('ok: ' + msg); };
const open = async (tab = 'script') => {
  await page.goto(URL);
  await page.waitForSelector('#conn.ok', { timeout: 5000 });
  await page.click(`button[data-tab="${tab}"]`);
};
await open();
ok(await page.isVisible('#script-text'), 'the Script tab is there');
ok(/no effect script/.test(await page.textContent('#script-state')), 'no effect script stored at first');
const bad = 'effect porch light:\n  wait for trig1\n  out5 = on\n  rly1 = 50%\n  sleep soon\n';
await page.fill('#script-text', bad);
await page.click('#script-check');
await page.waitForSelector('.smsg.err', { timeout: 5000 });
const msgs = await page.$$eval('.smsg', (e) => e.map((x) => x.textContent));
ok(msgs.length === 3, `${msgs.length} messages: ${msgs.join(' | ')}`);
ok(msgs[0] === 'line 3: there is no out5 on this board. The outputs are out1 to out4, rly1, rly2, cc1 and cc2.', 'the out5 message, word for word');
ok(/line 4: rly1 can only be on or off, not 50%/.test(msgs[1]), 'the on/off message');
await page.click('.smsg >> nth=2');
const sel = await page.evaluate(() => { const t = document.getElementById('script-text'); return t.value.slice(t.selectionStart, t.selectionEnd); });
ok(sel === '  sleep soon', 'tapping a message selects its line: ' + JSON.stringify(sel));
await page.screenshot({ path: `${S}/script-check-errors.png`, fullPage: false });
// a long script: the message for a line far down scrolls the text box to it and marks it (Enter works too)
const long = 'effect porch light:\n  wait for trig1\n' + '  out1 = on\n  sleep 1\n'.repeat(20) + '  rly1 = 50%\n';
await page.fill('#script-text', long);
await page.click('#script-check');
await page.waitForSelector('.smsg.err[data-line="43"]', { timeout: 5000 });
await page.evaluate(() => { const t = document.getElementById('script-text'); t.setSelectionRange(0, 0); t.scrollTop = 0; });
await page.focus('.smsg.err[data-line="43"]');
await page.keyboard.press('Enter');
const far = await page.evaluate(() => { const t = document.getElementById('script-text'); return { sel: t.value.slice(t.selectionStart, t.selectionEnd), top: t.scrollTop, at: t.dataset.gotoLine, focused: document.activeElement === t, lineTop: (43 - 1) * (parseFloat(getComputedStyle(t).lineHeight) || 20), h: t.clientHeight }; });
ok(far.sel === '  rly1 = 50%' && far.at === '43' && far.focused, 'Enter on a message selects line 43 in the text box: ' + JSON.stringify(far));
ok(far.top > 0 && far.lineTop >= far.top && far.lineTop < far.top + far.h, 'the text box scrolled line 43 into view: ' + JSON.stringify(far));
// Blocks only: the message marks the line's block; Script and blocks: both
await page.click('input[name=script-view][value=blocks]');
await page.click('.smsg.err[data-line="43"]');
// the blocks are drawn after the click (a busy CI runner can be slower than the evaluate below)
await page.waitForSelector('#script-blocks .blk.goto', { timeout: 3000 }).catch(() => {});
const blk =await page.evaluate(() => [...document.querySelectorAll('#script-blocks .blk.goto')].map((b) => [b.dataset.type, b.querySelector('input')?.value]));
ok(blk.length === 1 && blk[0][0] === 'ps_set' && blk[0][1] === 'rly1', 'Blocks only: the rly1 block is marked: ' + JSON.stringify(blk));
ok(await page.evaluate(() => document.querySelector('input[name=script-view]:checked').value) === 'blocks', 'Blocks only stays (a block holds the line)');
await page.screenshot({ path: `${S}/script-goto-block.png`, fullPage: false });
await page.click('input[name=script-view][value=text]');
// save with errors: refused, nothing stored
await page.click('#script-save');
await page.waitForFunction(() => /nothing saved/.test(document.getElementById('script-msg').textContent), null, { timeout: 5000 });
ok(true, 'save refused: ' + (await page.textContent('#script-msg')));
// fixed, with a sound by its number and a volume
const good = 'effect porch light:\n  wait for trig1\n  out1 = on\n  audio_volume = 60%\n  audio_single = 3\n  sleep 2\n  fade out1 to 0% over 1\n';
await page.fill('#script-text', good);
await page.click('#script-check');
await page.waitForFunction(() => /^no errors/.test(document.getElementById('script-msg').textContent), null, { timeout: 5000 });
ok(true, await page.textContent('#script-msg'));
await page.click('#script-save');
await page.waitForFunction(() => /saved: 1 effect/.test(document.getElementById('script-msg').textContent), null, { timeout: 5000 });
ok(true, await page.textContent('#script-msg'));
await page.screenshot({ path: `${S}/script-saved.png`, fullPage: false });
// run it: hold TRIG1 on the Live tab (after the 2 s boot lockout of the simulator)
await page.click('button[data-tab="live"]');
await page.waitForSelector('#live-script-card:not(.hidden)', { timeout: 5000 });
const chip = page.locator('#in-TRIG1');
await chip.scrollIntoViewIfNeeded();
const box = await chip.boundingBox();
await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
await page.mouse.down();
await page.waitForTimeout(400);
await page.mouse.up();
await page.waitForFunction(() => /line/.test(document.getElementById('live-script').textContent), null, { timeout: 4000 });
ok(true, 'running: ' + (await page.textContent('#live-script')));
await page.screenshot({ path: `${S}/script-running.png`, fullPage: false });
await page.waitForFunction(() => /waiting/.test(document.getElementById('live-script').textContent), null, { timeout: 8000 });
ok(true, 'back at its wait for: ' + (await page.textContent('#live-script')));
// reload and restart: the text exactly as written
await open();
await page.waitForFunction((g) => document.getElementById('script-text').value === g, good, { timeout: 5000 });
ok(true, 'the text is kept after a reload');
await page.click('button[data-tab="settings"]');
await page.evaluate(() => { window.confirm = () => true; });
await page.click('#p-reboot');
await page.waitForTimeout(2500);
await open();
await page.waitForFunction((g) => document.getElementById('script-text').value === g, good, { timeout: 5000 });
ok(/stored, running/.test(await page.textContent('#script-state')), 'still stored and running after a restart');
// another phone saves a script while this page has edits: the edits stay, the page says so, Save asks first
await page.fill('#script-text', good + '# my edit\n');
const other = 'effect other phone:\n  wait for trig2\n  out3 = on\n  sleep 1\n  out3 = off\n';
await page.evaluate(async (t) => fetch('/api/v1/rpc', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ v: 1, op: 'set_script', text: t }) }), other);
await page.waitForFunction(() => /changed on another phone/.test(document.getElementById('script-msg').textContent), null, { timeout: 5000 });
ok((await page.inputValue('#script-text')).includes('# my edit'), 'the edits stay: ' + await page.textContent('#script-msg'));
await page.click('#script-save');
await page.waitForSelector('.ask .ask-yes', { timeout: 2000 });
ok(/Replace the other phone/.test(await page.textContent('.ask')), 'Save asks before replacing the other phone\'s script');
await page.click('.ask .ask-no');
await page.click('#script-revert');
await page.waitForSelector('.ask .ask-yes', { timeout: 2000 });
await page.click('.ask .ask-yes');
await page.waitForFunction((t) => document.getElementById('script-text').value === t, other, { timeout: 3000 });
ok(true, 'Revert (asked first) loads the stored script');
// a show on the same terminals as the script: both tabs say so
await page.evaluate(async () => fetch('/api/v1/rpc', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ v: 1, op: 'put_show', new: true, data: { id: 0, name: 'Door', triggers: [{ on: 'input', input: 'TRIG2' }], tracks: [{ target: 'OUT3', cues: [{ at: 0, do: 'pulse', ms: 500 }] }] } }) }));
await page.waitForFunction(() => /"Door" starts from .*TRIG2.* and drives .*OUT3.*, as the script does/.test(document.getElementById('overlap-script').textContent), null, { timeout: 5000 });
ok(true, 'script and show share an input and an output: ' + await page.textContent('#overlap-script'));
// a show on another input that drives the same output: only the output is named, never "starts from"
await page.evaluate(async () => fetch('/api/v1/rpc', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ v: 1, op: 'put_show', new: true, data: { id: 0, name: 'Lamp', triggers: [{ on: 'input', input: 'USER1' }], tracks: [{ target: 'OUT3', cues: [{ at: 0, do: 'pulse', ms: 500 }] }] } }) }));
await page.waitForFunction(() => /"Lamp" drives/.test(document.getElementById('overlap-script').textContent), null, { timeout: 5000 });
const lap = await page.textContent('#overlap-script');
ok(/these shows/.test(lap) && /"Lamp" drives [^;]*OUT3[^;]*, as the script does/.test(lap) && !/"Lamp" starts from/.test(lap) && !/USER1/.test(lap), 'a show on another input: only the shared output is named: ' + lap);
// keyboard: Tab indents in the box, Esc then Tab leaves it
await page.focus('#script-text');
await page.keyboard.press('Escape');
await page.keyboard.press('Tab');
ok(await page.evaluate(() => document.activeElement.id !== 'script-text'), 'Esc, then Tab leaves the script box (now on ' + await page.evaluate(() => document.activeElement.id || document.activeElement.tagName) + ')');
ok((await page.textContent('#tab-script h2')) === 'Effect script', 'the tab says "Effect script"');
// phone width: no horizontal page scroll
const w = await page.evaluate(() => document.documentElement.scrollWidth);
ok(w <= 412, `no horizontal scroll (${w} px)`);
ok(errors.length === 0, 'no page errors ' + errors.join(' | '));
await browser.close();
