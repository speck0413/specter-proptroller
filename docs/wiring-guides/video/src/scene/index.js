// The shared wiring-guide scene: one three.js world (desk, board, case, the guide's parts and wires) that both the
// interactive HTML pages and the Remotion videos drive. A guide (src/guides/*.js) builds its parts and wires with the
// kit below and lists steps; `show(stepIndex, t, time)` puts the world in the state of that step, `t` (0..1) being how
// far the step's own animation (wires drawing, a plug sliding in) has got.
import * as THREE from 'three';
import { GLTFLoader } from 'three/examples/jsm/loaders/GLTFLoader.js';
import { MeshoptDecoder } from 'three/examples/jsm/libs/meshopt_decoder.module.js';
import { CSS2DRenderer, CSS2DObject } from 'three/examples/jsm/renderers/CSS2DRenderer.js';
import { buildBoard, BOARD_Y, TOP } from './board.js';
import * as props from './props.js';

export { props, TOP };
export { view } from './view.js';
export const COLORS = { red: 0xd62828, black: 0x1c1c1c, yellow: 0xf2c200, white: 0xf0f0f0, green: 0x2a9d3a, blue: 0x2b6cd6, orange: 0xf08a24, purple: 0x8e44ad, grey: 0x8a8f94 };
const ease = (x) => (x <= 0 ? 0 : x >= 1 ? 1 : x * x * (3 - 2 * x));

function label(text, cls = '') {
  const d = document.createElement('div');
  d.className = `wg-label ${cls}`; d.textContent = text;
  return new CSS2DObject(d);
}

