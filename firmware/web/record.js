// Record effect: the phone page's recorder (firmware/docs/RECORD-EFFECT.md). Pick what starts it and which
// outputs and sounds it uses, press Start, then tap, hold or slide the controls: the board follows live
// (set_output, sound). Stop: the board does the take back. Keep: the take becomes an effect script (take.js, the
// module Specter Studio shares) put into the board's stored script without touching the rest of it
// (get_script → merge → check_script → set_script).
// An ES module loaded after app.js: it uses app.js's page helpers ($, el, conn, call, momentary, tdisp, …) and
// hears about the board through pageHooks.
import { takeToScript, suggestName, mergeTakes, mergeScript, replacedBy, dimmable, seconds } from './take.js';

const TRIGGERS = ['TRIG1', 'TRIG2', 'USER1', 'USER2', 'USER3'];
const COUNT_MS = 800;           // one step of the 3-2-1
const SLIDER_SEND_MS = 100;     // a slider sends the board at most this often (the last value always goes)
const AFTER_MS = 400;           // the playback lets go this long after its last step
const empty = () => ({ holds: [], sounds: [], levels: [] });

const rx = {
  phase: 'setup',               // setup, count, rec, play, review
  base: empty(),                // earlier passes ("Add more on top")
  pass: empty(),                // this pass
  t0: 0,
  held: {},                     // target -> { start, timer, cutAt }
  slider: {},                   // target -> level (%) on the slider now
  sent: {},                     // target -> { at, timer } for the slider throttle
  passTouched: new Set(),       // outputs used in this pass (the earlier passes leave them alone)
  touched: new Set(),           // outputs this page set: let go (level null) when it ends
  queue: {},                    // target -> the last request (one after another, so an off never overtakes an on)
  timers: [],
  ticker: null,
  refused: '',
};
const now = () => performance.now() - rx.t0;
const lower = (x) => String(x).toLowerCase();
const active = () => rx.phase === 'count' || rx.phase === 'rec' || rx.phase === 'play';
const hasTake = (t) => t.holds.length > 0 || t.sounds.length > 0 || t.levels.length > 0;
const whole = () => mergeTakes(rx.base, rx.pass);

// ------------------------------------------------------------------ what the board offers

const inputTerms = () => terminals().filter((t) => t.kind === 'input' && TRIGGERS.includes(t.id));
// outputs a script may use: the role is "shows" and the pulse-and-recover effect does not own it
const outputTerms = () => terminals().filter((t) => t.kind === 'output' && t.shows !== false);
const labels = () => Object.fromEntries(terminals().filter((t) => t.alias || t.label).map((t) => [lower(t.id), t.alias || t.label]));
// a terminal's names as a script may use them (SHOW-SCRIPT.md SS-N): name -> terminal, lower case
const scriptNames = () => {
  const m = {};
  for (const t of terminals()) for (const n of [t.label, t.alias]) if (n && /^[A-Za-z]\w*$/.test(n)) m[lower(n)] = lower(t.id);
  return m;
};
const maxOn = () => {
  const m = {};
  for (const v of params?.values || []) { const k = /^([a-z]+\d)_max_on_ms$/.exec(v.key); if (k) m[k[1]] = Number(v.value) || 0; }
  return m;
};
// the sounds: 1 to 3 (sound N is the one file in the card's folder NN)
function soundChoices() {
  if (!has('sound')) return [];
  return [1, 2, 3].map((n) => ({ n, label: `Sound ${n}` }));
}

const picked = () => ({
  trigger: lower($('rx-trigger').value || 'trig1'),
  outs: [...document.querySelectorAll('#rx-outs input:checked')].map((i) => i.value),
  sounds: [...document.querySelectorAll('#rx-sounds input:checked')].map((i) => +i.value),
});
const options = (extra = {}) => ({
  name: $('rx-name').value.trim() || suggestName(whole(), labels()),
  trigger: picked().trigger, maxOnMs: maxOn(), labels: labels(), source: 'on the phone page', ...extra,
});

// ------------------------------------------------------------------ setup

