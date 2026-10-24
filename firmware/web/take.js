// "Record effect": what a person did on the live controls (a take) turned into an ordinary effect script, the
// beginner language of firmware/SHOW-SCRIPT.md. One shared, framework-free ES module: the board's phone page and
// Specter Studio both import it, so a take becomes the same script in either. Pure functions only (no DOM, no
// network); tested by firmware/web-test/take-test.mjs (node --test).
//
// Rules (firmware/docs/RECORD-EFFECT.md):
// - times are rounded to a 0.1 s grid as absolute times, then written as the waits between them, so rounding
//   never adds up over a long take; every hold lasts at least one step;
// - two holds of one output that touch or overlap (a second pass on top of the first) become one;
// - at the same moment, offs go before ons, then levels, then sounds, then a fade;
// - a slider (dimmable outputs out1-out4) becomes `outN = NN%` lines in 5 % steps, at most one per 0.1 s, with
//   repeats merged; a smooth movement becomes `fade outN to NN% over S` when nothing else in that effect happens
//   while it fades (a fade takes its time, like wait). Relays and contacts are on/off only; on the 0.1 s grid
//   they never switch faster than their 100 ms minimum;
// - a hold longer than the board's maximum on-time for that output (`<output>_max_on_ms`, 0 = no limit) is cut
//   to the longest whole step within it, and a note says so in plain words;
// - the quiet bit before the first action is cut (optional), and every output is off at the end;
// - one effect when it fits (64 lines); otherwise one effect per output and one for the sounds, all waiting for
//   the same input, so they run together. A slider with too many changes is written in coarser steps (and a note
//   says so). Over the board's limits: a plain error, nothing is written.
//
// Shapes:
//   Take   { holds: Hold[], sounds: SoundTap[], levels?: LevelChange[], end?: ms }
//   Hold   { target: 'out1', start: ms, end: ms, cut?: bool, heldMs?: ms }   tap / hold = on while held
//   LevelChange { target: 'out1', at: ms, level: 0-100 }                    a slider moved (percent)
//   SoundTap    { sound: n, at: ms }                                         audio_single = n
// Times are ms from the start of the take; targets are lower-case terminal ids.

/** Board limits (show-script golden README). */
export const LIMITS = { effectLines: 64, effects: 8, scriptLines: 256, bytes: 8192, nameLen: 32 };
/** The time grid of the script (ms). */
export const STEP_MS = 100;
/** Slider levels are written in steps of this many percent. */
export const LEVEL_STEP = 5;
/** Steps of a slider no further apart than this (ms) may become one fade. */
export const FADE_GAP_MS = 300;
/** A fade covers at least this many slider steps. */
export const FADE_MIN_STEPS = 3;
/** Relays and contacts: the shortest on or off time (firmware dwell; the grid keeps it). */
export const RELAY_DWELL_MS = 100;
// coarser and coarser slider detail when an output has too many changes for one effect
const DETAIL = [{ level: 5, grid: 100 }, { level: 10, grid: 200 }, { level: 20, grid: 500 }, { level: 25, grid: 1000 }];

/** out1-out4 can dim; relays (rly1, rly2) and contacts (cc1, cc2) are on or off. */
export const dimmable = (target) => /^out[1-4]$/i.test(target);

export const named = (target, labels = {}) =>
  labels[target] ? `${labels[target]} (${target.toUpperCase()})` : target.toUpperCase();

/** Seconds as the script writes them: 2, 0.5, 12.3. */
export function seconds(ms) {
  const s = Math.round(ms / 100) / 10;
  return Number.isInteger(s) ? String(s) : s.toFixed(1);
}
const secondsWords = (ms) => `${seconds(ms)} s`;

/** A level as the script writes it: on, off, 40%. */
export const levelWord = (l) => (l >= 100 ? 'on' : l <= 0 ? 'off' : `${l}%`);

const snapTo = (t, step) => Math.max(0, Math.round(t / step) * step);
const quantLevel = (l, step) => Math.max(0, Math.min(100, Math.round(l / step) * step));

