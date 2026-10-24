// Proptroller web UI. Plain JavaScript, no build step. Talks the protocol in PROTOCOL.md over a
// WebSocket (falls back to HTTP POST /api/v1/rpc). The desktop app sets window.PROPTROLLER_BASE.
'use strict';

const BASE = (window.PROPTROLLER_BASE || '').replace(/\/$/, '');
const $ = (id) => document.getElementById(id);
const el = (tag, attrs = {}, ...kids) => {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (k === 'class') e.className = v;
    else if (k.startsWith('on')) e.addEventListener(k.slice(2), v);
    else if (k === 'text') e.textContent = v;
    else if (v === true) e.setAttribute(k, '');
    else if (v !== false && v != null) e.setAttribute(k, v);
  }
  for (const c of kids) if (c != null) e.append(c);
  return e;
};

const store = {
  get(k, d) { try { const v = localStorage.getItem('pt.' + k); return v == null ? d : v; } catch { return d; } },
  set(k, v) { try { localStorage.setItem('pt.' + k, v); } catch { /* private mode */ } },
};

// ------------------------------------------------------------------ connection

const conn = {
  // the page password lives only in this page's memory (never stored, never shown)
  ws: null, open: false, heard: false, nextId: 1, pending: new Map(), pin: '', retry: 500, last: 0,
  fails: 0, busy: false, parked: false, hiddenMs: 30000,
  connect() {
    if (this.parked || this.ws) return;
    const url = (BASE ? BASE.replace(/^http/, 'ws') : (location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host) + '/api/v1/ws';
    let ws;
    try { ws = new WebSocket(url); } catch { return this.later(); }
    this.ws = ws;
    // every handler checks it is still the current socket: one dropped by the watchdog may close a minute later
    // connected once the board has said something on it (an open socket alone can be a dead link)
    ws.onopen = () => { if (this.ws !== ws) return; this.open = true; this.heard = false; this.fails = 0; this.busy = false; this.retry = 500; this.last = Date.now(); onConnected(); };
    ws.onclose = () => {
      if (this.ws !== ws) return;
      if (!this.open && ++this.fails >= 2) this.checkBusy();
      this.drop(); this.later();
    };
    ws.onerror = () => ws.close();
    ws.onmessage = (m) => {
      if (this.ws !== ws) return;
      this.last = Date.now();
      if (!this.heard) { this.heard = true; setConn(true); }
      let msg; try { msg = JSON.parse(m.data); } catch { return; }
      if ('ok' in msg) {
        const p = this.pending.get(msg.id);
        if (p) { this.pending.delete(msg.id); msg.ok ? p.resolve(msg.result) : p.reject(Object.assign(new Error(msg.error?.message || 'error'), { detail: msg.error })); }
      } else if (msg.event) onEvent(msg);
    };
  },
  // the socket is gone (closed, or silent too long): fail what waits on it, let go of held buttons
  drop() {
    const ws = this.ws;
    this.ws = null; this.open = false; this.heard = false;
    if (ws) { ws.onopen = ws.onclose = ws.onmessage = ws.onerror = null; try { ws.close(); } catch { /* already closed */ } }
    setConn(false); releaseAll(); hook('drop');
    for (const p of this.pending.values()) p.reject(Object.assign(new Error('the connection to the board dropped'), { dropped: true }));
    this.pending.clear();
  },
  // The socket keeps failing but the board answers plain HTTP: it has as many pages open as it takes (the 503
  // is not visible to a page). Say so and show its state over HTTP every 2 s; STOP and the buttons still work.
  async checkBusy() {
    try {
      const ac = new AbortController();
      setTimeout(() => ac.abort(), 3000);
      const r = await fetch(BASE + '/api/v1/state', { signal: ac.signal });
      if (!r.ok) return;
      const st = await r.json();
      if (this.open) return;
      this.busy = true;
      this.last = Date.now();
      if (!info) { const i = await fetch(BASE + '/api/v1/info'); if (i.ok) { info = await i.json(); applyInfo(); } }
      state = st; renderState(); renderConn();
    } catch { /* not answering at all */ }
  },
  later() { if (this.parked) return; clearTimeout(this.timer); this.timer = setTimeout(() => this.connect(), this.retry); this.retry = Math.min(this.retry * 2, 5000); },
  // The board pushes its state at least every 5 s: 7 s of silence means the link is dead even when the socket has
  // not noticed (a blackholed Wi-Fi link takes about a minute to close).
  watch() {
    if (this.open && Date.now() - this.last > STALE_MS) { this.drop(); this.later(); }
    if (this.busy && !this.open && !this.parked && Date.now() - this.last >= 2000) this.checkBusy();
    renderConn();
  },
  // A page left in the background lets go of its place after 30 s (a board takes only a few pages; a hidden
  // tab still answers pings) and comes back when it is shown again. What it held ends with the session.
  park(hidden) {
    clearTimeout(this.parkTimer);
    if (hidden) this.parkTimer = setTimeout(() => { this.parked = true; this.busy = false; clearTimeout(this.timer); this.drop(); renderConn(); }, this.hiddenMs);
    else if (this.parked) { this.parked = false; this.retry = 500; this.connect(); }
  },
  async call(op, args = {}) {
    const env = { v: 1, op, ...args };
    if (this.pin) env.pin = this.pin;
    if (this.open) {
      const id = this.nextId++;
      env.id = id;
      return new Promise((resolve, reject) => {
        this.pending.set(id, { resolve, reject });
        this.ws.send(JSON.stringify(env));
        setTimeout(() => { if (this.pending.delete(id)) reject(new Error('the board did not answer')); }, 8000);
      });
    }
    return this.http(env);
  },
  // over plain HTTP, given up after 4 s (a request to a board that is gone never ends by itself)
  async http(env, ms = 4000) {
    const ac = new AbortController();
    const t = setTimeout(() => ac.abort(), ms);
    let msg;
    try {
      const r = await fetch(BASE + '/api/v1/rpc', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(env), signal: ac.signal });
      msg = await r.json();
    } catch (e) {
      throw new Error(e.name === 'AbortError' ? 'the board did not answer' : 'the board cannot be reached');
    } finally { clearTimeout(t); }
    if (!msg.ok) throw Object.assign(new Error(msg.error?.message || 'error'), { detail: msg.error });
    return msg.result;
  },
};
const STALE_MS = 7000;
setInterval(() => conn.watch(), 1000);

function setConn(ok) {
  $('conn').classList.toggle('ok', ok);
  renderConn();
}
// Not connected: the dot is red, the values shown are greyed, and the header says how old they are.
function renderConn() {
  const live = conn.open && conn.heard;
  const fresh = conn.busy && Date.now() - conn.last < STALE_MS;
  document.body.classList.toggle('stale', !live && !fresh && !!state);
  $('conn').setAttribute('aria-label', live ? 'connected' : 'not connected');
  if (live) { if (info && $('dev-sub').dataset.live !== '1') { $('dev-sub').textContent = devSub(); $('dev-sub').dataset.live = '1'; } return; }
  $('dev-sub').dataset.live = '';
  const age = conn.last ? Math.round((Date.now() - conn.last) / 1000) : 0;
  if (conn.parked) { $('dev-sub').textContent = 'paused while in the background'; return; }
  if (fresh) { $('dev-sub').textContent = 'too many pages are open on this board: close it on another phone (updates every 2 s here)'; return; }
  $('dev-sub').textContent = !state ? 'connecting…' : `not connected: reconnecting… last update ${age} s ago`;
}
const devSub = () => `${info.fw} ${info.fw_version} · ${info.mcu}`;

// A failure with no place of its own on the page shows in the alert bar at the bottom (not only in the log).
function alertBar(text) {
  const a = $('alert');
  $('alert-text').textContent = text;
  a.hidden = false;
  clearTimeout(alertBar.t);
  alertBar.t = setTimeout(() => { a.hidden = true; }, 10000);
  log('error: ' + text);
}
$('alert-close').onclick = () => { $('alert').hidden = true; };
// call() has already said what went wrong: a click handler need not catch it
window.addEventListener('unhandledrejection', (e) => {
  e.preventDefault();
  // a dropped connection is shown in the header, not once per request that was waiting
  if (!e.reason?.shown && !e.reason?.dropped) alertBar(e.reason?.message || String(e.reason));
});

// The page lock: the board's own words (they also say when a wrong password makes the next try wait), the page
// forgets a password the board refused, and the Unlock button in the header opens the password box.
function pinRefused(e) {
  conn.pin = '';
  renderLock();
  openUnlock();
  // the board points at where its password is set; on this page it is entered with Unlock
  return e.message.replace(/\s*\(Settings \u2192 Device\)/, '') + ': tap Unlock at the top';
}
function renderLock() {
  const locked = has('pin');
  $('unlock').hidden = !locked;
  $('unlock').textContent = conn.pin ? 'Unlocked' : 'Unlock';
  $('unlock').setAttribute('aria-label', conn.pin ? 'page unlocked: tap to forget the password' : 'the page is locked: unlock it');
  if (!locked) $('unlock-box').hidden = true;
}
function openUnlock() {
  if (!has('pin')) return;
  $('unlock-box').hidden = false;
  $('unlock-pin').focus();
}
$('unlock').onclick = () => {
  if (conn.pin) { conn.pin = ''; renderLock(); return; }  // forget it (this page only)
  $('unlock-box').hidden ? openUnlock() : ($('unlock-box').hidden = true);
};
$('unlock-go').onclick = () => {
  if (!$('unlock-pin').value) return $('unlock-pin').focus();
  conn.pin = $('unlock-pin').value;
  $('unlock-pin').value = '';
  $('unlock-box').hidden = true;
  renderLock();
  log('page password in use for this page');
};
$('unlock-pin').addEventListener('keydown', (e) => { if (e.key === 'Enter') $('unlock-go').click(); });
$('unlock-cancel').onclick = () => { $('unlock-box').hidden = true; $('unlock-pin').value = ''; };

async function call(op, args, msgEl) {
  try {
    const r = await conn.call(op, args);
    if (msgEl) showMsg(msgEl, 'done', true);
    return r;
  } catch (e) {
    const t = e.detail?.code === 'pin' ? pinRefused(e) : e.message;
    if (msgEl) showMsg(msgEl, t, false); else alertBar(t);
    e.shown = true;
    throw e;
  }
}

function showMsg(target, text, ok) {
  const m = typeof target === 'string' ? $(target) : target;
  m.textContent = text;
  m.className = 'msg ' + (ok ? 'ok' : 'err');
}

// ------------------------------------------------------------------ momentary presses

// Press and hold: `down()` on press (repeated every 500 ms while held, so the device can time out a
// vanished client), `up()` on release. Releases on pointer up / cancel / leave, lost capture, page blur,
// hidden page and a dropped connection.
const held = new Set();
function releaseAll() { for (const r of [...held]) r(); }
function momentary(node, down, up, repeat = true) {
  let timer = null, active = false;
  const release = () => {
    if (!active) return;
    active = false; clearInterval(timer); held.delete(release); node.classList.remove('pressed'); node.setAttribute('aria-pressed', 'false');
    up();
  };
  node.addEventListener('pointerdown', (e) => {
    if (e.button > 0 || active) return;
    e.preventDefault();
    try { node.setPointerCapture(e.pointerId); } catch { /* not capturable */ }
    active = true; held.add(release); node.classList.add('pressed'); node.setAttribute('aria-pressed', 'true');
    // a button redrawn while held (another phone saved settings) is gone from the page: let go
    down(); timer = setInterval(() => (!node.isConnected ? release() : repeat ? down() : null), 500);
  });
  for (const ev of ['pointerup', 'pointercancel', 'lostpointercapture']) node.addEventListener(ev, release);
  // the keyboard too: Space or Enter held down presses it, letting go releases it
  node.addEventListener('keydown', (e) => {
    if ((e.key !== ' ' && e.key !== 'Enter') || e.repeat || active) return;
    e.preventDefault();
    active = true; held.add(release); node.classList.add('pressed'); node.setAttribute('aria-pressed', 'true');
    down(); timer = setInterval(() => (!node.isConnected ? release() : repeat ? down() : null), 500);
  });
  node.addEventListener('keyup', (e) => { if (e.key === ' ' || e.key === 'Enter') { e.preventDefault(); release(); } });
  node.addEventListener('blur', release);
  node.addEventListener('pointerleave', (e) => { if (!node.hasPointerCapture?.(e.pointerId)) release(); });
  node.addEventListener('contextmenu', (e) => e.preventDefault());
  return release;
}
window.addEventListener('blur', releaseAll);
document.addEventListener('visibilitychange', () => { if (document.hidden) releaseAll(); conn.park(document.hidden); });
function momentaryInput(input) {
  return [() => conn.call('sim_input', { input, active: true, lease_ms: 1500 }).catch(() => {}), () => conn.call('sim_input', { input, active: false }).catch(() => {})];
}

// ------------------------------------------------------------------ hooks

// Other parts of the page (record.js, an ES module loaded after this file) hear about these: `info` (the board's
// info arrived), `state` (a new state was drawn), `stop` (STOP was pressed), `drop` (the connection dropped),
// `script` (the Script tab's text was replaced from the board).
const pageHooks = { info: [], state: [], stop: [], drop: [], script: [] };
function hook(kind) {
  for (const f of pageHooks[kind]) { try { f(); } catch (e) { log(`${kind}: ${e.message}`); } }
}

// ------------------------------------------------------------------ state

let info = null, state = null, shows = [], params = null;
const has = (cap) => info?.capabilities?.includes(cap);

async function onConnected() {
  try {
    info = (await conn.call('info'));
    applyInfo();
    if (has('shows')) await loadShows();
    // after a reconnect, edits not saved yet stay on the form (the message offers the board's values)
    // a profile may give the remote trigger enable an alias: its switch is then found under that key (a board
    // without profiles answers bad_request; a firmware of its own may mark the setting with alias_of instead)
    remoteAlias = null;
    if (has('params')) { try { remoteAlias = ((await conn.call('get_profile')).aliases || []).find((a) => a.key === REMOTE)?.alias || null; } catch { /* no profiles */ } }
    if (has('params')) { if (params && paramsDirty()) paramsChanged('Reconnected: your changes are not saved yet. '); else await loadParams(); }
    if (has('script')) await loadScript();
    // a remote trigger enable given from this page ended with the old connection: read its value again
    if (remoteWanted && params) {
      const key = remoteKey();
      const v = (await conn.call('get_params')).values.find((x) => x.key === key);
      const mine = params.values.find((x) => x.key === key);
      if (v && mine) mine.value = v.value;
      renderRemote();
    }
  } catch (e) { log('connect: ' + e.message); }
}