function fillSetup() {
  if (!info) return;
  const keepT = $('rx-trigger').value || store.get('rx.trigger', 'TRIG1');
  $('rx-trigger').replaceChildren(...inputTerms().map((t) => {
    const why = t.shows === false ? (t.role === 'pause' ? 'set to pause the board (Settings)' : `used as ${t.role || 'something else'}`) : '';
    return el('option', { value: t.id, disabled: !!why, text: tdisp(t.id) + (why ? `: ${why}` : '') });
  }));
  const usable = inputTerms().filter((t) => t.shows !== false).map((t) => t.id);
  $('rx-trigger').value = usable.includes(keepT) ? keepT : (usable[0] || '');
  const keepO = new Set(JSON.parse(store.get('rx.outs', 'null') || 'null') || [outputTerms()[0] && lower(outputTerms()[0].id)]);
  const now0 = new Set(picked().outs);
  $('rx-outs').replaceChildren(...outputTerms().map((t) => chip('rx-out', lower(t.id), tdisp(t.id) + (dimmable(t.id) ? ' · dims' : ''), now0.size ? now0.has(lower(t.id)) : keepO.has(lower(t.id)))));
  const reserved = terminals().filter((t) => t.kind === 'output' && t.shows === false);
  const sc = soundChoices();
  $('rx-sounds-box').hidden = !sc.length;
  const keepS = new Set(picked().sounds);
  $('rx-sounds').replaceChildren(...sc.map((s) => chip('rx-snd', String(s.n), s.label, keepS.has(s.n))));
  $('rx-trigger-hint').hidden = !reserved.length;
  $('rx-trigger-hint').textContent = reserved.length ? `Not offered: ${reserved.map((t) => tdisp(t.id)).join(', ')} (an output role or the built-in effect, Settings).` : '';
  renderButtons();
}
function chip(name, value, text, checked) {
  const i = el('input', { type: 'checkbox', name, value, checked });
  i.addEventListener('change', () => { store.set('rx.outs', JSON.stringify(picked().outs)); renderButtons(); });
  return el('label', { class: 'check chipbox' }, i, el('span', { text }));
}
$('rx-trigger').addEventListener('change', () => store.set('rx.trigger', $('rx-trigger').value));

function renderButtons() {
  const p = picked();
  const ready = conn.open && has('outputs') && !state?.paused && !!p.trigger && (p.outs.length > 0 || p.sounds.length > 0);
  $('rx-start').disabled = !ready || active();
  $('rx-setup-msg').className = 'msg';
  $('rx-setup-msg').textContent = !conn.open ? 'Not connected to the board.' : !has('outputs') ? 'This board does not let the page switch its outputs, so effects cannot be recorded here.' : state?.paused ? 'The board is paused: Resume on the Live tab first.'
    : !(p.outs.length || p.sounds.length) ? 'Pick at least one output or sound.' : '';
  for (const id of ['rx-play', 'rx-keep', 'rx-more']) $(id).disabled = active() || !conn.open || state?.paused;
}

// ------------------------------------------------------------------ the board

// one request after another per output; level in permille, null = let go (back to the shows)
function send(target, level) {
  if (level != null) rx.touched.add(target);
  const p = (rx.queue[target] || Promise.resolve()).catch(() => {}).then(() => conn.call('set_output', { channel: target.toUpperCase(), level }));
  p.catch((e) => {
    const t = e.detail?.code === 'pin' ? pinRefused(e) : e.message;
    if (level && rx.refused !== t) { rx.refused = t; showMsg('rx-live-msg', `${tdisp(target.toUpperCase())}: ${t}`, false); }
  });
  rx.queue[target] = p;
  return p;
}
function letGoBoard() {
  for (const t of rx.touched) send(t, null);
  rx.touched.clear();
  for (const s of Object.values(rx.sent)) clearTimeout(s.timer);
  rx.sent = {};
}
const playSound = (n) => conn.call('sound', { cmd: 'play_folder', folder: n, track: 1 }).catch(() => {});
const later = (ms, f) => rx.timers.push(setTimeout(f, Math.max(0, ms)));
function stopTimers() { rx.timers.forEach(clearTimeout); rx.timers = []; clearInterval(rx.ticker); rx.ticker = null; }

// ------------------------------------------------------------------ live controls

