// The things you connect to the board, as simple shapes. Each builder returns a Group with userData.anchors:
// named points where a wire attaches ({ pos, dir }: dir is the way the wire leaves), and optional userData.setOn(on, t)
// for a load that lights, spins or moves.
import * as THREE from 'three';

const mat = (color, o = {}) => new THREE.MeshStandardMaterial({ color, roughness: 0.55, metalness: 0, ...o });
const box = (w, h, d, m) => new THREE.Mesh(new THREE.BoxGeometry(w, h, d), m);
const cyl = (r, h, m, seg = 24, r2 = r) => new THREE.Mesh(new THREE.CylinderGeometry(r, r2, h, seg), m);
const V = (x, y, z) => new THREE.Vector3(x, y, z);
const A = (pos, dir) => ({ pos, dir: dir.clone().normalize() });
const metal = () => mat(0xc8ccd0, { metalness: 0.9, roughness: 0.3 });
const brass = () => mat(0xd8b45a, { metalness: 0.9, roughness: 0.3 });

// A big arcade button in a small see-through box (so you can see its two tabs and wires), dome up, sitting on the desk.
export function arcadeButton() {
  const g = new THREE.Group();
  const shell = box(50, 30, 50, mat(0x9fb3c8, { transparent: true, opacity: 0.28, depthWrite: false })); shell.position.y = 15; g.add(shell);
  const ring = cyl(15, 4, mat(0x111111), 40); ring.position.y = 32; g.add(ring);
  const dome = new THREE.Mesh(new THREE.SphereGeometry(12, 32, 16, 0, Math.PI * 2, 0, Math.PI / 2), mat(0xd32f2f, { roughness: 0.3 }));
  dome.scale.y = 0.55; dome.position.y = 34; g.add(dome);
  const body = cyl(11, 14, mat(0x222222)); body.position.y = 23; g.add(body);
  for (const s of [-1, 1]) { const t = box(1, 7, 6, brass()); t.position.set(s * 4, 12.5, 0); g.add(t); }
  g.userData.anchors = { a: A(V(-4, 9.5, 0), V(0, -1, 0)), b: A(V(4, 9.5, 0), V(0, -1, 0)) };
  g.userData.press = (k) => { dome.position.y = 34 - 2.5 * k; };
  return g;
}

// A 3.5 mm TRS (stereo) plug: tip, ring and sleeve bands on its shaft, a handle, and the cable out the back.
// Built along +z (the tip points +z); rotate the group to aim it.
export function trsPlug({ handle = 0x202020, labels = true } = {}) {
  const g = new THREE.Group();
  const seg = (z0, z1, r, m) => { const c = cyl(r, z1 - z0, m); c.rotation.x = Math.PI / 2; c.position.z = (z0 + z1) / 2; g.add(c); return c; };
  const ins = mat(0x111111);
  seg(0, 6.5, 1.75, metal());          // sleeve
  seg(6.5, 7.5, 1.75, ins);
  seg(7.5, 10.5, 1.75, metal());       // ring
  seg(10.5, 11.5, 1.75, ins);
  seg(11.5, 13.5, 1.75, metal());      // tip
  const tipEnd = new THREE.Mesh(new THREE.SphereGeometry(1.75, 16, 8, 0, Math.PI * 2, 0, Math.PI / 2), metal());
  tipEnd.rotation.x = Math.PI / 2; tipEnd.position.z = 13.5; g.add(tipEnd);
  seg(-16, 0, 4, mat(handle));
  seg(-24, -16, 2.2, mat(handle));
  g.userData.bands = { tip: V(0, 0, 12.5), ring: V(0, 0, 9), sleeve: V(0, 0, 3) };
  g.userData.anchors = { cable: A(V(0, 0, -24), V(0, 0, -1)) };
  return g;
}