function applyInfo() {
  // a start button held right now keeps its element: the redraw waits until it is let go (or fires)
  if (holding) { infoDeferred = true; return; }
  document.body.classList.add('ready');  // what the board has is known: its cards show (no flash of every card)
  releaseAll();  // the buttons are about to be redrawn: none may stay held down unseen
  $('dev-name').textContent = info.name;
  document.title = info.name;
  $('dev-sub').textContent = devSub();
  // the page password box only while the page lock is on (SM-P1)
  renderLock();
  for (const n of document.querySelectorAll('[data-cap]')) n.classList.toggle('hidden', !n.dataset.cap.split('|').some(has));
  if (info.stop_button === false) $('stop-all').classList.add('hidden');  // setting stop_button off: no STOP here
  $('outputs').replaceChildren();
  $('sim-hint').hidden = !has('sim');
  const acts = $('actions');
  // the firmware's own buttons first, "Save now" last
  const actions = [...(info.actions || [])].sort((a, b) => (a.key === 'save') - (b.key === 'save'));
  acts.replaceChildren(...actions.map((a) => {
    if (a.momentary) {
      const b = el('button', { text: a.label + ' (hold)' });
      momentary(b, () => conn.call('action', { key: a.key, down: true }).catch((e) => actionRefused(a, e)), () => conn.call('action', { key: a.key, down: false }).catch(() => {}));
      return b;
    }
    if (a.hold) return holdStart(a);
    return el('button', { text: a.label, 'data-key': a.key, onclick: async () => { if (a.confirm && !confirm(a.label + '?')) return; await call('action', { key: a.key }, 'actions-msg'); } });
  }));
  renderRemote();
  buildInputs();
  buildWiring();
  const kv = $('dev-info');
  kv.replaceChildren(...[['Id', info.id], ['Firmware', `${info.fw} ${info.fw_version}`], ['MCU', info.mcu], ['Protocol', 'v' + info.v], ['Capabilities', info.capabilities.join(', ')]].flatMap(([k, v]) => [el('div', { text: k }), el('div', { text: v })]));
  fillLink();
  renderFirmware();
  renderState();
  hook('info');
}

// A start button pressed and held (setting start_buttons = hold): it sends once the bar is full, after
// PAGE_START_HOLD_MS; let go early and nothing is sent. Released on blur, page hide and a lost connection (the
// momentary rules), and redrawn buttons let go.
const PAGE_START_HOLD_MS = 1000;
let holding = null, infoDeferred = false;  // the start button held now; a redraw that waits for it
function holdStart(a) {
  const fill = el('i', { class: 'fill' });
  const b = el('button', { class: 'hold-start', 'data-key': a.key, 'aria-label': `${a.label} (press and hold about a second)` }, el('span', { text: a.label }), fill);
  let timer = null, raf = 0, start = 0;
  const tick = () => { fill.style.width = `${Math.min(100, ((performance.now() - start) / PAGE_START_HOLD_MS) * 100)}%`; raf = requestAnimationFrame(tick); };
  const stop = () => {
    clearTimeout(timer); cancelAnimationFrame(raf); timer = null; fill.style.width = '0';
    if (holding !== b) return;
    holding = null;
    if (infoDeferred) { infoDeferred = false; setTimeout(() => { if (info) applyInfo(); }, 0); }
  };
  momentary(b, () => {
    if (timer || !b.isConnected) return;  // momentary() repeats down() while held: the first press starts the timer
    start = performance.now();
    holding = b;
    raf = requestAnimationFrame(tick);
    timer = setTimeout(() => { stop(); call('action', { key: a.key }, 'actions-msg'); }, PAGE_START_HOLD_MS);
  }, stop, false);
  return b;
}

// a press-and-hold action refused (once per press, not every 500 ms)
function actionRefused(a, e) {
  const t = `${a.label}: ${e.detail?.code === 'pin' ? pinRefused(e) : e.message}`;
  if ($('actions-msg').textContent !== t) showMsg('actions-msg', t, false);
}
// The remote trigger enable (a setting the built-in effect has) sits next to its Trigger button. Given from this
// page it lasts only while the page is connected (SM-R2): after a dropped connection the page says it is off
// again, with one tap to turn it back on (never by itself).
const REMOTE = 'remote_trigger_enable';
let remoteWanted = false;
// the remote trigger enable under its own key, a profile's alias (get_profile), or marked `alias_of` by the firmware
let remoteAlias = null;  // the profile's alias of REMOTE, read at every connect
const remoteParam = () => params?.params.find((x) => x.key === REMOTE || x.alias_of === REMOTE || (remoteAlias && x.key === remoteAlias));
const remoteKey = () => remoteParam()?.key || REMOTE;
function renderRemote() {
  const p = remoteParam();  // under an alias, the switch reads the alias's label
  let box = $('remote-box');
  if (!p || !(info?.actions || []).length) { box?.remove(); return; }
  const on = !!params.values.find((v) => v.key === p.key)?.value;
  if (!box) {
    box = el('label', { class: 'check remote', id: 'remote-box' }, el('input', { type: 'checkbox', id: 'remote-on' }), el('span', { id: 'remote-text' }));
    $('actions').append(box);
    $('remote-on').onchange = async (e) => {
      const want = e.target.checked;
      try { await call('set_params', { values: [{ key: p.key, value: want }] }, 'actions-msg'); } catch { e.target.checked = !want; return; }
      remoteWanted = want;
      showMsg('actions-msg', want ? `${p.label} on while this page stays connected` : `${p.label} off`, true);
    };
  } else if (!box.isConnected) $('actions').append(box);
  $('remote-on').checked = on;
  $('remote-text').textContent = p.label;
  if (remoteWanted && !on) {
    remoteWanted = false;
    const m = $('actions-msg');
    m.className = 'msg err';
    m.replaceChildren(`${p.label} is off again: the connection dropped, and it lasts only while this page is connected. `,
      el('button', { class: 'small', text: 'Turn it on again', onclick: () => { $('remote-on').checked = true; $('remote-on').onchange({ target: $('remote-on') }); } }));
  }
}

function onEvent(msg) {
  switch (msg.event) {
    case 'state': state = msg.data; renderState(); break;
    case 'notice': log(noticeText(msg.data)); logEv('st', noticeText(msg.data)); break;
    case 'shows_changed': if (has('shows')) loadShows(); break;
    case 'params_changed': if (has('params')) paramsChanged(); conn.call('info').then((i) => { info = i; applyInfo(); }).catch(() => {}); break;
    case 'log': log(msg.data); break;
    case 'script_changed': if (has('script')) loadScript(); break;
    case 'update': fwEvent(msg.data); break;
  }
}

function noticeText(n) {
  const name = (id) => shows.find((s) => s.id === id)?.name || `show ${id}`;
  switch (n.notice) {
    case 'started': return `▶ ${name(n.show)} (${n.cause.by}${n.cause.detail ? ' ' + tdisp(n.cause.detail) : ''})`;
    case 'ended': return `■ ${name(n.show)} ${n.reason}`;
    case 'ignored': return `· ${name(n.show)} ignored: ${n.why}`;
    case 'watchdog': return `! ${tdisp(n.channel)} switched off by its max-on watchdog`;
    case 'interlock': return `! ${tdisp(n.channel)} held off by an interlock`;
    case 'paused': return n.paused ? '‖ paused' : '▶ resumed';
    default: return JSON.stringify(n);
  }
}

function log(t) {
  const p = $('log');
  const line = new Date().toLocaleTimeString() + '  ' + t + '\n';
  p.textContent = (line + p.textContent).slice(0, 8000);
}

// a firmware status line in plain words: "cooldown remaining 2:50" (not "cooldown remaining ms 170650"),
// yes / no for true / false, "none" for nothing
// (a profile's label when it gives one; `unit: "ms"` marks a time left under any name: "Time left 2:33")
function statusLine({ key, value, label, unit }) {
  const ms = (/_ms$/.test(key) || unit === 'ms') && typeof value === 'number';
  let k = label || key.replace(/_ms$/, '').replace(/_/g, ' ');
  k = k.charAt(0).toUpperCase() + k.slice(1);
  const v = ms ? (value >= 60000 ? fmtClock(value) : `${(value / 1000).toFixed(1)} s`) : value === true ? 'yes' : value === false ? 'no' : value === '' || value == null ? 'none' : String(value);
  return [k, v];
}
const fmtClock = (ms) => { const t = Math.round(ms / 1000); return `${Math.floor(t / 60)}:${String(t % 60).padStart(2, '0')}`; };
// whole seconds rounded first: 119600 ms is "2m 0s", never "1m 60s"
const fmtMs = (ms) => { if (ms < 60000) return `${(ms / 1000).toFixed(1)} s`; const t = Math.round(ms / 1000); return `${Math.floor(t / 60)}m ${t % 60}s`; };

let lastRun = { at: -1, clock: '' };  // the last run's clock time, worked out once per run
function renderState() {
  if (!state) return;
  renderScriptState();
  // paused: say why and how to go on (a held pause input, or Resume here)
  $('banner').classList.toggle('hidden', !state.paused);
  if (state.paused) {
    const pin = terminals().find((t) => t.role === 'pause');
    const held = pin && state.inputs.find((i) => i.input === pin.id)?.active;
    $('banner-text').textContent = 'PAUSED: every output is off and triggers are ignored. ' + (held ? `Release the pause input ${tdisp(pin.id)} to go on.` : 'Resume lets the shows start again (nothing turns back on by itself).');
  }
  // status
  const lines = [
    ['Network', state.net?.mode ? `${state.net.mode} ${state.net.ip || ''} ${state.net.ssid || ''}`.trim() : '—'],
    ['Up', fmtMs(state.uptime_ms)],
  ];
  // the stored board package cannot be used: no pins are driven until a good one is sent (BOARD-PACKAGES.md)
  if (state.board_problem) lines.unshift(['Board package', state.board_problem]);
  // the run counters: tonight = since the board was powered on (a show night), lifetime = kept in flash
  if (typeof state.runs_since_power_on === 'number') {
    const rl = state.run_labels || {};  // a profile's names for them ("Presses tonight")
    lines.push([rl.runs_since_power_on || 'Runs tonight (since power on)', String(state.runs_since_power_on)], [rl.runs_lifetime || 'Lifetime runs', String(state.runs_lifetime ?? 0)]);
    // the state comes when something changes: a clock time stays right between updates, "2 min ago" would not
    if (typeof state.last_run_uptime_ms === 'number') {
      if (lastRun.at !== state.last_run_uptime_ms) lastRun = { at: state.last_run_uptime_ms, clock: new Date(Date.now() - Math.max(0, state.uptime_ms - state.last_run_uptime_ms)).toLocaleTimeString() };
      lines.push([rl.last_run_uptime_ms || 'Last run', lastRun.clock]);
    }
  }
  if (has('sound')) lines.push(['Sound', !state.sound_online ? 'player not answering' : state.sound_playing ? 'playing' : 'idle']);
  for (const s of state.status || []) lines.push(statusLine(s));
  $('status-lines').replaceChildren(...lines.flatMap(([k, v]) => [el('div', { text: k }), el('div', { text: v })]));
  renderWifiAddress();
  renderNetWhere();
  // a firmware update in progress (from any phone or the desktop app) is said on every page
  const up = state.update;
  $('update-line').hidden = !up || fw.sending;
  if (up && !fw.sending) $('update-line').textContent = up.phase === 'restarting' ? 'The firmware was updated: the board restarts now and comes back in a moment.' : `The firmware is being updated (${up.total ? Math.round(100 * up.done / up.total) : 0} %): the board restarts when it is done.`;
  // the built-in effect's Cancel only while a run is queued
  const cancel = document.querySelector('#actions button[data-key="cancel"]');
  if (cancel) cancel.hidden = !(state.status || []).some((x) => x.key === 'queued' && x.value === true);
  $('snd-state').textContent = has('sound') ? (state.sound_playing ? 'playing' : 'idle') : '';
  // shows
  const noShows = !(state.shows || []).length;
  $('live-shows').replaceChildren(...(noShows ? [el('p', { class: 'hint', text: NO_SHOWS })] : []), ...(state.shows || []).map((s) => el('div', { class: 'item' },
    el('div', {}, el('div', { class: 'name', text: s.name }), el('div', { class: 'meta', text: statusText(s) })),
    el('div', { class: 'row' },
      el('span', { class: 'badge ' + s.state, text: s.state === 'idle' ? 'ready' : s.state === 'disabled' ? 'off' : s.state }),
      s.state === 'running' ? el('button', { class: 'small', text: 'Stop', onclick: () => call('stop', { show: s.id }) })
        : el('button', { class: 'small', text: 'Run', onclick: () => call('run', { show: s.id }) })))));
  // inputs
  for (const i of state.inputs) {
    const c = document.getElementById('in-' + i.input);
    if (!c) continue;
    c.classList.toggle('active', i.active);
    c.classList.toggle('waiting', !!i.waiting);
    c.textContent = tdisp(i.input) + (i.waiting ? ' (release)' : '');
  }
  renderWiring();
  logDiff();
  // outputs
  renderOutputs();
  hook('state');
}

function buildInputs() {
  const names = info.inputs || [];
  $('inputs').replaceChildren(...names.map((n) => {
    const c = el('span', { class: 'chip', id: 'in-' + n, text: n });
    if (has('sim')) { c.tabIndex = 0; c.setAttribute('role', 'button'); c.setAttribute('aria-label', `${tdisp(n)}: press and hold to close it`); }
    if (has('sim')) momentary(c, ...momentaryInput(n));
    return c;
  }));
}

function statusText(s) {
  if (s.state === 'running') return `${fmtMs(s.elapsed_ms)} / ${fmtMs(s.length_ms)}`;
  if (s.state === 'cooldown') return `resting, ready in ${fmtMs(s.remaining_ms)}`;
  // shows allowed several runs per cooldown say how many are left
  if (s.runs_left != null && s.window_ms > 0) return `${s.state}, ${s.runs_left} left for ${fmtMs(s.window_ms)}`;
  return s.state === 'idle' ? 'waiting for its trigger' : s.state === 'disabled' ? 'turned off: it does not start (Shows, Edit, Enabled)' : s.state;
}