function buildControls() {
  const p = picked();
  const box = $('rx-controls');
  box.replaceChildren(...p.outs.map((target) => {
    const id = target.toUpperCase();
    const hold = el('button', { class: 'hold', type: 'button', 'aria-label': `${tdisp(id)}: hold to turn on` }, 'Hold: on');
    momentary(hold, () => press(target), () => letGo(target), false);
    const card = el('div', { class: 'rxout', 'data-target': target },
      el('div', { class: 'head' }, el('span', { class: 'name', text: tdisp(id) }), el('span', { class: 'board', text: '' })),
      hold);
    if (dimmable(target)) {
      const v = el('output', { class: 'lv', text: '0 %' });
      const r = el('input', { type: 'range', min: 0, max: 100, step: 5, value: rx.slider[target] || 0, 'aria-label': `${tdisp(id)} level` });
      r.addEventListener('input', () => { v.textContent = `${r.value} %`; slide(target, +r.value); });
      card.append(el('label', { class: 'level' }, 'Level ', v, r));
    }
    return card;
  }), ...p.sounds.map((n) => {
    const s = soundChoices().find((x) => x.n === n);
    return el('button', { class: 'snd', type: 'button', 'data-sound': n, onclick: () => tapSound(n) }, `Play ${s ? s.label : `sound ${n}`}`);
  }));
  renderLive();
}

function press(target) {
  if (rx.phase !== 'rec' || rx.held[target]) return;
  const h = { start: now() };
  const max = maxOn()[target] || 0;
  if (max > 0) h.timer = setTimeout(() => {
    // the board's maximum on-time: off now, even though the finger is still down
    if (rx.held[target] !== h || h.cutAt != null) return;
    h.cutAt = h.start + max;
    send(target, 0);
    renderLive();
  }, max);
  rx.held[target] = h;
  rx.passTouched.add(target);
  send(target, 1000);
  renderLive();
}
function letGo(target, at = now()) {
  const h = rx.held[target];
  if (!h) return;
  clearTimeout(h.timer);
  delete rx.held[target];
  if (h.cutAt == null) send(target, (rx.slider[target] || 0) * 10);
  rx.pass.holds.push({ target, start: h.start, end: h.cutAt ?? at, cut: h.cutAt != null, heldMs: at - h.start });
  renderLive();
}
function slide(target, level) {
  if (rx.phase !== 'rec') return;
  rx.slider[target] = level;
  rx.passTouched.add(target);
  rx.pass.levels.push({ target, at: now(), level });
  if (rx.held[target]) return;  // held: full on until let go, then this level
  // at most every 0.1 s, and the last value always goes
  const s = rx.sent[target] || (rx.sent[target] = { at: -1e9, timer: null });
  clearTimeout(s.timer);
  const go = () => { s.at = performance.now(); send(target, level * 10); };
  const wait = SLIDER_SEND_MS - (performance.now() - s.at);
  if (wait <= 0) go(); else s.timer = setTimeout(go, wait);
}
function tapSound(n) {
  if (rx.phase !== 'rec') return;
  rx.pass.sounds.push({ sound: n, at: now() });
  playSound(n);
  const b = document.querySelector(`#rx-controls .snd[data-sound="${n}"]`);
  if (b) { b.classList.add('pressed'); setTimeout(() => b.classList.remove('pressed'), 250); }
}

// what each control does now, and what the board reports
function renderLive() {
  for (const card of document.querySelectorAll('#rx-controls .rxout')) {
    const t = card.dataset.target;
    const o = state?.outputs?.find((x) => x.channel === t.toUpperCase());
    const lv = o ? o.level : 0;
    card.querySelector('.board').textContent = `board: ${dimmable(t) ? (lv ? Math.round(lv / 10) + ' %' : 'off') : (lv ? 'on' : 'off')}`;
    card.classList.toggle('on', lv > 0);
    const h = rx.held[t];
    const hold = card.querySelector('.hold');
    hold.textContent = h?.cutAt != null ? 'Off: max on-time' : 'Hold: on';
    hold.disabled = rx.phase !== 'rec';
    const r = card.querySelector('input[type=range]');
    if (r) { r.disabled = rx.phase !== 'rec'; }
  }
  for (const b of document.querySelectorAll('#rx-controls .snd')) b.disabled = rx.phase !== 'rec';
}

// ------------------------------------------------------------------ the phases

