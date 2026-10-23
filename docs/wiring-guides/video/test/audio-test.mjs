// Narration format test: the guide pages pick Opus where the browser can play it and the AAC .m4a fallback where it
// cannot (Safari, older WebKit). Opens a guide page from disk, presses Listen, and checks which file was chosen and that
// it really plays (no media error, currentTime moves). `npm test`; browsers come from playwright-core
// (`npx playwright-core install chromium webkit`); CHROMIUM= / WEBKIT= point at other executables, SKIP_WEBKIT=1 skips it.
//
// Runs:
//   chromium            Opus chosen and plays
//   chromium, no Opus   canPlayType('audio/ogg; codecs=opus') forced to '': the .m4a is chosen and plays
//   chromium, bad Opus  Opus claimed but every .opus fails to decode (served as junk): the page falls back to the .m4a
//   webkit (all three)  the same in WebKit (WPE, the engine of Safari and of Tauri on Linux), when it is installed
import { chromium, webkit } from 'playwright-core';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { existsSync } from 'node:fs';

const HTML = fileURLToPath(new URL('../../html/', import.meta.url));
const PAGE = pathToFileURL(`${HTML}01-button-on-trig1.html`).href;

// Before any page script: remember the narration element when it plays, and optionally hide Opus support.
const hook = (noOpus) => {
  const cpt = HTMLMediaElement.prototype.canPlayType;
  if (noOpus) HTMLMediaElement.prototype.canPlayType = function (t) { return /opus/i.test(t) ? '' : cpt.call(this, t); };
  const play = HTMLMediaElement.prototype.play;
  HTMLMediaElement.prototype.play = function () { window.__wgAudio = this; return play.call(this); };
};

async function run(name, type, launch, { noOpus = false, badOpus = false, expect } = {}) {
  const browser = await type.launch(launch);
  try {
    const page = await browser.newPage();
    const errors = [];
    page.on('pageerror', (e) => errors.push(String(e)));
    await page.addInitScript(hook, noOpus);
    if (badOpus) {   // routing needs http, so serve the folder from a fake origin
      await page.route('http://guides.test/**', (route) => {
        const f = decodeURIComponent(new URL(route.request().url()).pathname).slice(1);
        if (f.endsWith('.opus')) return route.fulfill({ status: 200, contentType: 'audio/ogg', body: 'not an opus file' });
        return existsSync(HTML + f) ? route.fulfill({ path: HTML + f }) : route.fulfill({ status: 404 });
      });
      await page.goto('http://guides.test/01-button-on-trig1.html');
    } else await page.goto(PAGE);
    await page.waitForSelector('.wg-title:not(:empty)', { timeout: 30000 });
    await page.click('.wg-play');
    const r = await page.waitForFunction(() => {
      const a = window.__wgAudio;
      if (!a) return false;
      if (a.error) return { src: a.src, error: a.error.code };
      return a.currentTime > 0.3 && { src: a.src, t: a.currentTime, opus: a.canPlayType('audio/ogg; codecs=opus') };
    }, null, { timeout: 15000 }).then((h) => h.jsonValue(), async () => page.evaluate(() => {
      const a = window.__wgAudio;
      return a ? { src: a.src, error: a.error?.code ?? 'no progress', t: a.currentTime, state: a.readyState } : { error: 'never played' };
    }));
    const file = (r.src || '').split('/').pop();
    const ext = file.split('.').pop();
    const want = expect ?? (r.opus ? 'opus' : 'm4a');
    const ok = !r.error && ext === want && !errors.length;
    console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${file || '-'}${r.error ? ` error ${r.error}` : ` playing at ${r.t?.toFixed(2)} s`}${errors.length ? ` page errors: ${errors.join('; ')}` : ''}`);
    return ok;
  } finally { await browser.close(); }
}

const cr = { args: ['--autoplay-policy=no-user-gesture-required', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'] };
if (process.env.CHROMIUM) cr.executablePath = process.env.CHROMIUM;
const results = [
  await run('chromium', chromium, cr, { expect: 'opus' }),
  await run('chromium, no Opus', chromium, cr, { noOpus: true, expect: 'm4a' }),
  await run('chromium, bad Opus', chromium, cr, { badOpus: true, expect: 'm4a' }),
];
const wk = process.env.WEBKIT ? { executablePath: process.env.WEBKIT } : {};
if (process.env.SKIP_WEBKIT) console.log('skip webkit: SKIP_WEBKIT is set');
else if (!wk.executablePath && !existsSync(webkit.executablePath())) console.log('skip webkit: not installed (npx playwright-core install webkit)');
else {
  results.push(await run('webkit', webkit, wk));
  results.push(await run('webkit, no Opus', webkit, wk, { noOpus: true, expect: 'm4a' }));
  results.push(await run('webkit, bad Opus', webkit, wk, { badOpus: true, expect: 'm4a' }));
}
if (results.includes(false)) { console.log('audio test FAILED'); process.exit(1); }
console.log('audio test passed');