// A "3.5 mm TRS plug to screw terminal" adapter: the plug, and a small block with three screws marked T, R, S
// (some are marked L, R and minus: L is the tip, R the ring, minus the sleeve). The plug points +z.
export function trsAdapter() {
  const g = new THREE.Group();
  const p = trsPlug({ handle: 0x303030 }); g.add(p);
  const blk = box(14, 9, 11, mat(0x2e8b57)); blk.position.set(0, 0, -21); g.add(blk);
  g.userData.anchors = {};
  ['T', 'R', 'S'].forEach((n, i) => {
    const x = (i - 1) * 4.2;
    const s = cyl(1.4, 1, metal()); s.position.set(x, 5, -19); g.add(s);
    const m = box(3, 3, 0.5, mat(0x111111)); m.position.set(x, 0.5, -26.6); g.add(m);
    g.userData.anchors[n] = A(V(x, 0.5, -27), V(0, 0, -1));
  });
  g.userData.bands = p.userData.bands;
  return g;
}

// A 12 V LED strip: a white strip with LEDs, + and - pads at its input end (left), leads attached there.
export function ledStrip({ length = 160, color = 0xfff2d0 } = {}) {
  const g = new THREE.Group();
  const s = box(length, 1, 10, mat(0xf0f0f0)); s.position.x = length / 2; g.add(s);
  const leds = [];
  for (let x = 12; x < length; x += 16.5) {
    const l = box(5, 1.6, 5, mat(0x8c8a7a, { emissive: 0x000000 })); l.position.set(x, 0.8, 0); g.add(l); leds.push(l);
  }
  const pl = box(4, 0.3, 3, mat(0xd4b061, { metalness: 0.8 })); pl.position.set(3, 0.6, -2.5); g.add(pl);
  const mi = box(4, 0.3, 3, mat(0xd4b061, { metalness: 0.8 })); mi.position.set(3, 0.6, 2.5); g.add(mi);
  g.userData.anchors = { plus: A(V(1.5, 0.8, -2.5), V(-1, 0, 0)), minus: A(V(1.5, 0.8, 2.5), V(-1, 0, 0)) };
  g.userData.setOn = (k) => leds.forEach((l) => { l.material.emissive.setHex(color); l.material.emissiveIntensity = 1.6 * k; });
  return g;
}

// A WS2812 pixel strip (5 V): black strip of RGB LEDs, its input end with 5V / DIN / GND pads, a capacitor across
// 5V-GND at the input. No resistor at the strip: the board already has a 330 ohm resistor on DATA.
export function pixelStrip({ count = 15, pitch = 16.7, resistor = false } = {}) {
  const g = new THREE.Group();
  const L = count * pitch + 6;
  const s = box(L, 1, 10, mat(0x1a1a1a)); s.position.x = L / 2; g.add(s);
  const leds = [];
  for (let i = 0; i < count; i++) {
    const l = box(5, 1.6, 5, mat(0x3a3a3a, { emissive: 0x000000 })); l.position.set(9 + i * pitch, 0.8, 0); g.add(l); leds.push(l);
  }
  [[-3, 0xd4b061], [0, 0xd4b061], [3, 0xd4b061]].forEach(([z, c]) => { const p = box(3, 0.3, 2, mat(c, { metalness: 0.8 })); p.position.set(2.5, 0.6, z); g.add(p); });
  // the capacitor: a can standing at the input, its leads to 5V and GND
  const cap = cyl(4, 10, mat(0x1d2f6b), 20); cap.position.set(-14, 5, 0); g.add(cap);
  const stripe = box(0.6, 9.6, 3, mat(0xdddddd)); stripe.position.set(-14, 5, 3.9); g.add(stripe);
  // the resistor in the data lead
  if (resistor) { const res = cyl(1.2, 6, mat(0xd9c39a), 12); res.rotation.z = Math.PI / 2; res.position.set(-24, 1.2, 0); g.add(res); }
  g.userData.anchors = {
    v5: A(V(1, 0.8, -3), V(-1, 0, 0)), din: A(V(1, 0.8, 0), V(-1, 0, 0)), gnd: A(V(1, 0.8, 3), V(-1, 0, 0)),
    resIn: A(V(-27, 1.2, 0), V(-1, 0, 0)), resOut: A(V(-21, 1.2, 0), V(1, 0, 0)),
    capP: A(V(-15.5, 0, -2), V(0, -1, 0)), capN: A(V(-15.5, 0, 2), V(0, -1, 0)),
  };
  const colors = [0xff3300, 0xff8800, 0x8a2be2];
  g.userData.setOn = (k, t = 0, mode = 'chase') => leds.forEach((l, i) => {
    let c = 0x000000, e = 0;
    if (mode === 'chase') { const ph = (t * 12 - i) % count; e = ph >= 0 && ph < 3 ? 2 * k : 0.08 * k; c = colors[i % 3]; }
    else if (mode === 'glow') { c = 0xff7a10; e = k * (0.55 + 0.35 * Math.sin(t * 1.05)); }
    else if (mode === 'flicker') { c = 0xff1010; e = k * (Math.sin(t * 37 + i * 7.1) * Math.sin(t * 23 + i * 3.3) > 0 ? 1 : 0.15); }
    else { c = 0xffffff; e = k; }
    l.material.emissive.setHex(c); l.material.emissiveIntensity = e;
  });
  return g;
}