function setPhase(p) {
  rx.phase = p;
  if (p !== 'review') hideAsk();
  $('rx-setup').classList.toggle('hidden', p !== 'setup');
  $('rx-live').classList.toggle('hidden', !['count', 'rec', 'play'].includes(p));
  $('rx-review').classList.toggle('hidden', p !== 'review');
  $('rx-title').textContent = p === 'play' ? 'The board does it back' : 'Recording';
  $('rx-stop').textContent = p === 'play' ? 'Stop playing' : 'Stop';
  // a screen reader hears the changes, not the clock every 0.1 s
  $('rx-status').setAttribute('aria-live', p === 'rec' ? 'off' : 'polite');
  renderLive();
  renderButtons();
}
function status(text) { $('rx-status').textContent = text; }
const clock = (ms) => `${Math.floor(ms / 60000)}:${((ms % 60000) / 1000).toFixed(1).padStart(4, '0')}`;

function start() {
  if (active() || $('rx-start').disabled) return;
  const p = picked();
  store.set('rx.outs', JSON.stringify(p.outs));
  rx.pass = empty();
  rx.passTouched = new Set();
  rx.refused = '';
  for (const t of p.outs) rx.slider[t] = rx.slider[t] || 0;
  $('rx-live-msg').textContent = '';
  buildControls();
  if (!$('rx-count').checked) return begin();
  setPhase('count');
  status('3');
  later(COUNT_MS, () => status('2'));
  later(2 * COUNT_MS, () => status('1'));
  later(3 * COUNT_MS, begin);
}
function begin() {
  stopTimers();
  rx.t0 = performance.now();
  setPhase('rec');
  status('Recording 0:00.0: tap, hold or slide');
  rx.ticker = setInterval(() => status(`Recording ${clock(now())}: tap, hold or slide`), 100);
  // a dimmed output starts where its slider is
  for (const t of picked().outs) if (dimmable(t) && rx.slider[t]) { rx.pass.levels.push({ target: t, at: 0, level: rx.slider[t] }); send(t, rx.slider[t] * 10); }
  // Add more on top: the earlier passes play on the board while the new one is recorded over them
  if (hasTake(rx.base)) {
    const r = takeToScript(rx.base, options({ trimLead: false }));
    schedule(r.timeline, (e) => !rx.held[e.target] && !rx.passTouched.has(e.target));
  }
}
function schedule(timeline, allowed = () => true) {
  for (const e of timeline) {
    later(e.at, () => {
      if (e.sound != null) playSound(e.sound);
      else if (allowed(e)) send(e.target, e.level * 10);
    });
  }
}
function finish() {
  if (rx.phase === 'count') { stopTimers(); setPhase(hasTake(rx.base) ? 'review' : 'setup'); return; }
  if (rx.phase === 'play') return endPlay();
  if (rx.phase !== 'rec') return;
  const at = now();
  for (const t of Object.keys(rx.held)) letGo(t, at);
  // a slider left up ends here
  for (const [t, l] of Object.entries(rx.slider)) if (l && rx.passTouched.has(t)) rx.pass.levels.push({ target: t, at, level: 0 });
  rx.pass.end = at;
  stopTimers();
  letGoBoard();
  rx.base = whole();
  rx.pass = empty();
  for (const t of Object.keys(rx.slider)) rx.slider[t] = 0;
  for (const r of document.querySelectorAll('#rx-controls input[type=range]')) { r.value = 0; r.previousSibling.textContent = '0 %'; }
  if (!hasTake(rx.base)) { setPhase('setup'); showMsg('rx-setup-msg', 'Nothing was recorded: press Start, then tap, hold or slide the controls.', false); return; }
  // the board does the whole take back, straight away
  later(250, () => playBack());
}
function playBack() {
  const r = takeToScript(rx.base, options());
  if (r.error || !conn.open || state?.paused) return review();
  stopTimers();
  rx.t0 = performance.now();
  setPhase('play');
  status(`The board does it back: ${seconds(r.lengthMs)} s`);
  schedule(r.timeline);
  later(r.lengthMs + AFTER_MS, () => { letGoBoard(); review(); });
}
function endPlay() { stopTimers(); letGoBoard(); review(); }

