// The Script tab's colours (words.js through a layer behind the text box) at 390 px: the layer always shows the
// box's text (the stored script, typing, Tab, a block edit, Revert), lines up with it (font, padding, scroll, go to
// line), is hidden from screen readers, reads at 4.5:1 in the dark and light themes, and steps aside under forced
// colours; the box keeps its label and stays the control.
//   node highlight-test.mjs <screenshot dir> <url>      (CHROMIUM=/path/to/chrome)
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
const open = async () => { await page.goto(URL); await page.waitForSelector('#conn.ok', { timeout: 5000 }); await page.click('#tabs button[data-tab="script"]'); };
// the layer shows the box's text (one more line end at the end, for the scroll range)
const same = () => page.waitForFunction(() => document.querySelector('#script-hl').textContent === document.querySelector('#script-text').value + '\n', null, { timeout: 2000 }).then(() => true, () => false);
const contrast = () => page.evaluate(() => {
  const lum = (c) => {
    const [r, g, b] = c.match(/\d+(\.\d+)?/g).slice(0, 3).map((v) => +v / 255).map((v) => (v <= 0.03928 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4));
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
  };
  const bg = lum(getComputedStyle(document.querySelector('#script-hl')).backgroundColor);
  const worst = {};
  for (const s of document.querySelectorAll('#script-hl span')) {
    const l = lum(getComputedStyle(s).color);
    const k = s.className;
    const r = (Math.max(l, bg) + 0.05) / (Math.min(l, bg) + 0.05);
    worst[k] = Math.min(worst[k] ?? 99, r);
  }
  return worst;
});

const SCRIPT = '# the yard\neffect graveyard:   # by the gate\n  pixels = breathe purple\n  wait for trig1\n  pixels = comet 0x40ff80\n  out1 = 50%\n  wait 200 ms\n  fade out1 to 0% over 2\n  audio_single = 1\n  pixels = off\n';
await page.goto(URL);
await rpc({ op: 'set_script', text: SCRIPT, sounds: {} });
await open();
ok(await same(), 'the stored script: the layer shows the text');
ok((await page.getAttribute('#script-hl', 'aria-hidden')) === 'true', 'the layer is hidden from screen readers');
ok((await page.getAttribute('#script-text', 'aria-label')) === 'Effect script' && (await page.getAttribute('#script-text', 'aria-describedby')) === 'script-keys', 'the text box keeps its label and hint');
const cls = await page.$$eval('#script-hl span', (s) => s.map((x) => `${x.className}:${x.textContent}`));
for (const want of ['t-kw:effect', 't-name: graveyard', 't-kw:pixels', 't-fx:breathe', 't-fx:purple', 't-in:trig1', 't-num:0x40ff80', 't-out:out1', 't-num:50%', 't-num:ms', 't-cm:# the yard', 't-lv:off'])
  ok(cls.includes(want), `coloured: ${want}`);
const geo = await page.evaluate(() => {
  const a = getComputedStyle(document.querySelector('#script-text')), b = getComputedStyle(document.querySelector('#script-hl'));
  return ['fontFamily', 'fontSize', 'lineHeight', 'paddingTop', 'paddingLeft', 'borderTopWidth', 'borderLeftWidth', 'tabSize', 'whiteSpace', 'letterSpacing']
    .filter((k) => a[k] !== b[k]).map((k) => `${k} ${a[k]} / ${b[k]}`);
});
ok(geo.length === 0, 'the layer has the box\'s font, line height, padding and border' + (geo.length ? ': ' + geo.join(', ') : ''));
ok(await page.$eval('#script-text', (t) => getComputedStyle(t).color === 'rgba(0, 0, 0, 0)'), 'the box\'s own text is transparent over the layer');
let c = await contrast();
ok(Object.values(c).every((r) => r >= 4.5), 'dark theme: every colour 4.5:1 on the box: ' + JSON.stringify(c, (k, v) => (typeof v === 'number' ? +v.toFixed(1) : v)));
await page.screenshot({ path: `${S}/highlight-dark.png`, fullPage: false });