const pwm = (ch) => /^OUT/.test(ch);
function renderOutputs() {
  const host = $('outputs');
  if (host.children.length !== state.outputs.length) {
    host.replaceChildren(...state.outputs.map((o) => {
      // an output with a role or owned by the built-in effect is not tested by hand here: say what drives it
      const t = terminals().find((x) => x.id === o.channel);
      const owned = t && t.shows === false;
      const test = has('outputs') && !owned;
      const card = el('div', { class: 'out', id: 'out-' + o.channel },
        el('div', { class: 'head' }, el('span', { text: tdisp(o.channel) }), el('span', { class: 'v', text: '' })),
        el('div', { class: 'lvl' }, el('i')),
        el('div', { class: 'fault' }),
        owned ? el('div', { class: 'role', text: `role: ${ROLE_LABEL[t.role] || t.role || 'not for shows'} (Settings)` }) : null,
        !test ? null : el('div', { class: 'btns' },
          el('button', { text: 'On', 'aria-label': `${tdisp(o.channel)}: on`, onclick: () => call('set_output', { channel: o.channel, level: 1000 }) }),
          el('button', { text: 'Off', 'aria-label': `${tdisp(o.channel)}: off`, onclick: () => call('set_output', { channel: o.channel, level: 0 }) }),
          el('button', { text: 'Auto', title: 'release to the shows', 'aria-label': `${tdisp(o.channel)}: auto (the shows decide)`, onclick: () => call('set_output', { channel: o.channel, level: null }) })));
      if (pwm(o.channel) && test) {
        const r = el('input', { type: 'range', min: 0, max: 1000, step: 10, value: 0, 'aria-label': `${tdisp(o.channel)} level` });
        r.addEventListener('change', () => call('set_output', { channel: o.channel, level: +r.value }));
        // the slider follows the board (after STOP, a show), except while a finger is on it
        r.addEventListener('pointerdown', () => { r.dataset.busy = '1'; });
        r.addEventListener('pointerup', () => { setTimeout(() => { delete r.dataset.busy; }, 800); });
        card.insertBefore(r, card.querySelector('.btns'));
      }
      return card;
    }));
  }
  for (const o of state.outputs) {
    const c = $('out-' + o.channel);
    c.classList.toggle('manual', o.manual);
    c.querySelector('.v').textContent = pwm(o.channel) ? Math.round(o.level / 10) + '%' : (o.level ? 'ON' : 'off');
    c.querySelector('.lvl i').style.width = o.level / 10 + '%';
    const r = c.querySelector('input[type=range]');
    if (r && !r.dataset.busy && document.activeElement !== r) r.value = o.level;
    const f = c.querySelector('.fault');
    f.textContent = o.fault ? 'safety: ' + o.fault.replace(/_/g, ' ') : (o.manual ? 'held by this test' : '');
    f.classList.toggle('note', !o.fault);  // a manual hold is not a fault: not in red
  }
}

// ------------------------------------------------------------------ live controls

// STOP says what happened: it goes over the WebSocket and, when that has not answered within 1.5 s, also over
// plain HTTP (stopping twice is harmless); no answer either way is said plainly.
async function stopAll() {
  hook('stop');  // the recorder's playback ends first: nothing is turned back on after STOP
  stopLine('Stopping…', '');
  const ok = (p) => p.then(() => true, () => false);
  const http = () => ok(conn.http({ v: 1, op: 'stop_all' }));
  let done;
  if (conn.open) {
    const ws = conn.call('stop_all');
    done = await Promise.race([ok(ws), new Promise((r) => setTimeout(() => r(null), 1500))]);
    if (done === null) done = await Promise.any([ws, conn.http({ v: 1, op: 'stop_all' })]).then(() => true, () => false);
    else if (!done) done = await http();
  } else done = await http();
  if (done) stopLine('Stopped ✓ every show and output is off', 'ok');
  else stopLine('STOP not confirmed: the board is not answering. Unplug the prop or use its switch.', 'err');
  return done;
}
function stopLine(text, cls) {
  const l = $('stop-line');
  l.textContent = text;
  l.className = 'stopline ' + cls;
  l.hidden = false;
  clearTimeout(stopLine.t);
  if (cls === 'ok') stopLine.t = setTimeout(() => { l.hidden = true; }, 6000);
}
$('stop-all').onclick = stopAll;
$('resume').onclick = async () => {
  const b = $('resume');
  b.disabled = true;
  try { await call('pause', { on: false }); } catch { /* call() said why (the page lock: Unlock) */ } finally { b.disabled = false; }
};
$('snd-play').onclick = () => call('sound', { cmd: 'play_mp3', track: +$('snd-track').value });
$('snd-stop').onclick = () => call('sound', { cmd: 'stop' });
$('snd-vol').onchange = () => call('sound', { cmd: 'volume', volume: +$('snd-vol').value });
$('px-go').onclick = () => {
  const color = $('px-color').value, e = $('px-effect').value;
  const a = { fill: { do: 'fill', color }, flicker: { do: 'flicker', color }, chase: { do: 'chase', color, step_ms: 60, width: 3 },
    flash: { do: 'flash', color, period_ms: 400 }, rainbow: { do: 'rainbow', period_ms: 3000 }, fade: { do: 'fade', color, ms: 1500 } }[e];
  call('pixel', { action: a });
};
$('px-off').onclick = () => call('pixel', { action: null });

for (const b of document.querySelectorAll('#tabs button')) {
  b.setAttribute('role', 'tab');
  b.setAttribute('aria-controls', 'tab-' + b.dataset.tab);
  b.onclick = () => {
    if (!b.classList.contains('on')) window.scrollTo(0, 0);  // a new tab starts at its top
    for (const x of document.querySelectorAll('#tabs button')) { x.classList.toggle('on', x === b); x.setAttribute('aria-selected', String(x === b)); x.tabIndex = x === b ? 0 : -1; }
    for (const t of document.querySelectorAll('.tab')) t.classList.toggle('on', t.id === 'tab-' + b.dataset.tab);
    store.set('tab', b.dataset.tab);
    b.scrollIntoView({ block: 'nearest', inline: 'nearest' });
  };
}
// arrow keys move between tabs (the tablist pattern)
$('tabs').addEventListener('keydown', (e) => {
  if (e.key !== 'ArrowRight' && e.key !== 'ArrowLeft') return;
  const bs = [...document.querySelectorAll('#tabs button')].filter((x) => x.offsetParent);
  const i = bs.indexOf(document.activeElement);
  if (i < 0) return;
  const n = bs[(i + (e.key === 'ArrowRight' ? 1 : bs.length - 1)) % bs.length];
  n.click(); n.focus();
});
for (const t of document.querySelectorAll('.tab')) t.setAttribute('role', 'tabpanel');
// messages are read out when they change
for (const m of document.querySelectorAll('.msg')) if (!m.getAttribute('role')) m.setAttribute('role', 'status');
{ const t = location.hash.slice(1) || store.get('tab', 'live'); const b = document.querySelector(`#tabs button[data-tab="${t}"]`); if (b) b.click(); }

// ------------------------------------------------------------------ shows and the editor

// the board's own terminals (info.terminals), so a board with other inputs and outputs needs no change here
const outputIds = () => terminals().filter((t) => t.kind === 'output').map((t) => t.id);
const inputIds = () => terminals().filter((t) => t.kind === 'input').map((t) => t.id);
const targetIds = () => [...outputIds(), 'PIXEL', 'SOUND', 'NET'];
// action → [field, default]; the first entry decides where it can be used
const ACTIONS = {
  on: [], off: [], level: [['level', 500]], ramp: [['to', 1000], ['ms', 300]], pulse: [['ms', 500]],
  blink: [['on_ms', 250], ['off_ms', 250], ['count', 0]],
  // play: folder 0 is sound N (track N: the card's mp3/NNNN-….mp3, as in a script); a folder 1-99 is shown only on
  // an older cue that has one
  play: [['folder', 0], ['track', 1]], loop_folder: [['folder', 1]], volume: [['volume', 20]],
  fill: [['color', '#ff6000']], chase: [['color', '#ff6000'], ['step_ms', 60], ['width', 3]], rainbow: [['period_ms', 3000]],
  flash: [['color', '#ffffff'], ['period_ms', 400]], fade: [['color', '#ff6000'], ['ms', 1500]], flicker: [['color', '#ff6000']],
  emit: [['cue', 'next']],
};
function actionsFor(target) {
  if (target === 'SOUND') return ['play', 'loop_folder', 'volume', 'off'];
  if (target === 'PIXEL') return ['fill', 'flicker', 'chase', 'flash', 'fade', 'rainbow', 'off'];
  if (target === 'NET') return ['emit'];
  return pwm(target) ? ['on', 'off', 'pulse', 'blink', 'level', 'ramp'] : ['on', 'off', 'pulse', 'blink'];
}
const TRIGGERS = { input: [['input', 'TRIG1']], release: [['input', 'TRIG1']], every: [['period_ms', 60000]], boot: [['delay_ms', 0]], cue: [['name', 'go']], after: [['show', 1]] };

const NO_SHOWS = 'No shows yet: nothing runs when an input is pressed. Add one on the Shows tab (+ New, Link, or Add an example show), or record an effect on the Record effect tab.';

// An in-page "are you sure": the button makes way for the question, a Yes button and Cancel (no window.confirm).
function askInline(btn, question, yesText, onYes) {
  const box = el('span', { class: 'ask' });
  const back = () => { if (box.isConnected) box.replaceWith(btn); if (renderShowList.later) renderShowList(); };
  box.append(el('span', { class: 'q', text: question }),
    el('button', { class: 'danger small ask-yes', text: yesText, onclick: async () => { back(); await onYes(); } }),
    el('button', { class: 'small ask-no', text: 'Cancel', onclick: back }));
  btn.replaceWith(box);
  setTimeout(back, 8000);
}
async function deleteShow(id, name, msgEl) {
  await call('delete_show', { show: id }, msgEl);
  if (cur && cur.id === id) { $('editor').classList.add('hidden'); cur = null; }
  showMsg(msgEl, `deleted "${name}"`, true);
}
const showLabel = (s) => `${s.name} (#${s.id})`;

// the list waits while a question is asked in it (Delete?), so a change from another phone does not take it away
function renderShowList() {
  renderShowList.later = !!$('show-list').querySelector('.ask');
  if (renderShowList.later) return;
  $('show-list').replaceChildren(...(shows.length ? [] : [el('p', { class: 'hint', text: NO_SHOWS })]), ...shows.map((s) => {
    const del = el('button', { class: 'small', text: 'Delete' });
    del.onclick = () => askInline(del, `Delete "${s.name}"?`, 'Delete', () => deleteShow(s.id, s.name, 'shows-msg'));
    return el('div', { class: 'item', 'data-id': s.id },
      el('div', {}, el('div', { class: 'name', text: s.name }), el('div', { class: 'meta', text: `#${s.id} · ${s.tracks} tracks · ${fmtMs(s.length_ms)}${s.enabled ? '' : ' · disabled'}` })),
      el('div', { class: 'acts' }, el('button', { class: 'small', text: 'Edit', onclick: () => openShow(s.id) }), del));
  }));
}
async function loadShows() {
  const r = await conn.call('list_shows');
  shows = r.shows;
  renderShowList();
  fillSaveTo($('link-to'), null);
  if (cur) fillSaveTo($('ed-save-to'), cur);
  checkOverlap();
}
$('add-example').onclick = async () => {
  try {
    const r = await call('add_example', {});
    showMsg('shows-msg', `added "${r.data.name}" (#${r.data.id}): TRIG1 pulses OUT1 for 2 s`, true);
  } catch (e) { showMsg('shows-msg', e.message, false); }
};

// "Save as": this show, a new show, or replace an existing one (its id is overwritten)
function fillSaveTo(sel, show) {
  if (!sel) return;
  const keep = sel.value;
  const opts = [];
  if (show && shows.some((s) => s.id === show.id)) opts.push(el('option', { value: 'self', text: `this show (#${show.id})` }));
  opts.push(el('option', { value: 'new', text: 'a new show' }));  // the board gives it its number
  for (const s of shows) if (!show || s.id !== show.id) opts.push(el('option', { value: String(s.id), text: `replace: ${showLabel(s)}` }));
  sel.replaceChildren(...opts);
  sel.value = [...sel.options].some((o) => o.value === keep) ? keep : sel.options[0].value;
  sel.onchange?.();
}
// the id to save under, or 0 for a new show (put_show with new: the board picks the id)
function saveToId(sel, show) {
  const v = sel.value;
  if (v === 'self') return show.id;
  if (v === 'new') return 0;
  return +v;
}
async function putShow(data, id) {
  const r = await conn.call('put_show', id ? { data: { ...data, id } } : { data: { ...data, id: 0 }, new: true });
  return r?.data || { ...data, id };
}

// cur: the show in the editor; curBase: it as opened or last saved (unsaved edits = a difference); curStored: the
// board's copy when opened (saving over it checks nobody changed it meanwhile)
let cur = null, curBase = '', curStored = '';
const edDirty = () => !!cur && JSON.stringify(cur) !== curBase;
async function openShow(id) {
  // the open editor takes no typing until the chosen show has arrived (it would be overwritten)
  $('editor').inert = true;
  try {
    const r = await call('get_show', { show: id });
    edit(r.data);
    curStored = JSON.stringify(r.data);
  } finally { $('editor').inert = false; }
}
$('new-show').onclick = () => {
  edit({ id: 0, name: 'New show', enabled: true, triggers: [{ on: 'input', input: 'TRIG1' }], cooldown_ms: 0, retrigger: 'ignore', looping: false, priority: 1, stop_sound: true, tracks: [{ target: 'OUT1', cues: [{ at: 0, do: 'pulse', ms: 1000 }] }] });
};

function edit(show) {
  cur = JSON.parse(JSON.stringify(show));
  curBase = JSON.stringify(cur);
  curStored = '';
  $('editor').classList.remove('hidden');
  $('ed-msg').textContent = '';
  fillSaveTo($('ed-save-to'), cur);
  renderEditor();
  $('editor').scrollIntoView({ behavior: smooth() });
}

function bindNum(id, get, set) { const e = $(id); e.value = get() ?? ''; e.oninput = () => { set(e.value === '' ? null : +e.value); drawTimeline(); syncJson(); }; }
// the show editor shows seconds; the show itself keeps milliseconds
const toS = (ms) => (ms == null ? null : ms / 1000);
const toMs = (s) => (s == null ? null : Math.round(s * 1000));