// A 12 V lamp: a bulb in a socket with two leads.
export function lamp() {
  const g = new THREE.Group();
  const base = cyl(9, 10, mat(0x333333)); base.position.y = 5; g.add(base);
  const bulb = new THREE.Mesh(new THREE.SphereGeometry(14, 24, 16), mat(0xfff8e0, { transparent: true, opacity: 0.85, emissive: 0x000000 }));
  bulb.position.y = 24; g.add(bulb);
  g.userData.anchors = { plus: A(V(-4, 0, 0), V(0, -1, 0)), minus: A(V(4, 0, 0), V(0, -1, 0)) };
  g.userData.setOn = (k) => { bulb.material.emissive.setHex(0xffd27a); bulb.material.emissiveIntensity = 1.2 * k; };
  return g;
}

// A 12 V PIR motion sensor with a relay (contact) output: white dome on a small board, a four-screw terminal strip
// marked +12V, GND, COM, NO. Its contact closes COM to NO while it sees someone.
export function motionSensor() {
  const g = new THREE.Group();
  const pcb = box(36, 1.6, 30, mat(0x1b5e20)); g.add(pcb);
  const dome = new THREE.Mesh(new THREE.SphereGeometry(11, 24, 16, 0, Math.PI * 2, 0, Math.PI / 2), mat(0xf5f5f5, { roughness: 0.9 }));
  dome.position.set(0, 0.8, -3); g.add(dome);
  const tb = box(22, 6, 6, mat(0x2b6cd6)); tb.position.set(0, 3.8, 11.5); g.add(tb);
  const xs = [-7.5, -2.5, 2.5, 7.5];
  g.userData.anchors = {};
  ['p12', 'gnd', 'com', 'no'].forEach((n, i) => {
    const s = cyl(1.2, 0.8, metal()); s.position.set(xs[i], 7.1, 10.5); g.add(s);
    g.userData.anchors[n] = A(V(xs[i], 3, 14.6), V(0, 0, 1));
  });
  g.userData.setOn = (k) => { dome.material.emissive.setHex(k > 0 ? 0x553300 : 0x000000); };
  return g;
}
// A small inline fuse holder (a short tube), to sit on a wire.
export function fuse() {
  const g = new THREE.Group();
  const b = cyl(3.5, 22, mat(0x111111), 16); b.rotation.x = Math.PI / 2; g.add(b);
  return g;
}