// the take as a script: the name, notes in plain words, the text
function review() {
  stopTimers();
  if (!$('rx-name').value.trim()) $('rx-name').value = suggestName(rx.base, labels());
  setPhase('review');
  renderReview();
}
function renderReview() {
  const r = takeToScript(rx.base, options());
  $('rx-code').textContent = r.error ? '' : r.text;
  $('rx-code-box').hidden = !!r.error;
  const notes = [...(r.error ? [r.error] : []), ...r.notes];
  $('rx-notes').replaceChildren(...notes.map((n, i) => el('p', { class: 'hint' + (r.error && !i ? ' errline' : ''), text: n })));
  $('rx-keep').disabled = !!r.error || !conn.open;
  return r;
}
$('rx-name').addEventListener('input', () => { if (rx.phase === 'review') renderReview(); });

// everything off, the take ends here; what was recorded so far is kept
function halt(why) {
  if (!active()) { for (const t of Object.keys(rx.held)) letGo(t); return; }
  const wasRec = rx.phase === 'rec';
  if (wasRec) {
    const at = now();
    for (const t of Object.keys(rx.held)) letGo(t, at);
    for (const [t, l] of Object.entries(rx.slider)) if (l && rx.passTouched.has(t)) rx.pass.levels.push({ target: t, at, level: 0 });
    rx.pass.end = at;
    rx.base = whole();
    rx.pass = empty();
  }
  stopTimers();
  letGoBoard();
  for (const t of Object.keys(rx.slider)) rx.slider[t] = 0;
  if (hasTake(rx.base)) review(); else setPhase('setup');
  if (why) showMsg(rx.phase === 'review' ? 'rx-msg' : 'rx-setup-msg', why, false);
}

// ------------------------------------------------------------------ cancel: stop, let go, throw the take away

// Cancel (any step) and Escape: everything this page turned on is let go at once (set_output level null: each
// output goes back to what it did before, off or the level its show gives it), timers and the playback stop.
// With nothing recorded it ends there; with a take, the take is shown and the page asks before throwing it away.
function cancel() {
  const fromSetup = rx.phase === 'setup';
  hideAsk();
  halt('');
  for (const r of document.querySelectorAll('#rx-controls input[type=range]')) { r.value = 0; r.previousSibling.textContent = '0 %'; }
  if (!hasTake(rx.base)) return discard(fromSetup);
  if (rx.phase !== 'review') review();
  $('rx-msg').textContent = '';
  $('rx-ask').hidden = false;
  $('rx-keep-take').focus();
}
function hideAsk() { $('rx-ask').hidden = true; }
// the take is gone; back to the start of Record effect (from its first screen: back to the Live tab)
function discard(toLive) {
  hideAsk();
  rx.base = empty();
  rx.pass = empty();
  $('rx-name').value = '';
  $('rx-msg').textContent = '';
  $('rx-live-msg').textContent = '';
  setPhase('setup');
  fillSetup();
  if (toLive) {
    $('rx-setup-msg').textContent = '';
    const live = document.querySelector('#tabs button[data-tab="live"]');
    live.click(); live.focus();
    return;
  }
  showMsg('rx-setup-msg', 'Cancelled: nothing was kept, and everything the recording turned on is back to how it was.', true);
  $('rx-start').disabled ? $('rx-cancel-setup').focus() : $('rx-start').focus();
}
for (const id of ['rx-cancel-setup', 'rx-cancel-live', 'rx-cancel-review']) $(id).onclick = cancel;
$('rx-discard').onclick = () => discard(false);
$('rx-keep-take').onclick = () => { hideAsk(); $('rx-keep').focus(); };
// Escape cancels while the Record effect tab is open (a question open: Escape answers "No, go back"); on the first
// screen with nothing recorded there is nothing to cancel
document.addEventListener('keydown', (e) => {
  if (e.key !== 'Escape' || e.defaultPrevented || e.isComposing || !$('tab-record').classList.contains('on')) return;
  if (!$('rx-ask').hidden) { e.preventDefault(); $('rx-keep-take').click(); return; }
  if (rx.phase === 'setup' && !hasTake(rx.base)) return;
  e.preventDefault();
  cancel();
});

// ------------------------------------------------------------------ keep: into the board's stored script

