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
  assert.equal(blocksToText(m), 'effect fog:   # the porch\n  wait for trig2\n  rly1 = 50%    # the remote\n  wait 5\n  rly1 = off\n');
});

test('wait <time> and wait for <time> are a time block, wait for <input> an input block, sleep still a time block', () => {
  for (const [l, t, v] of [['wait 2', 'ps_sleep', '2'], ['WAIT 200 ms', 'ps_sleep', '200 ms'], ['wait for 2 s', 'ps_sleep', '2 s'],
    ['wait out1_on_ms', 'ps_sleep', 'out1_on_ms'], ['wait for cooldown_ms', 'ps_sleep', 'cooldown_ms'], ['sleep 1', 'ps_sleep', '1']]) {
    assert.equal(parse(l).type, t, l);
    assert.equal(parse(l).f.TIME, v, l);
  }
  assert.equal(parse('wait for trig1').type, 'ps_wait');
  assert.equal(parse('wait for door').f.INPUT, 'door');
  assert.equal(parse('wait trig1').type, 'ps_raw');
  assert.equal(parse('wait air_on_time').type, 'ps_sleep');  // a setting's alias: the board knows it
  assert.deepEqual(KINDS.ps_sleep.words, ['wait', 'TIME']);
});

test('sorts lines into kinds', () => {
  assert.equal(parse('  fade out1 to 0% over 3').type, 'ps_fade');
  assert.equal(parse('OUT1 =ON').type, 'ps_set');
  assert.equal(parse('dmx12 = 40%').type, 'ps_set');
  assert.equal(parse('waitfor trig1').type, 'ps_wait');
  assert.equal(parse('hello there').type, 'ps_raw');
  assert.equal(parse('audio_stop').type, 'ps_audio_stop');
  assert.deepEqual(parse('  flicker lamp for 5 # spooky').f, { TARGET: 'lamp', TIME: '5' });
  // the advanced lines (SHOW-SCRIPT.md §9)
  assert.deepEqual(parse('  pulse cc1 for 200 ms').f, { TARGET: 'cc1', TIME: '200 ms' });
  assert.equal(parse('pulse cc1 for 200 ms').type, 'ps_pulse');
  assert.deepEqual(parse('limit 3 runs per 3 min').f, { RUNS: '3', TIME: '3 min' });
  assert.deepEqual(parse('LIMIT runs_per_cooldown RUNS PER cooldown_ms').f, { RUNS: 'runs_per_cooldown', TIME: 'cooldown_ms' });
  assert.deepEqual(parse('audio_loop = off').f, { SOUND: 'off' });
});

test('the advanced lines as new blocks write their plain form', () => {
  const m = textToBlocks('effect a:\n  wait for trig1\n  out1 = on\n');
  const e = m.blocks[0];
  e.lines.splice(1, 0, newBlock('ps_limit', { RUNS: '1', TIME: '30' }), newBlock('ps_pulse'));
  assert.equal(blocksToText(m), 'effect a:\n  wait for trig1\n  limit 1 run per 30\n  pulse cc1 for 200 ms\n  out1 = on\n');
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
  assert.equal(blocksToText(w), 'effect a:\r\n  wait for trig1\r\n  wait 1\r\n');
});

test('the words of every kind are the words Studio shows', () => {
  const words = (t, f) => KINDS[t].words.map((w) => (w in KINDS[t].fields ? f[w] : w)).join(' ');
  assert.equal(words('ps_audio_single', { SOUND: '3' }), 'play sound 3 once');
  assert.equal(words('ps_audio_loop', { SOUND: '2' }), 'play sound 2 over and over');
  assert.equal(words('ps_audio_volume', { VOLUME: '60%' }), 'sound volume 60%');
  assert.equal(words('ps_audio_stop', {}), 'stop the sound');
  assert.deepEqual(fieldsOf('ps_fade'), ['TARGET', 'LEVEL', 'TIME']);
});

test('sound blocks default to numbers, and no block names a file except the card layout NN/001-….mp3', () => {
  assert.match(KINDS.ps_audio_single.fields.SOUND, /^\d+$/);
  assert.match(KINDS.ps_audio_loop.fields.SOUND, /^\d+$/);
  assert.deepEqual(KINDS.ps_audio_volume.slider.VOLUME, [0, 100, '%']);
  assert.equal(parse('audio_volume = 40%').type, 'ps_audio_volume');
  assert.equal(blocksToText({ eol: '\n', blocks: [newBlock('ps_audio_volume')] }), 'audio_volume = 60%\n');
  for (const [t, k] of Object.entries(KINDS)) {
    const all = [k.tip, ...k.words, ...Object.values(k.fields)].join(' ');
    for (const m of all.match(/\S*\.mp3/g) || []) assert.match(m, /^\(?([0-9N]{2}\/)?001-…\.mp3\)?\.?$/, `${t}: ${m}`);
  }
});
