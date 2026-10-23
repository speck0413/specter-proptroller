// Effect-script blocks (SHOW-SCRIPT.md SS-E2, SS-E3) without Blockly: the text of an effect script sorted into
// blocks, one block per line kind, an effect block holding its lines, and the blocks written back as text.
// One shared, framework-free ES module of pure functions (no DOM): the board's phone page draws its blocks view
// from it (scriptview.js), and Specter Studio can use it too. Tested by firmware/web-test/blocks-test.mjs.
//
// Blocks are a view of the text: every block keeps its line exactly as written (and its line ending); a block
// whose fields were not changed gives that text back unchanged, so text → blocks → text is the identical text.
// A changed block writes its line in the plain form, keeping its indentation and its comment. Blank lines get no
// block: each rides along with the block before it (at the very top, the block after it). The board's checker
// decides what is right; the blocks only sort lines into kinds. The rules are those of Specter Studio's
// src/effects/blocks.ts (Blockly), so both give the same text.
//
// Model: { eol, blocks: [Block] }; Block { type, f: { FIELD: text }, d?: { raw, eol, f, before?, after? },
// lines?: [Block] (an effect's lines) }. `d` is what the block was read from; a new block has none.

const WS = /[\t  ]/g;          // tabs and no-break spaces count as spaces
const TRAIL = /[ \t  ]+$/;

/** Block kinds: the words shown, the fields, the colour (Studio's Blockly hues as colours), a tooltip. */
export const KINDS = {
  ps_effect: { words: ['effect', 'NAME', ':'], fields: { NAME: 'my effect' }, colour: '#a6805b',
    tip: 'An effect: its lines run top to bottom, then it starts again at its first "wait for".' },
  ps_wait: { words: ['wait for', 'INPUT'], fields: { INPUT: 'trig1' }, colour: '#5b8da6',
    tip: 'Waits until the input is pressed (held for the debounce time), after it was released.' },
  ps_set: { words: ['TARGET', '=', 'LEVEL'], fields: { TARGET: 'out1', LEVEL: 'on' }, colour: '#5ba65b',
    tip: 'Sets an output: on, off or a percentage (out1-out4 and dmx channels dim; rly and cc are on / off).' },
  ps_sleep: { words: ['sleep', 'TIME'], fields: { TIME: '1' }, colour: '#745ba6',
    tip: 'Waits this long: seconds, or with a unit (200 ms, 3 min).' },
  ps_fade: { words: ['fade', 'TARGET', 'to', 'LEVEL', 'over', 'TIME'], fields: { TARGET: 'out1', LEVEL: '0%', TIME: '2' }, colour: '#745ba6',
    tip: 'Goes smoothly to a level.' },
  ps_flicker: { words: ['flicker', 'TARGET', 'for', 'TIME'], fields: { TARGET: 'out1', TIME: '3' }, colour: '#745ba6',
    tip: 'Flickers like a candle or a failing bulb, then gives the level back.' },
  ps_audio_single: { words: ['play sound', 'SOUND', 'once'], fields: { SOUND: 'chime.mp3' }, colour: '#a65b80',
    tip: 'Plays a sound once.' },
  ps_audio_loop: { words: ['play sound', 'SOUND', 'over and over'], fields: { SOUND: 'rain.mp3' }, colour: '#a65b80',
    tip: 'Plays a sound over and over, until another sound line.' },
  ps_audio_stop: { words: ['stop the sound'], fields: {}, colour: '#a65b80', tip: 'Stops the sound.' },
  ps_comment: { words: ['#', 'TEXT'], fields: { TEXT: '' }, colour: '#6d6d6d', tip: 'A comment or a blank line: the board skips it.' },
  ps_raw: { words: ['line', 'TEXT'], fields: { TEXT: '' }, colour: '#a65b5b', tip: 'A line the blocks do not know: Check says what is wrong with it.' },
};
/** The kinds a person can add inside an effect, in the order of a menu (with their menu words). */
export const ADDABLE = [
  ['ps_wait', 'wait for an input'], ['ps_set', 'set an output'], ['ps_sleep', 'sleep'], ['ps_fade', 'fade'],
  ['ps_flicker', 'flicker'], ['ps_audio_single', 'play a sound once'], ['ps_audio_loop', 'play a sound over and over'],
  ['ps_audio_stop', 'stop the sound'], ['ps_comment', 'a comment'],
];