export async function createScene({ canvas, labelHost, caseGlb, boardTexture, preserve = false }) {
  const renderer = new THREE.WebGLRenderer({ canvas, antialias: true, preserveDrawingBuffer: preserve });
  renderer.setPixelRatio(Math.min(2, globalThis.devicePixelRatio || 1));
  renderer.toneMapping = THREE.ACESFilmicToneMapping; renderer.outputColorSpace = THREE.SRGBColorSpace;
  const css = new CSS2DRenderer(); css.domElement.className = 'wg-labels'; labelHost.appendChild(css.domElement);
  const scene = new THREE.Scene(); scene.background = new THREE.Color(0x20252b);
  const camera = new THREE.PerspectiveCamera(35, 16 / 9, 2, 5000);
  scene.add(new THREE.HemisphereLight(0xffffff, 0x445566, 1.6));
  const sun = new THREE.DirectionalLight(0xffffff, 2.2); sun.position.set(-150, 400, 250); scene.add(sun);
  const fill = new THREE.DirectionalLight(0xffffff, 0.8); fill.position.set(250, 150, -200); scene.add(fill);
  // the desk
  const desk = new THREE.Mesh(new THREE.PlaneGeometry(1400, 1000), new THREE.MeshStandardMaterial({ color: 0x4a3c30, roughness: 0.95 }));
  desk.rotation.x = -Math.PI / 2; desk.position.set(67, 0, 60); desk.name = 'desk'; scene.add(desk);

  // the board, on four standoffs when it sits on the desk without its case
  const tex = await new THREE.TextureLoader().loadAsync(boardTexture);
  const board = buildBoard(tex); scene.add(board);
  const standoffs = new THREE.Group();
  for (const [x, z] of [[4, 4], [130, 4], [4, 76], [130, 76]]) {
    const s = new THREE.Mesh(new THREE.CylinderGeometry(2.6, 2.6, BOARD_Y, 6), new THREE.MeshStandardMaterial({ color: 0xd8d8d8 }));
    s.position.set(x, BOARD_Y / 2, z); standoffs.add(s);
  }
  scene.add(standoffs);

  // the case: tub, plates and latches; the lid on its hinge line so it can stand open
  const caseGroup = new THREE.Group(); caseGroup.name = 'case'; scene.add(caseGroup);
  const lidPivot = new THREE.Group(); lidPivot.position.set(0, 53.4, -40.4); caseGroup.add(lidPivot);
  const gltf = await new GLTFLoader().setMeshoptDecoder(MeshoptDecoder).parseAsync(caseGlb, '');
  for (const m of [...gltf.scene.children]) {
    // engraved text in near-black: the printed orange on the light lid and plates was hard to read
    const color = /text$/.test(m.name) ? new THREE.Color(0x15191e) : m.material.color;
    m.material = new THREE.MeshStandardMaterial({ color, roughness: 0.75, flatShading: true });
    if (m.name.startsWith('lid')) { m.position.sub(lidPivot.position); lidPivot.add(m); } else caseGroup.add(m);
  }
  const plateJack = (face, x) => face === 'front' ? new THREE.Vector3(x, 25.6, 123.4) : new THREE.Vector3(x, 25.6, -43.4);

  // the names of each terminal's screws, shown when that terminal is in focus or being wired
  const POLES = { POWER: { 1: '+', 2: '\u2212' }, PIXEL: { 1: '5V', 2: 'DATA', 3: 'GND' }, SPK: { 1: 'SPK', 2: 'SPK' },
    RAILS: { 1: 'VIN', 2: '5V', 3: '3V3', 4: 'GND', 5: 'GND' }, RLY1: { 1: 'NO', 2: 'COM', 3: 'NC' }, RLY2: { 1: 'NO', 2: 'COM', 3: 'NC' } };
  for (const n of [1, 2, 3, 4]) POLES[`OUT${n}`] = { 1: '+', 2: '\u2212' };
  const poleLabels = {};
  for (const [name, poles] of Object.entries(POLES)) {
    poleLabels[name] = Object.entries(poles).map(([k, text]) => {
      const a = board.userData.conn[name].userData.anchors[k]; const l = label(text, 'wg-pin wg-pole');
      l.position.copy(a.screw).add(new THREE.Vector3(0, 5, 0)); l.visible = false; scene.add(l); return l;
    });
  }

  // ---- the kit a guide builds with
  const objects = {}, wires = {}, extras = [], pinLabels = [];
  const guideRoot = new THREE.Group(); scene.add(guideRoot);
  const conn = board.userData.conn;
  const anchorWorld = (obj, key) => {
    obj.updateWorldMatrix(true, true);
    const a = obj.userData.anchors?.[key]; if (!a) throw new Error(`no anchor ${obj.name}.${key}`);
    return { pos: a.pos.clone().applyMatrix4(obj.matrixWorld), dir: a.dir.clone().transformDirection(obj.matrixWorld) };
  };
  const kit = {
    props, COLORS, TOP, conn,
    /** Place a part: name, the Group from props, { pos, rot: [x,y,z] radians, from: [dx,dy,dz] slide-in offset, label, at: 'OUT1' (binds it to an output) } */
    add(name, obj, o = {}) {
      obj.name = name; obj.position.set(...(o.pos || [0, 0, 0])); if (o.rot) obj.rotation.set(...o.rot);
      if (o.scale) obj.scale.setScalar(o.scale);
      obj.userData.home = obj.position.clone(); obj.userData.from = o.from ? new THREE.Vector3(...o.from) : null;
      obj.visible = false; guideRoot.add(obj); objects[name] = obj;
      for (const [key, text] of Object.entries(o.pins || {})) {   // small names on its screws or pads
        const a = obj.userData.anchors[key]; const l = label(text, 'wg-pin'); l.position.copy(a.pos).add(new THREE.Vector3(0, 6, 0)); obj.add(l); pinLabels.push(l);
      }
      for (const [band, p] of Object.entries(obj.userData.bands || {})) {   // tip, ring, sleeve on a 3.5 mm plug
        const l = label(band, 'wg-pin wg-band'); l.position.copy(p).add(new THREE.Vector3(0, 4.5, 0)); obj.add(l); pinLabels.push(l);
      }
      if (o.label) { const l = label(o.label, 'wg-part'); l.position.set(...(o.labelAt || [0, 30, 0])); obj.add(l); obj.userData.partLabel = l; }
      return obj;
    },
    /** A wire anchor: kit.at('button', 'a'), kit.at('OUT1', '1') for a terminal pole, kit.at('TRIG1') for a jack mouth */
    at(name, key) {
      if (conn[name]) return { ...(key === undefined ? anchorWorld(conn[name], 'mouth') : anchorWorld(conn[name], String(key))), conn: name };
      const o = objects[name]; if (!o) throw new Error(`no part ${name}`);
      const h = o.position.clone(); if (o.userData.home) o.position.copy(o.userData.home);
      const r = anchorWorld(o, key); o.position.copy(h); return r;
    },
    point: (x, y, z, dir = [0, 0, 1]) => ({ pos: new THREE.Vector3(x, y, z), dir: new THREE.Vector3(...dir).normalize() }),
    /** A wire from one anchor to another: { color, label, via: [[x,y,z]...], lead: mm straight out of each end, r: radius, strip: show a stripped end } */
    wire(id, a, b, o = {}) {
      const la = o.leadA ?? o.lead ?? 14, lb = o.leadB ?? o.lead ?? 14;
      const pts = [a.pos, a.pos.clone().addScaledVector(a.dir, la)];
      for (const v of o.via || []) pts.push(new THREE.Vector3(...v));
      if (!o.via) { const m = pts[1].clone().lerp(b.pos.clone().addScaledVector(b.dir, lb), 0.5); m.y = Math.max(m.y - (o.sag ?? 6), 1.2); pts.push(m); }
      pts.push(b.pos.clone().addScaledVector(b.dir, lb), b.pos);
      const curve = new THREE.CatmullRomCurve3(pts, false, 'centripetal');
      const segs = Math.max(40, Math.round(curve.getLength() / 2));
      const geo = new THREE.TubeGeometry(curve, segs, o.r ?? 0.9, 10, false);
      const col = COLORS[o.color] ?? o.color ?? COLORS.yellow;
      const mesh = new THREE.Mesh(geo, new THREE.MeshStandardMaterial({ color: col, roughness: 0.45 }));
      mesh.name = `wire:${id}`; mesh.visible = false; mesh.userData.count = geo.index.count; mesh.userData.curve = curve;
      // the bare copper end that goes into the terminal
      const tipEnd = new THREE.Mesh(new THREE.CylinderGeometry(0.55, 0.55, 3, 8), new THREE.MeshStandardMaterial({ color: 0xc87533, metalness: 0.8, roughness: 0.3 }));
      tipEnd.position.copy(b.pos.clone().addScaledVector(b.dir, -1)); tipEnd.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), b.dir); tipEnd.visible = false; mesh.add(tipEnd);
      mesh.userData.tipEnd = tipEnd;
      mesh.userData.conns = [a.conn, b.conn].filter(Boolean);
      guideRoot.add(mesh); wires[id] = mesh;
      if (o.label) { const l = label(o.label, 'wg-wire'); l.position.copy(curve.getPointAt(o.labelT ?? 0.5)); l.position.y += 4; mesh.add(l); mesh.userData.label = l; }
      return mesh;
    },
    /** Free-standing text in the scene (stays readable while you rotate) */
    note(id, text, pos, cls = 'wg-note') { const l = label(text, cls); l.position.set(...pos); l.visible = false; guideRoot.add(l); objects[id] = l; return l; },
    plateJack,
    /** A second board (for chaining): returns { group, at(name, key) } */
    board2(pos, rotY = 0) {
      const b2 = buildBoard(tex); b2.position.set(...pos); b2.rotation.y = rotY; b2.visible = false; b2.name = 'board2'; guideRoot.add(b2);
      const so = standoffs.clone(); b2.add(so); objects.board2 = b2; b2.userData.home = null;
      return { group: b2, at: (name, key) => anchorWorld(b2.userData.conn[name], key === undefined ? 'mouth' : String(key)), conn: b2.userData.conn, leds: b2.userData.leds };
    },
  };

  // ---- highlights: a pulsing frame round a connector, with its name
  const hl = new THREE.Group(); scene.add(hl);
  const hlCache = {};
  function highlightFor(name) {
    if (hlCache[name]) return hlCache[name];
    const target = conn[name] || objects[name]; if (!target) return null;
    const bx = new THREE.Box3().setFromObject(target); const size = bx.getSize(new THREE.Vector3()).addScalar(3); const c = bx.getCenter(new THREE.Vector3());
    const lines = new THREE.LineSegments(new THREE.EdgesGeometry(new THREE.BoxGeometry(size.x, size.y, size.z)), new THREE.LineBasicMaterial({ color: 0xffd400 }));
    const glow = new THREE.Mesh(new THREE.BoxGeometry(size.x, size.y, size.z), new THREE.MeshBasicMaterial({ color: 0xffd400, transparent: true, opacity: 0.18, depthWrite: false }));
    const g = new THREE.Group(); g.add(lines, glow); g.position.copy(c);
    if (conn[name]) { const l = label(name, 'wg-hl'); l.position.set(0, size.y / 2 + 13, 0); g.add(l); }
    hl.add(g); hlCache[name] = g; return g;
  }

  let guide = null;
  function load(g) {
    guide = g; g.build(kit);
    // precompute each step's cumulative state
    let vis = new Set(g.always || []), drawn = new Set(), caseMode = g.caseMode || 'none', lidOpen = true;
    // a step with `only` (case or bare) is a side trip: what it shows, hides or draws does not carry on to later steps
    g._states = g.steps.map((s) => {
      const prevVis = new Set(vis), prevDrawn = new Set(drawn), prevCase = caseMode;
      if (s.only) { const st = side(s, prevVis, prevDrawn, prevCase); return st; }
      for (const n of s.hide || []) { vis.delete(n); drawn.delete(n); }
      for (const n of s.show || []) vis.add(n);
      for (const n of s.unwire || []) drawn.delete(n);
      if (s.case) caseMode = s.case;
      const st = { vis: new Set(vis), entering: (s.show || []).filter((n) => !prevVis.has(n)), drawnBefore: new Set(drawn), drawing: s.draw || [], caseMode };
      for (const n of s.draw || []) drawn.add(n);
      return st;
    });
    function side(s, v0, d0, c0) {
      const v = new Set(v0), d = new Set(d0);
      for (const n of s.hide || []) { v.delete(n); d.delete(n); }
      for (const n of s.show || []) v.add(n);
      for (const n of s.unwire || []) d.delete(n);
      return { vis: v, entering: (s.show || []).filter((n) => !v0.has(n)), drawnBefore: d, drawing: s.draw || [], caseMode: s.case || c0 };
    }
  }

  // Put the world in step i at progress t (0..1) and clock time (s, for loops such as a spinning fan).
  function show(i, t = 1, time = 0) {
    const st = guide._states[i], s = guide.steps[i];
    for (const [n, o] of Object.entries(objects)) {
      o.visible = st.vis.has(n);
      if (o.userData?.home) {
        o.position.copy(o.userData.home);
        if (o.userData.from && st.entering.includes(n)) o.position.addScaledVector(o.userData.from, 1 - ease(t * 1.6));
      }
      if (o.userData?.partLabel) o.userData.partLabel.visible = !!(s.labels || st.entering.includes(n) || (s.focus || []).includes(n));
    }
    for (const [n, w] of Object.entries(wires)) {
      let k = st.drawnBefore.has(n) ? 1 : st.drawing.includes(n) ? ease(Math.min(1, t * 1.4 - 0.1 * st.drawing.indexOf(n))) : 0;
      w.visible = k > 0; w.geometry.setDrawRange(0, Math.floor(w.userData.count * k / 60) * 60);
      w.userData.tipEnd.visible = k >= 1; if (w.userData.label) w.userData.label.visible = k >= 0.6 && !!(s.labels || st.drawing.includes(n));
    }
    caseGroup.visible = st.caseMode !== 'none'; standoffs.visible = st.caseMode === 'none';
    lidPivot.visible = st.caseMode !== 'nolid'; lidPivot.rotation.x = st.caseMode === 'closed' ? 0 : -1.95;
    if (forceCase !== null) { caseGroup.visible = forceCase; standoffs.visible = !forceCase; if (forceCase && st.caseMode === 'none') { lidPivot.visible = true; lidPivot.rotation.x = -1.95; } }
    for (const g of Object.values(hlCache)) g.visible = false;
    const wiring = new Set([...(s.focus || []), ...st.drawing.flatMap((n) => wires[n]?.userData.conns || [])]);
    for (const [name, ls] of Object.entries(poleLabels)) for (const l of ls) l.visible = wiring.has(name);
    const pulse = 0.5 + 0.5 * Math.sin(time * 5);
    for (const n of s.focus || []) { const g = highlightFor(n); if (g) { g.visible = true; g.children[1].material.opacity = 0.08 + 0.2 * pulse; } }
    // live: outputs on (board LED + the bound part), a button press, pixel modes
    for (const l of Object.values(board.userData.leds)) { l.material.emissive.setHex(0x000000); }
    for (const o of Object.values(objects)) o.userData?.setOn?.(0, time);
    const live = s.live || {};
    for (const [k, v] of Object.entries(live)) {
      const amt = typeof v === 'number' ? v : 1;
      if (board.userData.leds[k]) { board.userData.leds[k].material.emissive.setHex(0x22ff44); board.userData.leds[k].material.emissiveIntensity = 2 * amt; }
      const o = objects[k];
      if (o?.userData?.press && v === 'press') o.userData.press(Math.max(0, Math.sin(Math.min(1, t * 3) * Math.PI)));
      else if (o?.userData?.setOn) o.userData.setOn(amt, time, typeof v === 'string' ? v : undefined);
    }
  }

  let forceCase = null;
  const setCase = (v) => { forceCase = v; };
  const setCamera = (c) => { camera.position.set(c[0], c[1], c[2]); camera.lookAt(c[3], c[4], c[5]); };
  function resize(w, h) { viewW = w; viewH = h; renderer.setSize(w, h, false); css.setSize(w, h); camera.aspect = w / h; camera.updateProjectionMatrix(); }
  const wp = new THREE.Vector3();
  // Labels: after CSS2DRenderer places them, move overlapping ones apart (pins first, then the rest), keep them inside
  // the view, and draw a thin leader line from a moved label back to the point it names.
  const leaders = document.createElementNS('http://www.w3.org/2000/svg', 'svg'); leaders.setAttribute('class', 'wg-leaders');
  labelHost.insertBefore(leaders, css.domElement);
  let viewW = 1, viewH = 1;
  function declutter() {
    const items = [];
    for (const el of css.domElement.children) {
      if (el.style.display === 'none' || el.style.opacity === '0') continue;
      const m = /translate\((-?[\d.]+)px, ?(-?[\d.]+)px\)/.exec(el.style.transform.replace('translate(-50%, -50%)', '').replace('translate(-50%,-50%)', ''));
      if (!m) continue;
      const w = el.offsetWidth, hh = el.offsetHeight;
      if (!w) continue;
      const pin = el.classList.contains('wg-pin');
      items.push({ el, x: +m[1], y: +m[2], w, h: hh, pin });
    }
    items.sort((a, b) => (b.pin - a.pin) || (a.y - b.y));
    const placed = []; let svg = '';
    const hit = (r) => placed.some((q) => r.x < q.x + q.w + 3 && r.x + r.w + 3 > q.x && r.y < q.y + q.h + 2 && r.y + r.h + 2 > q.y);
    for (const it of items) {
      const tries = [[0, 0]];
      for (let k = 1; k <= 6; k++) tries.push([0, -k * (it.h + 3)], [0, k * (it.h + 3)], [k * 24, -k * (it.h + 3)], [-k * 24, -k * (it.h + 3)]);
      let best = null;
      for (const [dx, dy] of tries) {
        let r = { x: it.x - it.w / 2 + dx, y: it.y - it.h / 2 + dy, w: it.w, h: it.h };
        r.x = Math.max(4, Math.min(viewW - r.w - 4, r.x)); r.y = Math.max(4, Math.min(viewH - r.h - 4, r.y));
        if (!hit(r)) { best = r; break; }
        if (!best) best = r;
      }
      placed.push(best);
      const cx = best.x + best.w / 2, cy = best.y + best.h / 2;
      it.el.style.transform = `translate(-50%, -50%) translate(${cx}px, ${cy}px)`;
      if (Math.hypot(cx - it.x, cy - it.y) > it.h * 0.8) {
        const ex = Math.max(best.x, Math.min(best.x + best.w, it.x)), ey = Math.max(best.y, Math.min(best.y + best.h, it.y));
        svg += `<line x1="${it.x.toFixed(1)}" y1="${it.y.toFixed(1)}" x2="${ex.toFixed(1)}" y2="${ey.toFixed(1)}"/><circle cx="${it.x.toFixed(1)}" cy="${it.y.toFixed(1)}" r="2.5"/>`;
      }
    }
    leaders.innerHTML = svg;
  }
  function render() {
    for (const l of pinLabels) l.element.style.opacity = l.getWorldPosition(wp).distanceTo(camera.position) < 280 ? '1' : '0';
    renderer.render(scene, camera); css.render(scene, camera);
    declutter();
  }
  // A step's camera, fitted to the view's shape: the views are framed for 16:9, so a narrower view (a phone, the
  // desktop page beside its panel) backs the camera off along the same line until the same width shows.
  function fit(c) {
    const k = Math.min(1.9, Math.max(1, Math.pow((16 / 9) / camera.aspect, 0.6)));
    if (k === 1) return c;
    return [c[3] + (c[0] - c[3]) * k, c[4] + (c[1] - c[4]) * k, c[5] + (c[2] - c[5]) * k, c[3], c[4], c[5]];
  }
  return { renderer, scene, camera, board, caseGroup, objects, wires, load, show, render, resize, setCamera, setCase, fit, labelLayer: css.domElement, plateJack };
}

// Camera helpers for step data: where to look from, as [px, py, pz, tx, ty, tz].
export const lerpCam = (a, b, k) => a.map((v, i) => v + (b[i] - v) * ease(k));
