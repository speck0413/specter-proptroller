// The Script tab's three views (Script and blocks / Script only / Blocks only) at 390 px: remembered on the phone,
// stacked when both show; the blocks follow the text, a block edit (value, ↑ ↓, ✕, add) writes the text with
// comments and blank lines kept; a recorded effect opens in it. At 1000 px: side by side, drag to move.
//   node views-test.mjs <screenshot dir> <url>      (CHROMIUM=/path/to/chrome)
import { chromium } from 'playwright-core';
const [S = '.', URL = 'http://127.0.0.1:18080/'] = process.argv.slice(2);
const browser = await chromium.launch({ executablePath: process.env.CHROMIUM || 'google-chrome', args: ['--no-sandbox'] });
const ctx = await browser.newContext({ viewport: { width: 390, height: 844 }, deviceScaleFactor: 2, colorScheme: 'dark' });
const page = await ctx.newPage();
const errors = [];
page.on('pageerror', (e) => errors.push(String(e)));
page.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
const ok = (c, msg) => { if (!c) { console.log('FAIL: ' + msg); process.exitCode = 1; } else console.log('ok: ' + msg); };
const rpc = (req) => page.evaluate(async (r) => (await (await fetch('/api/v1/rpc', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ v: 1, ...r }) })).json()), req);
const text = () => page.inputValue('#script-text');
const open = async () => { await page.goto(URL); await page.waitForSelector('#conn.ok', { timeout: 5000 }); await page.click('#tabs button[data-tab="script"]'); };
const small = () => page.evaluate(() => [...document.querySelectorAll('#tab-script button, #tab-script input, #tab-script select, #tab-script label.check')]
  .filter((e) => e.offsetParent && !(e.type === 'radio' || e.type === 'checkbox'))
  .filter((e) => e.getBoundingClientRect().height < 47.5 || (/^(INPUT|SELECT)$/.test(e.tagName) && parseFloat(getComputedStyle(e).fontSize) < 16))
  .map((e) => `${e.tagName} "${(e.textContent || e.value || '').trim().slice(0, 12)}" ${Math.round(e.getBoundingClientRect().height)}px`));
const noHScroll = async (what) => ok(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth), `${what}: no sideways scrolling`);

const SCRIPT = '# the porch\neffect porch light:   # by the door\n  wait for trig1\n  out1 = on\n\n  sleep 2\n  fade out1 to 0% over 3\n\n# the prop\neffect prop:\n  wait for trig2\n  rly1 = on\n  sleep 1\n  rly1 = off\n';
await page.goto(URL);
await rpc({ op: 'set_script', text: SCRIPT, sounds: {} });
await open();
ok((await page.$$eval('input[name=script-view]', (r) => r.map((x) => x.parentElement.textContent.trim()))).join(' | ') === 'Script and blocks | Script only | Blocks only', 'the three views are there');
ok((await page.$eval('input[name=script-view]:checked', (r) => r.value)) === 'text', 'a small screen starts with Script only');
ok(await page.isHidden('#script-blocks'), 'Script only: no blocks shown');
await page.click('label:has(input[value="both"])');
await page.waitForSelector('#script-blocks .blk.effect');
const ty = (await page.$('#script-text')).boundingBox(), by = (await page.$('#script-blocks')).boundingBox();
ok((await by).y > (await ty).y + (await ty).height - 1, 'Script and blocks at 390 px: the blocks are under the text (stacked)');
const kinds = await page.$$eval('#script-blocks .blk', (b) => b.map((x) => x.dataset.type));
ok(JSON.stringify(kinds) === JSON.stringify(['ps_comment', 'ps_effect', 'ps_wait', 'ps_set', 'ps_sleep', 'ps_fade', 'ps_comment', 'ps_effect', 'ps_wait', 'ps_set', 'ps_sleep', 'ps_set']), 'one block per line, effects hold their lines: ' + kinds.join(','));
ok((await page.textContent('#script-blocks .blk[data-type="ps_wait"] .w')) === 'wait for' && (await page.inputValue('#script-blocks .blk[data-type="ps_wait"] input')) === 'trig1', 'words as in Studio: "wait for [trig1]"');
await noHScroll('Script and blocks (390 px)');
const bad = await small();
ok(bad.length === 0, 'every control in the blocks 48 px high, fields 16 px text' + (bad.length ? ': ' + bad.slice(0, 5).join(', ') : ''));
await page.screenshot({ path: `${S}/views-both-390.png`, fullPage: true });