function renderEditor() {
  $('ed-title').textContent = cur.id ? `${cur.name} (#${cur.id})` : `${cur.name} (not saved yet)`;
  $('ed-name').value = cur.name; $('ed-name').oninput = (e) => { cur.name = e.target.value; syncJson(); };
  bindNum('ed-cooldown', () => toS(cur.cooldown_ms), (v) => cur.cooldown_ms = toMs(v) || 0);
  bindNum('ed-runs', () => cur.runs_per_cooldown ?? 1, (v) => { if (!v || v === 1) delete cur.runs_per_cooldown; else cur.runs_per_cooldown = v; });
  bindNum('ed-priority', () => cur.priority, (v) => cur.priority = v ?? 1);
  bindNum('ed-length', () => toS(cur.length_ms), (v) => { if (v == null) delete cur.length_ms; else cur.length_ms = toMs(v); });
  $('ed-retrigger').value = cur.retrigger || 'ignore'; $('ed-retrigger').onchange = (e) => { cur.retrigger = e.target.value; syncJson(); };
  $('ed-enabled').checked = cur.enabled !== false; $('ed-enabled').onchange = (e) => { cur.enabled = e.target.checked; syncJson(); };
  $('ed-looping').checked = !!cur.looping; $('ed-looping').onchange = (e) => { cur.looping = e.target.checked; syncJson(); };
  renderTriggers();
  renderTracks();
  const used = new Set(cur.tracks.map((t) => t.target));
  const reserved = new Set(terminals().filter((t) => t.kind === 'output' && t.shows === false).map((t) => t.id));
  $('ed-new-target').replaceChildren(...targetIds().filter((t) => !used.has(t) && !reserved.has(t) && (t !== 'SOUND' || has('sound')) && (t !== 'PIXEL' || has('pixels'))).map((t) => el('option', { value: t, text: tdisp(t) })));
  $('ed-reserved').textContent = reserved.size ? `${[...reserved].map(tdisp).join(', ')} are not available to shows (an output role, or the built-in effect: Settings).` : '';
  drawTimeline();
  syncJson();
}

// plain words for the show format's keywords (the stored words stay the format's)
const TRIGGER_WORDS = { input: 'an input is pressed', release: 'an input is let go', every: 'every so often', boot: 'power-on', cue: 'a cue arrives', after: 'another show ends' };
const ACTION_WORDS = { on: 'on', off: 'off', level: 'set a level', ramp: 'fade to a level', pulse: 'pulse', blink: 'blink', play: 'play a sound',
  loop_folder: 'loop a folder', volume: 'set the volume', fill: 'one colour', flicker: 'flicker', chase: 'chase', flash: 'flash', fade: 'fade in', rainbow: 'rainbow', emit: 'send a cue' };
const FIELD_WORDS = { level: 'level (%)', to: 'to (%)', ms: 'time (s)', on_ms: 'on for (s)', off_ms: 'off for (s)', count: 'times (0 = no end)', period_ms: 'every (s)',
  delay_ms: 'after (s)', step_ms: 'step (s)', name: 'cue name', cue: 'cue name', show: 'show number', input: 'input', color: 'colour', track: 'sound' };
function field(obj, key, def, onchange) {
  if (obj[key] === undefined) obj[key] = def;
  let input;
  if (key === 'input') input = el('select', {}, ...inputIds().map((i) => el('option', { value: i, text: tdisp(i) })));
  else if (key === 'color') input = el('input', { type: 'color' });
  else if (typeof def === 'number') input = el('input', { type: 'number', min: 0 });
  else input = el('input', {});
  const secs = /(^|_)ms$/.test(key);  // a time: shown in seconds
  const pct = key === 'level' || key === 'to';  // an output level: shown in %, kept in permille
  const v = obj[key] ?? def;  // a cue without this key shows its default, not NaN
  input.value = v == null ? '' : secs ? v / 1000 : pct ? v / 10 : v;
  if (secs) input.step = 0.1;
  if (pct) { input.max = 100; input.step = 1; }
  input.oninput = () => { obj[key] = secs ? toMs(+input.value) : pct ? Math.round(+input.value * 10) : typeof def === 'number' ? +input.value : input.value; onchange(); };
  const label = FIELD_WORDS[key] || (secs ? key.replace(/_ms$/, '').replace(/_/g, ' ') + ' (s)' : key.replace(/_/g, ' '));
  return el('label', {}, label, input);
}

function renderTriggers() {
  $('ed-triggers').replaceChildren(...cur.triggers.map((t, i) => {
    const sel = el('select', {}, ...Object.keys(TRIGGERS).map((k) => el('option', { value: k, text: TRIGGER_WORDS[k] || k })));
    sel.value = t.on;
    sel.onchange = () => { cur.triggers[i] = { on: sel.value }; for (const [k, d] of TRIGGERS[sel.value]) cur.triggers[i][k] = d; renderTriggers(); syncJson(); };
    const held = t.on === 'input' ? el('label', { class: 'check', title: 'The show ends when the input is let go (its length is the most it runs)' },
      el('input', { type: 'checkbox', class: 'until-release', checked: !!t.until_release, onchange: (e) => { if (e.target.checked) t.until_release = true; else delete t.until_release; syncJson(); } }), 'until released') : null;
    return el('div', { class: 'trig' }, el('label', {}, 'when', sel), ...TRIGGERS[t.on].map(([k, d]) => field(t, k, d, syncJson)), held,
      el('button', { class: 'small', text: '✕', 'aria-label': 'Remove this trigger', title: 'Remove this trigger', onclick: () => { cur.triggers.splice(i, 1); renderTriggers(); syncJson(); } }));
  }));
}
$('ed-add-trigger').onclick = () => { cur.triggers.push({ on: 'input', input: 'TRIG1' }); renderTriggers(); syncJson(); };

function renderTracks() {
  $('ed-tracks').replaceChildren(...cur.tracks.map((t, ti) => {
    const box = el('div', { class: 'track' },
      el('div', { class: 'thead' }, el('span', { text: tdisp(t.target) }),
        el('div', { class: 'row' },
          el('button', { class: 'small', text: '+ cue', onclick: () => { const last = t.cues.at(-1); const a = actionsFor(t.target)[0]; const c = { at: last ? last.at + 500 : 0, do: a }; for (const [k, d] of ACTIONS[a]) c[k] = d; t.cues.push(c); renderEditor(); } }),
          el('button', { class: 'small', text: 'remove', onclick: () => { cur.tracks.splice(ti, 1); renderEditor(); } }))));
    t.cues.forEach((c, ci) => {
      const sel = el('select', {}, ...actionsFor(t.target).map((a) => el('option', { value: a, text: ACTION_WORDS[a] || a })));
      sel.value = c.do;
      sel.onchange = () => { const nc = { at: c.at, do: sel.value }; for (const [k, d] of ACTIONS[sel.value]) nc[k] = d; t.cues[ci] = nc; renderEditor(); };
      const at = el('input', { type: 'number', min: 0, step: 0.1, value: c.at / 1000 });
      at.oninput = () => { c.at = toMs(+at.value); drawTimeline(); syncJson(); };
      box.append(el('div', { class: 'cue' }, el('label', {}, 'at (s)', at), el('label', {}, 'do', sel),
        el('div', { class: 'params' }, ...(ACTIONS[c.do] || []).filter(([k]) => !(c.do === 'play' && k === 'folder' && !c.folder)).map(([k, d]) => field(c, k, d, () => { drawTimeline(); syncJson(); }))),
        el('button', { class: 'small', text: '✕', 'aria-label': 'Remove this cue', title: 'Remove this cue', onclick: () => { t.cues.splice(ci, 1); renderEditor(); } })));
    });
    return box;
  }));
}
$('ed-add-track').onclick = () => { const tg = $('ed-new-target').value; if (!tg) return; const a = actionsFor(tg)[0]; const c = { at: 0, do: a }; for (const [k, d] of ACTIONS[a]) c[k] = d; cur.tracks.push({ target: tg, cues: [c] }); renderEditor(); };

function cueDuration(c) {
  switch (c.do) { case 'ramp': case 'pulse': case 'fade': return c.ms || 0; case 'blink': return (c.on_ms + c.off_ms) * (c.count || 0); default: return 0; }
}
function showLength(s) {
  if (s.length_ms != null) return s.length_ms;
  let m = 0;
  for (const t of s.tracks) for (const c of t.cues) m = Math.max(m, c.at + cueDuration(c));
  return m;
}
// same rules as the engine (proptroller-core show::Compiled::level_at)
function levelAt(cues, t) {
  const sorted = [...cues].sort((a, b) => a.at - b.at);
  let i = -1;
  for (let k = 0; k < sorted.length; k++) if (sorted[k].at <= t) i = k;
  if (i < 0) return null;
  const c = sorted[i], dt = t - c.at;
  switch (c.do) {
    case 'on': return 1000;
    case 'off': return 0;
    case 'level': return Math.min(1000, c.level);
    case 'ramp': { const from = i > 0 ? (levelAt(sorted.slice(0, i), c.at) ?? 0) : 0; return !c.ms || dt >= c.ms ? c.to : from + (c.to - from) * dt / c.ms; }
    case 'pulse': return dt < c.ms ? 1000 : 0;
    case 'blink': { const per = Math.max(1, c.on_ms + c.off_ms); if (c.count > 0 && dt >= per * c.count) return 0; return dt % per < c.on_ms ? 1000 : 0; }
    default: return 0;
  }
}

function drawTimeline() {
  const cv = $('timeline');
  if (!cur || !cv) return;
  // the picture in words, for a screen reader
  cv.setAttribute('aria-label', `Timeline, ${fmtMs(showLength(cur))} long: ` + (cur.tracks.map((t) => `${tdisp(t.target)} ${t.cues.length} cue${t.cues.length === 1 ? '' : 's'}`).join(', ') || 'no tracks'));
  const dpr = window.devicePixelRatio || 1;
  const rows = Math.max(1, cur.tracks.length), rowH = 26, labelW = (cv.clientWidth || 600) > 320 ? 96 : 54;
  const w = cv.clientWidth || 600, h = rows * rowH + 22;
  cv.width = w * dpr; cv.height = h * dpr; cv.style.height = h + 'px';
  const g = cv.getContext('2d');
  g.scale(dpr, dpr);
  const css = getComputedStyle(document.documentElement);
  const col = (v) => css.getPropertyValue(v).trim();
  g.fillStyle = col('--bg'); g.fillRect(0, 0, w, h);
  const len = Math.max(1000, showLength(cur)) * 1.05;
  const x = (t) => labelW + (w - labelW - 8) * t / len;
  g.font = '11px system-ui'; g.fillStyle = col('--dim'); g.strokeStyle = col('--line');
  const step = [100, 250, 500, 1000, 2000, 5000, 10000, 30000, 60000, 300000].find((s) => len / s < 10) || 600000;
  for (let t = 0; t <= len; t += step) { g.beginPath(); g.moveTo(x(t), 0); g.lineTo(x(t), h - 16); g.stroke(); g.fillText(fmtMs(t), x(t) + 2, h - 4); }
  cur.tracks.forEach((tr, r) => {
    const y0 = r * rowH + 4;
    g.fillStyle = col('--text');
    const nm = tdisp(tr.target);
    g.fillText(g.measureText(nm).width <= labelW - 6 ? nm : tr.target, 4, y0 + 15);  // the name from Settings when it fits
    if (outputIds().includes(tr.target)) {
      g.fillStyle = col('--on');
      const n = 300;
      for (let k = 0; k < n; k++) {
        const t0 = len * k / n, l = levelAt(tr.cues, t0);
        if (l) { const hh = (rowH - 8) * l / 1000; g.fillRect(x(t0), y0 + rowH - 6 - hh, (w - labelW) / n + 1, hh); }
      }
    } else {
      for (const c of tr.cues) {
        g.fillStyle = c.do === 'off' ? col('--dim') : (c.color || col('--accent'));
        g.fillRect(x(c.at) - 2, y0 + 2, 4, rowH - 8);
        g.fillStyle = col('--dim');
        g.fillText(c.do === 'play' ? (c.folder ? `${c.folder}/${c.track}` : `sound ${c.track}`) : c.do === 'emit' ? c.cue : c.do, x(c.at) + 4, y0 + 13);
      }
    }
  });
  if (state) {
    const s = state.shows?.find((x) => x.id === cur.id && x.state === 'running');
    if (s) { g.strokeStyle = col('--ok'); g.lineWidth = 2; g.beginPath(); g.moveTo(x(s.elapsed_ms), 0); g.lineTo(x(s.elapsed_ms), h - 16); g.stroke(); g.lineWidth = 1; }
  }
}
setInterval(() => { if (cur && state?.shows?.some((s) => s.id === cur.id && s.state === 'running')) drawTimeline(); }, 200);
window.addEventListener('resize', drawTimeline);

function syncJson() { if (cur) $('ed-json').value = JSON.stringify(cur, null, 2); }
// the JSON box: a whole show (name, triggers, tracks with cues), checked before the editor takes it; the number
// stays this show's (the board numbers shows: Save as picks another)
$('ed-json-apply').onclick = () => {
  let j;
  try { j = JSON.parse($('ed-json').value); } catch (e) { return showMsg('ed-msg', 'not JSON: ' + e.message, false); }
  const okShape = j && typeof j === 'object' && typeof j.name === 'string' && Array.isArray(j.triggers) && Array.isArray(j.tracks)
    && j.tracks.every((t) => t && typeof t.target === 'string' && Array.isArray(t.cues));
  if (!okShape) return showMsg('ed-msg', 'a show needs "name", "triggers": [...] and "tracks": [{"target": ..., "cues": [...]}]: nothing applied', false);
  cur = { ...j, id: cur.id };
  renderEditor();
  showMsg('ed-msg', 'JSON applied (not saved yet)', true);
};
async function saveShow(overwrite) {
  const sel = $('ed-save-to');
  const to = saveToId(sel, cur);
  const replacing = sel.value !== 'self' && sel.value !== 'new';
  // saving over this show: only if nobody changed it on another phone since it was opened
  if (sel.value === 'self' && !overwrite && curStored) {
    const g = await conn.call('get_show', { show: cur.id }).catch(() => null);
    if (g && JSON.stringify(g.data) !== curStored) {
      const m = $('ed-msg');
      m.className = 'msg err';
      m.replaceChildren(`"${g.data.name}" was changed on another phone since you opened it. `,
        el('button', { class: 'small', text: 'Load theirs', onclick: () => { edit(g.data); curStored = JSON.stringify(g.data); } }),
        ' ', el('button', { class: 'small danger', text: 'Save mine over it', onclick: () => saveShow(true) }));
      return;
    }
  }
  try {
    const data = await putShow(cur, to);
    cur = data;
    curBase = JSON.stringify(cur);
    curStored = JSON.stringify((await conn.call('get_show', { show: data.id }).catch(() => null))?.data || '');
    showMsg('ed-msg', replacing ? `saved over show #${data.id}` : `saved as show #${data.id}`, true);
    renderEditor();  // the editor now works on the stored show (its new number)
    await loadShows();
    sel.value = 'self';
    sel.onchange?.();
  } catch (e) {
    // the board says what is wrong and what to change, naming the output and the time (call() showed it)
    if (!e.shown) showMsg('ed-msg', e.message, false);
  }
}
$('ed-save-to').onchange = () => {
  const v = $('ed-save-to').value;
  const s = shows.find((x) => String(x.id) === v);
  $('ed-save').textContent = s ? `Replace "${s.name}"` : v === 'new' && cur && shows.some((x) => x.id === cur.id) ? 'Save as new' : 'Save';
};
$('ed-save').onclick = () => {
  const s = shows.find((x) => String(x.id) === $('ed-save-to').value);
  // replacing another show: ask first, in the page
  if (s) askInline($('ed-save'), `Replace "${s.name}"?`, 'Replace', () => saveShow(false));
  else saveShow(false);
};
$('ed-delete').onclick = () => {
  if (!shows.some((s) => s.id === cur.id)) { $('editor').classList.add('hidden'); cur = null; return; }  // never saved
  askInline($('ed-delete'), `Delete "${cur.name}"?`, 'Delete', () => deleteShow(cur.id, cur.name, 'ed-msg'));
};
const closeEditor = () => { $('editor').classList.add('hidden'); cur = null; };
$('ed-cancel').onclick = () => (edDirty() ? askInline($('ed-cancel'), 'Close without saving?', 'Close', closeEditor) : closeEditor());
// Run plays the stored show: say so while the editor holds changes
$('ed-run').onclick = async () => {
  if (!shows.some((s) => s.id === cur.id)) return showMsg('ed-msg', 'not saved yet: Save it first, then Run', false);
  try { await call('run', { show: cur.id }, 'ed-msg'); } catch { return; }
  if (edDirty()) showMsg('ed-msg', 'running the saved show: Save first to try your changes', true);
};
$('ed-stop').onclick = () => call('stop', { show: cur.id }, 'ed-msg');