// typing, Tab, Revert
await page.click('#script-text');
await page.keyboard.press('Control+End');
await page.keyboard.type('wait 1');
ok(await same(), 'typing: the layer follows');
await page.keyboard.press('Home');
await page.keyboard.press('Tab');
ok((await page.inputValue('#script-text')).endsWith('\n  wait 1') && await same(), 'Tab indents and the layer follows');
ok(await page.evaluate(() => document.activeElement.id) === 'script-text', 'Tab stays in the box');
await page.keyboard.press('Escape');
await page.keyboard.press('Tab');
ok(await page.evaluate(() => document.activeElement.id) !== 'script-text', 'Esc, then Tab leaves the box');
await page.click('#script-revert');
await page.click('.ask .ask-yes');
await page.waitForFunction((t) => document.querySelector('#script-text').value === t, SCRIPT, { timeout: 3000 }).catch(() => {});
ok((await page.inputValue('#script-text')) === SCRIPT && await same(), 'Revert: the stored script, and the layer with it');

// a block edit
await page.click('label:has(input[value="both"])');
await page.waitForSelector('#script-blocks .blk[data-type="ps_pixels"]');
await page.fill('#script-blocks .blk[data-type="ps_pixels"] >> nth=1 >> input', 'fire');
ok((await page.inputValue('#script-text')).includes('pixels = fire') && await same(), 'a block edit: the text and the layer');

// go to line: a long script scrolled to its line 60, the layer scrolled with it
const long = 'effect a:\n  wait for trig1\n' + Array.from({ length: 70 }, (_, i) => `  out1 = ${i % 2 ? 'on' : 'off'}${i === 57 ? ' and more' : ''}`).join('\n') + '\n';
await page.click('label:has(input[value="text"])');
await page.fill('#script-text', long);
await page.click('#script-check');
await page.waitForSelector('#script-msgs button[data-line="60"]', { timeout: 3000 });
await page.click('#script-msgs button[data-line="60"]');
await page.waitForTimeout(100);
const sc = await page.evaluate(() => [document.querySelector('#script-text').scrollTop, document.querySelector('#script-hl').scrollTop]);
ok(sc[0] > 0 && Math.abs(sc[0] - sc[1]) < 1, `go to line 60: the box and the layer scrolled together (${sc.join(' / ')})`);
ok(await page.$eval('#script-text', (t) => t.value.slice(t.selectionStart, t.selectionEnd)) === '  out1 = on and more', 'go to line selects the line, as before');
await page.screenshot({ path: `${S}/highlight-goto.png`, fullPage: false });

// the light theme, chosen on the page
await page.evaluate(() => document.documentElement.setAttribute('data-theme', 'light'));
await page.fill('#script-text', SCRIPT);
await same();
c = await contrast();
ok(Object.values(c).every((r) => r >= 4.5), 'light theme: every colour 4.5:1 on the box: ' + JSON.stringify(c, (k, v) => (typeof v === 'number' ? +v.toFixed(1) : v)));
await page.screenshot({ path: `${S}/highlight-light.png`, fullPage: false });
await page.evaluate(() => document.documentElement.removeAttribute('data-theme'));
// the phone's own light setting (no theme chosen on the page): its own copy of the colours
await page.emulateMedia({ colorScheme: 'light' });
c = await contrast();
ok(Object.values(c).every((r) => r >= 4.5), 'system light theme: every colour 4.5:1 on the box: ' + JSON.stringify(c, (k, v) => (typeof v === 'number' ? +v.toFixed(1) : v)));
await page.emulateMedia({ colorScheme: 'dark' });

// forced colours: the box shows its own text, the layer is off
await page.emulateMedia({ forcedColors: 'active' });
ok(await page.$eval('#script-hl', (p) => getComputedStyle(p).display === 'none'), 'forced colours: no layer');
ok(await page.$eval('#script-text', (t) => getComputedStyle(t).color !== 'rgba(0, 0, 0, 0)'), 'forced colours: the box\'s text shows');
await page.emulateMedia({ forcedColors: 'none' });

ok(errors.length === 0, 'no page errors' + (errors.length ? ': ' + errors.join(' | ') : ''));
await browser.close();