// a value changed in a block: only that line changes, comments and blank lines stay
await page.fill('#script-blocks .blk[data-type="ps_sleep"] input', '5');
ok(await text() === SCRIPT.replace('sleep 2', 'wait 5'), 'a value changed in a block rewrites only its line');
// ↑ moves a line up, ✕ removes it, Add puts a new line at the end of the effect
await page.click('#script-blocks .blk[data-type="ps_fade"] button[aria-label^="Move up"]');
ok((await text()).includes('  out1 = on\n\n  fade out1 to 0% over 3\n\n  wait 5\n# the prop'), 'Move up: the fade before the wait (a blank line goes with the line above it, as in Studio)');
await page.click('#script-blocks .blk.effect >> nth=1 >> .blk[data-type="ps_sleep"] button[aria-label^="Remove"]');
ok(!(await text()).includes('sleep 1'), 'Remove: the prop\'s sleep is gone');
await page.selectOption('#script-blocks .blk.effect >> nth=1 >> select', 'ps_audio_single');
await page.click('#script-blocks .blk.effect >> nth=1 >> button:has-text("Add the line")');
ok((await text()).endsWith('  rly1 = off\n  audio_single = 1\n'), 'Add the line: "play sound 1 once" written as audio_single at the end of the effect');
ok(/not saved|/.test(await page.textContent('#script-state')), 'the Script tab knows it changed');
// typing in the text box redraws the blocks
await page.fill('#script-text', 'effect one:\n  wait for user1\n  out2 = 40%\n  sleep 1\n  out2 = off\n');
await page.waitForFunction(() => document.querySelectorAll('#script-blocks .blk').length === 5, null, { timeout: 2000 });
ok((await page.inputValue('#script-blocks .blk.effect input')) === 'one', 'typing in the text box redraws the blocks');
await page.click('#script-save');
await page.waitForFunction(() => /^saved/.test(document.querySelector('#script-msg').textContent), null, { timeout: 3000 });
ok((await rpc({ op: 'get_script' })).result.text === await text(), 'Save stores the text the blocks show');

// Blocks only, remembered after a reload
await page.click('label:has(input[value="blocks"])');
ok(await page.isHidden('#script-text') && await page.isVisible('#script-blocks'), 'Blocks only: no text box');
await page.screenshot({ path: `${S}/views-blocks-390.png`, fullPage: true });
await open();
ok((await page.$eval('input[name=script-view]:checked', (r) => r.value)) === 'blocks', 'the view is remembered on this phone');

// a recorded effect opens in the blocks
await page.click('#tabs button[data-tab="record"]');
await page.$$eval('#rx-outs input, #rx-sounds input', (xs) => xs.forEach((x) => { if (x.checked !== (x.value === 'rly2')) x.click(); }));
await page.uncheck('#rx-count');
await page.click('#rx-start');
await page.waitForFunction(() => /^Recording/.test(document.querySelector('#rx-status').textContent), null, { timeout: 3000 });
const hb = await (await page.$('.rxout[data-target="rly2"] .hold')).boundingBox();
await page.mouse.move(hb.x + 20, hb.y + 20); await page.mouse.down(); await page.waitForTimeout(400); await page.mouse.up();
await page.click('#rx-stop');
await page.waitForSelector('#rx-review:not(.hidden)', { timeout: 6000 });
await page.fill('#rx-name', 'recorded one');
await page.click('#rx-keep');
await page.waitForFunction(() => /^Saved/.test(document.querySelector('#rx-msg').textContent), null, { timeout: 5000 });
await page.click('#rx-msg button:has-text("Change it on the Script tab")');
await page.waitForSelector('#script-blocks .blk.effect.flash', { timeout: 3000 });
ok((await page.inputValue('#script-blocks .blk.effect.flash input')) === 'recorded one', 'Keep → "Change it on the Script tab": the recorded effect opens in the blocks');
await page.screenshot({ path: `${S}/views-recorded-in-blocks.png`, fullPage: false });

// a desktop: side by side, drag a block by its grip
await page.setViewportSize({ width: 1100, height: 900 });
await page.click('label:has(input[value="both"])');
const t2 = await (await page.$('#script-text')).boundingBox(), b2 = await (await page.$('#script-blocks')).boundingBox();
ok(b2.x > t2.x + t2.width - 1, 'Script and blocks on a wide screen: side by side');
await page.fill('#script-text', 'effect d:\n  wait for trig1\n  out1 = on\n  sleep 1\n  out1 = off\n');
await page.waitForFunction(() => document.querySelectorAll('#script-blocks .blk').length === 5, null, { timeout: 2000 });
await page.dragAndDrop('#script-blocks .blk[data-type="ps_sleep"] .grip', '#script-blocks .blk[data-type="ps_wait"]');
ok(await text() === 'effect d:\n  sleep 1\n  wait for trig1\n  out1 = on\n  out1 = off\n', 'drag by ⠿: the sleep moved above the wait: ' + JSON.stringify(await text()));
await page.screenshot({ path: `${S}/views-both-wide.png`, fullPage: false });

ok(errors.length === 0, 'no page errors' + (errors.length ? ': ' + errors.join(' | ') : ''));
await browser.close();
