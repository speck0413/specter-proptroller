// blocks.js (the effect-script blocks without Blockly): text → blocks → text gives the identical text for every
// golden case and example; a changed block rewrites only its line; blank lines and comments are kept.
// Run: node --test firmware/web-test/blocks-test.mjs   (no packages needed)
import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { blocksToText, newBlock, parse, textToBlocks, fieldsOf, KINDS } from '../web/blocks.js';
import { takeToScript } from '../web/take.js';

const FW = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const scripts = [];
const g = path.join(FW, 'show-script-golden/cases');
for (const d of fs.readdirSync(g).sort()) scripts.push([d, fs.readFileSync(path.join(g, d, 'script.txt'), 'utf8')]);
const ex = path.join(FW, 'show-script-examples');
for (const f of fs.readdirSync(ex).filter((f) => f.endsWith('.txt')).sort()) scripts.push([f, fs.readFileSync(path.join(ex, f), 'utf8')]);
scripts.push(['mixed line endings', 'effect a:\r\n  wait for trig1\n  out1 = on\r  sleep 1\n']);
scripts.push(['no final newline', 'effect a:\n  wait for trig1\n  out1 = on']);
scripts.push(['empty', '']);
scripts.push(['a recorded effect', takeToScript({ holds: [{ target: 'rly1', start: 0, end: 500 }], sounds: [{ sound: 2, at: 100 }], levels: [{ target: 'out1', at: 0, level: 40 }, { target: 'out1', at: 900, level: 0 }] }, { name: 'rec', trigger: 'trig1' }).text]);
const all = (m) => m.blocks.flatMap((b) => [b, ...(b.lines || [])]);

test('reads every golden case and example', () => assert.ok(scripts.length > 80, String(scripts.length)));

test('round trip: text → blocks → text is the identical text, for every script', () => {
  for (const [name, text] of scripts) assert.equal(blocksToText(textToBlocks(text)), text, name);
});

test('a changed block rewrites only its line (indentation and comment kept)', () => {
  const m = textToBlocks('effect fog:   # the porch\n  wait for trig2\n  rly1 = on    # the remote\n  sleep 3.\n  rly1 = off\n');
  all(m).find((b) => b.type === 'ps_sleep').f.TIME = '5';
  all(m).find((b) => b.type === 'ps_set').f.LEVEL = '50%';
  assert.equal(blocksToText(m), 'effect fog:   # the porch\n  wait for trig2\n  rly1 = 50%    # the remote\n  sleep 5\n  rly1 = off\n');
});

test('sorts lines into kinds', () => {
  assert.equal(parse('  fade out1 to 0% over 3').type, 'ps_fade');
  assert.equal(parse('OUT1 =ON').type, 'ps_set');
  assert.equal(parse('dmx12 = 40%').type, 'ps_set');
  assert.equal(parse('waitfor trig1').type, 'ps_wait');
  assert.equal(parse('hello there').type, 'ps_raw');
  assert.equal(parse('audio_stop').type, 'ps_audio_stop');
  assert.deepEqual(parse('  flicker lamp for 5 # spooky').f, { TARGET: 'lamp', TIME: '5' });
});

test('blank lines get no block and stay where they were, also after a change', () => {
  const text = '\n\neffect a:\n  wait for trig1\n\n  out1 = on\n  \n\n  # note\n  sleep 1\n\n';
  const m = textToBlocks(text);
  assert.deepEqual(all(m).map((b) => b.type), ['ps_effect', 'ps_wait', 'ps_set', 'ps_comment', 'ps_sleep']);
  assert.equal(blocksToText(m), text);
  all(m).find((b) => b.type === 'ps_set').f.LEVEL = '50%';
  assert.equal(blocksToText(m), text.replace('out1 = on', 'out1 = 50%'));
  assert.equal(blocksToText(textToBlocks('\n \n')), '\n \n');  // only blank lines
});

test('new, moved and removed blocks', () => {
  const m = textToBlocks('effect a:\n  wait for trig1\n  out1 = on\n  sleep 1\n  out1 = off\n');
  const e = m.blocks[0];
  e.lines.splice(3, 0, newBlock('ps_fade', { TARGET: 'out2' }));  // added: the plain form, indented like a line
  [e.lines[1], e.lines[2]] = [e.lines[2], e.lines[1]];              // moved: its own text goes with it
  e.lines.pop();                                                     // removed
  assert.equal(blocksToText(m), 'effect a:\n  wait for trig1\n  sleep 1\n  out1 = on\n  fade out2 to 0% over 2\n');
  m.blocks.push(newBlock('ps_effect', { NAME: 'second' }));
  m.blocks[1].lines.push(newBlock('ps_wait'), newBlock('ps_audio_stop'));
  assert.equal(blocksToText(m), 'effect a:\n  wait for trig1\n  sleep 1\n  out1 = on\n  fade out2 to 0% over 2\neffect second:\n  wait for trig1\n  audio_stop\n');
  // a new block keeps the script's own line endings
  const w = textToBlocks('effect a:\r\n  wait for trig1\r\n');
  w.blocks[0].lines.push(newBlock('ps_sleep'));
  assert.equal(blocksToText(w), 'effect a:\r\n  wait for trig1\r\n  sleep 1\r\n');
});

test('the words of every kind are the words Studio shows', () => {
  const words = (t, f) => KINDS[t].words.map((w) => (w in KINDS[t].fields ? f[w] : w)).join(' ');
  assert.equal(words('ps_audio_single', { SOUND: 'chime.mp3' }), 'play sound chime.mp3 once');
  assert.equal(words('ps_audio_loop', { SOUND: 'rain.mp3' }), 'play sound rain.mp3 over and over');
  assert.equal(words('ps_audio_stop', {}), 'stop the sound');
  assert.deepEqual(fieldsOf('ps_fade'), ['TARGET', 'LEVEL', 'TIME']);
});
