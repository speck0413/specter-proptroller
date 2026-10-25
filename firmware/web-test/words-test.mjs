// words.js (the language's words and the script box's colours): the colours keep every character of every golden
// case and example, and each kind of word gets its class. The lists themselves are checked against the firmware's
// checker by test_script (words_js_matches_the_checker).
// Run: node --test firmware/web-test/words-test.mjs   (no packages needed)
import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { highlight, classOf } from '../web/words.js';

const FW = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const scripts = [];
const g = path.join(FW, 'show-script-golden/cases');
for (const d of fs.readdirSync(g).sort()) scripts.push([d, fs.readFileSync(path.join(g, d, 'script.txt'), 'utf8')]);
const ex = path.join(FW, 'show-script-examples');
for (const f of fs.readdirSync(ex).filter((f) => f.endsWith('.txt')).sort()) scripts.push([f, fs.readFileSync(path.join(ex, f), 'utf8')]);
const plain = (html) => html.replace(/<[^>]*>/g, '').replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&amp;/g, '&');

test('the colours keep the text: the tags stripped give every script back', () => {
  assert.ok(scripts.length > 100, String(scripts.length));
  for (const [name, text] of scripts) assert.equal(plain(highlight(text.replace(/\r\n?/g, '\n'))), text.replace(/\r\n?/g, '\n'), name);
  for (const t of ['a < b & c > d', '', '\n\n', '#only a comment', 'effect x: # <b>', '   ']) assert.equal(plain(highlight(t)), t);
});

test('each kind of word gets its class', () => {
  const cls = (line, word) => {
    const m = highlight(line).match(new RegExp(`<span class="t-([a-z]+)">${word}</span>`));
    return m ? m[1] : '';
  };
  assert.equal(cls('effect porch light:', 'effect'), 'kw');
  assert.equal(cls('effect porch light:', ' porch light'), 'name');
  assert.equal(cls('  wait for trig1', 'wait'), 'kw');
  assert.equal(cls('  wait for trig1', 'trig1'), 'in');
  assert.equal(cls('  OUT1 = on', 'OUT1'), 'out');
  assert.equal(cls('  OUT1 = on', 'on'), 'lv');
  assert.equal(cls('  dmx12 = 50%', 'dmx12'), 'out');
  assert.equal(cls('  dmx12 = 50%', '50%'), 'num');
  assert.equal(cls('  wait 200 ms', 'ms'), 'num');
  assert.equal(cls('  wait out1_on_ms', 'out1_on_ms'), 'num');
  assert.equal(cls('  pixels = chase red', 'pixels'), 'kw');
  assert.equal(cls('  pixels = chase red', 'chase'), 'fx');
  assert.equal(cls('  pixels = chase red', 'red'), 'fx');
  assert.equal(cls('  pixels = Warm  White', 'Warm  White'), 'fx');
  assert.equal(cls('  pixels = comet 0x40ff80', '0x40ff80'), 'num');
  assert.equal(cls('  out1 = on   # the lamp', '# the lamp'), 'cm');
  assert.equal(classOf('lamp'), '');
  assert.match(highlight('a <b>'), /&lt;b&gt;/);
});

test('a long script colours quickly', () => {
  const text = scripts.map(([, t]) => t).join('\n').slice(0, 8192);
  const t0 = performance.now();
  for (let i = 0; i < 20; i++) highlight(text);
  assert.ok((performance.now() - t0) / 20 < 20, 'under 20 ms for 8 KB on the host');
});
