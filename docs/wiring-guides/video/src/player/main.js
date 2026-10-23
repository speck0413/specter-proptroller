// The interactive wiring guide pages: one live three.js scene you can rotate, pan and zoom, Next / Back steps that move
// the camera and draw the wires, the step in big plain words, and the step's narration. Bundled into one classic script
// (html/wiring-guides.js) so the pages work from disk (file://) and inside the desktop app, with no network.
import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { createScene, lerpCam } from '../scene/index.js';
import { GUIDES, ORDER } from '../guides/all.js';
import { LEVELS } from '../guides/template.js';
import { GLOSSARY } from '../guides/glossary.js';
import { insetHTML } from './insets.js';
import caseGlb from '../../assets/case.glb';
import boardTexture from '../../assets/board-top.webp';

const $ = (sel, el = document) => el.querySelector(sel);
const h = (tag, attrs = {}, ...kids) => { const e = document.createElement(tag); Object.assign(e, attrs); for (const k of kids) if (k != null) e.append(k); return e; };
const store = { get(k) { try { return localStorage.getItem(k); } catch { return null; } }, set(k, v) { try { localStorage.setItem(k, v); } catch { /* private window */ } } };
const pageOf = (g) => `${g.id}-${g.slug}.html`;
const byId = (id) => GUIDES.find((x) => x.id === id);
const reduced = () => !!globalThis.matchMedia?.('(prefers-reduced-motion: reduce)').matches;

export function index(el) {
  for (const sec of ORDER) {
    el.append(h('h2', { className: 'wg-sec', textContent: sec.title }));
    const grid = h('div', { className: 'wg-grid' });
    for (const id of sec.ids) {
      const g = byId(id); if (!g) continue;
      grid.append(h('a', { className: 'wg-card', href: pageOf(g) },
        h('img', { src: `posters/${g.id}-${g.slug}.webp`, alt: '', loading: 'lazy' }),
        h('span', { className: 'wg-num', textContent: g.label ?? `Guide ${Number(g.id)}` }),
        h('strong', { textContent: g.title }), h('span', { textContent: g.blurb }),
        h('span', { className: 'wg-cardbadge', textContent: `${g.time} · ${LEVELS[g.level] ?? ''}` })));
    }
    el.append(grid);
  }
}

// The step's words, with glossary terms as buttons that open their meaning (first use in the step only).
function richText(text) {
  const frag = document.createDocumentFragment();
  let rest = text; const used = new Set();
  while (rest) {
    let best = null;
    for (const [name, re, def] of GLOSSARY) {
      if (used.has(name)) continue;
      const m = re.exec(rest);
      if (m && (!best || m.index < best.m.index)) best = { m, name, def };
    }
    if (!best) { frag.append(rest); break; }
    used.add(best.name);
    frag.append(rest.slice(0, best.m.index));
    const tip = h('span', { className: 'wg-tip', role: 'tooltip', textContent: best.def, hidden: true });
    const b = h('button', { className: 'wg-term', type: 'button', textContent: best.m[0], title: best.def });
    b.setAttribute('aria-expanded', 'false');
    b.onclick = (e) => { e.stopPropagation(); const open = tip.hidden; document.querySelectorAll('.wg-tip').forEach((t) => { t.hidden = true; }); tip.hidden = !open; b.setAttribute('aria-expanded', String(open)); };
    frag.append(h('span', { className: 'wg-termwrap' }, b, tip));
    rest = rest.slice(best.m.index + best.m[0].length);
  }
  return frag;
}