// A passive speaker: a round cone in a frame, two terminals on its back.
export function speaker() {
  const g = new THREE.Group();
  const frame = cyl(26, 4, mat(0x222222), 40); frame.rotation.x = Math.PI / 2; g.add(frame);
  const cone = cyl(22, 10, mat(0x111111), 40, 6); cone.rotation.x = -Math.PI / 2; cone.position.z = -6; g.add(cone);
  const mag = cyl(10, 8, metal(), 24); mag.rotation.x = Math.PI / 2; mag.position.z = -15; g.add(mag);
  for (const s of [-1, 1]) { const t = box(2, 5, 1, brass()); t.position.set(s * 14, -12, -3); g.add(t); }
  g.userData.anchors = { a: A(V(-14, -15, -3), V(0, -1, 0)), b: A(V(14, -15, -3), V(0, -1, 0)) };
  g.userData.setOn = (k, t = 0) => { cone.position.z = -6 + k * 0.8 * Math.sin(t * 40); };
  return g;
}

// A 12 V DC fan: square frame, spinning blades, red and black leads.
export function fan() {
  const g = new THREE.Group();
  const f = box(40, 40, 10, mat(0x1c1c1c)); g.add(f);
  const hole = cyl(18.5, 10.5, mat(0x050505), 32); hole.rotation.x = Math.PI / 2; g.add(hole);
  const rotor = new THREE.Group(); g.add(rotor);
  const hub = cyl(7, 8, mat(0x2a2a2a)); hub.rotation.x = Math.PI / 2; rotor.add(hub);
  for (let i = 0; i < 7; i++) { const b = box(10, 3, 1, mat(0x333333)); b.position.set(Math.cos(i * 0.9) * 11, Math.sin(i * 0.9) * 11, 0); b.rotation.z = i * 0.9; b.rotation.x = 0.4; rotor.add(b); }
  g.userData.anchors = { plus: A(V(-16, -20, 0), V(0, -1, 0)), minus: A(V(-12, -20, 0), V(0, -1, 0)) };
  g.userData.setOn = (k, t = 0) => { rotor.rotation.z = k > 0 ? -t * 25 : rotor.rotation.z; };
  return g;
}

// A 12 V push solenoid with its flyback diode across the two terminals (the diode's stripe on the + side).
export function solenoid() {
  const g = new THREE.Group();
  const frame = box(30, 3, 20, mat(0x9aa0a6, { metalness: 0.6 })); frame.position.y = -8; g.add(frame);
  for (const x of [-14, 14]) { const e = box(2, 19, 20, mat(0x9aa0a6, { metalness: 0.6 })); e.position.set(x, 1, 0); g.add(e); }
  const coil = cyl(8, 26, mat(0xb5651d), 24); coil.rotation.z = Math.PI / 2; coil.position.y = 1; g.add(coil);
  const plunger = cyl(3, 22, metal()); plunger.rotation.z = Math.PI / 2; plunger.position.set(18, 1, 0); g.add(plunger);
  const tp = box(1.5, 6, 3, brass()); tp.position.set(-10, 11, -5); g.add(tp);
  const tm = box(1.5, 6, 3, brass()); tm.position.set(-10, 11, 5); g.add(tm);
  // the diode bridging the terminals: black body, silver stripe toward +
  const d = cyl(1.4, 6, mat(0x111111), 12); d.rotation.x = Math.PI / 2; d.position.set(-13, 15, 0); g.add(d);
  const st = cyl(1.45, 1, mat(0xdddddd), 12); st.rotation.x = Math.PI / 2; st.position.set(-13, 15, -2.3); g.add(st);
  const legP = cyl(0.3, 3, metal()); legP.rotation.x = Math.PI / 2; legP.position.set(-13, 15, -4.3); g.add(legP);
  const legM = cyl(0.3, 3, metal()); legM.rotation.x = Math.PI / 2; legM.position.set(-13, 15, 4.3); g.add(legM);
  g.userData.anchors = { plus: A(V(-10, 14, -5), V(-1, 0.3, 0)), minus: A(V(-10, 14, 5), V(-1, 0.3, 0)), stripe: A(V(-13, 15, -2.3), V(0, 1, 0)) };
  g.userData.setOn = (k) => { plunger.position.x = 18 - 6 * k; };
  return g;
}