// ------------------------------------------------------------------ link: outputs follow an input

const linkMode = () => document.querySelector('input[name=link-mode]:checked')?.value || 'held';
function fillLink() {
  const ins = $('link-input');
  const keepIn = ins.value;
  ins.replaceChildren(...inputIds().map((i) => el('option', { value: i, text: tdisp(i) })));
  if (keepIn) ins.value = keepIn;
  const reserved = new Set(terminals().filter((t) => t.kind === 'output' && t.shows === false).map((t) => t.id));
  const picked = new Set([...document.querySelectorAll('#link-outputs input:checked')].map((c) => c.value));
  $('link-outputs').replaceChildren(...outputIds().filter((c) => !reserved.has(c)).map((c) => {
    const cb = el('input', { type: 'checkbox', value: c, checked: picked.has(c) });
    const chip = el('label', { class: 'chip pick' + (picked.has(c) ? ' on' : ''), 'data-ch': c }, cb, tdisp(c));
    cb.onchange = () => chip.classList.toggle('on', cb.checked);
    return chip;
  }));
  linkModeChanged();
}
function linkModeChanged() {
  const held = linkMode() === 'held';
  const secs = $('link-secs');
  $('link-secs-text').textContent = held ? 'At most (s)' : 'On for (s)';
  if (held && secs.dataset.mode !== 'held') secs.value = 60;
  if (!held && secs.dataset.mode !== 'timed') secs.value = 2;
  secs.dataset.mode = held ? 'held' : 'timed';
  const user = /^USER/.test($('link-input').value);
  $('link-hint').textContent = (held
    ? 'The outputs are on while the input is held and go off when it is let go, or after the time above if it stays held.'
    : 'Each press turns the outputs on for that time; presses while they are on are ignored.')
    + (user ? ' USER buttons start shows only while Settings \u2192 buttons is "triggers" (in "learn" they are the record buttons).' : '');
}
for (const r of document.querySelectorAll('input[name=link-mode]')) r.onchange = linkModeChanged;
$('link-input').onchange = linkModeChanged;
function linkShow(id) {
  const input = $('link-input').value;
  const outs = [...document.querySelectorAll('#link-outputs input:checked')].map((c) => c.value);
  const ms = Math.round(+$('link-secs').value * 1000);
  const held = linkMode() === 'held';
  const show = {
    id, name: `Link: ${input} -> ${outs.join(', ')}`.slice(0, 40), enabled: true,
    triggers: [held ? { on: 'input', input, until_release: true } : { on: 'input', input }],
    cooldown_ms: 0, retrigger: 'ignore', looping: false, priority: 1, stop_sound: true,
    tracks: outs.map((target) => ({ target, cues: held ? [{ at: 0, do: 'on' }] : [{ at: 0, do: 'pulse', ms }] })),
  };
  if (held) show.length_ms = ms;
  return { show, outs, ms };
}
$('link-open').onclick = () => {
  fillLink();
  fillSaveTo($('link-to'), null);
  $('link-msg').textContent = '';
  $('link').classList.remove('hidden');
  $('link').scrollIntoView({ behavior: smooth() });
};
$('link-cancel').onclick = () => $('link').classList.add('hidden');
async function saveLink() {
  const { show } = linkShow(0);
  try {
    const d = await putShow(show, saveToId($('link-to'), null));
    showMsg('link-msg', `saved "${d.name}" as show #${d.id}`, true);
    await loadShows();
  } catch (e) { showMsg('link-msg', e.message, false); }
}
$('link-save').onclick = () => {
  const { outs, ms } = linkShow(0);
  if (!outs.length) return showMsg('link-msg', 'pick at least one output', false);
  if (!(ms >= 100)) return showMsg('link-msg', 'the time must be at least 0.1 s', false);
  const s = shows.find((x) => String(x.id) === $('link-to').value);
  if (s) askInline($('link-save'), `Replace "${s.name}"?`, 'Replace', saveLink);
  else saveLink();
};

// ------------------------------------------------------------------ settings

// Plain words for a choice's options, taken from the board's own help for that setting ("switch: switch, button or
// relay contact (closes to ground). sensor_high: powered sensor ...", "cycle = ...; timed = ..."). The stored value
// stays the option itself; an option the help does not describe shows as it is.
function optionWords(p) {
  const out = {};
  for (const o of p.options || []) {
    const esc = o.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
    const m = (p.help || '').match(new RegExp(`(?:^|[.;]\\s*)${esc}\\s*[:=]\\s*([^.;]+)`));
    if (!m) continue;
    let w = m[1].trim();
    if (w.length > 55) { const c = w.lastIndexOf(',', 55); if (c > 10) w = w.slice(0, c); }
    out[o] = w.charAt(0).toUpperCase() + w.slice(1);
  }
  return out;
}

// Times the board keeps in milliseconds are shown and typed in seconds (as everywhere else on the page).
const isMs = (p) => p.type === 'int' && p.unit === 'ms' && /_ms$/.test(p.key);  // a profile's own unit (an alias) stays as it is
const fromMs = (v) => (v == null ? '' : String(v / 1000));
// options with no words in the help read without underscores ("trigger repeat")
const plain = (o) => o.replace(/_/g, ' ');
// groups open on the page: the first one and the ones opened before (this phone remembers)
const openGroups = () => { try { return new Set(JSON.parse(store.get('pgroups', 'null')) || []); } catch { return new Set(); } };
async function loadParams() { renderParams(await conn.call('get_params')); }
// What a field shows for the board's value (the form keeps it in data-orig).
const shownValue = (p, v) => (p.type === 'bool' ? String(!!v) : isMs(p) ? fromMs(v) : String(v ?? ''));
// Does the board hold values other than the ones the form was drawn from? (A save's own params_changed event
// can arrive after the save has read the values back: then nothing differs.)
function paramsDiffer(got) {
  for (const i of document.querySelectorAll('#params [data-key]')) {
    if (i.dataset.secret) continue;
    const p = got.params.find((x) => x.key === i.dataset.key);
    if (!p) return true;
    if (shownValue(p, got.values.find((v) => v.key === p.key)?.value) !== i.dataset.orig) return true;
  }
  return got.params.length !== document.querySelectorAll('#params [data-key]').length;
}
function renderParams(got) {
  params = got;
  renderRemote();
  // the units line says what the boxes take: a time kept in ms (a profile's or a firmware's own unit) is typed in ms
  const inMs = params.params.some((p) => p.type === 'int' && p.unit === 'ms' && !isMs(p));
  $('p-units').textContent = inMs ? 'Times are in seconds, except where the name says (ms): milliseconds, 1000 ms = 1 s.' : 'Times are in seconds.';
  const groups = {};
  for (const p of params.params) (groups[p.group] ||= []).push(p);
  const value = (k) => params.values.find((v) => v.key === k)?.value;
  const opened = openGroups();
  const first = Object.keys(groups)[0];
  $('params').replaceChildren(...Object.entries(groups).map(([g, ps]) => {
    const d = el('details', { class: 'card pgroup', 'data-group': g, open: opened.has(g) || (!store.get('pgroups', '') && g === first) },
      el('summary', {}, el('h2', { text: g }), el('span', { class: 'count' })), ...ps.map((p) => {
        let input;
        const v = value(p.key);
        const ms = isMs(p);
        const name = `${p.label}${ms ? ' (s)' : p.unit ? ' (' + p.unit + ')' : ''}`;
        if (p.type === 'bool') { input = el('input', { type: 'checkbox' }); input.checked = !!v; }
        else if (p.type === 'choice') { const w = optionWords(p); input = el('select', {}, ...p.options.map((o) => el('option', { value: o, text: w[o] || plain(o) }))); input.value = v; }
        else if (ms) input = el('input', { type: 'number', inputmode: 'decimal', min: p.min / 1000, max: p.max / 1000, step: 'any', value: fromMs(v) });
        else if (p.type === 'int') input = el('input', { type: 'number', inputmode: 'numeric', min: p.min, max: p.max, step: 1, value: v });
        else {
          // a secret is never shown: the board says only whether one is set
          const set = params.values.find((x) => x.key === p.key)?.set;
          const ph = !p.secret ? '' : set === true ? 'set (not shown): leave empty to keep it' : set === false ? 'not set' : 'not shown: leave empty to keep it';
          input = el('input', { type: p.secret ? 'password' : 'text', maxlength: p.max_len, value: p.secret ? '' : v, placeholder: ph, autocomplete: p.secret ? 'new-password' : 'off' });
        }
        input.dataset.key = p.key; input.dataset.type = p.type; input.dataset.orig = shownValue(p, v);
        if (ms) input.dataset.ms = '1';
        if (p.secret) input.dataset.secret = '1';
        return el('div', { class: 'param' }, el('label', { class: p.type === 'bool' ? 'check' : '' }, p.type === 'bool' ? input : null, name, p.reboot ? el('span', { class: 'reboot', title: 'takes effect after a restart', text: ' ⟳' }) : null, p.type === 'bool' ? null : input),
          p.help ? el('div', { class: 'help', text: p.help }) : null);
      }));
    d.addEventListener('toggle', () => { const o = openGroups(); d.open ? o.add(g) : o.delete(g); store.set('pgroups', JSON.stringify([...o])); });
    return d;
  }));
  wifiExtras();
  paramsCount();
}
// how many changes wait to be saved (in the Save bar and on each group)
function paramChanged(i) { return (i.dataset.type === 'bool' ? String(i.checked) : i.value) !== (i.dataset.secret ? '' : i.dataset.orig); }
function paramsCount() {
  let n = 0;
  for (const d of document.querySelectorAll('#params .pgroup')) {
    const k = [...d.querySelectorAll('[data-key]')].filter(paramChanged).length;
    d.querySelector('.count').textContent = k ? `${k} not saved` : '';
    n += k;
  }
  $('p-count').textContent = n ? `${n} change${n > 1 ? 's' : ''} not saved` : 'no changes';
  $('p-save').disabled = !n;
}
$('params').addEventListener('input', paramsCount);
$('params').addEventListener('change', paramsCount);
// Settings changed (on this page or another device): the form follows, unless it holds edits not saved yet;
// then they stay and a message offers to reload.
let savingParams = false;
function paramsDirty() {
  for (const i of document.querySelectorAll('#params [data-key]')) {
    const now = i.dataset.type === 'bool' ? String(i.checked) : i.value;
    if (i.dataset.secret ? now !== '' : now !== i.dataset.orig) return true;
  }
  return false;
}
function paramsChanged(why = 'Settings were changed on another device. ') {
  if (savingParams) return; // the save reads them back itself
  if (!paramsDirty()) {
    // no edit: the form follows; a field that had the focus (nothing typed yet) gets it back
    conn.call('get_params').then((got) => {
      if (savingParams) return; // a save started meanwhile: it reads them back itself
      if (paramsDirty()) {
        // something was typed while the values were on their way: keep it; offer a reload only if they differ
        if (paramsDiffer(got)) paramsEdited(why);
        return;
      }
      const key = document.activeElement?.closest?.('#params') ? document.activeElement.dataset?.key : null;
      renderParams(got);
      if (key) document.querySelector(`#params [data-key="${CSS.escape(key)}"]`)?.focus();
    }).catch(() => {});
    return;
  }
  paramsEdited(why);
}
function paramsEdited(why) {
  const m = $('p-msg');
  m.className = 'msg err';
  m.replaceChildren(why, el('button', { class: 'small', text: 'Load the board\'s values', onclick: () => loadParams().then(() => { m.replaceChildren(); m.className = 'msg'; }) }));
}
// checked on the phone before anything is sent, in the units shown
function paramProblem(i) {
  const p = params.params.find((x) => x.key === i.dataset.key);
  const name = `"${p?.label || i.dataset.key}"`;
  const ms = !!i.dataset.ms;
  if (i.value.trim() === '') return `${name} is empty: enter a number (it was ${i.dataset.orig}${ms ? ' s' : ''})`;
  const n = +i.value;
  if (!Number.isFinite(n)) return `${name} takes a number, not ${i.value}`;
  if (!ms && !Number.isInteger(n)) return `${name} takes a whole number, not ${i.value}`;
  const v = ms ? Math.round(n * 1000) : n;
  if (p && (v < p.min || v > p.max)) return ms ? `${name} goes from ${p.min / 1000} to ${p.max / 1000} s, not ${i.value}` : `${name} goes from ${p.min} to ${p.max}, not ${i.value}`;
  return '';
}
$('p-save').onclick = async () => {
  const values = [];
  for (const i of document.querySelectorAll('#params [data-key]')) {
    const t = i.dataset.type;
    const now = t === 'bool' ? String(i.checked) : i.value;
    if (i.dataset.secret ? now === '' : now === i.dataset.orig) continue;
    // a number field left empty is not 0 (0 can mean "no limit"): refuse, nothing is saved
    const bad = t === 'int' && paramProblem(i);
    if (bad) { i.closest('details').open = true; i.focus(); return showMsg('p-msg', bad + ': nothing saved', false); }
    values.push({ key: i.dataset.key, value: t === 'bool' ? i.checked : t === 'int' ? (i.dataset.ms ? Math.round(+i.value * 1000) : +i.value) : i.value });
  }
  if (!values.length) return showMsg('p-msg', 'nothing changed', true);
  // a new network with no password is saved as an open one (the board drops the old password): if the board saw
  // that network with a password, say so before anything is saved
  const newSsid = values.find((v) => v.key === 'wifi_ssid');
  if (newSsid && newSsid.value && !values.some((v) => v.key === 'wifi_pass')) {
    const seen = wifiScan?.networks?.find((n) => n.ssid === newSsid.value);
    if (seen?.secured) {
      const pass = document.querySelector('#params [data-key="wifi_pass"]');
      pass?.closest('details') && (pass.closest('details').open = true);
      pass?.focus();
      return showMsg('p-msg', `"${newSsid.value}" needs its password: type it in Wi-Fi password (empty is only for an open network). Nothing saved`, false);
    }
  }
  savingParams = true;
  try { await call('set_params', { values }, 'p-msg'); } catch { savingParams = false; return; } // the board said why
  // the page password just set is this page's from now on (it turned the lock on: the next save needs it)
  const newPin = values.find((v) => v.key === 'pin')?.value;
  if (newPin) { conn.pin = newPin; renderLock(); }
  // read back what the board now has (the form then shows its values, and the next save sends only new changes)
  try { await loadParams(); } finally { savingParams = false; }
  showMsg('p-msg', values.some((v) => params.params.find((p) => p.key === v.key)?.reboot) ? 'saved; ⟳ settings take effect after a restart (Restart below)' : `saved ${values.length} setting${values.length > 1 ? 's' : ''}`, true);
  // a new network: after the restart this phone loses the board until it joins the same network
  const ssid = values.find((v) => v.key === 'wifi_ssid');
  if (ssid) $('p-msg').append(el('div', { class: 'hint', text: ssid.value ? `After the restart the board joins "${ssid.value}" and keeps its own Wi-Fi on too. On its own Wi-Fi this page is at http://192.168.4.1; on "${ssid.value}" open http://${hostLabel(params.values.find((v) => v.key === 'name')?.value || '', info?.id || '')}.local (or the address your router gives it).` : 'With no network set, after the restart the board has only its own Wi-Fi: join it on this phone, then open http://192.168.4.1.' }));
};
// ------------------------------------------------------------------ Wi-Fi: the board's address and the network picker

