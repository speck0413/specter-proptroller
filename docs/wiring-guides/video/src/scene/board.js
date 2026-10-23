// The Proptroller board as a light three.js model: the real top-side artwork (KiCad copper, mask and silkscreen) on a
// 1.6 mm slab, the parts as simple shapes from enclosure/board-parts.json, and the connectors you wire drawn in detail.
// World frame (everything in millimetres): x from the board's left edge, z from its back edge (toward the front),
// y up. The board's underside sits at y = BOARD_Y (where the case's standoffs put it), so the case lines up.
import * as THREE from 'three';
import data from './board-data.json';

export const BOARD_Y = 12;               // board underside height in the case (enclosure params: z_board)
export const TOP = BOARD_Y + 1.6;        // board top surface
const [BW, BH] = [134, 80];

const mat = (color, o = {}) => new THREE.MeshStandardMaterial({ color, roughness: 0.6, metalness: 0, ...o });
const M = {
  term: mat(0x2e8b57), termDark: mat(0x1f6b40), screw: mat(0xc0c4c8, { metalness: 0.9, roughness: 0.3 }),
  jack: mat(0x1a1a1a), relay: mat(0x1f5fbf), part: mat(0x30363b), cap: mat(0x222a44), metal: mat(0xb8bcc0, { metalness: 0.8, roughness: 0.35 }),
  socket: mat(0x111111), module: mat(0x20242a), modBoard: mat(0x1d3d8f), dfp: mat(0xb22222), sd: mat(0x333333),
  btn: mat(0x111111), btnCap: mat(0x444444), led: mat(0x0a3d0a, { emissive: 0x000000 }), gold: mat(0xd4b061, { metalness: 0.8, roughness: 0.3 }),
};

const box = (w, h, d, m) => new THREE.Mesh(new THREE.BoxGeometry(w, h, d), m);
const cyl = (r, h, m, seg = 20) => new THREE.Mesh(new THREE.CylinderGeometry(r, r, h, seg), m);

export const pad = (ref, n) => {
  const p = data.pads[ref]?.[n];
  if (!p) throw new Error(`no pad ${ref}.${n}`);
  return p;
};

// A 5.08 mm screw terminal block: body behind its pins, the wire mouths facing the nearest board edge, a screw on top of
// each pole. `face` is +1 when the mouths face the front edge (z grows), -1 for the back edge.
function terminal(ref, poles, face) {
  const g = new THREE.Group(); g.name = ref;
  const ps = Object.keys(data.pads[ref]).sort().map((n) => pad(ref, n));
  const xs = ps.map((p) => p[0]), zc = ps[0][1];
  const x0 = Math.min(...xs) - 2.54, x1 = Math.max(...xs) + 2.54, w = x1 - x0;
  const H = 10, D = 9.5;
  const body = box(w, H, D, M.term); body.position.set((x0 + x1) / 2, TOP + H / 2, zc + face * 1.2); g.add(body);
  g.userData.anchors = {};
  for (const n of Object.keys(data.pads[ref])) {
    const [x] = pad(ref, n);
    const s = cyl(1.6, 1.2, M.screw); s.position.set(x, TOP + H + 0.3, zc - face * 0.8); g.add(s);
    const slot = box(3.2, 0.5, 0.5, M.part); slot.position.set(x, TOP + H + 0.95, zc - face * 0.8); slot.rotation.y = 0.6; g.add(slot);
    const mouth = box(3.6, 3.6, 0.6, M.part); mouth.position.set(x, TOP + 3.2, zc + face * (1.2 + D / 2)); g.add(mouth);
    g.userData.anchors[n] = { pos: new THREE.Vector3(x, TOP + 3.2, zc + face * (1.2 + D / 2)), dir: new THREE.Vector3(0, 0, face), screw: new THREE.Vector3(x, TOP + H + 1, zc - face * 0.8) };
  }
  g.userData.box = new THREE.Box3().setFromObject(g);
  return g;
}