// A plug-in DC power supply ("power brick") with its cord ending in a 5.5 x 2.1 mm barrel plug.
export function powerBrick() {
  const g = new THREE.Group();
  const b = box(60, 30, 110, mat(0x1e1e1e)); g.add(b);
  const lab = box(40, 0.4, 60, mat(0xdddddd)); lab.position.y = 15.2; g.add(lab);
  g.userData.anchors = { cord: A(V(0, 0, 55), V(0, 0, 1)) };
  return g;
}
// A barrel plug (male, 5.5 x 2.1 mm) on its own, pointing +z.
export function barrelPlug() {
  const g = new THREE.Group();
  const h = cyl(5, 14, mat(0x1e1e1e)); h.rotation.x = Math.PI / 2; h.position.z = -7; g.add(h);
  const s = cyl(2.75, 9.5, metal()); s.rotation.x = Math.PI / 2; s.position.z = 4.75; g.add(s);
  g.userData.anchors = { cable: A(V(0, 0, -14), V(0, 0, -1)) };
  return g;
}
// A female barrel jack to screw terminal adapter (+ and - screws): what a power brick plugs into.
export function barrelAdapter() {
  const g = new THREE.Group();
  const blk = box(14, 10, 14, mat(0x1e1e1e)); g.add(blk);
  const j = cyl(5, 12, mat(0x111111)); j.rotation.x = Math.PI / 2; j.position.z = -12; g.add(j);
  ['plus', 'minus'].forEach((n, i) => { const s = cyl(1.4, 1, metal()); s.position.set(i ? 3.5 : -3.5, 5.2, 2); g.add(s); });
  g.userData.anchors = { plus: A(V(-3.5, 1, 7.5), V(0, 0, 1)), minus: A(V(3.5, 1, 7.5), V(0, 0, 1)), mouth: A(V(0, 0, -18), V(0, 0, -1)) };
  return g;
}

// A DC 12 V air valve (5/2 solenoid valve): an aluminium body with air ports and a coil on top whose two leads end in
// spade tabs; a flyback diode across the tabs, stripe on +. Anchors plus / minus point -x (toward the board).
export function airValve({ diode = true } = {}) {
  const g = new THREE.Group();
  const body = box(50, 16, 18, mat(0xb8bec6, { metalness: 0.6 })); body.position.y = 8; g.add(body);
  for (const x of [-15, 0, 15]) { const p = cyl(3, 4, mat(0x2b6cd6)); p.position.set(x, 8, 10); p.rotation.x = Math.PI / 2; g.add(p); }
  const coil = box(22, 22, 18, mat(0x1c1c1c)); coil.position.set(-6, 27, 0); g.add(coil);
  const lab = box(14, 0.4, 10, mat(0xeeeeee)); lab.position.set(-6, 38.3, 0); g.add(lab);
  const tp = box(1.5, 6, 3, brass()); tp.position.set(-17.5, 30, -5); g.add(tp);
  const tm = box(1.5, 6, 3, brass()); tm.position.set(-17.5, 30, 5); g.add(tm);
  if (diode) {
    const d = cyl(1.4, 6, mat(0x111111), 12); d.rotation.x = Math.PI / 2; d.position.set(-20, 34, 0); g.add(d);
    const st = cyl(1.45, 1, mat(0xdddddd), 12); st.rotation.x = Math.PI / 2; st.position.set(-20, 34, -2.3); g.add(st);
  }
  const piston = cyl(2, 30, metal()); piston.rotation.z = Math.PI / 2; piston.position.set(45, 8, 0); g.add(piston);
  g.userData.anchors = { plus: A(V(-18, 30, -5), V(-1, -0.2, 0)), minus: A(V(-18, 30, 5), V(-1, -0.2, 0)), stripe: A(V(-20, 35, -2.3), V(0, 1, 0)) };
  g.userData.setOn = (k) => { piston.position.x = 45 + 14 * k; };
  return g;
}