// The board's mDNS label from its name, as the firmware makes it (proto.cpp hostLabel): lower case letters and
// digits, every other run of characters one '-', at most 40 characters; no letters at all: proptroller-<short id>.
const shortId = (id) => String(id || '').slice(-4).toLowerCase();
function hostLabel(name, id) {
  let s = String(name || '').toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-+|-+$/g, '');
  s = s.slice(0, 40).replace(/-+$/, '');
  return s || 'proptroller-' + shortId(id);
}
// Where to find the board: on the Wi-Fi settings card, kept up to date with state.net.
function renderWifiAddress() {
  const box = $('wifi-address');
  if (!box || !state) return;
  const net = state.net || {};
  const label = hostLabel(params?.values.find((v) => v.key === 'name')?.value || info?.name || '', info?.id);
  const link = (url) => el('a', { href: url, text: url });
  const lines = [];
  if (net.mode === 'sta') {
    const url = net.host ? `http://${net.host}/` : `http://${net.ip}/`;
    lines.push(el('div', {}, `On "${net.ssid}" this board is at `, link(url), net.host ? ` (or http://${net.ip}/)` : ''));
    if (!net.host) lines.push(el('div', { class: 'hint', text: `Its name address, http://${label}.local/, comes up a few seconds after it joins.` }));
    if (net.hotspot) lines.push(el('div', {}, `Its own Wi-Fi "${net.hotspot}" is on too: `, link('http://192.168.4.1/')));
  } else if (net.mode === 'ap') {
    lines.push(el('div', {}, `On the board's own Wi-Fi "${net.ssid}" this board is at `, link('http://192.168.4.1/')));
    lines.push(el('div', { class: 'hint', text: `Its own Wi-Fi is always on. Once it is also on your Wi-Fi, open http://${label}.local/ there. If another board already uses that name, this one becomes http://${label}-${shortId(info?.id)}.local/ (this card and the board's log show which).` }));
  } else {
    lines.push(el('div', {}, 'This page is at ', link(location.origin + '/')));
  }
  lines.push(el('div', { class: 'hint', text: 'The name address comes from Settings \u2192 Device \u2192 Name; a new name takes effect after a restart.' }));
  box.replaceChildren(...lines);
}

// Top of the Live tab: where to find the board on the other network. On the board's own Wi-Fi (the hotspot, which
// is always on) it gives the board's address on your Wi-Fi, or says it is not on one; on your Wi-Fi it gives the
// hotspot's address too.
function renderNetWhere() {
  const card = $('net-card');
  if (!card || !state) return;
  const net = state.net || {};
  const link = (url) => el('a', { href: url, text: url });
  const lines = [];
  if (net.mode === 'sta') {
    const url = net.host ? `http://${net.host}/` : `http://${net.ip}/`;
    lines.push(el('div', {}, `On your Wi-Fi "${net.ssid}" this board is at `, link(url), net.host ? ` (or http://${net.ip}/)` : ''));
    if (net.hotspot) lines.push(el('div', { class: 'hint' }, `On its own Wi-Fi "${net.hotspot}": `, link('http://192.168.4.1/')));
  } else if (net.mode === 'ap') {
    lines.push(el('div', { text: 'This board is not joined to a Wi-Fi network.' }));
    lines.push(el('div', { class: 'hint' }, `On its own Wi-Fi "${net.ssid}": `, link('http://192.168.4.1/'), ' · join a network in Settings \u2192 Wi-Fi.'));
  }
  card.hidden = !lines.length;
  $('net-where').replaceChildren(...lines);
}

// The networks the board sees (op wifi_scan): kept across form reloads, asked again while a scan runs.
let wifiScan = null, wifiPollTimer = 0, wifiPollUntil = 0;
const signalWords = (rssi) => (rssi >= -60 ? 'strong' : rssi >= -72 ? 'good' : rssi >= -82 ? 'weak' : 'very weak');
const signalBars = (rssi) => (rssi >= -60 ? '\u2582\u2584\u2586\u2588' : rssi >= -72 ? '\u2582\u2584\u2586' : rssi >= -82 ? '\u2582\u2584' : '\u2582');
async function askWifiScan(rescan) {
  clearTimeout(wifiPollTimer);
  try {
    wifiScan = await conn.call('wifi_scan', rescan ? { rescan: true } : {});
  } catch (e) {
    wifiScan = { networks: wifiScan?.networks || [], error: e.message };
  }
  if (wifiScan.scanning) {
    if (!wifiPollUntil) wifiPollUntil = Date.now() + 20000;
    if (Date.now() < wifiPollUntil) wifiPollTimer = setTimeout(() => askWifiScan(false), 1500);
  } else wifiPollUntil = 0;
  renderWifiList();
}
function renderWifiList() {
  const list = $('wifi-list');
  if (!list) return;
  const s = wifiScan || { networks: [], scanning: true };
  const status = s.scanning ? 'Looking for networks\u2026' : s.error ? s.error
    : s.retry_ms ? `Looked ${Math.round((s.age_ms || 0) / 1000)} s ago; Rescan works again in ${Math.ceil(s.retry_ms / 1000)} s.`
    : s.networks.length ? `Networks this board can see (strongest first${s.age_ms > 5000 ? `, ${Math.round(s.age_ms / 1000)} s ago` : ''}). Tap one, or type the name above.`
    : 'The board sees no networks. Type the name above, or Rescan.';
  const input = document.querySelector('#params [data-key="wifi_ssid"]');
  list.replaceChildren(
    el('div', { class: 'row between' }, el('span', { class: 'hint wifi-status', role: 'status', text: status }),
      el('button', { class: 'small', type: 'button', id: 'wifi-rescan', text: 'Rescan', disabled: !!s.scanning, onclick: () => askWifiScan(true) })),
    ...s.networks.map((n) => el('button', {
      type: 'button', class: 'wifi-net', role: 'option', 'aria-selected': String(input?.value === n.ssid), 'data-ssid': n.ssid,
      onclick: () => pickWifi(n),
    }, el('span', { class: 'wifi-name', text: n.ssid }),
    el('span', { class: 'wifi-meta', text: `${signalBars(n.rssi)} ${signalWords(n.rssi)} \u00b7 ${n.secured ? 'password' : 'open'}` }))));
}
function pickWifi(n) {
  const input = document.querySelector('#params [data-key="wifi_ssid"]');
  if (!input) return;
  input.value = n.ssid;
  input.dispatchEvent(new Event('input', { bubbles: true }));  // the Save bar counts it
  openWifiList(false);
  const pass = document.querySelector('#params [data-key="wifi_pass"]');
  const hint = $('wifi-pick-hint');
  if (hint) hint.textContent = n.secured ? `Now type the password of "${n.ssid}" in Wi-Fi password, then Save settings.` : `"${n.ssid}" is open (no password): leave Wi-Fi password empty, then Save settings.`;
  if (pass && n.secured) pass.focus();
}
function openWifiList(open) {
  const list = $('wifi-list'), btn = $('wifi-pick'), input = document.querySelector('#params [data-key="wifi_ssid"]');
  if (!list) return;
  list.hidden = !open;
  btn?.setAttribute('aria-expanded', String(open));
  input?.setAttribute('aria-expanded', String(open));
  if (open) { renderWifiList(); askWifiScan(false); }
}
// Escape closes an open network list wherever the focus is (the list is redrawn when a scan comes in, which can
// take the focus off it), back to the field
document.addEventListener('keydown', (e) => {
  const list = $('wifi-list');
  if (e.key !== 'Escape' || !list || list.hidden) return;
  openWifiList(false);
  document.querySelector('#params [data-key="wifi_ssid"]')?.focus();
});
// The Wi-Fi card: the address on top, and the network field as an editable combo box (type a name, or pick one
// the board sees) when the board can scan.
function wifiExtras() {
  const input = document.querySelector('#params [data-key="wifi_ssid"]');
  if (!input) return;
  const group = input.closest('.pgroup');
  group.querySelector('summary').after(el('div', { id: 'wifi-address', class: 'wifi-address' }));
  renderWifiAddress();
  if (!has('wifi_scan')) return;
  input.setAttribute('role', 'combobox');
  input.setAttribute('aria-autocomplete', 'none');
  input.setAttribute('aria-controls', 'wifi-list');
  input.setAttribute('aria-expanded', 'false');
  input.placeholder = 'type a name, or choose \u25be';
  const btn = el('button', { type: 'button', class: 'small', id: 'wifi-pick', 'aria-label': 'Choose a network the board can see', 'aria-controls': 'wifi-list', 'aria-expanded': 'false', text: '\u25be',
    onclick: (e) => { e.preventDefault(); openWifiList($('wifi-list').hidden); } });
  const combo = el('div', { class: 'combo' });
  input.replaceWith(combo);
  combo.append(input, btn);
  const param = combo.closest('.param');
  param.append(el('div', { id: 'wifi-list', class: 'wifi-list', role: 'listbox', 'aria-label': 'Wi-Fi networks', hidden: true }),
    el('div', { id: 'wifi-pick-hint', class: 'hint', 'aria-live': 'polite' }));
  input.addEventListener('keydown', (e) => { if (e.key === 'ArrowDown') { e.preventDefault(); openWifiList(true); } if (e.key === 'Escape') openWifiList(false); });
}

$('p-reboot').onclick = () => { if (confirm('Restart the controller?')) call('reboot', {}, 'p-msg'); };
$('p-factory').onclick = () => { if (confirm('Erase every setting, every show and the effect script, and the Wi-Fi settings? The board restarts as new.')) call('factory_reset', {}, 'p-msg'); };

// ------------------------------------------------------------------ wiring view

// a terminal as displayed: the profile's alias, else the name given in Settings, beside the board name:
// `REFILL (TRIG2)`, `Fog machine (RLY1)`; just `RLY1` when it has neither
function tdisp(id) {
  const t = terminals().find((x) => x.id === id);
  const name = t?.alias || ((t?.kind === 'input' || t?.kind === 'output') && t.label);
  return name ? `${name} (${id})` : id;
}

function terminals() {
  if (info?.terminals?.length) return info.terminals;
  return [...(info?.inputs || []).map((id) => ({ id, kind: 'input', label: '' })), ...(info?.channels || []).map((id) => ({ id, kind: 'output', label: '' }))];
}
// smooth scrolling only when the phone does not ask for less motion
const smooth = () => (window.matchMedia?.('(prefers-reduced-motion: reduce)').matches ? 'auto' : 'smooth');
const ROLE_LABEL = { trigger_repeat: 'trigger repeat', sound_trigger: 'sound trigger', none: 'not used', pause: 'pause' };

function buildWiring() {
  const host = $('wiring');
  const ts = terminals();
  const left = ts.filter((t) => t.kind === 'input' || t.kind === 'analog');
  const right = ts.filter((t) => t.kind !== 'input' && t.kind !== 'analog');
  const rows = Math.max(left.length, right.length, 1);
  const kids = [el('div', { class: 'wboard', style: `grid-row: 1 / ${rows + 1}` }, el('span', { text: 'PROPTROLLER' }))];
  left.forEach((t, r) => {
    const box = el('div', { class: 'wbox input' + (has('sim') && t.kind === 'input' ? ' sim' : ''), id: 'wb-' + t.id, style: `grid-column:1;grid-row:${r + 1}` });
    if (has('sim') && t.kind === 'input') { momentary(box, ...momentaryInput(t.id)); box.tabIndex = 0; box.setAttribute('role', 'button'); box.setAttribute('aria-label', `${tdisp(t.id)}: press and hold to close it`); }
    kids.push(box, el('div', { class: 'wwire in', id: 'ww-' + t.id, style: `grid-column:2;grid-row:${r + 1}` }, el('i'), el('b'), el('em', { text: t.id })));
  });
  right.forEach((t, r) => {
    kids.push(el('div', { class: 'wwire out', id: 'ww-' + t.id, style: `grid-column:4;grid-row:${r + 1}` }, el('i'), el('b'), el('em', { text: t.id })),
      el('div', { class: 'wbox', id: 'wb-' + t.id, style: `grid-column:5;grid-row:${r + 1}` }));
  });
  host.replaceChildren(...kids);
  $('wiring-sub').textContent = info.mcu;
  $('wiring-hint').textContent = has('sim') ? 'Simulator: press and hold an input box to close that contact.' : 'Live from the board. Names come from Settings.';
}