// the holds of one output on the grid, joined where they touch or overlap
function joinedHolds(holds, step) {
  const list = [];
  for (const h of holds) {
    if (!(h.end >= h.start)) continue;
    const start = snapTo(h.start, step);
    const end = Math.max(start + step, snapTo(h.end, step));
    list.push({ start, end, raw: Math.max(h.heldMs ?? 0, h.end - h.start), cut: !!h.cut });
  }
  list.sort((a, b) => a.start - b.start);
  const joined = [];
  for (const x of list) {
    const last = joined[joined.length - 1];
    if (last && x.start <= last.end) {
      last.end = Math.max(last.end, x.end);
      last.raw = Math.max(last.raw, x.raw, last.end - last.start);
      last.cut = last.cut || x.cut;
    } else joined.push({ ...x });
  }
  return joined;
}

// One output's level over the take: [{ t, level }] changes, starting from off. Holds are on (100 %) while held;
// between them the slider's level applies.
function outputSteps(target, holds, changes, takeEnd, detail, step) {
  const joined = joinedHolds(holds, step);
  if (!changes.length || !dimmable(target)) {
    // on / off only (a level change on a relay counts as on from 50 %)
    const segs = joined.slice();
    if (changes.length) {
      const extra = changes.map((c) => ({ t: snapTo(c.at, step), on: c.level >= 50 })).sort((a, b) => a.t - b.t);
      let open = null;
      for (const c of extra) {
        if (c.on && open == null) open = c.t;
        else if (!c.on && open != null) { segs.push({ start: open, end: Math.max(open + step, c.t), raw: c.t - open, cut: false }); open = null; }
      }
      if (open != null) { const end = Math.max(open + step, snapTo(takeEnd ?? open + step, step)); segs.push({ start: open, end, raw: end - open, cut: false }); }
    }
    const all = joinedHolds(segs.map((s) => ({ start: s.start, end: s.end, heldMs: s.raw, cut: s.cut })), step);
    const out = [];
    for (const s of all) out.push({ t: s.start, level: 100, raw: s.raw, cut: s.cut }, { t: s.end, level: 0 });
    return out;
  }
  // the slider: snapped to its grid (the last change in a slot wins), levels in steps
  const grid = Math.max(step, detail.grid);
  const slider = new Map();
  for (const c of [...changes].sort((a, b) => a.at - b.at)) slider.set(snapTo(c.at, grid), quantLevel(c.level, detail.level));
  const sliderTimes = [...slider.keys()].sort((a, b) => a - b);
  const sliderAt = (t) => { let l = 0; for (const k of sliderTimes) { if (k > t) break; l = slider.get(k); } return l; };
  const times = new Set(sliderTimes);
  for (const s of joined) { times.add(s.start); times.add(s.end); }
  const raws = new Map(joined.map((s) => [s.start, s]));
  const out = [];
  let last = 0;
  for (const t of [...times].sort((a, b) => a - b)) {
    const held = joined.some((s) => s.start <= t && t < s.end);
    const level = held ? 100 : sliderAt(t);
    if (level === last) continue;
    const st = { t, level };
    if (raws.has(t) && held) { st.raw = raws.get(t).raw; st.cut = raws.get(t).cut; }
    out.push(st);
    last = level;
  }
  if (last > 0) {
    const t = out[out.length - 1].t;
    out.push({ t: Math.max(t + step, snapTo(takeEnd ?? t + step, step)), level: 0 });
  }
  return out;
}

// cut every on-time longer than the board's maximum on-time; notes in plain words
function capOnTimes(target, steps, maxMs, step, labels, notes) {
  const allowed = maxMs > 0 ? Math.floor(maxMs / step) * step || maxMs : Infinity;
  const out = [];
  let i = 0;
  while (i < steps.length) {
    const s = steps[i];
    if (s.level <= 0) { out.push({ t: s.t, level: 0 }); i++; continue; }
    // a run of on (any level above 0) from s.t to the next off
    let j = i;
    while (j + 1 < steps.length && steps[j + 1].level > 0) j++;
    const offAt = j + 1 < steps.length ? steps[j + 1].t : steps[j].t + step;
    const len = offAt - s.t;
    const held = Math.max(len, s.raw ?? 0);
    let cut = !!s.cut;
    let until = offAt;
    if (len > allowed) { until = s.t + allowed; cut = true; }
    for (let k = i; k <= j; k++) if (steps[k].t < until) out.push({ t: steps[k].t, level: steps[k].level });
    const first = out.findIndex((x) => x.t === s.t && x.level === s.level);
    if (first >= 0 && cut) out[first].cut = true;
    out.push({ t: until, level: 0 });
    if (cut && maxMs > 0) notes.push(`${named(target, labels)} was ${s.raw != null ? 'held' : 'on'} for ${secondsWords(held)}, but this board turns it off after ${secondsWords(maxMs)} (its maximum on-time), so it stays on for ${secondsWords(until - s.t)}.`);
    i = j + 2;
  }
  return out;
}

