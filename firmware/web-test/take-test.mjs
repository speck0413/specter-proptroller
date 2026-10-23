// take.js (the shared "Record effect" take → effect script module): rounding, joined holds, maximum on-times,
// sounds, sliders and fades, splits, limits, and putting the result into a stored script.
// Run: node --test firmware/web-test/take-test.mjs   (no packages needed)
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { plan, seconds, suggestName, takeToScript, mergeScript, effectsIn, outputsByEffect, replacedBy, mergeTakes, LIMITS } from '../web/take.js';

const opts = { name: 'siren and lamp', trigger: 'trig1' };
const bodyOf = (text) => text.split('\n').filter((l) => l && !l.startsWith('#'));
const linesOf = (r) => bodyOf(r.text).slice(2);
const T = (holds = [], sounds = [], levels = [], end) => ({ holds, sounds, levels, end });

test('writes seconds the way people do', () => {
  assert.equal(seconds(2000), '2');
  assert.equal(seconds(500), '0.5');
  assert.equal(seconds(12340), '12.3');
});

test('rounds to 0.1 s on absolute times, cuts the quiet start and writes the sleeps between', () => {
  const r = takeToScript(T([{ target: 'rly1', start: 1234, end: 3480 }, { target: 'out1', start: 2010, end: 2560 }]), opts);
  assert.equal(r.error, undefined);
  assert.deepEqual(bodyOf(r.text), [
    'effect siren and lamp:', '  wait for trig1',
    '  rly1 = on', '  sleep 0.8', '  out1 = on', '  sleep 0.6', '  out1 = off', '  sleep 0.9', '  rly1 = off',
  ]);
  assert.equal(r.lengthMs, 2300);
  const sum = linesOf(r).filter((l) => l.includes('sleep')).reduce((a, l) => a + Number(l.split(' ').pop()), 0);
  assert.equal(Math.round(sum * 10) / 10, 2.3);  // no drift
  assert.match(r.text, /^# Recorded \(Record effect\): when TRIG1 is pressed/);
  assert.match(takeToScript(T([{ target: 'out1', start: 0, end: 100 }]), { ...opts, source: 'in Specter Studio' }).text, /^# Recorded in Specter Studio \(Record effect\)/);
});

test('keeps the quiet start when asked, and never writes sleep 0', () => {
  const r = takeToScript(T([{ target: 'out2', start: 1500, end: 2000 }, { target: 'rly2', start: 1520, end: 2030 }]), { ...opts, trimLead: false });
  assert.deepEqual(linesOf(r), ['  sleep 1.5', '  out2 = on', '  rly2 = on', '  sleep 0.5', '  out2 = off', '  rly2 = off']);
  assert.doesNotMatch(r.text, /sleep 0\n/);
});

test('a very short tap still lasts one step (a relay never switches faster than 100 ms)', () => {
  assert.deepEqual(linesOf(takeToScript(T([{ target: 'cc1', start: 0, end: 20 }]), opts)), ['  cc1 = on', '  sleep 0.1', '  cc1 = off']);
  // two quick taps 20 ms apart: one press (never off and on again within 0.1 s)
  const r = takeToScript(T([{ target: 'rly1', start: 0, end: 120 }, { target: 'rly1', start: 140, end: 260 }]), opts);
  assert.deepEqual(linesOf(r), ['  rly1 = on', '  sleep 0.3', '  rly1 = off']);
});

test('joins holds of one output that overlap or touch (a second pass on top)', () => {
  const p = plan(T([{ target: 'out1', start: 0, end: 1000 }, { target: 'out1', start: 800, end: 1500 }, { target: 'out1', start: 1520, end: 1800 }, { target: 'out1', start: 3000, end: 3200 }]));
  assert.deepEqual(p.segments.map((s) => [s.start, s.end]), [[0, 1800], [3000, 3200]]);
});

test('outputs side by side: offs before ons at the same moment', () => {
  const r = takeToScript(T([{ target: 'out1', start: 0, end: 1000 }, { target: 'out2', start: 1000, end: 2000 }]), opts);
  assert.deepEqual(linesOf(r), ['  out1 = on', '  sleep 1', '  out1 = off', '  out2 = on', '  sleep 1', '  out2 = off']);
});

test('cuts a hold at the maximum on-time and says so plainly', () => {
  const r = takeToScript(T([{ target: 'rly1', start: 0, end: 7200 }, { target: 'out1', start: 0, end: 7200 }]), { ...opts, maxOnMs: { rly1: 5050, out1: 0 }, labels: { rly1: 'siren' } });
  assert.deepEqual(linesOf(r), ['  out1 = on', '  rly1 = on', '  sleep 5', '  rly1 = off', '  sleep 2.2', '  out1 = off']);
  assert.deepEqual(r.notes, ['siren (RLY1) was held for 7.2 s, but this board turns it off after 5.1 s (its maximum on-time), so it stays on for 5 s.']);
  assert.equal(r.segments.find((s) => s.target === 'rly1').cut, true);
  // the recorder already let go at the limit: the note keeps how long the finger stayed down
  const c = takeToScript(T([{ target: 'rly1', start: 0, end: 3000, cut: true, heldMs: 4500 }]), { ...opts, maxOnMs: { rly1: 3000 } });
  assert.deepEqual(linesOf(c), ['  rly1 = on', '  sleep 3', '  rly1 = off']);
  assert.equal(c.notes[0], 'RLY1 was held for 4.5 s, but this board turns it off after 3 s (its maximum on-time), so it stays on for 3 s.');
});

test('sound taps: audio_single by number, or by file name when the sound list has it', () => {
  const r = takeToScript(T([{ target: 'out1', start: 500, end: 1500 }], [{ sound: 1, at: 520 }, { sound: 3, at: 1000 }]), { ...opts, soundNames: { 1: 'scream.mp3' } });
  assert.deepEqual(linesOf(r), ['  out1 = on', '  audio_single = scream.mp3', '  sleep 0.5', '  audio_single = 3', '  sleep 0.5', '  out1 = off']);
  assert.deepEqual(linesOf(takeToScript(T([], [{ sound: 2, at: 300 }]), opts)), ['  audio_single = 2']);
});

test('a slider becomes levels in 5 % steps, at most one per 0.1 s, repeats merged, off at the end', () => {
  const levels = [
    { target: 'out1', at: 0, level: 41 },     // 40 %
    { target: 'out1', at: 30, level: 52 },    // same 0.1 s slot: the last wins (50 %)
    { target: 'out1', at: 1000, level: 51 },  // still 50 %: merged
    { target: 'out1', at: 2000, level: 100 }, // on
    { target: 'out1', at: 3000, level: 12 },  // 10 %
  ];
  const r = takeToScript(T([], [], levels, 4000), opts);
  assert.equal(r.error, undefined);
  assert.deepEqual(linesOf(r), ['  out1 = 50%', '  sleep 2', '  out1 = on', '  sleep 1', '  out1 = 10%', '  sleep 1', '  out1 = off']);
  // a hold on a dimmed output is full on while held, then back to the slider's level
  const h = takeToScript(T([{ target: 'out2', start: 1000, end: 1500 }], [], [{ target: 'out2', at: 0, level: 30 }, { target: 'out2', at: 2500, level: 0 }]), opts);
  assert.deepEqual(linesOf(h), ['  out2 = 30%', '  sleep 1', '  out2 = on', '  sleep 0.5', '  out2 = 30%', '  sleep 1', '  out2 = off']);
});

test('a smooth slider movement becomes one fade when nothing else happens meanwhile', () => {
  const levels = [{ target: 'out1', at: 0, level: 100 }];
  for (let k = 1; k <= 10; k++) levels.push({ target: 'out1', at: 1000 + k * 100, level: 100 - k * 10 });  // 90 … 0 over 1 s
  const r = takeToScript(T([], [], levels), opts);
  assert.deepEqual(linesOf(r), ['  out1 = on', '  sleep 1.1', '  out1 = 90%', '  fade out1 to 0% over 0.9']);
  // the playback follows the fade in 0.1 s steps
  const tl = r.timeline.filter((e) => e.target === 'out1');
  assert.deepEqual(tl.slice(0, 3), [{ at: 0, target: 'out1', level: 100 }, { at: 1100, target: 'out1', level: 90 }, { at: 1200, target: 'out1', level: 80 }]);
  assert.deepEqual(tl[tl.length - 1], { at: 2000, target: 'out1', level: 0 });
});

test('a fade takes its time: another output switching meanwhile keeps the slider as steps', () => {
  const levels = [];
  for (let k = 0; k <= 4; k++) levels.push({ target: 'out1', at: k * 100, level: 20 + k * 20 });  // 20 … 100 over 0.4 s
  levels.push({ target: 'out1', at: 1000, level: 0 });
  // rly1 goes on in the middle of the movement: a fade would hold it back
  const r = takeToScript(T([{ target: 'rly1', start: 200, end: 800 }], [], levels), opts);
  assert.ok(!r.text.includes('fade'), r.text);
  assert.ok(linesOf(r).includes('  rly1 = on'));
  // the same movement alone fades; the relay at exactly the end of it is fine
  const ok = takeToScript(T([{ target: 'rly1', start: 400, end: 800 }], [], levels), opts);
  assert.deepEqual(linesOf(ok), ['  out1 = 20%', '  fade out1 to 100% over 0.4', '  rly1 = on', '  sleep 0.4', '  rly1 = off', '  sleep 0.2', '  out1 = off']);
  // times still add up: the fade's 0.4 s counts as time passed
  assert.equal(ok.lengthMs, 1000);
});

test('relays and contacts are on or off only, even from a level', () => {
  const r = takeToScript(T([], [], [{ target: 'rly2', at: 0, level: 70 }, { target: 'rly2', at: 500, level: 10 }]), opts);
  assert.deepEqual(linesOf(r), ['  rly2 = on', '  sleep 0.5', '  rly2 = off']);
});

test('a slider respects the maximum on-time too', () => {
  const r = takeToScript(T([], [], [{ target: 'out3', at: 0, level: 60 }, { target: 'out3', at: 9000, level: 0 }]), { ...opts, maxOnMs: { out3: 4000 } });
  assert.deepEqual(linesOf(r), ['  out3 = 60%', '  sleep 4', '  out3 = off']);
  assert.match(r.notes[0], /^OUT3 was on for 9 s, but this board turns it off after 4 s/);
});

test('splits a long take into one effect per output (they start together), within the board limits', () => {
  const holds = [];
  for (let i = 0; i < 14; i++) holds.push({ target: 'out1', start: i * 1000, end: i * 1000 + 400 }, { target: 'rly1', start: i * 1000 + 500, end: i * 1000 + 800 });
  const r = takeToScript(T(holds, [{ sound: 1, at: 0 }]), { ...opts, labels: { rly1: 'siren' } });
  assert.equal(r.error, undefined);
  assert.equal(r.effects, 3);
  assert.deepEqual(r.text.split('\n').filter((l) => l.startsWith('effect')), ['effect siren and lamp - out1:', 'effect siren and lamp - siren:', 'effect siren and lamp - sound:']);
  for (const e of r.text.split(/\n(?=effect )/).slice(1)) {
    assert.equal(e.split('\n')[1], '  wait for trig1');
    assert.ok(e.split('\n').filter((l) => l.trim()).length <= LIMITS.effectLines);
  }
  assert.equal(r.text.match(/rly1 = on/g).length, 14);
});

test('a slider that moves too often is written in coarser steps, with a note', () => {
  const levels = [];
  // a jittery slider: up and down every 0.1 s for 8 s (no smooth runs to fade)
  for (let k = 0; k < 80; k++) levels.push({ target: 'out4', at: k * 100, level: k % 2 ? 30 : 75 + (k % 4) });
  levels.push({ target: 'out4', at: 8000, level: 0 });
  const r = takeToScript(T([], [], levels), opts);
  assert.equal(r.error, undefined, r.error);
  assert.ok(bodyOf(r.text).length <= LIMITS.effectLines);
  assert.match(r.notes.join(' '), /written in \d+ % steps/);
});

test('refuses a take that cannot fit, with a plain reason', () => {
  const holds = [];
  for (let i = 0; i < 40; i++) holds.push({ target: 'out3', start: i * 500, end: i * 500 + 200 });
  const r = takeToScript(T(holds), opts);
  assert.equal(r.text, '');
  assert.match(r.error, /too much for one board: 40 taps on OUT3/);
  assert.match(takeToScript(T(), opts).error, /Nothing was tapped/);
});

test('names: a colon or a long name cannot break the header; a suggestion from what was used', () => {
  const r = takeToScript(T([{ target: 'out1', start: 0, end: 100 }]), { ...opts, name: 'scare: the  big one at the front door of the house' });
  assert.ok(r.text.includes('effect scare the big one at the front d:'));
  assert.equal(suggestName(T([{ target: 'rly1', start: 0, end: 1 }, { target: 'out1', start: 0, end: 1 }]), { rly1: 'siren' }), 'siren and OUT1');
  assert.equal(suggestName(T([], [{ sound: 1, at: 0 }])), 'sound');
  assert.equal(suggestName(T([], [], [{ target: 'out2', at: 0, level: 50 }]), { out2: 'eyes' }), 'eyes');
});

test('add more on top: two passes become one take', () => {
  const m = mergeTakes(T([{ target: 'out1', start: 0, end: 500 }], [], [], 1000), T([{ target: 'out1', start: 400, end: 900 }], [{ sound: 2, at: 100 }], [], 1200));
  assert.equal(m.end, 1200);
  assert.deepEqual(plan(m).segments.map((s) => [s.start, s.end]), [[0, 900]]);
});

// ------------------------------------------------------------------ into the stored script

const STORED = `# my haunt
effect porch light:
  wait for trig2
  out2 = on
  sleep 30
  out2 = off

# the scare
effect scare:
  wait for trig1
  rly1 = on
  sleep 1
  rly1 = off
`;

test('effectsIn finds the effects, with the comment just above each', () => {
  const { effects, head } = effectsIn(STORED);
  assert.deepEqual(effects.map((e) => e.name), ['porch light', 'scare']);
  assert.equal(head, 0);  // "# my haunt" is right above the first effect: it goes with it
  assert.equal(effects[1].from, 7);  // "# the scare"
  assert.equal(effects[0].to, 7);  // the blank line stays with the effect above
});

test('mergeScript adds a new effect at the end and keeps the rest exactly', () => {
  const rec = takeToScript(T([{ target: 'out1', start: 0, end: 1000 }]), { ...opts, name: 'lamp' }).text;
  const m = mergeScript(STORED, rec, 'lamp');
  assert.deepEqual(m.replaced, []);
  assert.deepEqual(m.clashes, []);
  assert.ok(m.text.startsWith(STORED.trimEnd() + '\n\n# Recorded'));
  assert.deepEqual(effectsIn(m.text).effects.map((e) => e.name), ['porch light', 'scare', 'lamp']);
});

test('mergeScript replaces an effect of the same name in place (any case and spacing), and its split parts', () => {
  const rec = takeToScript(T([{ target: 'rly1', start: 0, end: 2000 }]), { ...opts, name: 'Scare' }).text;
  assert.deepEqual(replacedBy(STORED, 'SCARE'), ['scare']);
  const m = mergeScript(STORED, rec, 'Scare');
  assert.deepEqual(m.replaced, ['scare']);
  assert.deepEqual(m.clashes, []);
  assert.deepEqual(effectsIn(m.text).effects.map((e) => e.name), ['porch light', 'Scare']);
  assert.ok(m.text.includes('sleep 2'));
  assert.ok(m.text.startsWith('# my haunt\neffect porch light:'));
  // a split recording replaced by a short one: every part goes
  const split = 'effect big - out1:\n  wait for trig1\n  out1 = on\n  sleep 1\n  out1 = off\neffect big - sound:\n  wait for trig1\n  audio_single = 1\neffect other:\n  wait for trig2\n  out3 = on\n  sleep 1\n  out3 = off\n';
  const again = mergeScript(split, takeToScript(T([{ target: 'out1', start: 0, end: 500 }]), { ...opts, name: 'big' }).text, 'big');
  assert.deepEqual(again.replaced, ['big - out1', 'big - sound']);
  assert.deepEqual(effectsIn(again.text).effects.map((e) => e.name), ['big', 'other']);
});

test('mergeScript reports an output another effect already sets (each output belongs to one effect)', () => {
  const rec = takeToScript(T([{ target: 'out2', start: 0, end: 500 }]), { ...opts, name: 'new one' }).text;
  assert.deepEqual(mergeScript(STORED, rec, 'new one').clashes, [{ output: 'out2', effect: 'porch light' }]);
  // a terminal's name counts as the terminal (SS-N)
  const named = 'effect hall:\n  wait for trig2\n  lamp = on\n  sleep 1\n  fade lamp to 0% over 2\n';
  assert.deepEqual([...outputsByEffect(named, { lamp: 'out1' }).hall], ['out1']);
  const rec1 = takeToScript(T([{ target: 'out1', start: 0, end: 500 }]), { ...opts, name: 'x' }).text;
  assert.deepEqual(mergeScript(named, rec1, 'x', { lamp: 'out1' }).clashes, [{ output: 'out1', effect: 'hall' }]);
});

test('mergeScript into an empty board is just the recording', () => {
  const rec = takeToScript(T([{ target: 'out1', start: 0, end: 500 }]), opts).text;
  assert.equal(mergeScript('', rec, opts.name).text, rec);
});