// A 12 V DC motor (a pump or a pop-up's motor): a can with two solder tabs at the back and a spinning shaft.
export function dcMotor() {
  const g = new THREE.Group();
  const can = cyl(14, 36, mat(0x9aa0a6, { metalness: 0.7 })); can.rotation.z = Math.PI / 2; can.position.y = 14; g.add(can);
  const shaft = cyl(1.5, 12, metal()); shaft.rotation.z = Math.PI / 2; shaft.position.set(24, 14, 0); g.add(shaft);
  const vane = box(1.5, 18, 4, mat(0xd62828)); vane.position.set(28, 14, 0); g.add(vane);
  const tp = box(1.5, 5, 3, brass()); tp.position.set(-19, 18, -5); g.add(tp);
  const tm = box(1.5, 5, 3, brass()); tm.position.set(-19, 18, 5); g.add(tm);
  const dot = cyl(1.2, 0.5, mat(0xd62828)); dot.rotation.z = Math.PI / 2; dot.position.set(-18.3, 24, -5); g.add(dot);
  g.userData.anchors = { plus: A(V(-20, 18, -5), V(-1, 0, 0)), minus: A(V(-20, 18, 5), V(-1, 0, 0)) };
  g.userData.setOn = (k, t = 0) => { vane.rotation.x = k ? t * 25 : 0; };
  return g;
}

// A "MAX485 TTL to RS-485" module: a small blue board, header pins VCC / DI / DE / RE / GND on one end and a 2-screw
// A / B terminal on the other. Pins point -x, the terminal +x.
export function max485() {
  const g = new THREE.Group();
  const pcb = box(44, 1.6, 14, mat(0x1d4fa0)); pcb.position.y = 3; g.add(pcb);
  const chip = box(10, 3, 6, mat(0x111111)); chip.position.set(0, 5, 0); g.add(chip);
  const term = box(8, 8, 12, mat(0x2a6f3a)); term.position.set(17, 8, 0); g.add(term);
  g.userData.anchors = {};
  ['vcc', 'di', 'de', 'gnd'].forEach((n, i) => {
    const z = (i - 1.5) * 2.54; const p = cyl(0.35, 8, metal(), 6); p.rotation.z = Math.PI / 2; p.position.set(-24, 3, z); g.add(p);
    g.userData.anchors[n] = A(V(-28, 3, z), V(-1, 0, 0));
  });
  g.userData.anchors.a = A(V(21, 10, -3), V(1, 0.3, 0)); g.userData.anchors.b = A(V(21, 10, 3), V(1, 0.3, 0));
  return g;
}

// A 3-pin XLR plug on a short cable end (female, to plug into a fog machine's DMX IN). The bare wire end at -x.
export function xlrPlug() {
  const g = new THREE.Group();
  const b = cyl(9, 34, mat(0x202020)); b.rotation.z = Math.PI / 2; g.add(b);
  const ring = cyl(9.4, 6, metal()); ring.rotation.z = Math.PI / 2; ring.position.x = 15; g.add(ring);
  g.userData.anchors = { cable: A(V(-17, 0, 0), V(-1, 0, 0)) };
  return g;
}