/** The take on the step grid, joined, cut to the maximum on-times and moved to start at 0 (with trimLead).
 * `detail`: the slider's level step and grid (default 5 %, 0.1 s). */
export function plan(take, o = {}, detail = DETAIL[0]) {
  const step = o.step ?? STEP_MS;
  const notes = [];
  const levels = take.levels || [];
  const targets = [...new Set([...take.holds.map((h) => h.target), ...levels.map((l) => l.target)])];
  const steps = {};
  for (const target of targets) {
    const raw = outputSteps(target, take.holds.filter((h) => h.target === target), levels.filter((l) => l.target === target), take.end, detail, step);
    const capped = capOnTimes(target, raw, o.maxOnMs?.[target] || 0, step, o.labels, notes);
    if (capped.some((s) => s.level > 0)) steps[target] = capped;
  }
  let sounds = take.sounds.map((s) => ({ sound: s.sound, at: snapTo(s.at, step) }));
  if (o.trimLead ?? true) {
    const first = Math.min(...Object.values(steps).map((s) => s[0].t), ...sounds.map((s) => s.at));
    if (Number.isFinite(first) && first > 0) {
      for (const t of Object.keys(steps)) steps[t] = steps[t].map((s) => ({ ...s, t: s.t - first }));
      sounds = sounds.map((s) => ({ ...s, at: s.at - first }));
    }
  }
  sounds.sort((a, b) => a.at - b.at);
  // on-times as segments (what was on, from when to when), for a timeline view
  const segments = [];
  for (const [target, st] of Object.entries(steps)) {
    for (let i = 0; i < st.length; i++) {
      if (st[i].level <= 0 || (i > 0 && st[i - 1].level > 0)) continue;
      let j = i;
      while (j + 1 < st.length && st[j + 1].level > 0) j++;
      const end = st[j + 1]?.t ?? st[j].t;
      segments.push({ target, start: st[i].t, end, cut: !!st[i].cut, heldMs: end - st[i].t });
    }
  }
  segments.sort((a, b) => a.start - b.start || a.target.localeCompare(b.target));
  const lengthMs = Math.max(0, ...Object.values(steps).map((s) => s[s.length - 1].t), ...sounds.map((s) => s.at));
  return { steps, segments, sounds, lengthMs, notes };
}

// fades: runs of slider steps that move one way, close together
function fadeCandidates(target, st) {
  if (!dimmable(target)) return [];
  const out = [];
  let i = 0;
  while (i < st.length - 1) {
    const dir = Math.sign(st[i + 1].level - st[i].level);
    let j = i;
    while (j + 1 < st.length && Math.sign(st[j + 1].level - st[j].level) === dir && st[j + 1].t - st[j].t <= FADE_GAP_MS) j++;
    if (j - i + 1 >= FADE_MIN_STEPS) { out.push({ target, i, j, start: st[i].t, end: st[j].t, to: st[j].level }); i = j; } else i++;
  }
  return out;
}

const ORDER = { off: 0, set: 1, sound: 2, fade: 3 };