// A PJ-320D 3.5 mm jack lying on the board, its mouth at the board edge.
function jack(ref, face, axis = 'z') {
  const g = new THREE.Group(); g.name = ref;
  const c = pad(ref, 'M');
  const L = 12, W = 6, H = 5;
  const b = box(axis === 'z' ? W : L, H, axis === 'z' ? L : W, M.jack);
  const off = 2.5 * face;
  if (axis === 'z') b.position.set(c[0], TOP + H / 2, c[1] + off); else b.position.set(c[0] + off, TOP + H / 2, c[1]);
  g.add(b);
  const m = cyl(2.6, 2, M.jack); const hole = cyl(1.8, 2.2, M.part);
  for (const o of [m, hole]) {
    if (axis === 'z') { o.rotation.x = Math.PI / 2; o.position.set(c[0], TOP + H / 2, c[1] + off + face * (L / 2 + 0.9)); }
    else { o.rotation.z = Math.PI / 2; o.position.set(c[0] + off + face * (L / 2 + 0.9), TOP + H / 2, c[1]); }
    g.add(o);
  }
  const mouth = axis === 'z' ? new THREE.Vector3(c[0], TOP + H / 2, c[1] + off + face * (L / 2 + 2)) : new THREE.Vector3(c[0] + off + face * (L / 2 + 2), TOP + H / 2, c[1]);
  g.userData.anchors = { mouth: { pos: mouth, dir: axis === 'z' ? new THREE.Vector3(0, 0, face) : new THREE.Vector3(face, 0, 0) } };
  g.userData.box = new THREE.Box3().setFromObject(g);
  return g;
}