// A fog machine (only its low-voltage side matters here): a box with an outlet nozzle and, on its back, two XLR-looking
// sockets: DMX IN (use) and REMOTE (never). Its mains cord is not shown.
export function fogMachine() {
  const g = new THREE.Group();
  const b = box(120, 70, 70, mat(0x2b2f35)); b.position.y = 35; g.add(b);
  const noz = cyl(7, 20, metal()); noz.rotation.z = Math.PI / 2; noz.position.set(70, 50, 0); g.add(noz);
  const dmx = cyl(8, 4, mat(0x999999)); dmx.rotation.x = Math.PI / 2; dmx.position.set(-30, 30, -36); g.add(dmx);
  const rem = cyl(8, 4, mat(0x999999)); rem.rotation.x = Math.PI / 2; rem.position.set(20, 30, -36); g.add(rem);
  g.userData.anchors = { dmx: A(V(-30, 30, -40), V(0, 0, -1)), remote: A(V(20, 30, -40), V(0, 0, -1)) };
  const puff = new THREE.Mesh(new THREE.SphereGeometry(18, 16, 10), mat(0xffffff, { transparent: true, opacity: 0.0, roughness: 1 }));
  puff.position.set(100, 50, 0); g.add(puff);
  g.userData.setOn = (k, t = 0) => { puff.material.opacity = k ? 0.55 + 0.15 * Math.sin(t * 3) : 0; puff.scale.setScalar(k ? 1 + 0.25 * Math.sin(t * 2) : 1); };
  return g;
}

// A microSD card, flat, its contacts end pointing +x (into the sound module's slot at the board's right edge, so it
// goes in moving -x... the guide aims it).
export function sdCard() {
  const g = new THREE.Group();
  const c = box(15, 1, 11, mat(0x1d3b8f)); g.add(c);
  const lab = box(9, 0.2, 9, mat(0xeeeeee)); lab.position.set(-2, 0.6, 0); g.add(lab);
  return g;
}

// An enclosed (metal-case) DC power supply with a screw terminal strip: the mains side (L, N, earth) is behind a
// cover and is never shown; the DC side has V+ and V- screws. The terminals face +z.
export function enclosedSupply() {
  const g = new THREE.Group();
  const b = box(110, 40, 80, mat(0xb9bec4, { metalness: 0.7, roughness: 0.4 })); b.position.y = 20; g.add(b);
  const cover = box(108, 14, 10, mat(0x222222)); cover.position.set(0, 10, 44); g.add(cover);
  const strip = box(40, 12, 8, mat(0x111111)); strip.position.set(30, 8, 44); g.add(strip);
  g.userData.anchors = {};
  [['plus', 20], ['minus', 32]].forEach(([n, x]) => { const s = cyl(1.8, 1, metal()); s.position.set(x, 14.5, 44); g.add(s); g.userData.anchors[n] = A(V(x, 6, 48.5), V(0, 0, 1)); });
  return g;
}
// A short 3.5 mm stereo cable (plug at both ends); the guide places the two plugs and draws the cable as a wire.

// A mini 3-pin PIR motion sensor (AM312 kind, 2.7-12 V): a small dome on a tiny board, pins VCC, OUT, GND.
// Its OUT goes high (to its supply voltage) while it sees someone. The pins point +z.
export function miniPir() {
  const g = new THREE.Group();
  const body = cyl(5, 9, mat(0x1b5e20), 20); body.position.y = 4.5; g.add(body);
  const dome = new THREE.Mesh(new THREE.SphereGeometry(5, 20, 12, 0, Math.PI * 2, 0, Math.PI / 2), mat(0xf5f5f5, { roughness: 0.9 }));
  dome.position.y = 9; g.add(dome);
  g.userData.anchors = {};
  ['vcc', 'out', 'gnd'].forEach((n, i) => {
    const x = (i - 1) * 2.54; const p = cyl(0.35, 8, metal(), 6); p.rotation.x = Math.PI / 2; p.position.set(x, 1.5, 8); g.add(p);
    g.userData.anchors[n] = A(V(x, 1.5, 12), V(0, 0, 1));
  });
  g.userData.setOn = (k) => { dome.material.emissive.setHex(k > 0 ? 0x553300 : 0x000000); };
  return g;
}