// The events of one effect: sets (and fades where nothing else happens meanwhile), sounds.
function effectEvents(steps, sounds) {
  const targets = Object.keys(steps);
  const cands = targets.flatMap((t) => fadeCandidates(t, steps[t]));
  // longest first: a long smooth movement is worth more lines than a short one
  cands.sort((a, b) => (b.j - b.i) - (a.j - a.i) || a.start - b.start);
  const inner = (c) => new Set(steps[c.target].slice(c.i + 1, c.j + 1));
  const accepted = [];
  const removed = new Set();
  for (const c of cands) {
    const skip = inner(c);
    const clash = accepted.some((f) => !(f.end <= c.start || c.end <= f.start)) ||
      targets.some((t) => steps[t].some((s) => !skip.has(s) && !removed.has(s) && s.t > c.start && s.t < c.end)) ||
      sounds.some((s) => s.at > c.start && s.at < c.end);
    if (clash) continue;
    accepted.push(c);
    for (const s of skip) removed.add(s);
  }
  const evs = [];
  for (const t of targets) {
    for (const s of steps[t]) {
      if (removed.has(s)) continue;
      evs.push({ t: s.t, order: s.level <= 0 ? ORDER.off : ORDER.set, kind: 'set', target: t, level: s.level, line: `${t} = ${levelWord(s.level)}` });
    }
  }
  for (const f of accepted) evs.push({ t: f.start, order: ORDER.fade, kind: 'fade', target: f.target, level: f.to, dur: f.end - f.start, line: `fade ${f.target} to ${f.to}% over ${seconds(f.end - f.start)}` });
  for (const s of sounds) evs.push({ t: s.at, order: ORDER.sound, kind: 'sound', sound: s.sound, line: `audio_single = ${s.sound}` });
  evs.sort((a, b) => a.t - b.t || a.order - b.order || String(a.target).localeCompare(String(b.target)));
  return evs;
}

function bodyLines(evs) {
  const out = [];
  let now = 0;
  for (const e of evs) {
    if (e.t > now) { out.push(`wait ${seconds(e.t - now)}`); now = e.t; }
    out.push(e.line);
    if (e.dur) now += e.dur;
  }
  return out;
}

// What the board does along these events, for playing the take back: [{ at, target, level }] (fades in 0.1 s
// steps) and [{ at, sound }], time order.
function timelineOf(evs) {
  const out = [];
  const cur = {};
  for (const e of evs) {
    if (e.kind === 'sound') out.push({ at: e.t, sound: e.sound });
    else if (e.kind === 'set') { out.push({ at: e.t, target: e.target, level: e.level }); cur[e.target] = e.level; }
    else {
      const from = cur[e.target] ?? 0;
      const n = Math.max(1, Math.round(e.dur / STEP_MS));
      for (let k = 1; k <= n; k++) out.push({ at: e.t + k * STEP_MS, target: e.target, level: Math.round(from + ((e.level - from) * k) / n) });
      cur[e.target] = e.level;
    }
  }
  return out.sort((a, b) => a.at - b.at);
}