export function buildBoard(texture) {
  const g = new THREE.Group(); g.name = 'board';
  const conn = {};
  // the slab: artwork on top, plain green sides and bottom
  texture.colorSpace = THREE.SRGBColorSpace; texture.anisotropy = 8;
  const side = mat(0x174a2c), top = new THREE.MeshStandardMaterial({ map: texture, roughness: 0.55 });
  const slab = new THREE.Mesh(new THREE.BoxGeometry(BW, 1.6, BH), [side, side, top, side, side, side]);
  slab.position.set(BW / 2, BOARD_Y + 0.8, BH / 2); slab.name = 'pcb'; g.add(slab);

  // the parts we do not wire, as boxes from their keep-outs (sockets, modules, relays, capacitors ...)
  const skip = /^(J_O|J_R\d|J_IN|J_PX|J_SPK|J_RAIL|J_T|J_C\d|J_LO|J_PIN header|J_PIS header|Pico W|J_PI[NS])/;
  for (const [ref, x0, z0, y0, x1, z1, y1, kind] of data.parts) {
    if (skip.test(ref) || kind === 'term' || kind === 'jack') continue;
    const h = y1 - Math.max(y0, 1.6), w = x1 - x0, d = z1 - z0;
    if (h <= 0.1) continue;
    let m = M.part;
    if (kind === 'relay') m = M.relay;
    else if (ref.startsWith('C_BK') || ref === 'C_DF') m = M.cap;
    else if (/^J_(DK|DF|X)/.test(ref)) m = M.socket;
    else if (ref === 'ESP32 DevKit') m = M.module;
    else if (ref === 'DFPlayer Mini') m = M.dfp;
    let mesh;
    if (ref.startsWith('C_BK') || ref === 'C_DF') { mesh = cyl(Math.min(w, d) / 2 - 0.4, h, m); }
    else if (ref === 'ESP32 DevKit') {   // the DevKit: blue board, metal RF can, on its sockets
      mesh = new THREE.Group();
      const pcb = box(w, 1.6, d, M.modBoard); pcb.position.y = -h / 2 + 8; mesh.add(pcb);
      const can = box(17, 3, 16, M.metal); can.position.set(-w / 2 + 14, -h / 2 + 10.3, 0); mesh.add(can);
      const usb = box(7, 3, 8, M.metal); usb.position.set(w / 2 - 3, -h / 2 + 10.3, 0); mesh.add(usb);
    } else if (ref === 'DFPlayer Mini') {
      mesh = new THREE.Group();
      const pcb = box(w, 1.6, d, M.dfp); pcb.position.y = h / 2 - 0.8; mesh.add(pcb);
      const sd = box(4, 2.2, 14, M.sd); sd.position.set(w / 2 - 2, h / 2 + 0.6, 0); mesh.add(sd);
    } else mesh = box(w, h, d, m);
    mesh.position.set((x0 + x1) / 2, TOP + Math.max(y0 - 1.6, 0) + h / 2, (z0 + z1) / 2);
    mesh.name = ref; g.add(mesh);
    if (kind === 'relay') conn[ref] = mesh;
    if (ref === 'DFPlayer Mini') conn.DFPLAYER = mesh;
    if (ref === 'ESP32 DevKit') conn.MODULE = mesh;
  }
  // the USER buttons (tactile switches, east edge)
  for (const n of [1, 2, 3]) {
    const [a, b] = [pad(`SW_U${n}`, '1'), pad(`SW_U${n}`, '2')];
    const s = box(6, 3.5, 6, M.btn); s.position.set((a[0] + b[0]) / 2, TOP + 1.75, a[1]); g.add(s);
    const c = cyl(1.8, 1.5, M.btnCap); c.position.set((a[0] + b[0]) / 2, TOP + 4.2, a[1]); g.add(c);
    s.name = c.name = `USER${n}`;
  }
  // the connectors you wire
  const T = (ref, poles, face, name) => { const t = terminal(ref, poles, face); t.name = name; g.add(t); conn[name] = t; };
  [1, 2, 3, 4].forEach((n) => T(`J_O${n}`, 2, 1, `OUT${n}`));
  [1, 2].forEach((n) => T(`J_R${n}`, 3, 1, `RLY${n}`));
  T('J_IN', 2, -1, 'POWER'); T('J_PX', 3, -1, 'PIXEL'); T('J_SPK', 2, -1, 'SPK'); T('J_RAIL', 5, -1, 'RAILS');
  const J = (ref, face, axis, name) => { const j = jack(ref, face, axis); j.name = name; g.add(j); conn[name] = j; };
  J('J_T1', 1, 'z', 'TRIG1'); J('J_T2', -1, 'z', 'TRIG2'); J('J_C1', -1, 'z', 'CONTACT1'); J('J_C2', -1, 'z', 'CONTACT2'); J('J_LO', 1, 'x', 'LINE OUT');
  // the barrel input (DC-005): a black block with its mouth at the back edge
  { const c = pad('J_PWR', '1'); const b = box(9, 11, 14, M.jack); b.position.set(c[0] - 2.4, TOP + 5.5, 7.5); b.name = 'BARREL'; g.add(b);
    const m = cyl(3, 1, M.metal); m.rotation.x = Math.PI / 2; m.position.set(c[0] - 2.4, TOP + 6, 0.2); g.add(m);
    conn.BARREL = b; b.userData.anchors = { mouth: { pos: new THREE.Vector3(c[0] - 2.4, TOP + 6, -0.5), dir: new THREE.Vector3(0, 0, -1) } }; }
  // output and relay indicator LEDs, lit by the runner when the output is on
  const leds = {};
  for (const [ref, name] of [['D_LO1', 'OUT1'], ['D_LO2', 'OUT2'], ['D_LO3', 'OUT3'], ['D_LO4', 'OUT4'], ['D_RL1', 'RLY1'], ['D_RL2', 'RLY2']]) {
    const [a, b] = [pad(ref, '1'), pad(ref, '2')];
    const l = box(1.4, 0.9, 2.2, M.led.clone()); l.position.set((a[0] + b[0]) / 2, TOP + 0.45, (a[1] + b[1]) / 2);
    if (a[1] === b[1]) l.rotation.y = Math.PI / 2;
    g.add(l); leds[name] = l;
  }
  g.userData = { conn, leds };
  return g;
}

export { data as boardData };