// ---------------------------------------------------------------- one line → kind and fields

// a keyword at the start, case and spaces inside it ignored (as the board reads it)
function kw(s, word) {
  let i = 0;
  for (const ch of word) {
    while (s[i] === ' ') i++;
    if ((s[i] || '').toLowerCase() !== ch) return null;
    i++;
  }
  return s.slice(i);
}
// splits a line into its indentation, its body and its comment (with the spaces before it)
export function parts(raw) {
  const norm = raw.replace(WS, ' ');
  const ind = norm.match(/^ */)[0].length;
  const hash = raw.indexOf('#');
  let body = hash >= 0 ? raw.slice(ind, hash) : raw.slice(ind);
  let comment;
  const b = body.replace(TRAIL, '');
  if (hash >= 0) comment = raw.slice(ind + b.length);
  else comment = body.slice(b.length);  // trailing spaces
  body = b;
  return { indent: raw.slice(0, ind), body, comment };
}

/** One line: { type, f }. */
export function parse(raw) {
  const p = parts(raw);
  const s = p.body.replace(/[‘’]/g, "'").replace(/[“”]/g, '"');
  const low = s.replace(WS, ' ');
  let r;
  if (!low.trim()) return { type: 'ps_comment', f: { TEXT: p.comment.trim().replace(/^#\s?/, '') } };
  if ((r = kw(low, 'effect')) !== null) {
    const plain = r === '' || r[0] === ' ' || r[0] === ':';
    const part = plain ? r : low;
    const i = part.indexOf(':');
    return { type: 'ps_effect', f: { NAME: (i >= 0 ? part.slice(0, i) : part).trim() } };
  }
  if ((r = kw(low, 'waitfor')) !== null) return { type: 'ps_wait', f: { INPUT: r.trim() } };
  if ((r = kw(low, 'audio_stop')) !== null && !r.trim()) return { type: 'ps_audio_stop', f: {} };
  for (const [w, type] of [['audio_single', 'ps_audio_single'], ['audio_loop', 'ps_audio_loop']]) {
    if ((r = kw(low, w)) !== null) {
      const v = r.trim();
      if (v === '' || v[0] === '=' || v[0] === ':') return { type, f: { SOUND: v.slice(1).trim() } };
    }
  }
  if ((r = kw(low, 'sleep')) !== null) return { type: 'ps_sleep', f: { TIME: r.trim() } };
  if ((r = kw(low, 'fade')) !== null) {
    const m = r.match(/^\s*([A-Za-z_]+\d*)\s*[Tt][Oo]\s*(.*?)\s*[Oo][Vv][Ee][Rr]\s*(.*)$/);
    if (m && m[1] && m[2] && m[3]) return { type: 'ps_fade', f: { TARGET: m[1], LEVEL: m[2], TIME: m[3].trim() } };
  }
  if ((r = kw(low, 'flicker')) !== null) {
    const m = r.match(/^\s*([A-Za-z_]+\d*)\s*[Ff][Oo][Rr]\s*(.+)$/);
    if (m) return { type: 'ps_flicker', f: { TARGET: m[1], TIME: m[2].trim() } };
  }
  const eq = low.search(/[=:]/);
  if (eq > 0 && /^[A-Za-z_]+\d*$/.test(low.slice(0, eq).replace(/ /g, '')) && !/^\s*wait/i.test(low)) {
    return { type: 'ps_set', f: { TARGET: low.slice(0, eq).trim(), LEVEL: low.slice(eq + 1).trim() } };
  }
  return { type: 'ps_raw', f: { TEXT: s } };
}

/** The plain form of a line from its fields (only for a block that was changed or added). */
export function lineOf(type, f) {
  switch (type) {
    case 'ps_effect': return `effect ${f.NAME}:`;
    case 'ps_wait': return `wait for ${f.INPUT}`;
    case 'ps_set': return `${f.TARGET} = ${f.LEVEL}`;
    case 'ps_sleep': return `sleep ${f.TIME}`;
    case 'ps_fade': return `fade ${f.TARGET} to ${f.LEVEL} over ${f.TIME}`;
    case 'ps_flicker': return `flicker ${f.TARGET} for ${f.TIME}`;
    case 'ps_audio_single': return `audio_single = ${f.SOUND}`;
    case 'ps_audio_loop': return `audio_loop = ${f.SOUND}`;
    case 'ps_audio_stop': return 'audio_stop';
    case 'ps_comment': return f.TEXT ? `# ${f.TEXT}` : '';
    default: return f.TEXT || '';
  }
}

// ---------------------------------------------------------------- text ⇄ blocks

// lines with their own endings ("\n", "\r\n", "\r" or "" for the last)
function splitLines(text) {
  const out = [];
  const re = /\r\n|\n|\r/g;
  let at = 0, m;
  while ((m = re.exec(text))) { out.push([text.slice(at, m.index), m[0]]); at = m.index + m[0].length; }
  if (at < text.length) out.push([text.slice(at), '']);
  return out;
}
const isBlank = (raw) => !raw.replace(/[\t   ]/g, '');

/** The text as blocks. */
export function textToBlocks(text) {
  const lines = splitLines(text);
  const eol = lines.find(([, e]) => e)?.[1] || '\n';
  const blocks = [];
  let effect = null, prev = null, before = [];
  const anyText = lines.some(([r]) => !isBlank(r));
  for (const [raw, e] of lines) {
    if (isBlank(raw) && anyText) {
      if (prev) prev.d.after.push([raw, e]); else before.push([raw, e]);
      continue;
    }
    const x = parse(raw);
    const b = { type: x.type, f: { ...x.f }, d: { raw, eol: e, f: { ...x.f }, before, after: [] } };
    before = [];
    prev = b;
    if (x.type === 'ps_effect') { b.lines = []; effect = b; blocks.push(b); }
    else if (effect) effect.lines.push(b);
    else blocks.push(b);
  }
  return { eol, blocks };
}

function emit(b, inside, eol, out) {
  const d = b.d;
  const same = d && Object.keys(b.f).every((k) => b.f[k] === d.f[k]);
  let line;
  if (same) line = d.raw;
  else {
    const p = d ? parts(d.raw) : { indent: inside && b.type !== 'ps_effect' ? '  ' : '', comment: '' };
    const body = lineOf(b.type, b.f);
    const keepComment = b.type !== 'ps_comment' && p.comment.trim().startsWith('#') ? p.comment : '';
    line = (body || keepComment ? (b.type === 'ps_effect' ? '' : (p.indent || (inside ? '  ' : ''))) : '') + body + keepComment;
  }
  for (const x of d?.before || []) out.push(x);
  out.push([line, d ? d.eol : eol]);
  for (const x of d?.after || []) out.push(x);
}

/** The blocks as text. */
export function blocksToText(model) {
  const out = [];
  for (const b of model.blocks) {
    emit(b, false, model.eol, out);
    for (const c of b.lines || []) emit(c, true, model.eol, out);
  }
  // every line ends as it did; a line that had no ending (the last) gets one when lines follow it
  let s = '';
  out.forEach(([line, e], i) => { s += line + (e || (i < out.length - 1 ? model.eol : '')); });
  return s;
}

/** A new block of a kind, with its default fields (`fields` overrides some). */
export function newBlock(type, fields = {}) {
  const b = { type, f: { ...KINDS[type].fields, ...fields } };
  if (type === 'ps_effect') b.lines = [];
  return b;
}

/** The field names of a kind, in the order they are shown. */
export const fieldsOf = (type) => KINDS[type].words.filter((w) => w in KINDS[type].fields);