const clean = (name) => String(name).replace(/[:#\n\r]/g, ' ').replace(/\s+/g, ' ').trim();

function effectName(base, suffix = '') {
  const s = suffix ? ` - ${suffix}` : '';
  return (clean(base).slice(0, LIMITS.nameLen - s.length).trim() || 'tapped') + s;
}

function build(take, o, detail) {
  const p = plan(take, o, detail);
  const effect = (name, lines) => [`effect ${name}:`, `  wait for ${o.trigger}`, ...lines.map((l) => `  ${l}`)];
  const allEvs = effectEvents(p.steps, p.sounds);
  const one = bodyLines(allEvs);
  if (one.length + 2 <= LIMITS.effectLines) return { p, effects: [effect(effectName(o.name), one)], evs: [allEvs] };
  // one effect per output (and one for sounds): each output belongs to one effect, and they start together
  const effects = [], evs = [];
  for (const t of Object.keys(p.steps).sort()) {
    const e = effectEvents({ [t]: p.steps[t] }, []);
    const lines = bodyLines(e);
    if (lines.length + 2 > LIMITS.effectLines) {
      const st = p.steps[t];
      const onOff = st.every((s) => s.level === 0 || s.level === 100);
      const n = onOff ? st.filter((s) => s.level > 0).length : st.length;
      const per = Math.floor((LIMITS.effectLines - 2) / (onOff ? 4 : 2));
      return { p, error: `That is too much for one board: ${n} ${onOff ? 'taps' : 'changes'} on ${named(t, o.labels)}, and an effect can hold about ${per}. Try a shorter take.`, target: t };
    }
    effects.push(effect(effectName(o.name, o.labels?.[t] || t), lines));
    evs.push(e);
  }
  if (p.sounds.length) {
    const e = effectEvents({}, p.sounds);
    const lines = bodyLines(e);
    if (lines.length + 2 > LIMITS.effectLines) return { p, error: `That is too much for one board: ${p.sounds.length} sounds in one take. Try a shorter take.` };
    effects.push(effect(effectName(o.name, 'sound'), lines));
    evs.push(e);
  }
  if (effects.length > LIMITS.effects) return { p, error: `That is too much for one board: it needs ${effects.length} effects and a board runs at most ${LIMITS.effects}.` };
  return { p, effects, evs };
}

/** The take as an effect script.
 * Options: name, trigger ('trig1' … 'user3'), maxOnMs { out1: ms }, labels { out1: 'lamp' },
 * step (ms, 100), trimLead (true), source (where it was recorded, for the comment at the
 * top: 'in Specter Studio', 'on the phone page').
 * Result: { text, error?, effects (count), notes, steps, segments, sounds, lengthMs, timeline }. */
export function takeToScript(take, o) {
  const hasSlider = (take.levels || []).some((l) => dimmable(l.target));
  let b = null;
  const notes = [];
  for (const [k, detail] of (hasSlider ? DETAIL : DETAIL.slice(0, 1)).entries()) {
    b = build(take, o, detail);
    if (!b.error) {
      if (k > 0) notes.push(`The slider moved too often for one effect, so its levels are written in ${detail.level} % steps, at most one change every ${seconds(detail.grid)} s.`);
      break;
    }
  }
  const p = b.p;
  const result = (extra) => ({ ...p, notes: [...p.notes, ...notes], timeline: [], effects: 0, text: '', ...extra });
  if (!Object.keys(p.steps).length && !p.sounds.length) return result({ error: 'Nothing was tapped. Press Start, then tap, hold or slide the controls.' });
  if (b.error) return result({ error: b.error });
  const where = o.source ? `${o.source} ` : '';
  const head = [
    `# Recorded ${where}(Record effect): when ${o.trigger.toUpperCase()} is pressed, the board does what you tapped.`,
    '# Change anything you like, as text or as blocks.',
  ];
  const text = [...head, ...b.effects.map((e) => e.join('\n'))].join('\n') + '\n';
  const lines = text.split('\n').length - 1;
  if (lines > LIMITS.scriptLines) return result({ error: `That is too much for one board: ${lines} lines, and a board takes at most ${LIMITS.scriptLines}. Try a shorter take.` });
  if (new TextEncoder().encode(text).length > LIMITS.bytes) return result({ error: 'That is too much for one board. Try a shorter take.' });
  const timeline = timelineOf(b.evs.flat().sort((x, y) => x.t - y.t || x.order - y.order));
  return result({ text, effects: b.effects.length, timeline });
}

/** A name from what was recorded: "lamp and siren", "OUT1 and RLY1". */
export function suggestName(take, labels = {}) {
  const used = [...new Set([...take.holds.map((h) => h.target), ...(take.levels || []).map((l) => l.target)])];
  const words = used.map((t) => labels[t] || t.toUpperCase());
  if (take.sounds.length) words.push('sound');
  if (!words.length) return 'tapped effect';
  return clean(words.length > 2 ? `${words.slice(0, 2).join(', ')} and more` : words.join(' and ')).slice(0, LIMITS.nameLen);
}

/** Two takes as one (an "add more on top" pass over the earlier ones). */
export function mergeTakes(a, b) {
  return {
    holds: [...a.holds, ...b.holds],
    sounds: [...a.sounds, ...b.sounds],
    levels: [...(a.levels || []), ...(b.levels || [])],
    end: Math.max(a.end ?? 0, b.end ?? 0) || undefined,
  };
}

// ------------------------------------------------------------------ the stored script

/** An effect name as the board compares them: case and spaces do not matter. */
export const sameName = (a, b) => clean(a).toLowerCase() === clean(b).toLowerCase();

const HEADER = /^\s*effect\b(.*?)\s*[:=]\s*(#.*)?$/i;

/** The effects in a script text: [{ name, from, to }] (line indexes, `to` exclusive); comment lines just above an
 * effect belong to it. `head` is the number of lines before the first effect. */
export function effectsIn(text) {
  const lines = text.replace(/\r\n?/g, '\n').split('\n');
  const found = [];
  lines.forEach((l, i) => {
    const m = HEADER.exec(l.replace(/ /g, ' '));
    if (m) found.push({ name: clean(m[1]), at: i });
  });
  const out = found.map((f, k) => ({ name: f.name, from: f.at, to: k + 1 < found.length ? found[k + 1].at : lines.length }));
  // comments and blank lines right above a header go with it
  for (let k = 0; k < out.length; k++) {
    let f = out[k].from;
    const floor = k ? out[k - 1].from + 1 : 0;
    while (f - 1 >= floor && /^\s*(#.*)?$/.test(lines[f - 1])) f--;
    // blank lines stay with the effect above; only the comment block directly above moves
    while (f < out[k].from && /^\s*$/.test(lines[f])) f++;
    if (k) out[k - 1].to = f;
    out[k].from = f;
  }
  return { lines, effects: out, head: out.length ? out[0].from : lines.length };
}

/** The effects of a script that a recorded effect named `name` replaces: that name, and its parts when a long
 * take was split ("name - out1", "name - sound"). */
export function replacedBy(text, name) {
  const base = clean(name).toLowerCase();
  return effectsIn(text).effects.filter((e) => {
    const n = e.name.toLowerCase();
    return n === base || n.startsWith(base + ' - ');
  }).map((e) => e.name);
}

/** The outputs each effect of a script sets: { effect name: Set('out1', …) }. `names`: the terminals' names
 * (lower case name → lower case terminal, SHOW-SCRIPT.md SS-N), so `lamp = on` counts as out1. */
export function outputsByEffect(text, names = {}) {
  const { lines, effects } = effectsIn(text);
  const term = (w) => {
    const x = w.toLowerCase();
    if (/^(out[1-4]|rly[12]|cc[12]|dmx\d+)$/.test(x)) return x;
    return names[x] || null;
  };
  const out = {};
  for (const e of effects) {
    const set = new Set();
    for (const raw of lines.slice(e.from, e.to)) {
      const l = raw.replace(/#.*/, '').trim();
      let m = /^(fade|flicker)\s+([A-Za-z_][\w]*)/i.exec(l);
      if (m) { const t = term(m[2]); if (t) set.add(t); continue; }
      m = /^([A-Za-z_][\w]*)\s*[=:]/.exec(l);
      if (m && !/^(audio_single|audio_loop|effect)$/i.test(m[1])) { const t = term(m[1]); if (t) set.add(t); }
    }
    out[e.name] = set;
  }
  return out;
}

/** Put a recorded script into the stored one: the effects named like it (and its split parts) are replaced in
 * place, otherwise it goes at the end. The rest of the stored text stays exactly as it was.
 * Returns { text, replaced: [names], clashes: [{ output, effect }] } — a clash is an output that a kept effect
 * already sets (each output belongs to one effect, SS-R8: the board would refuse the script). */
export function mergeScript(stored, recorded, name, names = {}) {
  const base = effectsIn(stored || '');
  const rec = effectsIn(recorded);
  const replaced = replacedBy(stored || '', name);
  const isReplaced = (e) => replaced.includes(e.name);
  // the recorded text's own lines (its comment head goes with its first effect)
  const recLines = rec.lines.slice(0, rec.lines.length - (rec.lines[rec.lines.length - 1] === '' ? 1 : 0));
  // clashes: outputs of kept effects that the recording also sets
  const keptOut = outputsByEffect(stored || '', names);
  const recOut = outputsByEffect(recorded, names);
  const mine = new Set(Object.values(recOut).flatMap((s) => [...s]));
  const clashes = [];
  for (const [eff, set] of Object.entries(keptOut)) {
    if (replaced.includes(eff)) continue;
    for (const o of set) if (mine.has(o)) clashes.push({ output: o, effect: eff });
  }
  if (!base.effects.length && !String(stored || '').trim()) return { text: recLines.join('\n') + '\n', replaced, clashes };
  let out;
  if (replaced.length) {
    out = [];
    let placed = false;
    out.push(...base.lines.slice(0, base.head));
    for (const e of base.effects) {
      if (isReplaced(e)) {
        if (!placed) { out.push(...recLines); placed = true; }
        // keep a blank line after it when the replaced block ended with one
        if (/^\s*$/.test(base.lines[e.to - 1] ?? 'x') && e.to < base.lines.length) out.push('');
      } else out.push(...base.lines.slice(e.from, e.to));
    }
  } else {
    out = base.lines.slice();
    while (out.length && /^\s*$/.test(out[out.length - 1])) out.pop();
    out.push('', ...recLines);
  }
  while (out.length && /^\s*$/.test(out[out.length - 1])) out.pop();
  return { text: out.join('\n') + '\n', replaced, clashes };
}
