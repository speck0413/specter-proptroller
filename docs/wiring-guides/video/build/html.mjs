// Build the offline HTML guides into ../html/: one bundle (three.js, the scene, the step data, the models, the board
// artwork and the narration lengths) as a classic script, so the pages open from disk and inside the desktop app.
import { build } from 'esbuild';
import { readFileSync, writeFileSync, mkdirSync, readdirSync, statSync } from 'node:fs';
import { GUIDES } from '../src/guides/all.js';

const OUT = '../html';
mkdirSync(OUT, { recursive: true });
await build({
  entryPoints: ['src/player/main.js'], bundle: true, minify: true, format: 'iife', globalName: 'WG',
  outfile: `${OUT}/wiring-guides.js`, target: ['es2020'], legalComments: 'eof', tsconfigRaw: '{}',
  loader: { '.glb': 'binary', '.webp': 'dataurl', '.json': 'json' },
});
const css = readFileSync('src/player/style.css', 'utf8');
writeFileSync(`${OUT}/style.css`, css);
const page = (title, body) => `<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>${title}</title><link rel="stylesheet" href="style.css"></head>
<body>${body}<script src="wiring-guides.js"></script></body></html>
`;
writeFileSync(`${OUT}/index.html`, page('Proptroller wiring guides', `
<main class="wg-index"><h1>Proptroller wiring guides</h1>
<p class="wg-lead">Short, safe how-tos for the most common hookups. Each guide is a 3D scene you can turn around, with big
plain steps and a voice that reads them out. New here? Start with <b>Meet your board</b>: it shows how to open the board's
page on your phone. Everything here is low voltage: these guides never show mains (wall power) wiring.</p>
<div id="list"></div>
<p class="wg-foot">Every guide also has a one-page written version with a parts list and a "Not working?" list.</p></main>
<script>addEventListener('DOMContentLoaded',function(){WG.index(document.getElementById('list'))})</script>`).replace('<script src="wiring-guides.js"></script></body>', '</body>').replace('<body>', '<body><script src="wiring-guides.js"></script>'));
for (const g of GUIDES) {
  writeFileSync(`${OUT}/${g.id}-${g.slug}.html`, page(`${g.title} - Proptroller wiring guide`,
    `<div id="wg" class="wg-guide"></div>`).replace('</body>', `<script>WG.guide(${JSON.stringify(g.id)},{})</script></body>`));
}
let total = 0;
const walk = (d) => readdirSync(d).forEach((f) => { const p = `${d}/${f}`; const s = statSync(p); s.isDirectory() ? walk(p) : (total += s.size); });
walk(OUT);
console.log(`html/: ${(total / 1024).toFixed(0)} KB in all (bundle ${(statSync(`${OUT}/wiring-guides.js`).size / 1024).toFixed(0)} KB)`);