function setBox(id, cls, html) {
  const b = document.getElementById('wb-' + id), w = document.getElementById('ww-' + id);
  if (!b) return;
  b.className = 'wbox ' + cls.box + (b.classList.contains('sim') ? ' sim' : '') + (b.classList.contains('pressed') ? ' pressed' : '');
  b.innerHTML = html;
  if (w) w.className = 'wwire ' + (w.classList.contains('in') ? 'in ' : 'out ') + cls.wire;
}
const esc = (t) => String(t ?? '').replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));

function renderWiring() {
  if (!state || !info) return;
  const tmap = Object.fromEntries(terminals().map((t) => [t.id, t]));
  for (const i of state.inputs) {
    const t = tmap[i.input] || { label: '' };
    const flags = [];
    if (i.raw != null && i.raw !== i.active) flags.push('debouncing');
    if (i.waiting) flags.push('release to re-arm');
    else if (i.armed === true) flags.push('armed');
    if (i.held) flags.push('held');
    setBox(i.input, { box: 'input' + (i.active ? ' on' : ''), wire: i.active ? 'on' : '' },
      `<div class="t">${esc(t.label || i.input)}${t.role ? `<span class="tag">${esc(ROLE_LABEL[t.role] || t.role)}</span>` : ''}</div>${t.alias ? `<div class="a">${esc(tdisp(i.input))}</div>` : ''}<div class="v">${i.active ? 'closed' : 'open'}</div><div class="s">${esc(flags.join(' · '))}</div>`);
  }
  for (const o of state.outputs) {
    const t = tmap[o.channel] || { label: '' };
    const pwmCh = /^OUT/.test(o.channel);
    let v;
    if (o.ramping) v = `ramp ${Math.round(o.level / 10)}% → ${Math.round((o.requested ?? 1000) / 10)}%`;
    else if (pwmCh && o.level > 0 && o.level < 1000) v = `PWM ${Math.round(o.level / 10)}%`;
    else v = o.level ? 'ON' : 'off';
    const blocked = o.blocked && o.blocked !== 'not used';
    const disabled = t.disabled || o.blocked === 'not used';
    const tag = t.role ? `<span class="tag">${esc(ROLE_LABEL[t.role] || t.role)}</span>` : '';
    // the driver line, unless it only repeats the role tag ("trigger repeat" twice)
    const drv = o.driver && !(t.role && o.driver === (ROLE_LABEL[t.role] || t.role)) ? `<div class="s">${esc(o.driver)}</div>` : '';
    setBox(o.channel, { box: (o.level ? 'on' : '') + (blocked ? ' blocked' : '') + (disabled ? ' disabled' : ''), wire: blocked ? 'blocked' : disabled ? 'disabled' : o.level >= 1000 ? 'on' : o.level > 0 ? 'part' : '' },
      `<div class="t">${esc(t.label || o.channel)}${tag}</div>${t.alias ? `<div class="a">${esc(tdisp(o.channel))}</div>` : ''}<div class="v">${esc(v)}</div>${drv}${blocked ? `<div class="bad">${esc(o.blocked)}</div>` : ''}`);
  }
  if (tmap.SOUND) {
    const snd = state.sound || { playing: state.sound_playing, online: state.sound_online };
    const line = snd.playing ? `${esc(snd.track || 'playing')}${snd.source ? ` · ${esc(snd.source)}` : ''}` : 'idle';
    setBox('SOUND', { box: (snd.playing ? 'on' : '') + (snd.online ? '' : ' blocked') + (tmap.SOUND.disabled ? ' disabled' : ''), wire: snd.playing ? 'on' : snd.online ? '' : 'blocked' },
      `<div class="t">${esc(tmap.SOUND.label || 'sound')}</div><div class="v">${line}</div>${snd.online ? '' : '<div class="bad">player not answering</div>'}${snd.ambient_missing ? '<div class="s">no ambient file</div>' : ''}`);
  }
  if (tmap.PIXEL) {
    const on = state.pixel && state.pixel !== 'off';
    setBox('PIXEL', { box: (on ? 'on' : '') + (tmap.PIXEL.disabled ? ' disabled' : ''), wire: tmap.PIXEL.disabled ? 'disabled' : on ? 'on' : '' }, `<div class="t">${esc(tmap.PIXEL.label || 'pixels')}</div><div class="v">${esc(state.pixel || 'off')}</div>`);
  }
  // state machine panel
  const m = state.machine;
  $('machine-card').classList.toggle('hidden', !m);
  if (m) {
    $('machine').replaceChildren(...m.states.map((x) => el('span', { class: x === m.state ? 'cur' : '', text: x })));
    $('timers').replaceChildren(...(m.timers || []).map((t) => el('div', { class: 'timer' },
      el('div', { text: `${t.label}: ${(t.ms / 1000).toFixed(1)} s${t.total_ms ? ` of ${(t.total_ms / 1000).toFixed(1)} s` : ''}` }),
      t.total_ms ? el('div', { class: 'wmeter' }, el('i', { style: `width:${Math.min(100, 100 * t.ms / t.total_ms)}%` })) : null)));
  }
}

// ------------------------------------------------------------------ event log

let prevState = null;
const evlog = [];
function logEv(kind, text) {
  const t = state ? (state.uptime_ms / 1000).toFixed(1) + ' s' : '';
  evlog.unshift({ kind, text: `${new Date().toLocaleTimeString()}  [${t}]  ${text}` });
  if (evlog.length > 300) evlog.pop();
  $('evlog').replaceChildren(...evlog.slice(0, 150).map((e) => el('div', { class: 'k-' + e.kind, text: e.text })));
}
function logDiff() {
  const p = prevState, s = state;
  prevState = s;
  if (!p) return;
  const tmap = Object.fromEntries(terminals().map((t) => [t.id, t]));
  const nm = tdisp;
  s.inputs.forEach((i, k) => { const o = p.inputs[k]; if (o && o.active !== i.active) logEv('in', `${nm(i.input)} ${i.active ? 'closed' : 'opened'}`); });
  s.outputs.forEach((o, k) => {
    const q = p.outputs[k]; if (!q) return;
    const lvl = (x) => (x.level === 0 ? 'off' : x.level >= 1000 ? 'on' : `${Math.round(x.level / 10)}%`);
    const settled = !o.ramping && (lvl(o) !== lvl(q)) && ((o.level === 0) !== (q.level === 0) || (!q.ramping && Math.abs(o.level - q.level) >= 50));
    if (settled || ((o.level === 0) !== (q.level === 0))) logEv('out', `${nm(o.channel)} ${lvl(o)}${o.driver ? ' · ' + o.driver : ''}`);
    if (o.blocked && o.blocked !== q.blocked) logEv('out', `${nm(o.channel)} blocked: ${o.blocked}`);
  });
  if (s.machine && p.machine && s.machine.state !== p.machine.state) logEv('st', `state ${p.machine.state} → ${s.machine.state}`);
  const st = (x) => (x.sound?.playing ? x.sound.track : null);
  if (st(s) !== st(p) && st(s)) logEv('st', `sound ${st(s)}${s.sound.source ? ' (' + s.sound.source + ')' : ''}`);
  if (s.paused !== p.paused) logEv('st', s.paused ? 'paused' : 'resumed');
}
$('evlog-clear').onclick = () => { evlog.length = 0; $('evlog').replaceChildren(); };

// ------------------------------------------------------------------ show script

// The text is the script (SHOW-SCRIPT.md): the board checks it and keeps it exactly as written.
let scriptSaved = { text: '' };
let scriptLoaded = false;  // never save over a stored script the page could not read
let scriptConflict = false;  // the stored script changed on another phone while this page had edits
const scriptDirty = () => $('script-text').value !== scriptSaved.text;
async function loadScript(force) {
  try {
    const r = await conn.call('get_script');
    scriptLoaded = true;
    const wasDirty = scriptDirty();
    const changed = r.text !== scriptSaved.text;
    scriptSaved = { text: r.text };
    if (force || !wasDirty) {
      $('script-text').value = r.text;
      scriptConflict = false;
      hook('script');
    } else if (changed) {
      // changed on another phone while this one has edits: keep them, say so (Save would overwrite it)
      scriptConflict = true;
      const m = $('script-msg');
      m.className = 'msg err';
      m.replaceChildren('The effect script was changed on another phone; your edits are kept and Save would replace it. ',
        el('button', { class: 'small', text: 'Load theirs', onclick: () => loadScript(true).then(() => showMsg('script-msg', 'loaded the stored effect script', true)) }));
    }
    $('script-state').textContent = r.text ? (r.check.ok ? 'stored, running' : 'stored, not running') : 'no effect script stored';
    showScriptCheck(r.text ? r.check : null);
    checkOverlap();
  } catch (e) { log('script: ' + e.message); }
}
// A checker message was tapped (or Enter on it): the text box selects that line and scrolls it into view; the
// blocks view marks that line's block (Blocks only, and no block holds the line: Script only opens at it).
function goToLine(n) {
  const ta = $('script-text');
  const blocksOnly = document.querySelector('input[name=script-view]:checked')?.value === 'blocks';
  const inBlocks = !!window.blockForLine?.(n);
  if (blocksOnly && inBlocks) return;
  if (blocksOnly) {
    const r = document.querySelector('input[name=script-view][value=text]');
    if (r) { r.checked = true; r.dispatchEvent(new Event('change')); }
  }
  const lines = ta.value.split('\n');
  n = Math.min(Math.max(n, 1), lines.length);
  let at = 0;
  for (let i = 0; i < n - 1; i++) at += lines[i].length + 1;
  ta.focus({ preventScroll: true });
  ta.setSelectionRange(at, at + lines[n - 1].length);
  // lines never wrap in the text box (white-space: pre): line n is (n - 1) line heights down
  const cs = getComputedStyle(ta);
  const lh = parseFloat(cs.lineHeight) || parseFloat(cs.fontSize) * 1.2 || 20;
  ta.scrollTop = Math.max(0, (parseFloat(cs.paddingTop) || 0) + (n - 1) * lh - ta.clientHeight / 3);
  ta.scrollLeft = 0;
  ta.scrollIntoView({ block: 'nearest' });
  ta.dataset.gotoLine = String(n);
  ta.classList.remove('flash');
  void ta.offsetWidth;
  ta.classList.add('flash');
  setTimeout(() => ta.classList.remove('flash'), 2000);
}
function showScriptCheck(c) {
  const box = $('script-msgs');
  box.replaceChildren();
  if (!c) return;
  const add = (m, cls) => {
    const b = el('button', { class: 'smsg ' + cls }, m.message);
    if (m.line > 0) { b.onclick = () => goToLine(m.line); b.title = `Go to line ${m.line}`; b.dataset.line = String(m.line); }
    box.append(b);
  };
  for (const m of c.errors) add(m, 'err');
  for (const m of c.warnings) add(m, 'warn');
}
async function scriptCall(op) {
  const args = { text: $('script-text').value };
  if (op === 'set_script') {
    if (!scriptLoaded) return showMsg('script-msg', 'the stored effect script could not be loaded: press Revert first, nothing saved', false);
    if (scriptSaved?.text && !/^\s*effect\b/mi.test(args.text) && !scriptCall.sure) {
      return askInline($('script-save'), 'This removes the stored effect script. Remove it?', 'Remove', () => { scriptCall.sure = true; return scriptCall(op).finally(() => { scriptCall.sure = false; }); });
    }
    if (scriptConflict && !scriptCall.sure) {
      return askInline($('script-save'), 'Replace the other phone\'s effect script?', 'Replace', () => { scriptCall.sure = true; return scriptCall(op).finally(() => { scriptCall.sure = false; }); });
    }
  }
  try {
    const c = await conn.call(op, args);
    showScriptCheck(c);
    const n = c.effects ?? 0;
    const warn = c.warnings?.length ? `, ${c.warnings.length} warning${c.warnings.length === 1 ? '' : 's'} (below)` : '';
    if (!c.ok) showMsg('script-msg', `${c.errors.length} problem${c.errors.length === 1 ? '' : 's'} (tap one to go to its line): ${op === 'set_script' ? 'nothing saved' : 'fix, then Check again'}`, false);
    else if (op === 'check_script') showMsg('script-msg', `no errors${warn}: ${n} effect${n === 1 ? '' : 's'}`, true);
    else showMsg('script-msg', n ? `saved: ${n} effect${n === 1 ? '' : 's'} running${warn}` : 'saved: no effect script (it was removed)', true);
    if (c.ok && op === 'set_script') { scriptSaved = args; scriptConflict = false; checkOverlap(); $('script-state').textContent = n ? 'stored, running' : 'no effect script stored'; }
  } catch (e) {
    showMsg('script-msg', e.detail?.code === 'pin' ? pinRefused(e) : e.message, false);
  }
}
$('script-check').onclick = () => scriptCall('check_script');
$('script-save').onclick = () => scriptCall('set_script');
$('script-revert').onclick = () => (scriptDirty() ? askInline($('script-revert'), 'Throw away your edits?', 'Revert', () => loadScript(true)) : loadScript(true));
let scriptEsc = false;
$('script-text').addEventListener('keydown', (e) => {
  // Tab indents (two spaces) instead of leaving the box; Esc, then Tab leaves it (keyboard users are not trapped)
  if (e.key === 'Escape') { scriptEsc = true; return; }
  if (e.key !== 'Tab' || e.shiftKey || scriptEsc) { scriptEsc = false; return; }
  e.preventDefault();
  const ta = e.target, a = ta.selectionStart;
  ta.setRangeText('  ', a, ta.selectionEnd, 'end');
});
// The effect script and a show on the same terminals (say both start on TRIG1, or both drive OUT2) run together:
// the page says so on the Live and Script tabs.
function scriptTerminals(text) {
  const code = text.split('\n').map((l) => l.replace(/#.*/, '')).join('\n').toLowerCase();
  const out = new Set();
  for (const t of terminals()) {
    if (t.kind !== 'input' && t.kind !== 'output') continue;
    for (const n of [t.id, t.label, t.alias].filter(Boolean)) {
      const q = n.toLowerCase().replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
      if (new RegExp(`(^|[^a-z0-9_])${q}($|[^a-z0-9_])`).test(code)) out.add(t.id);
    }
  }
  return out;
}
let overlapRun = 0;
async function checkOverlap() {
  const run = ++overlapRun;
  const st = scriptSaved.text ? scriptTerminals(scriptSaved.text) : new Set();
  const hits = [];
  if (st.size) {
    for (const s of shows) {
      if (s.enabled === false) continue;
      const g = await conn.call('get_show', { show: s.id }).catch(() => null);
      if (run !== overlapRun) return;  // a newer check started
      if (!g?.data) continue;
      const used = new Set([...(g.data.triggers || []).filter((t) => t.input).map((t) => String(t.input).toUpperCase()), ...(g.data.tracks || []).map((t) => t.target)]);
      // say only what is shared: an input both start from, outputs both drive, or both
      const isInput = (id) => terminals().find((t) => t.id === id)?.kind === 'input';
      const both = [...st].filter((x) => used.has(x));
      const ins = both.filter(isInput), outs = both.filter((x) => !isInput(x));
      const what = [ins.length && `starts from ${ins.map(tdisp).join(', ')}`, outs.length && `drives ${outs.map(tdisp).join(', ')}`].filter(Boolean).join(' and ');
      if (what) hits.push(`"${s.name}" ${what}, as the script does`);
    }
  }
  const text = hits.length ? `The effect script and ${hits.length > 1 ? 'these shows' : 'this show'} use the same terminals, so they run together: ${hits.join('; ')}.` : '';
  for (const id of ['overlap-live', 'overlap-script']) { $(id).textContent = text; $(id).hidden = !text; }
}

function renderScriptState() {
  const sc = state?.script;
  $('live-script-card').classList.toggle('hidden', !sc);
  if (!sc) return;
  const list = $('live-script');
  list.replaceChildren();
  if (sc.error) list.append(el('div', { class: 'msg err' }, 'not running: ' + sc.error));
  for (const e of sc.effects) {
    list.append(el('div', { class: 'item' }, el('span', { class: 'name' }, e.name), el('span', { class: 'badge' + (e.running ? ' running' : '') }, e.running ? `line ${e.line}` : 'waiting')));
  }
}

// ------------------------------------------------------------------ firmware update (PROTOCOL.md "Firmware updates")

// SHA-256 in plain JavaScript: crypto.subtle exists only in a secure context (https or localhost), and the
// board's page is plain http on a LAN address.
function sha256js(bytes) {
  const K = new Uint32Array([0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb,
    0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f,
    0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2]);
  const h = new Uint32Array([0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19]);
  const n = bytes.length, total = Math.ceil((n + 9) / 64) * 64;
  const m = new Uint8Array(total);
  m.set(bytes); m[n] = 0x80;
  const dv = new DataView(m.buffer);
  dv.setUint32(total - 8, Math.floor(n / 0x20000000)); dv.setUint32(total - 4, (n * 8) >>> 0);
  const w = new Uint32Array(64);
  const rotr = (x, r) => (x >>> r) | (x << (32 - r));
  for (let o = 0; o < total; o += 64) {
    for (let i = 0; i < 16; i++) w[i] = dv.getUint32(o + i * 4);
    for (let i = 16; i < 64; i++) {
      const s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >>> 3), s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >>> 10);
      w[i] = (w[i - 16] + s0 + w[i - 7] + s1) | 0;
    }
    let [a, b, c, d, e, f, g, k] = h;
    for (let i = 0; i < 64; i++) {
      const t1 = (k + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i]) | 0;
      const t2 = ((rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c))) | 0;
      k = g; g = f; f = e; e = (d + t1) | 0; d = c; c = b; b = a; a = (t1 + t2) | 0;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += k;
  }
  return [...h].map((x) => x.toString(16).padStart(8, '0')).join('');
}
async function sha256hex(bytes) {
  if (window.isSecureContext && crypto?.subtle) {
    const d = new Uint8Array(await crypto.subtle.digest('SHA-256', bytes));
    return [...d].map((x) => x.toString(16).padStart(2, '0')).join('');
  }
  return sha256js(bytes);
}