async function keep() {
  const name = $('rx-name').value.trim() || suggestName(rx.base, labels());
  const r = takeToScript(rx.base, options({ name }));
  if (r.error) return showMsg('rx-msg', r.error, false);
  const btn = $('rx-keep');
  btn.disabled = true;
  showMsg('rx-msg', 'Saving…', true);
  try {
    const stored = await conn.call('get_script');
    const replaced = replacedBy(stored.text || '', name);
    if (replaced.length && !keep.sure) {
      btn.disabled = false;
      $('rx-msg').textContent = '';
      return askInline(btn, `The board already has an effect called “${replaced.join('”, “')}”. Replace it?`, 'Replace', () => { keep.sure = true; return keep().finally(() => { keep.sure = false; }); });
    }
    const m = mergeScript(stored.text || '', r.text, name, scriptNames());
    if (m.clashes.length) {
      const c = m.clashes[0];
      return showMsg('rx-msg', `${tdisp(c.output.toUpperCase())} is already used by the effect “${c.effect}” on the board, and an output belongs to one effect only. Leave it out of this recording (Try again), or change “${c.effect}” on the Script tab first.`, false);
    }
    const chk = await conn.call('check_script', { text: m.text });
    if (!chk.ok) {
      const first = chk.errors?.[0]?.message || 'it has a problem';
      const theirs = stored.text && stored.check && !stored.check.ok;
      return showMsg('rx-msg', theirs ? `The effect script stored on the board already has a problem, so nothing was saved: ${first} Fix it on the Script tab first.` : `The board says: ${first} Nothing was saved.`, false);
    }
    const res = await conn.call('set_script', { text: m.text });
    if (!res.ok) return showMsg('rx-msg', `The board says: ${res.errors?.[0]?.message || 'it has a problem'} Nothing was saved.`, false);
    const trig = tdisp(picked().trigger.toUpperCase());
    $('rx-msg').className = 'msg ok';
    $('rx-msg').replaceChildren(`Saved “${name}”${m.replaced.length ? ' (it replaced the old one)' : ''}. Press ${trig} and the board does it. `,
      el('button', { class: 'small', text: 'Change it on the Script tab', onclick: () => document.dispatchEvent(new CustomEvent('open-effect', { detail: { name } })) }));
    loadScript();  // the Script tab follows (edits not saved there are kept, and it says so)
  } catch (e) {
    showMsg('rx-msg', e.detail?.code === 'pin' ? pinRefused(e) : `${e.message}. Nothing was saved.`, false);
  } finally {
    btn.disabled = !conn.open;
  }
}

// ------------------------------------------------------------------ buttons and the page

$('rx-start').onclick = start;
$('rx-stop').onclick = finish;
$('rx-play').onclick = () => { $('rx-msg').textContent = ''; playBack(); };
$('rx-again').onclick = () => { stopTimers(); letGoBoard(); rx.base = empty(); rx.pass = empty(); $('rx-name').value = ''; $('rx-msg').textContent = ''; setPhase('setup'); fillSetup(); };
$('rx-more').onclick = () => { $('rx-msg').textContent = ''; setPhase('setup'); fillSetup(); showMsg('rx-setup-msg', 'Your earlier take plays on the board while you record more on top of it.', true); };
$('rx-keep').onclick = () => { hideAsk(); keep(); };

pageHooks.info.push(() => { fillSetup(); renderButtons(); });
pageHooks.state.push(() => {
  if (state?.paused && active()) halt('The board was paused: everything is off. What you recorded so far is kept.');
  renderLive();
  renderButtons();
});
pageHooks.stop.push(() => halt('STOP: everything is off. What you recorded so far is kept.'));
pageHooks.drop.push(() => {
  // the board lets go of this page's outputs itself when the connection closes
  rx.touched.clear();
  rx.queue = {};
  halt('The connection to the board dropped: everything this page turned on is off. What you recorded so far is kept.');
});
// the page hidden or left (another app, the screen off): timers may run late, so the take ends here
document.addEventListener('visibilitychange', () => { if (document.hidden) halt('The page was hidden, so everything was turned off. What you recorded so far is kept.'); });
window.addEventListener('pagehide', () => { stopTimers(); letGoBoard(); });
window.addEventListener('blur', () => { if (active()) halt('The page lost focus, so everything was turned off. What you recorded so far is kept.'); });
// the setup is filled again each time the tab opens (the outputs and their jobs may have changed)
document.querySelector('#tabs button[data-tab="record"]').addEventListener('click', fillSetup);
fillSetup();
setPhase('setup');
