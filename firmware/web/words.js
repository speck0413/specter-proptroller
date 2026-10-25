// The show-script language's words (SHOW-SCRIPT.md) for every editor (this page, Specter Studio): the checker's own
// tables (script.h), checked both ways by test_script. No DOM.
export const KEYWORDS = ['effect', 'wait', 'for', 'sleep', 'fade', 'to', 'over', 'flicker', 'pulse', 'limit', 'run', 'runs',
  'per', 'audio_single', 'audio_loop', 'audio_volume', 'audio_stop', 'pixels', 'pattern',
  'press'];
export const OUTPUTS = ['out1', 'out2', 'out3', 'out4', 'rly1', 'rly2', 'cc1', 'cc2'];
export const INPUTS = ['trig1', 'trig2', 'user1', 'user2', 'user3'];
export const LEVELS = ['on', 'off'];
export const UNITS = ['ms', 's', 'sec', 'secs', 'second', 'seconds', 'min', 'mins', 'minute', 'minutes'];
export const SETTINGS = ['out1_on_ms', 'out2_on_ms', 'out3_on_ms', 'recover_time_ms', 'cooldown_ms', 'trigger_out_pulse_ms',
  'role_pulse_ms', 'runs_per_cooldown'];
export const EFFECTS = ['off', 'fill', 'chase', 'rainbow', 'flash', 'fade', 'flicker', 'comet', 'sparkle', 'breathe', 'fire'];
export const COLOURS = [['red', 'ff0000'], ['orange', 'ff6000'], ['yellow', 'ffc000'], ['green', '00ff00'], ['cyan', '00ffff'],
  ['blue', '0000ff'], ['purple', '8000ff'], ['pink', 'ff40a0'], ['white', 'ffffff'], ['warm white', 'ffa040']];

const COLOUR_NAMES = COLOURS.map(([n]) => n);

// a word's class: kw, out, in, lv (on/off), num (unit, time setting), fx (pixel effect, colour), '' (other)
export function classOf(word) {
  const w = word.toLowerCase();
  if (KEYWORDS.includes(w)) return 'kw';
  if (OUTPUTS.includes(w) || /^dmx\d+$/.test(w)) return 'out';
  if (INPUTS.includes(w)) return 'in';
  if (LEVELS.includes(w)) return 'lv';
  if (UNITS.includes(w) || SETTINGS.includes(w)) return 'num';
  if (EFFECTS.includes(w) || COLOUR_NAMES.includes(w)) return 'fx';
  return '';
}

const esc = (s) => s.replace(/[&<>]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;' }[c]));
const span = (c, s) => (!s ? '' : c ? `<span class="t-${c}">${esc(s)}</span>` : esc(s));
// a two-word colour, 0xRRGGBB, a number (and %), a word, anything else
const TOKEN = new RegExp(`(${COLOUR_NAMES.filter((n) => n.includes(' ')).map((n) => n.replace(' ', ' +')).join('|')})(?![a-z0-9_])` +
  '|(0x[0-9a-f]{6})(?![a-z0-9_])|(\\d+(?:\\.\\d*)?|\\.\\d+)( *%)?|([a-z_][a-z0-9_]*)|([^a-z0-9_.]+|\\.)', 'gi');
function tokens(s) {
  return s.replace(TOKEN, (all, two, hex, n, pct, w, other) =>
    two ? span('fx', two) : hex ? span('num', hex) : n ? span('num', n + (pct || '')) : w ? span(classOf(w), w) : esc(other));
}

// the script as HTML, every character kept, in spans t-<class>, t-name (an effect's name), t-cm (a comment)
export function highlight(text) {
  return text.split('\n').map((line) => {
    const h = line.indexOf('#');
    const code = h < 0 ? line : line.slice(0, h);
    const note = h < 0 ? '' : line.slice(h);
    const m = /^(\s*)(effect)(?![a-z0-9_])([^:]*)(:?)(.*)$/i.exec(code);
    const body = m ? esc(m[1]) + span('kw', m[2]) + span('name', m[3]) + esc(m[4]) + tokens(m[5]) : tokens(code);
    return body + span('cm', note);
  }).join('\n');
}