const ascii = (b) => String.fromCharCode(...b);
const findBytes = (hay, needle, limit) => {
  const n = new TextEncoder().encode(needle), end = Math.min(hay.length, limit) - n.length;
  outer: for (let i = 0; i <= end; i++) { for (let j = 0; j < n.length; j++) if (hay[i + j] !== n[j]) continue outer; return i; }
  return -1;
};

// What a file is, read in the browser before anything is sent (the board checks again):
// { ok, what, version } or { ok: false, why } in plain words.
function fwInspect(b, name) {
  const mcu = info.mcu, max = info.update?.max_bytes || 0;
  if (!b.length) return { ok: false, why: `${name} is empty` };
  if (b.length > max) return { ok: false, why: `${name} is ${b.length.toLocaleString()} bytes; this board takes at most ${max.toLocaleString()}` };
  const u16 = (o) => b[o] | (b[o + 1] << 8), u32 = (o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;
  // the board marker "PROPTROLLER-MCU:<mcu>" NUL anywhere in the image (a Pico image keeps it among its
  // constants); the bare prefix (the firmware's own copy of it) carries no name
  const names = [];
  for (let at = 0, i; names.length < 8 && (i = findBytes(b.subarray(at), 'PROPTROLLER-MCU:', b.length)) >= 0; at += i + 16) {
    const nm = ascii(b.slice(at + i + 16, at + i + 32)).split('\0')[0];
    if (/^[a-z0-9-]+$/.test(nm)) names.push(nm);
  }
  const markerName = names.includes(mcu) ? mcu : names[0] || null;
  const esp = b[0] === 0xE9 && b.length >= 64;
  const espApp = esp && u16(12) === 0 && u32(32) === 0xABCD5432;
  const blob = ascii(b.slice(0, 8)) === 'PTCYW43\0';
  const sim = ascii(b.slice(0, 22)) === 'PROPTROLLER-SIM-IMAGE\n';
  const other = (what) => ({ ok: false, why: `${name} is ${what}, not for this board (${mcu})` });
  if (mcu === 'esp32-devkit') {
    if (espApp) return { ok: true, what: 'ESP32 firmware', version: ascii(b.slice(48, 80)).split('\0')[0] };
    if (esp) return other('a firmware for another ESP32 chip');
  } else if (mcu === 'sim') {
    if (sim) {
      try {
        const h = JSON.parse(ascii(b.slice(22, Math.min(b.length, 400))).split('\n')[0]);
        return { ok: true, what: 'simulator firmware', version: `${h.fw_version} (${h.fw_build})` };
      } catch { return { ok: false, why: `${name} is a damaged simulator image` }; }
    }
  } else if (markerName === mcu) {
    return { ok: true, what: `${mcu} firmware` };
  }
  if (markerName) return other(`a firmware for ${markerName}`);
  if (espApp || esp) return other('an ESP32 firmware');
  if (blob) return other('an older firmware format this board cannot use');
  if (sim) return other('a simulator image');
  return { ok: false, why: `${name} is not a firmware image for ${mcu}` };
}

// sending: an upload of this page is in flight (only then do `update` events move the progress bar; its
// answer, not an event, says how it ended)
const fw = { bytes: null, name: '', check: null, sending: false };
function renderFirmware() {
  if (!info) return;
  const u = info.update || {};
  const rows = [['Firmware', `${info.fw} ${info.fw_version}`], ['Build', info.fw_build || '?'], ['Board', info.mcu]];
  if (info.update) {
    rows.push(['State', u.state === 'pending' ? 'new, checking itself (about 30 s)' : 'checked and kept'], ['Memory slot', `${u.slot} (the other one keeps the firmware before)`]);
    if (u.rolled_back) rows.push(['Last update', 'failed its self-check: went back to this firmware']);
    rows.push(['Largest file', `${(u.max_bytes || 0).toLocaleString()} bytes`]);
  }
  $('fw-info').replaceChildren(...rows.flatMap(([k, v]) => [el('div', { text: k }), el('div', { text: v })]));
}

async function fwPick(file) {
  if (!file || !info) return;
  fw.bytes = new Uint8Array(await file.arrayBuffer());
  fw.name = file.name || 'the file';
  fw.check = fwInspect(fw.bytes, fw.name);
  $('fw-send').disabled = !fw.check.ok;
  $('fw-stop').classList.add('hidden');
  showMsg('fw-msg', '', true);
  showMsg('fw-file-info', fw.check.ok ? `${fw.name}: ${fw.check.what}${fw.check.version ? ' ' + fw.check.version : ''}, ${fw.bytes.length.toLocaleString()} bytes` : fw.check.why, fw.check.ok);
}
{
  const drop = $('fw-drop');
  for (const ev of ['dragenter', 'dragover']) drop.addEventListener(ev, (e) => { e.preventDefault(); drop.classList.add('over'); });
  for (const ev of ['dragleave', 'drop']) drop.addEventListener(ev, () => drop.classList.remove('over'));
  drop.addEventListener('drop', (e) => { e.preventDefault(); fwPick(e.dataTransfer?.files?.[0]); });
  $('fw-file').onchange = (e) => fwPick(e.target.files[0]);
}

function fwEvent(p) {
  if (!fw.sending) return;
  const bar = $('fw-progress');
  if (p.phase === 'writing') { bar.classList.remove('hidden'); bar.value = p.total ? p.done / p.total : 0; showMsg('fw-msg', `writing: ${Math.round(100 * bar.value)} %`, true); }
  else if (p.phase === 'restarting') { bar.value = 1; showMsg('fw-msg', fwRestartText(), true); }
}

async function fwSend() {
  if (!fw.check?.ok) return;
  const to = fw.check.version ? ` to ${fw.check.version}` : ` with ${fw.name}`;
  if (!confirm(`Update "${info.name}" (${info.mcu}) from ${info.fw_version} (${info.fw_build || '?'})${to}?`)) return;
  // locked: the password goes in the Unlock box (masked), never a prompt() that shows it
  if (has('pin') && !conn.pin) { openUnlock(); return showMsg('fw-msg', 'the page is locked: enter the page password in the box at the top, then Update', false); }
  $('fw-send').disabled = true;
  $('fw-stop').classList.add('hidden');
  showMsg('fw-msg', 'checking the file…', true);
  const sha = await sha256hex(fw.bytes);
  const before = { build: info.fw_build, version: info.fw_version, slot: info.update?.slot, uptime: state?.uptime_ms ?? 0 };
  const headers = { 'Content-Type': 'application/octet-stream', 'X-Proptroller-Sha256': sha };
  if (conn.pin) headers['X-Proptroller-Pin'] = conn.pin;
  showMsg('fw-msg', 'sending…', true);
  let r;
  fw.sending = true;
  try {
    r = await (await fetch(BASE + '/api/v1/update', { method: 'POST', headers, body: fw.bytes })).json();
  } catch (e) {
    fw.sending = false;
    $('fw-send').disabled = false;
    return showMsg('fw-msg', 'the board did not answer: ' + e.message, false);
  }
  fw.sending = false;
  if (!r.ok) {
    $('fw-progress').classList.add('hidden');
    $('fw-send').disabled = false;
    if (r.error?.code === 'pin') { conn.pin = ''; renderLock(); openUnlock(); }
    // something runs: one button stops it all and sends again
    if (r.error?.code === 'refused') $('fw-stop').classList.remove('hidden');
    return showMsg('fw-msg', 'not updated: ' + (r.error?.message || 'error'), false);
  }
  showMsg('fw-msg', fwRestartText(), true);
  fwWait(before);
}
$('fw-send').onclick = fwSend;
$('fw-stop').onclick = async () => { try { await call('stop_all'); } catch { return; } fwSend(); };

// A board whose bootloader installs the update at the restart (the Pico W / Pico 2 W swap: about 16 s, up to
// about 2.5 min with slow flash) says how long that may take: info.update.install_max_s. The ESP32 starts the
// other slot at once.
const fwInstallS = () => info?.update?.install_max_s || 0;
const fwInstallText = () => `the board is installing the update, this can take up to ${Math.ceil(fwInstallS() / 60)} minutes`;
const fwRestartText = () => fwInstallS() ? `written and checked: ${fwInstallText()}…` : 'written and checked: the board restarts into it…';
// how long to wait for the board to come back and pass its self-check (30 s) before giving up
const fwWaitMs = () => Math.max(180, fwInstallS() + 120) * 1000;

// After the restart: pending, then valid, or rolled back.
async function fwWait(before) {
  const t0 = Date.now(), limit = fwWaitMs();
  // a new image reports pending until its self-check: seen after the restart, the update went in even when its
  // build and slot read as before (the Pico's slot is always "active"; a build from a changed tree keeps its hash)
  let restarted = false, sawPending = false;
  while (Date.now() - t0 < limit) {
    await new Promise((res) => setTimeout(res, 700));
    let i, st;
    try { i = await conn.call('info'); st = await conn.call('state'); } catch {
      restarted = true;
      if (fwInstallS()) showMsg('fw-msg', `${fwInstallText()} (${Math.round((Date.now() - t0) / 1000)} s)…`, true);
      continue;
    }
    if (st.uptime_ms < before.uptime) restarted = true;
    before.uptime = Math.max(before.uptime, restarted ? 0 : st.uptime_ms);
    if (!restarted) continue;
    info = i; applyInfo();
    const u = i.update || {};
    if (u.state === 'pending') { sawPending = true; showMsg('fw-msg', `running ${i.fw_version} (${i.fw_build}): checking itself, about 30 s…`, true); continue; }
    $('fw-progress').classList.add('hidden');
    $('fw-send').disabled = false;
    if (u.rolled_back) return showMsg('fw-msg', `the new firmware failed its self-check: the board went back to ${i.fw_version} (${i.fw_build})`, false);
    if (sawPending || i.fw_build !== before.build || i.fw_version !== before.version || u.slot !== before.slot) return showMsg('fw-msg', `updated: now ${i.fw_version} (${i.fw_build}), valid`, true);
    return showMsg('fw-msg', 'the board restarted with the same firmware', false);
  }
  $('fw-send').disabled = false;
  showMsg('fw-msg', `the board has not come back after ${limit / 60000} minutes: check its power and network`, false);
}

// leaving the page with edits not saved (the show editor, the effect script, settings) asks first
window.addEventListener('beforeunload', (e) => {
  const settings = [...document.querySelectorAll('#params [data-key]')].some((i) => (i.dataset.type === 'bool' ? String(i.checked) : i.value) !== (i.dataset.secret ? '' : i.dataset.orig));
  if (edDirty() || (scriptLoaded && scriptDirty()) || settings) { e.preventDefault(); e.returnValue = ''; }
});

// the theme: like the phone, or dark / light for this phone only (kept in this browser)
{
  const apply = (t) => { if (t) document.documentElement.dataset.theme = t; else delete document.documentElement.dataset.theme; if (cur) drawTimeline(); };
  const t = store.get('theme', '');
  $('theme').value = t;
  apply(t);
  $('theme').onchange = () => { store.set('theme', $('theme').value); apply($('theme').value); };
}
// things scrolled into view (a focused field, an opened editor) land below the sticky header, not under it
{
  const top = document.querySelector('.stickytop');
  const pad = () => { document.documentElement.style.scrollPaddingTop = top.offsetHeight + 8 + 'px'; };
  if (window.ResizeObserver) new ResizeObserver(pad).observe(top);
  pad();
}

conn.connect();