export async function guide(id, { poster = false } = {}) {
  const g = byId(id);
  document.title = `${g.title} - Proptroller wiring guide`;
  const root = $('#wg');
  root.innerHTML = `
    <div class="wg-stage"><canvas></canvas><div class="wg-hud">
      <button class="wg-btn wg-reset" title="Reset the view to this step (R)">Reset view</button>
      <button class="wg-btn wg-case" aria-pressed="false">Show case</button></div>
      <div class="wg-help">Drag to turn &middot; right-drag or two fingers to move &middot; wheel or pinch to zoom &middot; double-click a part to look at it<br>Keys: &larr; &rarr; step &middot; Space listen &middot; A D W S turn &middot; + &minus; zoom &middot; R reset</div>
      <div class="wg-stageinset" aria-hidden="true"></div>
      <div class="wg-touchhint" hidden>Drag to turn &middot; pinch to zoom<br><small>tap to hide</small></div></div>
    <div class="wg-panel">
      <div class="wg-top"><a href="index.html" class="wg-home">&larr; All guides</a><span class="wg-count"></span></div>
      <div class="wg-live" aria-live="polite"><h1 class="wg-title"></h1><div class="wg-say"></div></div>
      <div class="wg-extra"></div>
      <div class="wg-footbar"><div class="wg-controls">
        <button class="wg-btn wg-back">&larr; Back</button>
        <button class="wg-btn wg-play" title="Hear this step (Space)">&#9654; Listen</button>
        <button class="wg-btn wg-all" title="Play every step from here, with the voice">Play all</button>
        <button class="wg-btn wg-next wg-primary">Next &rarr;</button></div>
      <nav class="wg-dots" aria-label="Steps"></nav></div></div>`;
  if (/poster/.test(location.hash)) root.classList.add('wg-poster');
  const canvas = $('canvas', root), stage = $('.wg-stage', root);
  canvas.setAttribute('role', 'img');
  const S = await createScene({ canvas, labelHost: stage, caseGlb: caseGlb.buffer.slice(caseGlb.byteOffset, caseGlb.byteOffset + caseGlb.byteLength), boardTexture, preserve: poster });
  S.load(g);
  const controls = new OrbitControls(S.camera, S.labelLayer);
  controls.enableDamping = true; controls.screenSpacePanning = true; controls.minDistance = 30; controls.maxDistance = 1500;
  controls.maxPolarAngle = Math.PI * 0.495;
  const audio = new Audio();
  // Opus where the browser says it can, else the AAC .m4a (Safari / older WebKit); and if an Opus file still fails to
  // load or decode (a browser that answers 'maybe' but cannot), switch to AAC for the rest of the page and retry.
  let ext = audio.canPlayType('audio/ogg; codecs=opus') ? 'opus' : 'm4a';
  audio.onerror = () => { if (ext === 'opus') { ext = 'm4a'; say(); } };

  // Which build the reader has: bare board or in the case. Steps marked `only` follow it.
  const hasBranch = g.steps.some((s) => s.only);
  let mode = store.get('wg-build') === 'case' ? 'case' : 'bare';
  let visible = [];
  const dots = $('.wg-dots', root);
  function rebuild() {
    visible = g.steps.map((s, i) => i).filter((i) => !g.steps[i].only || g.steps[i].only === mode);
    dots.replaceChildren(...visible.map((i, k) => { const d = h('button', { className: 'wg-dot', title: g.steps[i].title, onclick: () => go(k) }); d.setAttribute('aria-label', `Step ${k + 1}: ${g.steps[i].title}`); return d; }));
  }
  rebuild();
  let pos = 0, step = 0, stepStart = performance.now(), camFrom = null, camTo = null, camStart = 0, autoplay = false, advanceTimer = 0;

  function setCam(c, animate = !reduced()) {
    c = S.fit(c);
    camTo = c; camStart = performance.now();
    camFrom = animate ? [...S.camera.position.toArray(), ...controls.target.toArray()] : c;
  }
  function extras(s) {
    const box = $('.wg-extra', root); box.replaceChildren();
    if (s.kind === 'intro') {
      box.append(h('div', { className: 'wg-badges' }, h('span', { textContent: `⏱ ${g.time}` }), h('span', { textContent: `🛠 ${LEVELS[g.level] ?? g.level}` })));
      if (g.firmware) box.append(h('p', { className: 'wg-fw', textContent: g.firmware }));
      if (hasBranch) {
        const pick = (m, label) => { const b = h('button', { className: 'wg-btn' + (mode === m ? ' wg-primary' : ''), textContent: label, type: 'button' });
          b.setAttribute('aria-pressed', String(mode === m)); b.onclick = () => { mode = m; store.set('wg-build', m); rebuild(); go(0); }; return b; };
        box.append(h('div', { className: 'wg-build' }, h('span', { textContent: 'My board is:' }), pick('bare', 'Bare board'), pick('case', 'In the case')));
      }
    }
    if (s.kind === 'need') box.append(h('ul', { className: 'wg-check' }, ...g.need.map((t, i) => h('li', {}, h('label', {}, h('input', { type: 'checkbox', id: `need-${i}` }), ' ', t)))));
    if (s.warn) box.append(h('div', { className: 'wg-warnbox', role: 'note' }, h('b', { textContent: '⚠ ' }), s.warn));
    if (s.card) box.append(h('div', { className: 'wg-card-dd' },
      h('div', { className: 'wg-do' }, h('b', { textContent: s.kind === 'before' ? 'ALWAYS' : 'DO' }), ...s.card.do.map((t) => h('div', { textContent: '✔ ' + t }))),
      h('div', { className: 'wg-dont' }, h('b', { textContent: "DON'T" }), ...s.card.dont.map((t) => h('div', { textContent: '✖ ' + t })))));
    // the 2D card: over the scene on a wide screen, in the panel on a phone (CSS shows one of the two)
    const over = $('.wg-stageinset', root); over.innerHTML = '';
    if (s.inset) { const d = h('div', { className: 'wg-insetbox' }); d.innerHTML = insetHTML(s.inset); box.append(d); over.innerHTML = insetHTML(s.inset); }
    if (s.kind === 'test' || s.connect) {
      const c = byId('00'); const k = c ? c.steps.findIndex((x) => x.id === 'connect') : -1;
      if (c && g.id !== '00' && k >= 0) box.append(h('a', { className: 'wg-link', href: `${pageOf(c)}#step=${k + 1}`, textContent: 'How do I open the board’s page on my phone?' }));
    }
    if (s.kind === 'trouble') box.append(h('ul', { className: 'wg-trouble' }, ...g.trouble.map((t) => h('li', { textContent: t }))));
    if (s.kind === 'next') box.append(h('div', { className: 'wg-next-list' }, ...(g.next || []).map((n) => byId(n)).filter(Boolean).map((x) =>
      h('a', { className: 'wg-btn', href: pageOf(x), textContent: `${x.label ?? 'Guide ' + Number(x.id)}: ${x.title}` }))));
  }
  function go(k, { speak = autoplay } = {}) {
    pos = Math.max(0, Math.min(visible.length - 1, k)); step = visible[pos]; stepStart = reduced() ? -1e9 : performance.now();
    const s = g.steps[step];
    const text = (s.text ?? s.say).replace(/`/g, '');
    $('.wg-title', root).textContent = s.title; $('.wg-say', root).replaceChildren(richText(text));
    canvas.setAttribute('aria-label', `3D view: ${s.alt ?? s.title}`);
    $('.wg-count', root).textContent = `Step ${pos + 1} of ${visible.length}`;
    [...dots.children].forEach((d, i) => { d.classList.toggle('on', i === pos); if (i === pos) d.setAttribute('aria-current', 'step'); else d.removeAttribute('aria-current'); });
    $('.wg-back', root).disabled = pos === 0; $('.wg-next', root).textContent = pos === visible.length - 1 ? 'Done' : 'Next →';
    extras(s);
    setCam(s.cam);
    forced = null; S.setCase(null); caseBtn();
    clearTimeout(advanceTimer); audio.pause();
    if (speak) say();
  }
  function say() {
    audio.src = `audio/${g.id}-${String(step + 1).padStart(2, '0')}.${ext}`;
    audio.currentTime = 0; audio.play().catch(() => {});
    $('.wg-play', root).textContent = '❚❚ Pause';
  }
  audio.onended = audio.onpause = () => {
    $('.wg-play', root).textContent = '▶ Listen';
    if (autoplay && audio.ended) {
      if (pos < visible.length - 1) advanceTimer = setTimeout(() => go(pos + 1, { speak: true }), 2500);
      else { autoplay = false; $('.wg-all', root).textContent = 'Play all'; }
    }
  };
  // the case button: shows the opposite of what the step shows
  let forced = null;
  const caseShown = () => (forced ?? (g._states[step].caseMode !== 'none'));
  function caseBtn() { const b = $('.wg-case', root); b.textContent = caseShown() ? 'Hide case' : 'Show case'; b.setAttribute('aria-pressed', String(caseShown())); }
  $('.wg-case', root).onclick = () => { forced = !caseShown(); S.setCase(forced); caseBtn(); };
  $('.wg-play', root).onclick = () => (audio.paused ? say() : audio.pause());
  $('.wg-all', root).onclick = () => {
    autoplay = !autoplay; $('.wg-all', root).textContent = autoplay ? 'Stop' : 'Play all';
    if (autoplay) say(); else { audio.pause(); clearTimeout(advanceTimer); }
  };
  $('.wg-next', root).onclick = () => (pos === visible.length - 1 ? (location.href = 'index.html') : go(pos + 1));
  $('.wg-back', root).onclick = () => go(pos - 1);
  $('.wg-reset', root).onclick = () => setCam(g.steps[step].cam);
  // keyboard: arrows step, Space listens, A/D turn, W/S tilt, +/- zoom, R resets the view
  const orbit = (dAz, dEl, zoom = 1) => {
    const off = S.camera.position.clone().sub(controls.target); const sph = new THREE.Spherical().setFromVector3(off);
    sph.theta += dAz; sph.phi = Math.max(0.15, Math.min(Math.PI * 0.495, sph.phi + dEl)); sph.radius = Math.max(30, Math.min(1500, sph.radius * zoom));
    S.camera.position.copy(controls.target).add(new THREE.Vector3().setFromSpherical(sph)); camTo = null;
  };
  addEventListener('keydown', (e) => {
    if (e.target.closest?.('input, textarea, select') || e.altKey || e.ctrlKey || e.metaKey) return;
    const onButton = e.target.closest?.('button, a');
    switch (e.key) {
      case 'ArrowRight': go(pos + 1); break;
      case 'ArrowLeft': go(pos - 1); break;
      case ' ': if (onButton) return; e.preventDefault(); $('.wg-play', root).click(); break;
      case 'a': case 'A': orbit(0.12, 0); break;
      case 'd': case 'D': orbit(-0.12, 0); break;
      case 'w': case 'W': orbit(0, -0.08); break;
      case 's': case 'S': orbit(0, 0.08); break;
      case '+': case '=': orbit(0, 0, 0.85); break;
      case '-': case '_': orbit(0, 0, 1.18); break;
      case 'r': case 'R': setCam(g.steps[step].cam); break;
      default: return;
    }
  });
  // phones: a one-time hint on how to move the scene
  const hint = $('.wg-touchhint', root);
  if (globalThis.matchMedia?.('(pointer: coarse)').matches && !store.get('wg-hinted') && !poster) {
    hint.hidden = false;
    const hide = () => { hint.hidden = true; store.set('wg-hinted', '1'); };
    hint.onclick = hide; S.labelLayer.addEventListener('pointerdown', hide, { once: true });
  }
  // double-click a part: look at it
  const ray = new THREE.Raycaster();
  S.labelLayer.addEventListener('dblclick', (e) => {
    const r = canvas.getBoundingClientRect();
    ray.setFromCamera(new THREE.Vector2(((e.clientX - r.left) / r.width) * 2 - 1, -((e.clientY - r.top) / r.height) * 2 + 1), S.camera);
    const hit = ray.intersectObjects(S.scene.children, true).find((x) => x.object.visible && x.object.name !== 'desk' && x.object.type === 'Mesh');
    if (!hit) return;
    const p = hit.point, dir = S.camera.position.clone().sub(controls.target).normalize().multiplyScalar(90);
    setCam([...p.clone().add(dir).toArray(), ...p.toArray()]);
  });
  document.addEventListener('click', (e) => { if (!e.target.closest('.wg-termwrap')) document.querySelectorAll('.wg-tip').forEach((t) => { t.hidden = true; }); });

  function resize() { const w = stage.clientWidth, hh = stage.clientHeight; S.resize(w, hh); }
  new ResizeObserver(resize).observe(stage); resize();
  const hs = location.hash.match(/step=(\d+)/);
  const want = hs ? Number(hs[1]) - 1 : 0;   // a deep link names the step in the full list
  if (hs && g.steps[want]?.only && g.steps[want].only !== mode) { mode = g.steps[want].only; rebuild(); }
  go(Math.max(0, visible.indexOf(want)), { speak: false });
  if (hs) { camTo = null; const c = S.fit(g.steps[step].cam); S.setCamera(c); controls.target.set(...c.slice(3)); stepStart = -1e9; }
  // wires draw quickly (1.2 s); with reduced motion, or on a deep link, the step shows its end state at once
  function frame(now) {
    if (camTo) {
      const k = Math.min(1, (now - camStart) / 1200);
      const c = lerpCam(camFrom, camTo, k);
      S.camera.position.set(c[0], c[1], c[2]); controls.target.set(c[3], c[4], c[5]);
      if (k >= 1) camTo = null;
    }
    controls.update();
    const t = Math.min(1, (now - stepStart) / 1200);
    S.show(step, t, reduced() ? 0.3 : now / 1000);
    S.render();
    requestAnimationFrame(frame);
  }
  requestAnimationFrame(frame);
  return S;
}
